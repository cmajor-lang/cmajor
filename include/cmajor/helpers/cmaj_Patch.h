//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  The Cmajor project is subject to commercial or open-source licensing.
//  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
//  visit https://cmajor.dev to learn about our commercial licence options.
//
//  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
//  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
//  DISCLAIMED.

#pragma once

#include "cmaj_PatchHelpers.h"
#include "cmaj_AudioMIDIPerformer.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <chrono>

namespace cmaj
{

struct PatchView;
struct PatchParameter;
using PatchParameterPtr = std::shared_ptr<PatchParameter>;

//==============================================================================
/// Acts as a high-level representation of a patch.
/// This class allows the patch to be asynchronously loaded and played, performing
/// rebuilds safely on a background thread.
struct Patch
{
    Patch();
    ~Patch();

    struct LoadParams
    {
        PatchManifest manifest;
        std::unordered_map<std::string, float> parameterValues;
    };

    /// This will do a quick, synchronous load of a patch manifest, to allow a
    /// caller to get vital statistics such as the name, description, channels,
    /// parameters etc., before calling loadPatch() to do a full build on a
    /// background thread.
    bool preload (const PatchManifest&);

    /// Kicks off a full build of a patch, optionally providing some initial
    /// parameter values to apply before processing starts.
    /// If synchronous is true, this method will block while the build completes,
    /// otherwise it will return and leave a background thread doing the build.
    bool loadPatch (const LoadParams&, bool synchronous = false);

    /// Tries to load a patch from a file path.
    /// If synchronous is true, this method will block while the build completes,
    /// otherwise it will return and leave a background thread doing the build.
    bool loadPatchFromFile (const std::string& patchFilePath, bool synchronous = false);

    /// Tries to load a patch from a manifest.
    /// If synchronous is true, this method will block while the build completes,
    /// otherwise it will return and leave a background thread doing the build.
    bool loadPatchFromManifest (PatchManifest&&, bool synchronous = false);

    /// Triggers a rebuild of the current patch, which may be needed if the code
    /// or playback parameters change.
    void rebuild();

    /// Unloads any currently loaded patch
    void unload();

    /// Checks whether a patch has been selected
    bool isLoaded() const                               { return renderer != nullptr; }

    /// Checks whether a patch is currently loaded and ready to play.
    bool isPlayable() const;

    /// Returns the log from the last build that was performed, if there was one.
    std::string getLastBuildLog() const;

    /// When the patch is loaded and playing, this resets it to the
    /// state is has when initially loaded.
    void resetToInitialState();

    /// Represents some basic information about the context in which the patch
    /// will be getting rendered.
    struct PlaybackParams
    {
        PlaybackParams() = default;
        PlaybackParams (double rate, uint32_t bs, choc::buffer::ChannelCount ins, choc::buffer::ChannelCount outs);

        bool isValid() const     { return sampleRate != 0 && blockSize != 0; }
        bool operator== (const PlaybackParams&) const;
        bool operator!= (const PlaybackParams&) const;

        double sampleRate = 0;
        uint32_t blockSize = 0;

        /// Number of channels that the host will provide/expect
        choc::buffer::ChannelCount numInputChannels = 0,
                                   numOutputChannels = 0;
    };

    /// Updates the playback parameters, which may trigger a rebuild if
    /// they have changed.
    void setPlaybackParams (PlaybackParams);

    /// Returns the current playback parameters.
    PlaybackParams getPlaybackParams() const        { return currentPlaybackParams; }

    /// The caller that is using this patch should call this to provide a description
    /// of the host that is loading it. This string will be passed through to
    /// the status that is reported via the javascript PatchConnection class.
    void setHostDescription (const std::string& hostName);

    /// Returns the host description that was previously set with setHostDescription().
    std::string getHostDescription() const;

    /// Enables/disables use of a background thread to check for any changes
    /// to patch files, and to trigger a rebuild when that happens.
    /// This defaults to false.
    void setAutoRebuildOnFileChange (bool shouldMonitorFilesForChanges);

    /// Attempts to code-generate from a patch.
    Engine::CodeGenOutput generateCode (const LoadParams&,
                                        const std::string& targetType,
                                        const std::string& extraOptionsJSON);

    //==============================================================================
    const PatchManifest* getManifest() const;
    std::string getManifestFile() const;

    std::string getUID() const;
    std::string getName() const;
    std::string getDescription() const;
    std::string getManufacturer() const;
    std::string getVersion() const;
    std::string getCategory() const;
    std::string getPatchFile() const;
    bool isInstrument() const;
    bool hasMIDIInput() const;
    bool hasMIDIOutput() const;
    bool hasAudioInput() const;
    bool hasAudioOutput() const;
    bool wantsTimecodeEvents() const;
    double getFramesLatency() const;

    choc::value::Value getProgramDetails() const;
    std::string getMainProcessorName() const;
    EndpointDetailsList getInputEndpoints() const;
    EndpointDetailsList getOutputEndpoints() const;

    choc::span<PatchParameterPtr> getParameterList() const;
    PatchParameterPtr findParameter (const EndpointID&) const;

    //==============================================================================
    /// Processes the next block, optionally adding or replacing the audio output data
    void process (const choc::audio::AudioMIDIBlockDispatcher::Block&, bool replaceOutput);

    /// Renders a block using a juce-style single array of input + output audio channels.
    /// For this one, make calls to addMIDIMessage() beforehand to provide the MIDI.
    void process (float* const* audioChannels, uint32_t numFrames, const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn&);

    /// Instead of calling process(), if you're performing multiple small chunked render ops
    /// as part of a larger chunk, you can improve performance by calling beginChunkedProcess(),
    /// then making multiple calls to processChunk(), and then endChunkedProcess() at the end.
    void beginChunkedProcess();
    /// Called to render the next sub-chunk. Must only be called after beginChunkedProcess()
    /// has been called, but may be called multiple times. After all chunks are done, call
    /// endChunkedProcess() to finish.
    void processChunk (const choc::audio::AudioMIDIBlockDispatcher::Block&, bool replaceOutput);
    /// Called after beginChunkedProcess() and processChunk() have been used, to clear up
    /// after a sequence of chunks have been rendered.
    void endChunkedProcess();

    /// Queues a MIDI message for use by the next call to process(). This isn't
    /// needed if you use the version of process that takes a Block object.
    void addMIDIMessage (int frameIndex, const void* data, uint32_t length);

    /// Can be called before process() to update the time sig details
    void sendTimeSig (int numerator, int denominator, uint32_t timeoutMilliseconds);
    /// Can be called before process() to update the BPM
    void sendBPM (float bpm, uint32_t timeoutMilliseconds);
    /// Can be called before process() to update the transport status
    void sendTransportState (bool isRecording, bool isPlaying, bool isLooping, uint32_t timeoutMilliseconds);
    /// Can be called before process() to update the playhead time
    void sendPosition (int64_t currentFrame, double ppq, double ppqBar, uint32_t timeoutMilliseconds);

    /// Sets a persistent value that should be saved and restored for this
    /// patch by the host.
    void setStoredStateValue (const std::string& key, const choc::value::ValueView& newValue);

    /// Iterates any persistent state values that have been stored
    const std::unordered_map<std::string, choc::value::Value>& getStoredStateValues() const;

    /// Returns an object containing the full state representing this patch,
    /// which includes both parameter values and custom stored values
    choc::value::Value getFullStoredState() const;

    /// Applies a state which was previously returned by getFullStoredState()
    bool setFullStoredState (const choc::value::ValueView& newState);

    //==============================================================================
    /// This must be supplied by the client using this class before trying to load a patch.
    std::function<cmaj::Engine()> createEngine;

    using HandleOutputEventFn = std::function<void(uint64_t frame, std::string_view endpointID, const choc::value::ValueView&)>;
    /// This must be supplied by the client before processing any data
    HandleOutputEventFn handleOutputEvent;

    // These are optional callbacks that the client can supply to be called when
    // various events occur:
    std::function<void()> stopPlayback,
                          startPlayback,
                          patchChanged;

    /// This struct is used by the statusChanged callback.
    struct Status
    {
        std::string statusMessage;
        cmaj::DiagnosticMessageList messageList;
    };

    /// A client can set this callback to be given patch status updates, such as
    /// build failures, etc.
    std::function<void(const Status&)> statusChanged;

    /// This object can optionally be provided if you have a build cache that you'd like
    /// the engine to use when compiling code.
    cmaj::CacheDatabaseInterface::Ptr cache;

    // These dispatch various types of event to any active views that the patch has open.
    void sendMessageToView (PatchView&, std::string_view type, const choc::value::ValueView&) const;
    void broadcastMessageToViews (std::string_view type, const choc::value::ValueView&) const;
    void sendPatchStatusChangeToViews() const;
    void sendParameterChangeToViews (const EndpointID&, float value) const;
    void sendCurrentParameterValueToViews (const EndpointID&) const;
    void sendCPUInfoToViews (float level) const;
    void sendStoredStateValueToViews (const std::string& key) const;

    // These can be called by things like the GUI to control the patch
    bool sendEventOrValueToPatch (const EndpointID&, const choc::value::ValueView&,
                                  int32_t rampFrames, uint32_t timeoutMilliseconds);

    bool sendMIDIInputEvent (const EndpointID&, choc::midi::ShortMessage,
                             uint32_t timeoutMilliseconds);

    void sendGestureStart (const EndpointID&);
    void sendGestureEnd (const EndpointID&);

    /// A client that is receiving messages over a websocket or other connection
    /// can call this to apply a message to the patch
    bool handleClientMessage (PatchView&, const choc::value::ValueView&);

    /// Sets the number of frames processed per CPU usage message
    void setCPUInfoMonitorChunkSize (uint32_t);

    /// Starts sending data messages to clients for a particular endpoint.
    /// The replyType is the type ID to use for the events that are sent to the client.
    /// For audio endpoints, granularity == 1 sends complete blocks of all incoming data
    /// For audio endpoints, granularity > 1 sends min/max ranges for each chunk of this many frames
    /// For non-audio endpoints, granularity is ignored.
    /// If fullAudioData is true, then instead of min/max levels, the entire audio stream is
    /// sent, in chunks that are the size specified by granularity.
    bool startEndpointData (PatchView&, const EndpointID&, std::string replyType,
                            uint32_t granularity, bool fullAudioData);

    /// Disables a client callback that was previously added with startEndpointData().
    bool stopEndpointData (PatchView&, const EndpointID&, std::string replyType);

    /// This base class is used for defining audio sources which can be attached to
    /// an audio input endpoint with `setCustomAudioSourceForInput()`
    struct CustomAudioSource
    {
        virtual ~CustomAudioSource() = default;
        virtual void prepare (double sampleRate) = 0;
        virtual void read (choc::buffer::InterleavedView<float>) = 0;
        virtual void read (choc::buffer::InterleavedView<double>) = 0;
    };

    using CustomAudioSourcePtr = std::shared_ptr<CustomAudioSource>;

    /// Sets (or clears) the custom audio source to attach to an audio input endpoint.
    void setCustomAudioSourceForInput (const EndpointID&, CustomAudioSourcePtr);

    CustomAudioSourcePtr getCustomAudioSourceForInput (const EndpointID&) const;

    std::function<choc::javascript::Context()> createContextForPatchWorker;

    /// A client can provide this callback to get a callback when something modifies
    /// one of the patches in the bundle
    std::function<void(PatchFileChangeChecker::ChangeType)> patchFilesChanged;

    /// A caller can supply a callback here to be told when an overrun or underrun
    /// in the FIFOs used to communicate with the process
    std::function<void()> handleXrun;

    /// A caller can supply a function here to be told when an infinite loop
    /// is detected in the running patch code. Set this before loading a patch,
    /// because if no callback is supplied, no checking will be done.
    std::function<void()> handleInfiniteLoop;


private:
    //==============================================================================
    struct PatchRenderer;
    struct PatchWorker;
    struct Build;
    struct BuildThread;
    friend struct PatchView;
    friend struct PatchParameter;

    bool scanFilesForChanges = false;
    LoadParams lastLoadParams;
    std::shared_ptr<PatchRenderer> renderer;
    PlaybackParams currentPlaybackParams;
    std::string hostDescription;
    std::unordered_map<std::string, CustomAudioSourcePtr> customAudioInputSources;
    std::unique_ptr<PatchFileChangeChecker> fileChangeChecker;
    std::vector<PatchView*> activeViews;
    std::unordered_map<std::string, choc::value::Value> storedState;

    struct ClientEventQueue;
    std::unique_ptr<ClientEventQueue> clientEventQueue;
    static constexpr uint32_t clientEventQueueSize = 65536;

    static constexpr uint32_t performerEventQueueSize = 65536;

    std::vector<choc::midi::ShortMessage> midiMessages;
    std::vector<int> midiMessageTimes;

    std::unique_ptr<BuildThread> buildThread;
    std::atomic<uint16_t> nextViewID { 0 };

    void sendPatchChange();
    void setNewRenderer (std::shared_ptr<PatchRenderer>);
    void sendOutputEventToViews (uint64_t frame, std::string_view endpointID, const choc::value::ValueView&);
    PatchView* findViewForID (uint16_t) const;
    void startCheckingForChanges();
    void handleFileChange (PatchFileChangeChecker::ChangeType);
    void failedToPushToPatch();
    void setStatus (std::string);
    void setErrorStatus (const std::string& error, const std::string& file, choc::text::LineAndColumn, bool unloadFirst);
    void addActiveView (PatchView&);
    void removeActiveView (PatchView&);
};

//==============================================================================
/// Represents a patch parameter, and provides various helpers to deal with
/// its range, text conversion, etc.
struct PatchParameter  : public std::enable_shared_from_this<PatchParameter>
{
    PatchParameter (std::shared_ptr<Patch::PatchRenderer>, const EndpointDetails&, EndpointHandle);

    /// Changes the parameter's value. For a default number of ramp frames, pass -1
    bool setValue (float newValue, bool forceSend, int32_t rampFrames, uint32_t timeoutMilliseconds);
    /// Changes the parameter's value. For a default number of ramp frames, pass -1
    bool setValue (const choc::value::ValueView&, bool forceSend, int32_t numRampFrames, uint32_t timeoutMilliseconds);
    /// Resets the parameter's value. For a default number of ramp frames, pass -1
    bool resetToDefaultValue (bool forceSend, int32_t numRampFrames, uint32_t timeoutMilliseconds);

    //==============================================================================
    const PatchParameterProperties properties;
    EndpointHandle endpointHandle;
    float currentValue = 0;

    // optional callback that's invoked when the value is changed
    std::function<void(float)> valueChanged;

    // optional callbacks for gesture start/end events
    std::function<void()> gestureStart, gestureEnd;

private:
    std::weak_ptr<Patch::PatchRenderer> renderer;
};

//==============================================================================
/// Base class for a GUI for a patch.
struct PatchView
{
    PatchView (Patch&);
    PatchView (Patch&, const PatchManifest::View&);
    virtual ~PatchView();

    void update (const PatchManifest::View&);

    void setActive (bool);
    bool isActive() const;
    bool isViewOf (Patch&) const;
    virtual void sendMessage (const choc::value::ValueView&) = 0;

    uint32_t width = 0, height = 0;
    bool resizable = true;

    Patch& patch;
    const uint16_t viewID;
};



//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================


struct Patch::ClientEventQueue
{
    ClientEventQueue (Patch& p) : patch (p)
    {
        cpu.handleCPULevel = [this] (float newLevel) { postCPULevel (newLevel); };
    }

    ~ClientEventQueue() { stop(); }

    void stop()
    {
        dispatchClientEventsCallback.reset();
        clientEventHandlerThread.stop();
    }

    void prepare (double sampleRate)
    {
        cpu.reset (sampleRate);
        fifo.reset (patch.clientEventQueueSize);
        dispatchClientEventsCallback = [this] { dispatchClientEvents(); };
        clientEventHandlerThread.start (0, [this]
        {
            choc::messageloop::postMessage ([dispatchEvents = dispatchClientEventsCallback] { dispatchEvents(); });
        });
    }

    void postParameterChange (const std::string& endpointID, float value)
    {
        auto endpointChars = endpointID.data();
        auto endpointLen = static_cast<uint32_t> (endpointID.length());

        fifo.push (5 + endpointLen, [=] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            d[0] = static_cast<char> (EventType::paramChange);
            choc::memory::writeNativeEndian (d + 1, value);
            memcpy (d + 5, endpointChars, endpointLen);
        });

        clientEventHandlerThread.trigger();
    }

    void dispatchParameterChange (const char* d, uint32_t size)
    {
        auto value = choc::memory::readNativeEndian<float> (d + 1);
        auto endpointID = std::string_view (d + 5, size - 5);
        patch.sendParameterChangeToViews (EndpointID::create (endpointID), value);
    }

    void postCPULevel (float level)
    {
        fifo.push (1 + sizeof (float), [&] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            d[0] = static_cast<char> (EventType::cpuLevel);
            choc::memory::writeNativeEndian (d + 1, level);
        });

        triggerDispatchOnEndOfBlock = true;
    }

    void dispatchCPULevel (const char* d)
    {
        auto value = choc::memory::readNativeEndian<float> (d + 1);
        patch.sendCPUInfoToViews (value);
    }

    void postAudioMinMax (const PatchView& view, const std::string& eventName, const choc::buffer::ChannelArrayBuffer<float>& levels)
    {
        triggerDispatchOnEndOfBlock = true;
        auto numChannels = levels.getNumChannels();

        auto eventNameChars = eventName.data();
        auto eventNameLen = static_cast<uint32_t> (eventName.length());

        fifo.push (5 + numChannels * sizeof (float) * 2 + eventNameLen, [&] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            *d++ = static_cast<char> (EventType::audioMinMaxLevels);
            choc::memory::writeNativeEndian<uint16_t> (d, view.viewID);
            d += sizeof (uint16_t);
            choc::memory::writeNativeEndian<uint16_t> (d, static_cast<uint16_t> (numChannels));
            d += sizeof (uint16_t);

            for (uint32_t chan = 0; chan < numChannels; ++chan)
            {
                auto i = levels.getIterator (chan);
                choc::memory::writeNativeEndian (d, *i);
                d += sizeof (float);
                ++i;
                choc::memory::writeNativeEndian (d, *i);
                d += sizeof (float);
            }

            memcpy (d, eventNameChars, eventNameLen);
        });
    }

    void dispatchAudioMinMax (const char* d, const char* end)
    {
        auto viewID = choc::memory::readNativeEndian<uint16_t> (++d);

        if (auto view = patch.findViewForID (viewID))
        {
            d += sizeof (uint16_t);
            auto numChannels = choc::memory::readNativeEndian<uint16_t> (d);
            d += sizeof (uint16_t);

            choc::SmallVector<float, 8> mins, maxs;

            for (uint32_t chan = 0; chan < numChannels; ++chan)
            {
                mins.push_back (choc::memory::readNativeEndian<float> (d));
                d += sizeof (float);
                maxs.push_back (choc::memory::readNativeEndian<float> (d));
                d += sizeof (float);
            }

            CMAJ_ASSERT (end > d);

            patch.sendMessageToView (*view, std::string_view (d, static_cast<std::string_view::size_type> (end - d)),
                                     choc::json::create (
                                        "min", choc::value::createArrayView (mins.data(), static_cast<uint32_t> (mins.size())),
                                        "max", choc::value::createArrayView (maxs.data(), static_cast<uint32_t> (maxs.size()))));
        }
    }

    void postAudioFullData (const PatchView& view, const std::string& eventName, const choc::buffer::ChannelArrayBuffer<float>& levels)
    {
        triggerDispatchOnEndOfBlock = true;
        auto numChannels = levels.getNumChannels();
        auto numFrames = levels.getNumFrames();
        CMAJ_ASSERT (numFrames < 65536);

        auto eventNameChars = eventName.data();
        auto eventNameLen = static_cast<uint32_t> (eventName.length());

        fifo.push (6 + numChannels * numFrames * sizeof (float) + eventNameLen, [&] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            *d++ = static_cast<char> (EventType::audioFullData);
            choc::memory::writeNativeEndian<uint16_t> (d, view.viewID);
            d += sizeof (uint16_t);
            *d++ = static_cast<char> (numChannels);
            choc::memory::writeNativeEndian<uint16_t> (d, static_cast<uint16_t> (numFrames));
            d += sizeof (uint16_t);

            for (uint32_t chan = 0; chan < numChannels; ++chan)
            {
                auto i = levels.getIterator (chan);

                for (uint32_t frame = 0; frame < numFrames; ++frame)
                {
                    choc::memory::writeNativeEndian (d, *i++);
                    d += sizeof (float);
                }
            }

            memcpy (d, eventNameChars, eventNameLen);
        });
    }

    void dispatchAudioFullData (const char* d, const char* end)
    {
        auto viewID = choc::memory::readNativeEndian<uint16_t> (++d);

        if (auto view = patch.findViewForID (viewID))
        {
            d += sizeof (uint16_t);

            auto numChannels = static_cast<uint32_t> (static_cast<uint8_t> (*d++));
            auto numFrames = static_cast<uint32_t> (choc::memory::readNativeEndian<uint16_t> (d));
            d += sizeof (uint16_t);
            auto audioData = d;
            d += numFrames * numChannels * sizeof (float);
            CMAJ_ASSERT (end > d);

            auto levels = choc::value::createArray (numChannels, [=] (uint32_t channel)
            {
                auto channelData = audioData + channel * numFrames * sizeof (float);

                return choc::value::createArray (numFrames, [=] (uint32_t frame)
                {
                    return choc::memory::readNativeEndian<float> (channelData + frame * sizeof (float));
                });
            });

            patch.sendMessageToView (*view, std::string_view (d, static_cast<std::string_view::size_type> (end - d)),
                                     choc::json::create ("data", std::move (levels)));
        }
    }

    void postEndpointEvent (const PatchView& view, const std::string& eventName, const void* messageData, uint32_t messageSize)
    {
        auto eventNameChars = eventName.data();
        auto eventNameLen = static_cast<uint32_t> (eventName.length());
        auto viewID = view.viewID;

        fifo.push (4 + eventNameLen + messageSize, [=] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            d[0] = static_cast<char> (EventType::endpointEvent);
            choc::memory::writeNativeEndian (d + 1, viewID);
            d[3] = static_cast<char> (eventNameLen);
            memcpy (d + 4, eventNameChars, eventNameLen);
            memcpy (d + 4 + eventNameLen, messageData, messageSize);
        });

        triggerDispatchOnEndOfBlock = true;
    }

    void postEndpointEvent (const PatchView& view, const std::string& eventName, const choc::value::ValueView& message)
    {
        auto serialisedMessage = message.serialise();
        postEndpointEvent (view, eventName, serialisedMessage.data.data(), static_cast<uint32_t> (serialisedMessage.data.size()));
    }

    void postEndpointMIDI (const PatchView& view, const std::string& eventName, choc::midi::ShortMessage message)
    {
        auto data = serialisedMIDIMessage.getSerialisedData (message);
        postEndpointEvent (view, eventName, data.data, data.size);
    }

    void dispatchEndpointEvent (const char* d, uint32_t size)
    {
        auto viewID = choc::memory::readNativeEndian<uint16_t> (d + 1);

        if (auto view = patch.findViewForID (viewID))
        {
            auto eventNameLen = d[3] == 0 ? 256u : static_cast<uint32_t> (static_cast<uint8_t> (d[3]));
            CMAJ_ASSERT (eventNameLen + 6 < size);
            auto valueData = choc::value::InputData { reinterpret_cast<const uint8_t*> (d + 4 + eventNameLen),
                                                      reinterpret_cast<const uint8_t*> (d + size) };

            patch.sendMessageToView (*view, std::string_view (d + 4, eventNameLen),
                                     choc::value::Value::deserialise (valueData));
        }
    }

    void startOfProcessCallback()
    {
        cpu.startProcess();
        framesProcessedInBlock = 0;
    }

    void postProcessChunk (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
    {
        framesProcessedInBlock += block.audioOutput.getNumFrames();
    }

    void endOfProcessCallback()
    {
        cpu.endProcess (framesProcessedInBlock);

        if (triggerDispatchOnEndOfBlock)
        {
            triggerDispatchOnEndOfBlock = false;
            clientEventHandlerThread.trigger();
        }
    }

    void dispatchClientEvents()
    {
        fifo.popAllAvailable ([this] (const void* data, uint32_t size)
        {
            auto d = static_cast<const char*> (data);

            switch (static_cast<EventType> (d[0]))
            {
                case EventType::paramChange:            dispatchParameterChange (d, size); break;
                case EventType::audioMinMaxLevels:      dispatchAudioMinMax (d, d + size); break;
                case EventType::audioFullData:          dispatchAudioFullData (d, d + size); break;
                case EventType::endpointEvent:          dispatchEndpointEvent (d, size); break;
                case EventType::cpuLevel:               dispatchCPULevel (d); break;
                default:                                break;
            }
        });
    }

    enum class EventType  : char
    {
        paramChange,
        audioMinMaxLevels,
        audioFullData,
        endpointEvent,
        cpuLevel
    };

    Patch& patch;
    choc::fifo::VariableSizeFIFO fifo;
    choc::threading::TaskThread clientEventHandlerThread;
    choc::threading::ThreadSafeFunctor<std::function<void()>> dispatchClientEventsCallback;
    MIDIEvents::SerialisedShortMIDIMessage serialisedMIDIMessage;
    bool triggerDispatchOnEndOfBlock = false;
    uint32_t framesProcessedInBlock = 0;

    CPUMonitor cpu;
};

//==============================================================================
/// This class manages a javascript patch worker thread
struct Patch::PatchWorker  : public PatchView
{
    PatchWorker (Patch& p, PatchManifest& m) : PatchView (p), manifest (m)
    {
        initCallback = [this] { initialiseWorker(); };
        runCodeCallback = [this] (const std::string& code) { runCode (code); };
        running = true;
        choc::messageloop::postMessage ([f = initCallback] { f(); });
    }

    ~PatchWorker() override
    {
        initCallback.reset();
        runCodeCallback.reset();
        context = {};
    }

    void sendMessage (const choc::value::ValueView& msg) override
    {
        if (running)
        {
            auto code = "currentView?.deliverMessageFromServer(" + choc::json::toString (msg, true) + ");";
            choc::messageloop::postMessage ([f = runCodeCallback, code = std::move (code)] { f (code); });
        }
    }

    void initialiseWorker()
    {
        try
        {
            // NB: Some types of context need to be created by the thread that uses it
            context = patch.createContextForPatchWorker();

            if (! context)
                return; // If a client deliberately provides a null context, we'll just not run the worker

            choc::javascript::Context::ReadModuleContentFn resolveModule = [this] (std::string_view path) -> std::optional<std::string>
            {
                return readJavascriptResource (path, std::addressof (manifest));
            };

            context.registerFunction ("cmaj_sendMessageToServer", [this] (choc::javascript::ArgumentList args) -> choc::value::Value
            {
                if (auto message = args[0])
                    patch.handleClientMessage (*this, *message);

                return {};
            });

            context.runModule (getGlueCode(), resolveModule);
        }
        catch (const std::exception& e)
        {
            patch.setErrorStatus (std::string ("Error in patch worker script: ") + e.what(), manifest.patchWorker, {}, true);
        }
    }

    void runCode (const std::string& code)
    {
        try
        {
            context.run (code);
        }
        catch (const std::exception& e)
        {
            patch.setErrorStatus (std::string ("Error in patch worker script: ") + e.what(), manifest.patchWorker, {}, true);
        }
    }

    std::string getGlueCode() const
    {
        return choc::text::replace (R"(
import { PatchConnection } from "./cmaj_api/cmaj-patch-connection.js"
import runWorker from WORKER_MODULE

class WorkerPatchConnection  extends PatchConnection
{
    constructor()
    {
        super();
        this.manifest = MANIFEST;
        globalThis.currentView = this;
    }

    getResourceAddress (path)
    {
        return path.startsWith ("/") ? path : ("/" + path);
    }

    sendMessageToServer (message)
    {
        cmaj_sendMessageToServer (message);
    }
}

const connection = new WorkerPatchConnection();

connection.readResource = (path) =>
{
    return {
        then (resolve, reject)
        {
            const data = _internalReadResource (path);

            if (data)
                resolve (data);
            else
                reject ("Failed to load resource");

            return this;
        },
        catch() {},
        finally() {}
    };
}

connection.readResourceAsAudioData = (path) =>
{
    return {
        then (resolve, reject)
        {
            const data = _internalReadResourceAsAudioData (path);

            if (data)
                resolve (data);
            else
                reject ("Failed to load resource");

            return this;
        },
        catch() {},
        finally() {}
    };
}

runWorker (connection);
)",
        "MANIFEST", choc::json::toString (manifest.manifest),
        "WORKER_MODULE", choc::json::getEscapedQuotedString (manifest.patchWorker));
    }

    PatchManifest& manifest;
    choc::javascript::Context context;
    choc::threading::ThreadSafeFunctor<std::function<void()>> initCallback;
    choc::threading::ThreadSafeFunctor<std::function<void(const std::string&)>> runCodeCallback;

private:
    bool running = false;
};

//==============================================================================
struct Patch::PatchRenderer  : public std::enable_shared_from_this<PatchRenderer>
{
    PatchRenderer (Patch& p) : patch (p)
    {
        handleOutputEvent = [&p] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
        {
            p.sendOutputEventToViews (frame, endpointID, v);
        };
    }

    ~PatchRenderer()
    {
        patchWorker.reset();
        infiniteLoopCheckTimer.clear();
        handleOutputEvent.reset();
    }

    bool isPlayable() const     { return performer != nullptr; }

    cmaj::AudioMIDIPerformer& getPerformer()
    {
        CMAJ_ASSERT (performer != nullptr);
        return *performer;
    }

    cmaj::AudioMIDIPerformer* getPerformerPointer() const
    {
        return performer.get();
    }

    //==============================================================================
    struct AudioLevelMonitor
    {
        AudioLevelMonitor (PatchView& v, const EndpointDetails& endpoint, std::string type, uint32_t gran, bool fullData)
            : view (v),
              endpointID (endpoint.endpointID.toString()),
              replyType (std::move (type)),
              sendFullData (fullData),
              granularity (gran >= minGranularity && gran <= maxGranularity ? gran : defaultGranularity)
        {
            auto numChannels = endpoint.getNumAudioChannels();
            CMAJ_ASSERT (numChannels > 0);

            if (sendFullData)
                levels.resize ({ numChannels, granularity });
            else
                levels.resize ({ numChannels, 2 });
        }

        template <typename SampleType>
        void process (ClientEventQueue& queue, const choc::buffer::InterleavedView<SampleType>& data)
        {
            if (sendFullData)
                processFullData (queue, data);
            else
                processMinMax (queue, data);
        }

        template <typename SampleType>
        void processMinMax (ClientEventQueue& queue, const choc::buffer::InterleavedView<SampleType>& data)
        {
            auto numFrames = data.getNumFrames();
            auto numChannels = data.getNumChannels();

            for (uint32_t i = 0; i < numFrames; ++i)
            {
                for (uint32_t chan = 0; chan < numChannels; ++chan)
                {
                    auto level = static_cast<float> (data.getSample (chan, i));
                    auto minMax = levels.getIterator (chan);

                    if (frameCount == 0)
                    {
                        *minMax = level;
                        ++minMax;
                        *minMax = level;
                    }
                    else
                    {
                        *minMax = std::min (level, *minMax);
                        ++minMax;
                        *minMax = std::max (level, *minMax);
                    }
                }

                if (++frameCount == granularity)
                {
                    frameCount = 0;
                    queue.postAudioMinMax (view, replyType, levels);
                }
            }
        }

        template <typename SampleType>
        void processFullData (ClientEventQueue& queue, const choc::buffer::InterleavedView<SampleType>& data)
        {
            auto numFrames = data.getNumFrames();
            choc::buffer::FrameCount sourceStart = 0;

            while (numFrames != 0)
            {
                auto numToAdd = std::min (numFrames, granularity - frameCount);

                copy (levels.getFrameRange ({ frameCount, frameCount + numToAdd }),
                      data.getFrameRange ({ sourceStart, sourceStart + numToAdd }));

                frameCount += numToAdd;

                if (frameCount == granularity)
                {
                    frameCount = 0;
                    queue.postAudioFullData (view, replyType, levels);
                    break;
                }

                sourceStart += numToAdd;
                numFrames -= numToAdd;
            }
        }

        bool isFor (PatchView& v, const EndpointID& e, const std::string& type) const
        {
            return std::addressof (view) == std::addressof (v) && replyType == type && endpointID == e.toString();
        }

        PatchView& view;
        const std::string endpointID, replyType;
        const bool sendFullData;
        const uint32_t granularity;

        static constexpr uint32_t minGranularity = 64;
        static constexpr uint32_t maxGranularity = 8192;
        static constexpr uint32_t defaultGranularity = 500;

    private:
        choc::buffer::ChannelArrayBuffer<float> levels;
        uint32_t frameCount = 0;
    };

    //==============================================================================
    bool createPerformer (AudioMIDIPerformer::Builder& builder)
    {
        performer = builder.createPerformer();
        CMAJ_ASSERT (performer);

        if (! performer->prepareToStart())
            return false;

        framesLatency = performer->performer.getLatency();
        return true;
    }

    //==============================================================================
    struct DataListener  : public AudioMIDIPerformer::AudioDataListener
    {
        DataListener (ClientEventQueue& c) : queue (c) {}

        void process (const choc::buffer::InterleavedView<float>& block) override
        {
            if (customSource != nullptr)
                customSource->read (block);

            for (auto& m : audioMonitors)
                m->process (queue, block);
        }

        void process (const choc::buffer::InterleavedView<double>& block) override
        {
            if (customSource != nullptr)
                customSource->read (block);

            for (auto& m : audioMonitors)
                m->process (queue, block);
        }

        bool removeMonitor (PatchView& view, const EndpointID& e, const std::string& type)
        {
            auto oldEnd = audioMonitors.end();
            auto newEnd = std::remove_if (audioMonitors.begin(), oldEnd,
                                          [&] (auto& m) { return m->isFor (view, e, type); });

            if (newEnd != oldEnd)
            {
                audioMonitors.erase (newEnd, oldEnd);
                return true;
            }

            return false;
        }

        void removeMonitorsForView (PatchView& view)
        {
            auto oldEnd = audioMonitors.end();
            auto newEnd = std::remove_if (audioMonitors.begin(), oldEnd,
                                          [&] (auto& m) { return std::addressof (m->view) == std::addressof (view); });

            if (newEnd != oldEnd)
                audioMonitors.erase (newEnd, oldEnd);
        }

        ClientEventQueue& queue;
        CustomAudioSourcePtr customSource;
        std::vector<std::unique_ptr<AudioLevelMonitor>> audioMonitors;
    };

    //==============================================================================
    void build (cmaj::Engine& engine,
                LoadParams& loadParams,
                const PlaybackParams& playbackParams,
                bool shouldResolveExternals,
                bool shouldLink,
                const cmaj::CacheDatabaseInterface::Ptr& c,
                const std::function<void()>& checkForStopSignal,
                uint32_t eventFIFOSize)
    {
        try
        {
            manifest = std::move (loadParams.manifest);

            if (! loadProgram (engine, playbackParams, shouldResolveExternals, checkForStopSignal))
                return;

            if (! shouldResolveExternals)
                return;

            checkForStopSignal();
            AudioMIDIPerformer::Builder performerBuilder (engine, eventFIFOSize);
            scanEndpointList (engine);
            checkForStopSignal();
            sampleRate = playbackParams.sampleRate;
            connectPerformerEndpoints (playbackParams, performerBuilder);
            checkForStopSignal();

            if (! shouldLink)
                return;

            if (! engine.link (errors, c.get()))
                return;

            lastBuildLog = engine.getLastBuildLog();

            if (performerBuilder.setEventOutputHandler ([this] { outputEventsReady(); }))
                startOutputEventThread();

            if (createPerformer (performerBuilder))
                applyParameterValues (loadParams.parameterValues, 0, 0);
        }
        catch (const choc::json::ParseError& e)
        {
            errors.add (cmaj::DiagnosticMessage::createError (std::string (e.what()) + ":" + e.lineAndColumn.toString(), {}));
        }
        catch (const std::runtime_error& e)
        {
            errors.add (cmaj::DiagnosticMessage::createError (e.what(), {}));
        }
    }

    bool loadProgram (cmaj::Engine& engine,
                      const PlaybackParams& playbackParams,
                      bool shouldResolveExternals,
                      const std::function<void()>& checkForStopSignal)
    {
        cmaj::Program program;

        if (manifest.needsToBuildSource)
        {
            for (auto& file : manifest.sourceFiles)
            {
                checkForStopSignal();

                if (auto content = manifest.readFileContent (file))
                {
                    if (! program.parse (errors, manifest.getFullPathForFile (file), std::move (*content)))
                        return false;
                }
                else
                {
                    errors.add (cmaj::DiagnosticMessage::createError ("Could not open source file: " + file, {}));
                    return false;
                }
            }
        }

        engine.setBuildSettings (engine.getBuildSettings()
                                   .setFrequency (playbackParams.sampleRate)
                                   .setMaxBlockSize (playbackParams.blockSize)
                                   .setMainProcessor (manifest.mainProcessor));

        checkForStopSignal();

        if (engine.load (errors, program,
                         shouldResolveExternals ? manifest.createExternalResolverFunction()
                                                : [] (const cmaj::ExternalVariable&) -> choc::value::Value { return {}; },
                         {}))
        {
            programDetails = engine.getProgramDetails();
            inputEndpoints = engine.getInputEndpoints();
            outputEndpoints = engine.getOutputEndpoints();
            return true;
        }

        return false;
    }

    //==============================================================================
    void scanEndpointList (const Engine& engine)
    {
        for (auto& e : inputEndpoints)
        {
            if (e.isParameter())
            {
                auto patchParam = std::make_shared<PatchParameter> (shared_from_this(), e, engine.getEndpointHandle (e.endpointID));
                parameterList.push_back (patchParam);
                parameterIDMap[e.endpointID.toString()] = std::move (patchParam);
            }
            else if (auto numAudioChans = e.getNumAudioChannels())
            {
                numAudioInputChans += numAudioChans;
            }
            else
            {
                if (e.isTimelineTimeSignature())        { timeSigEventID = e.endpointID;        hasTimecodeInputs = true; }
                else if (e.isTimelinePosition())        { positionEventID = e.endpointID;       hasTimecodeInputs = true; }
                else if (e.isTimelineTransportState())  { transportStateEventID = e.endpointID; hasTimecodeInputs = true; }
                else if (e.isTimelineTempo())           { tempoEventID = e.endpointID;          hasTimecodeInputs = true; }
            }
        }

        for (auto& e : outputEndpoints)
        {
            if (auto numAudioChans = e.getNumAudioChannels())
            {
                numAudioOutputChans += numAudioChans;
            }
            else if (e.isMIDI())
            {
                hasMIDIOutputs = true;
            }
        }
    }

    void connectPerformerEndpoints (const PlaybackParams& playbackParams,
                                    AudioMIDIPerformer::Builder& performerBuilder)
    {
        uint32_t inputChanIndex = 0;

        for (auto& e : inputEndpoints)
        {
            if (auto numChans = e.getNumAudioChannels())
            {
                std::vector<uint32_t> inChans, endpointChans;

                for (uint32_t i = 0; i < numChans; ++i)
                {
                    if (inputChanIndex >= playbackParams.numInputChannels)
                        break;

                    endpointChans.push_back (i);
                    inChans.push_back (inputChanIndex);

                    if (playbackParams.numInputChannels != 1)
                        ++inputChanIndex;
                }

                performerBuilder.connectAudioInputTo (inChans, e, endpointChans,
                                                      createAudioDataListener (e.endpointID));
            }
            else if (e.isMIDI())
            {
                performerBuilder.connectMIDIInputTo (e);
            }
        }

        uint32_t outputChanIndex = 0;
        uint32_t totalOutputChans = 0;

        for (auto& e : outputEndpoints)
            totalOutputChans += e.getNumAudioChannels();

        for (auto& e : outputEndpoints)
        {
            if (auto numChans = e.getNumAudioChannels())
            {
                std::vector<uint32_t> outChans, endpointChans;

                // Handle mono -> stereo as a special case
                if (totalOutputChans == 1 && playbackParams.numOutputChannels > 1)
                {
                    outChans.push_back (0);
                    endpointChans.push_back (outputChanIndex);
                    outChans.push_back (1);
                    endpointChans.push_back (outputChanIndex);
                }
                else
                {
                    for (uint32_t i = 0; i < numChans; ++i)
                    {
                        if (outputChanIndex >= playbackParams.numOutputChannels)
                            break;

                        endpointChans.push_back (i);
                        outChans.push_back (outputChanIndex);

                        if (playbackParams.numOutputChannels != 1)
                            ++outputChanIndex;
                    }
                }

                performerBuilder.connectAudioOutputTo (e, endpointChans, outChans,
                                                       createAudioDataListener (e.endpointID));
            }
            else if (e.isMIDI())
            {
                performerBuilder.connectMIDIOutputTo (e);
            }
        }
    }

    std::shared_ptr<DataListener> createAudioDataListener (const EndpointID& endpointID)
    {
        auto l = std::make_shared<PatchRenderer::DataListener> (*patch.clientEventQueue);

        if (auto s = patch.getCustomAudioSourceForInput (endpointID))
        {
            l->customSource = s;
            s->prepare (sampleRate);
        }

        endpointListeners.add (endpointID, l);
        return l;
    }

    const EndpointDetails* findEndpointDetails (const EndpointID& e) const
    {
        for (auto& d : inputEndpoints)
            if (d.endpointID == e)
                return std::addressof (d);

        for (auto& d : outputEndpoints)
            if (d.endpointID == e)
                return std::addressof (d);

        return {};
    }

    bool setCustomAudioSource (const EndpointID& e, CustomAudioSourcePtr source)
    {
        if (auto l = endpointListeners.findAudioDataListener (e))
        {
            if (source != nullptr)
                source->prepare (sampleRate);

            std::lock_guard<decltype(processLock)> lock (processLock);
            l->customSource = source;
            return true;
        }

        return false;
    }

    bool startEndpointData (PatchView& view, const EndpointID& e, std::string replyType, uint32_t granularity, bool fullData)
    {
        if (auto details = findEndpointDetails (e))
        {
            if (auto l = endpointListeners.findAudioDataListener (e))
            {
                auto monitor = std::make_unique<AudioLevelMonitor> (view, *details, std::move (replyType), granularity, fullData);

                std::lock_guard<decltype(processLock)> lock (processLock);
                l->audioMonitors.push_back (std::move (monitor));
                return true;
            }

            if (details->isEvent())
            {
                auto monitor = std::make_unique<EndpointListeners::EventMonitor> (view, *details, std::move (replyType));

                std::lock_guard<decltype(processLock)> lock (processLock);
                endpointListeners.add (std::move (monitor));
                return true;
            }
        }

        return false;
    }

    bool stopEndpointData (PatchView& view, const EndpointID& e, std::string replyType)
    {
        std::lock_guard<decltype(processLock)> lock (processLock);
        return endpointListeners.remove (view, e, replyType);
    }

    //==============================================================================
    void sendTimeSig (int numerator, int denominator, uint32_t timeoutMilliseconds)
    {
        if (timeSigEventID)
            performer->postEvent (timeSigEventID, timelineEvents.getTimeSigEvent (numerator, denominator),
                                  timeoutMilliseconds);
    }

    void sendBPM (float bpm, uint32_t timeoutMilliseconds)
    {
        if (tempoEventID)
            performer->postEvent (tempoEventID, timelineEvents.getBPMEvent (bpm),
                                  timeoutMilliseconds);
    }

    void sendTransportState (bool isRecording, bool isPlaying, bool isLooping, uint32_t timeoutMilliseconds)
    {
        if (transportStateEventID)
            performer->postEvent (transportStateEventID, timelineEvents.getTransportStateEvent (isRecording, isPlaying, isLooping),
                                  timeoutMilliseconds);
    }

    void sendPosition (int64_t currentFrame, double quarterNote, double barStartQuarterNote, uint32_t timeoutMilliseconds)
    {
        if (positionEventID)
            performer->postEvent (positionEventID, timelineEvents.getPositionEvent (currentFrame, quarterNote, barStartQuarterNote),
                                  timeoutMilliseconds);
    }

    //==============================================================================
    PatchParameter* findParameter (const EndpointID& endpointID)
    {
        if (endpointID)
        {
            auto param = parameterIDMap.find (endpointID.toString());

            if (param != parameterIDMap.end())
                return param->second.get();
        }

        return {};
    }

    void applyParameterValues (const std::unordered_map<std::string, float>& values, int32_t rampFrames, uint32_t timeoutMilliseconds) const
    {
        for (auto& p : parameterIDMap)
        {
            auto oldValue = values.find (p.first);

            if (oldValue != values.end())
                p.second->setValue (oldValue->second, true, rampFrames, timeoutMilliseconds);
            else
                p.second->resetToDefaultValue (true, rampFrames, timeoutMilliseconds);
        }
    }

    bool sendEventOrValueToPatch (ClientEventQueue& queue, const EndpointID& endpointID, const choc::value::ValueView& value,
                                  int32_t rampFrames, uint32_t timeoutMilliseconds)
    {
        if (performer == nullptr)
            return false;

        if (auto param = findParameter (endpointID))
            return param->setValue (value, false, rampFrames, timeoutMilliseconds);

        if (! performer->postEventOrValue (endpointID, value, rampFrames > 0 ? (uint32_t) rampFrames : 0,
                                           timeoutMilliseconds))
            return false;

        for (auto& m : endpointListeners.eventMonitors)
            m->process (queue, endpointID.toString(), value);

        return true;
    }

    bool sendMIDIInputEvent (ClientEventQueue& queue, const EndpointID& endpointID,
                             choc::midi::ShortMessage message, uint32_t timeoutMilliseconds)
    {
        auto value = cmaj::MIDIEvents::createMIDIMessageObject (message);

        if (! performer->postEvent (endpointID, value, timeoutMilliseconds))
            return false;

        for (auto& m : endpointListeners.eventMonitors)
            m->process (queue, endpointID.toString(), value);

        return true;
    }

    void sendGestureStart (const EndpointID& endpointID)
    {
        if (auto param = findParameter (endpointID))
            if (param->gestureStart)
                param->gestureStart();
    }

    void sendGestureEnd (const EndpointID& endpointID)
    {
        if (auto param = findParameter (endpointID))
            if (param->gestureEnd)
                param->gestureEnd();
    }

    void resetToInitialState()
    {
        if (performer == nullptr)
            return;

        auto newPerformer = performer->engine.createPerformer();
        CMAJ_ASSERT (newPerformer);

        {
            std::lock_guard<decltype(processLock)> lock (processLock);
            std::swap (performer->performer, newPerformer);
        }

        for (auto& param : parameterList)
            param->resetToDefaultValue (true, -1, 0);
    }

    void beginProcessBlock()    { processLock.lock(); }
    void endProcessBlock()      { processLock.unlock(); }

    //==============================================================================
    bool postParameterChange (const PatchParameterProperties& properties, EndpointHandle endpointHandle,
                              float newValue, int32_t numRampFrames, uint32_t timeoutMilliseconds)
    {
        if (performer)
        {
            bool ok = properties.isEvent
                        ? performer->postEvent (endpointHandle, choc::value::createFloat32 (newValue),
                                                timeoutMilliseconds)
                        : performer->postValue (endpointHandle, choc::value::createFloat32 (newValue),
                                                numRampFrames >= 0 ? static_cast<uint32_t> (numRampFrames)
                                                                   : properties.rampFrames,
                                                timeoutMilliseconds);

            if (! ok)
                return false;
        }

        patch.clientEventQueue->postParameterChange (properties.endpointID, newValue);
        return true;
    }

    //==============================================================================
    void processMIDIMessage (choc::midi::ShortMessage message)
    {
        for (auto& monitor : endpointListeners.eventMonitors)
            if (monitor->isMIDI)
                monitor->process (*patch.clientEventQueue, monitor->endpointID, message);
    }

    void processMIDIBlock (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
    {
        if (! block.midiMessages.empty())
            for (auto& monitor : endpointListeners.eventMonitors)
                if (monitor->isMIDI)
                    for (auto& m : block.midiMessages)
                        monitor->process (*patch.clientEventQueue, monitor->endpointID, m);
    }

    //==============================================================================
    void startOutputEventThread()
    {
        outputEventThread.start (0, [this] { sendOutputEventMessages(); });
    }

    void outputEventsReady()
    {
        outputEventThread.trigger();
    }

    void sendOutputEventMessages()
    {
        performer->handlePendingOutputEvents ([this] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& value)
        {
            choc::messageloop::postMessage ([handler = handleOutputEvent,
                                             frame,
                                             endpointID = std::string (endpointID),
                                             value = addTypeToValueAsProperty (value)]
                                            {
                                                handler (frame, endpointID, value);
                                            });
        });
    }

    void removeReferencesToView (PatchView& v)
    {
        std::lock_guard<decltype(processLock)> lock (processLock);
        endpointListeners.removeReferencesToView (v);
    }

    void startPatchWorker()
    {
        if (! manifest.patchWorker.empty())
        {
            // You must provide a function to create a patch worker context before building
            CMAJ_ASSERT (patch.createContextForPatchWorker != nullptr);
            patchWorker = std::make_unique<PatchWorker> (patch, manifest);
        }
    }

    void startInfiniteLoopCheck (std::function<void()> handleInfiniteLoopFn)
    {
        if (performer != nullptr)
        {
            infiniteLoopCheckTimer = choc::messageloop::Timer (300, [this, handleInfiniteLoopFn]
            {
                if (performer->isStuckInInfiniteLoop (1000))
                    handleInfiniteLoopFn();

                return true;
            });
        }
    }

    Patch& patch;
    PatchManifest manifest;
    cmaj::DiagnosticMessageList errors;
    std::string lastBuildLog;

    choc::value::Value programDetails;
    std::vector<PatchParameterPtr> parameterList;
    cmaj::EndpointDetailsList inputEndpoints, outputEndpoints;
    double sampleRate = 0;
    double framesLatency = 0;
    uint32_t numAudioInputChans = 0;
    uint32_t numAudioOutputChans = 0;
    bool hasMIDIInputs = false;
    bool hasMIDIOutputs = false;
    bool hasTimecodeInputs = false;

    std::unique_ptr<PatchWorker> patchWorker;

    //==============================================================================
    struct EndpointListeners
    {
        struct EventMonitor
        {
            EventMonitor (PatchView& v, const EndpointDetails& e, std::string type)
                : view (v), endpointID (e.endpointID.toString()), replyType (std::move (type)), isMIDI (e.isMIDI())
            {
            }

            bool process (ClientEventQueue& queue, const std::string& endpoint, const choc::value::ValueView& message)
            {
                if (endpointID == endpoint)
                {
                    queue.postEndpointEvent (view, replyType, message);
                    return true;
                }

                return false;
            }

            bool process (ClientEventQueue& queue, const std::string& endpoint, choc::midi::ShortMessage message)
            {
                if (isMIDI && endpointID == endpoint)
                {
                    queue.postEndpointMIDI (view, replyType, message);
                    return true;
                }

                return false;
            }

            bool isFor (PatchView& v, const EndpointID& e, const std::string& type) const
            {
                return std::addressof (view) == std::addressof (v) && replyType == type && endpointID == e.toString();
            }

            PatchView& view;
            const std::string endpointID, replyType;
            const bool isMIDI;
        };

        void add (std::unique_ptr<EventMonitor> m)
        {
            eventMonitors.push_back (std::move (m));
        }

        void add (const EndpointID& e, std::shared_ptr<PatchRenderer::DataListener> l)
        {
            dataListeners[e.toString()] = l;
        }

        bool remove (PatchView& view, const EndpointID& e, std::string replyType)
        {
            if (auto l = findAudioDataListener (e))
                return l->removeMonitor (view, e, replyType);

            auto oldEnd = eventMonitors.end();
            auto newEnd = std::remove_if (eventMonitors.begin(), oldEnd,
                                          [&] (auto& m) { return m->isFor (view, e, replyType); });

            if (newEnd != oldEnd)
            {
                eventMonitors.erase (newEnd, oldEnd);
                return true;
            }

            return false;
        }

        DataListener* findAudioDataListener (const EndpointID& e) const
        {
            if (auto l = dataListeners.find (e.toString()); l != dataListeners.end())
                return l->second.get();

            return {};
        }

        void sendOutputEventToViews (Patch& p, std::string_view endpointID, const choc::value::ValueView& value)
        {
            if (! value.isVoid())
                for (auto& m : eventMonitors)
                    if (m->endpointID == endpointID)
                        p.sendMessageToView (m->view, m->replyType, value);
        }

        void removeReferencesToView (PatchView& v)
        {
            auto oldEnd = eventMonitors.end();
            auto newEnd = std::remove_if (eventMonitors.begin(), oldEnd,
                                          [&] (auto& m) { return std::addressof (m->view) == std::addressof (v); });

            if (newEnd != oldEnd)
                eventMonitors.erase (newEnd, oldEnd);

            for (auto& d : dataListeners)
                d.second->removeMonitorsForView (v);
        }

        std::unordered_map<std::string, std::shared_ptr<DataListener>> dataListeners;
        std::vector<std::unique_ptr<EventMonitor>> eventMonitors;
    };

    EndpointListeners endpointListeners;

private:
    std::unique_ptr<cmaj::AudioMIDIPerformer> performer;
    std::unordered_map<std::string, PatchParameterPtr> parameterIDMap;
    choc::threading::ThreadSafeFunctor<HandleOutputEventFn> handleOutputEvent;
    choc::threading::TaskThread outputEventThread;
    choc::messageloop::Timer infiniteLoopCheckTimer;
    TimelineEventGenerator timelineEvents;
    cmaj::EndpointID timeSigEventID, tempoEventID, transportStateEventID, positionEventID;

    // The process callback is protected by a mutex that is uncontended during normal
    // playback. The only times that another thread may lock it are when a newly compiled
    // engine is being swapped over, or a new custom source is being applied.
    std::mutex processLock;
};

//==============================================================================
struct Patch::Build
{
    Build (Patch& p, LoadParams lp, bool shouldResolveExternals, bool shouldLink)
       : patch (p), loadParams (std::move (lp)),
         resolveExternals (shouldResolveExternals),
         performLink (shouldLink)
    {}

    cmaj::DiagnosticMessageList& getMessageList()
    {
        CMAJ_ASSERT (renderer != nullptr);
        return renderer->errors;
    }

    Engine build (const std::function<void()>& checkForStopSignal)
    {
        CMAJ_ASSERT (patch.createEngine);
        auto engine = patch.createEngine();
        CMAJ_ASSERT (engine);

        renderer = std::make_shared<PatchRenderer> (patch);
        renderer->build (engine, loadParams, patch.currentPlaybackParams,
                         resolveExternals, performLink, patch.cache,
                         checkForStopSignal, patch.performerEventQueueSize);
        return engine;
    }

    std::shared_ptr<PatchRenderer> takeRenderer()
    {
        CMAJ_ASSERT (renderer != nullptr);
        return std::move (renderer);
    }

private:
    Patch& patch;
    LoadParams loadParams;
    const bool resolveExternals, performLink;
    std::shared_ptr<PatchRenderer> renderer;
    std::unique_ptr<AudioMIDIPerformer::Builder> performerBuilder;
};

//==============================================================================
struct Patch::BuildThread
{
    BuildThread (Patch& p) : owner (p)
    {
        handleBuildMessage = [this] { handleFinishedBuild(); };
    }

    ~BuildThread()
    {
        handleBuildMessage.reset();
        clearTaskList();
    }

    void startBuild (std::unique_ptr<Build> build)
    {
        std::lock_guard<decltype(buildLock)> lock (buildLock);
        cancelBuild();
        activeTasks.push_back (std::make_unique<BuildTask> (*this, std::move (build)));
    }

    void cancelBuild()
    {
        for (auto& t : activeTasks)
            t->cancelled = true;
    }

private:
    struct BuildTask
    {
        BuildTask (BuildThread& o, std::unique_ptr<Build> b) : owner (o), build (std::move (b))
        {
            thread = std::thread ([this] { run(); });
        }

        ~BuildTask()
        {
            cancelled = true;
            finished = true;
            thread.join();
        }

        void run()
        {
            struct Interrupted {};

            try
            {
                build->build ([this]
                {
                    if (cancelled)
                        throw Interrupted();
                });

                owner.handleFinishedBuildAsync();
                finished = true;
            }
            catch (Interrupted) {}
        }

        BuildThread& owner;
        std::unique_ptr<Build> build;
        std::atomic<bool> cancelled { false }, finished { false };
        std::thread thread;
    };

    Patch& owner;
    std::mutex buildLock;
    std::vector<std::unique_ptr<BuildTask>> activeTasks;
    choc::threading::ThreadSafeFunctor<std::function<void()>> handleBuildMessage;

    void handleFinishedBuildAsync()
    {
        choc::messageloop::postMessage (handleBuildMessage);
    }

    void handleFinishedBuild()
    {
        std::unique_ptr<BuildTask> finishedTask;

        {
            std::lock_guard<decltype(buildLock)> lock (buildLock);

            if (activeTasks.empty())
                return;

            auto& current = activeTasks.back();

            if (! current->finished)
                return handleFinishedBuildAsync();

            if (! current->cancelled)
                finishedTask = std::move (current);

            activeTasks.clear();
        }

        if (finishedTask && finishedTask->build)
            owner.setNewRenderer (finishedTask->build->takeRenderer());
    }

    void clearTaskList()
    {
        cancelBuild();
        activeTasks.clear();
    }
};

//==============================================================================
inline Patch::Patch()
{
    const size_t midiBufferSize = 256;
    midiMessageTimes.reserve (midiBufferSize);
    midiMessages.reserve (midiBufferSize);

    clientEventQueue = std::make_unique<ClientEventQueue> (*this);
}

inline Patch::~Patch()
{
    unload();
    clientEventQueue.reset();
}

inline bool Patch::preload (const PatchManifest& m)
{
    // You must provide a function to create a patch worker context before building
    CMAJ_ASSERT (createContextForPatchWorker != nullptr);

    LoadParams params;
    params.manifest = m;

    auto build = std::make_unique<Build> (*this, params, false, false);
    build->build ([] {});
    setNewRenderer (build->takeRenderer());
    return renderer != nullptr && ! renderer->errors.hasErrors();
}

inline bool Patch::loadPatch (const LoadParams& params, bool synchronous)
{
    // You must provide a function to create a patch worker context before building
    CMAJ_ASSERT (createContextForPatchWorker != nullptr);

    if (! currentPlaybackParams.isValid())
        return false;

    fileChangeChecker.reset();

    if (std::addressof (lastLoadParams) != std::addressof (params))
        lastLoadParams = params;

    auto build = std::make_unique<Build> (*this, params, true, true);

    setStatus ("Loading: " + params.manifest.manifestFile);

    if (synchronous)
    {
        build->build ([] {});
        setNewRenderer (build->takeRenderer());
        return isPlayable();
    }

    if (buildThread == nullptr)
        buildThread = std::make_unique<BuildThread> (*this);

    buildThread->startBuild (std::move (build));
    return true;
}

inline bool Patch::loadPatchFromManifest (PatchManifest&& m, bool synchronous)
{
    LoadParams params;

    try
    {
        params.manifest = std::move (m);
        params.manifest.reload();
    }
    catch (const choc::json::ParseError& e)
    {
        setErrorStatus (e.what(), params.manifest.manifestFile, e.lineAndColumn, true);
        lastLoadParams = params;
        startCheckingForChanges();
        return false;
    }
    catch (const std::runtime_error& e)
    {
        setErrorStatus (e.what(), params.manifest.manifestFile, {}, true);
        lastLoadParams = params;
        startCheckingForChanges();
        return false;
    }

    return loadPatch (params, synchronous);
}

inline bool Patch::loadPatchFromFile (const std::string& patchFile, bool synchronous)
{
    PatchManifest manifest;
    manifest.createFileReaderFunctions (patchFile);
    return loadPatchFromManifest (std::move (manifest), synchronous);
}

inline Engine::CodeGenOutput Patch::generateCode (const LoadParams& params, const std::string& target, const std::string& options)
{
    unload();

    auto build = std::make_unique<Build> (*this, params, true, false);
    auto engine = build->build ([] {});

    if (build->getMessageList().hasErrors())
    {
        Engine::CodeGenOutput result;
        result.messages = build->getMessageList();
        return result;
    }

    return engine.generateCode (target, options);
}

inline void Patch::unload()
{
    clientEventQueue->stop();

    if (renderer)
    {
        if (stopPlayback)
            stopPlayback();

        renderer.reset();
        sendPatchChange();
        setStatus ({});
        customAudioInputSources.clear();
    }
}

inline std::string Patch::getLastBuildLog() const
{
    return renderer != nullptr ? renderer->lastBuildLog : std::string();
}

inline void Patch::setAutoRebuildOnFileChange (bool shouldMonitorFilesForChanges)
{
    scanFilesForChanges = shouldMonitorFilesForChanges;

    if (! scanFilesForChanges)
        fileChangeChecker.reset();
}

inline void Patch::startCheckingForChanges()
{
    fileChangeChecker.reset();

    if (scanFilesForChanges && lastLoadParams.manifest.needsToBuildSource)
        if (lastLoadParams.manifest.getFileModificationTime != nullptr)
            fileChangeChecker = std::make_unique<PatchFileChangeChecker> (lastLoadParams.manifest, [this] (auto c) { handleFileChange (c); });
}

inline void Patch::setStatus (std::string message)
{
    if (statusChanged)
        statusChanged ({ message, {} });
}

inline void Patch::setErrorStatus (const std::string& error, const std::string& file,
                                   choc::text::LineAndColumn lineAndCol, bool unloadFirst)
{
    if (unloadFirst)
        unload();

    if (statusChanged)
    {
        cmaj::FullCodeLocation location;
        location.filename = file;
        location.lineAndColumn = lineAndCol;

        Status s;
        s.messageList.add (cmaj::DiagnosticMessage::createError (error, location));
        s.statusMessage = s.messageList.toString();
        statusChanged (s);
    }
}

inline void Patch::handleFileChange (PatchFileChangeChecker::ChangeType change)
{
    rebuild();

    if (patchFilesChanged)
        patchFilesChanged (change);
}

inline void Patch::rebuild()
{
    try
    {
        if (isPlayable())
            for (auto& param : getParameterList())
                lastLoadParams.parameterValues[param->properties.endpointID] = param->currentValue;

        if (lastLoadParams.manifest.reload())
        {
            loadPatch (lastLoadParams, false);
            return;
        }
    }
    catch (const choc::json::ParseError& e)
    {
        if (auto f = getManifestFile(); ! f.empty())
            setErrorStatus (e.what(), f, e.lineAndColumn, true);
        else
            setErrorStatus (e.what(), {}, e.lineAndColumn, true);
    }
    catch (const std::runtime_error& e)
    {
        if (auto f = getManifestFile(); ! f.empty())
            setErrorStatus (e.what(), f, {}, true);
        else
            setErrorStatus (e.what(), {}, {}, true);
    }

    startCheckingForChanges();
}

inline void Patch::resetToInitialState()
{
    if (renderer != nullptr)
        renderer->resetToInitialState();
}

inline Patch::PlaybackParams::PlaybackParams (double rate, uint32_t bs, choc::buffer::ChannelCount ins, choc::buffer::ChannelCount outs)
    : sampleRate (rate), blockSize (bs), numInputChannels (ins), numOutputChannels (outs)
{}

inline bool Patch::PlaybackParams::operator== (const PlaybackParams& other) const
{
    return sampleRate == other.sampleRate
        && blockSize == other.blockSize
        && numInputChannels == other.numInputChannels
        && numOutputChannels == other.numOutputChannels;
}

inline bool Patch::PlaybackParams::operator!= (const PlaybackParams& other) const
{
    return ! (*this == other);
}

inline void Patch::setPlaybackParams (PlaybackParams newParams)
{
    if (currentPlaybackParams != newParams)
    {
        currentPlaybackParams = newParams;
        rebuild();
    }
}

inline void Patch::setHostDescription (const std::string& h)
{
    hostDescription = h;
}

inline std::string Patch::getHostDescription() const
{
    return hostDescription;
}

inline std::string Patch::getUID() const
{
    return isLoaded() ? renderer->manifest.ID
                      : "cmajor";
}

inline std::string Patch::getName() const
{
    return isLoaded() && ! renderer->manifest.name.empty()
            ? renderer->manifest.name
            : "Cmajor Patch Loader";
}

inline const PatchManifest* Patch::getManifest() const      { return renderer != nullptr ? std::addressof (renderer->manifest) : nullptr; }

inline std::string Patch::getManifestFile() const
{
    if (auto m = getManifest())
        return m->getFullPathForFile (m->manifestFile);

    if (lastLoadParams.manifest.getFullPathForFile)
        return lastLoadParams.manifest.getFullPathForFile (lastLoadParams.manifest.manifestFile);

    return {};
}

inline bool Patch::isPlayable() const                       { return renderer != nullptr && renderer->isPlayable(); }
inline std::string Patch::getDescription() const            { return renderer != nullptr ? renderer->manifest.description : std::string(); }
inline std::string Patch::getManufacturer() const           { return renderer != nullptr ? renderer->manifest.manufacturer : std::string(); }
inline std::string Patch::getVersion() const                { return renderer != nullptr ? renderer->manifest.version : std::string(); }
inline std::string Patch::getCategory() const               { return renderer != nullptr ? renderer->manifest.category : std::string(); }
inline std::string Patch::getPatchFile() const              { return renderer != nullptr ? renderer->manifest.manifestFile : std::string(); }
inline bool Patch::isInstrument() const                     { return renderer != nullptr && renderer->manifest.isInstrument; }
inline bool Patch::hasMIDIInput() const                     { return renderer != nullptr && renderer->hasMIDIInputs; }
inline bool Patch::hasMIDIOutput() const                    { return renderer != nullptr && renderer->hasMIDIOutputs; }
inline bool Patch::hasAudioInput() const                    { return renderer != nullptr && renderer->numAudioInputChans != 0; }
inline bool Patch::hasAudioOutput() const                   { return renderer != nullptr && renderer->numAudioOutputChans != 0; }
inline bool Patch::wantsTimecodeEvents() const              { return renderer != nullptr && renderer->hasTimecodeInputs; }
inline double Patch::getFramesLatency() const               { return renderer != nullptr ? renderer->framesLatency : 0.0; }
inline choc::value::Value Patch::getProgramDetails() const  { return renderer != nullptr ? renderer->programDetails : choc::value::Value(); }

inline std::string Patch::getMainProcessorName() const
{
    if (renderer != nullptr && renderer->programDetails.isObject())
        return renderer->programDetails["mainProcessor"].toString();

    return {};
}

inline EndpointDetailsList Patch::getInputEndpoints() const
{
    if (renderer != nullptr)
        return renderer->inputEndpoints;

    return {};
}

inline EndpointDetailsList Patch::getOutputEndpoints() const
{
    if (renderer != nullptr)
        return renderer->outputEndpoints;

    return {};
}

inline choc::span<PatchParameterPtr> Patch::getParameterList() const
{
    if (renderer != nullptr)
        return renderer->parameterList;

    return {};
}

inline PatchParameterPtr Patch::findParameter (const EndpointID& endpointID) const
{
    if (renderer)
        if (auto p = renderer->findParameter (endpointID))
            return p->shared_from_this();

    return {};
}

inline void Patch::addMIDIMessage (int frameIndex, const void* data, uint32_t length)
{
    if (length < 4)
    {
        auto message = choc::midi::ShortMessage (data, static_cast<size_t> (length));
        midiMessages.push_back (message);
        midiMessageTimes.push_back (frameIndex);

        if (renderer != nullptr)
            renderer->processMIDIMessage (message);
    }
}

inline void Patch::process (float* const* audioChannels, uint32_t numFrames,
                            const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn& handleMIDIOut)
{
    beginChunkedProcess();
    renderer->getPerformer().processWithTimeStampedMIDI (choc::buffer::createChannelArrayView (audioChannels, currentPlaybackParams.numInputChannels, numFrames),
                                                         choc::buffer::createChannelArrayView (audioChannels, currentPlaybackParams.numOutputChannels, numFrames),
                                                         midiMessages.data(), midiMessageTimes.data(), static_cast<uint32_t> (midiMessages.size()),
                                                         handleMIDIOut, true);
    midiMessages.clear();
    midiMessageTimes.clear();
    endChunkedProcess();
}

inline void Patch::process (const choc::audio::AudioMIDIBlockDispatcher::Block& block, bool replaceOutput)
{
    beginChunkedProcess();
    processChunk (block, replaceOutput);
    endChunkedProcess();
}

inline void Patch::beginChunkedProcess()
{
    clientEventQueue->startOfProcessCallback();
    renderer->beginProcessBlock();
}

inline void Patch::processChunk (const choc::audio::AudioMIDIBlockDispatcher::Block& block, bool replaceOutput)
{
    renderer->getPerformer().process (block, replaceOutput);
    clientEventQueue->postProcessChunk (block);
    renderer->processMIDIBlock (block);
}

inline void Patch::endChunkedProcess()
{
    clientEventQueue->endOfProcessCallback();
    renderer->endProcessBlock();
}

inline void Patch::failedToPushToPatch()
{
    if (handleXrun)
        handleXrun();
}

inline void Patch::sendTimeSig (int numerator, int denominator, uint32_t timeoutMilliseconds)
{
    renderer->sendTimeSig (numerator, denominator, timeoutMilliseconds);
}

inline void Patch::sendBPM (float bpm, uint32_t timeoutMilliseconds)
{
    renderer->sendBPM (bpm, timeoutMilliseconds);
}

inline void Patch::sendTransportState (bool isRecording, bool isPlaying, bool isLooping, uint32_t timeoutMilliseconds)
{
    renderer->sendTransportState (isRecording, isPlaying, isLooping, timeoutMilliseconds);
}

inline void Patch::sendPosition (int64_t currentFrame, double ppq, double ppqBar, uint32_t timeoutMilliseconds)
{
    renderer->sendPosition (currentFrame, ppq, ppqBar, timeoutMilliseconds);
}

inline void Patch::sendMessageToView (PatchView& view, std::string_view type, const choc::value::ValueView& message) const
{
    if (std::find (activeViews.begin(), activeViews.end(), std::addressof (view)) != activeViews.end())
        view.sendMessage (choc::json::create ("type", type,
                                              "message", message));
}

inline void Patch::broadcastMessageToViews (std::string_view type, const choc::value::ValueView& message) const
{
    auto msg = choc::json::create ("type", type,
                                   "message", message);

    for (auto pv : activeViews)
        pv->sendMessage (msg);
}

inline void Patch::sendPatchStatusChangeToViews() const
{
    if (renderer)
    {
        broadcastMessageToViews ("status",
                                 choc::json::create (
                                    "error", renderer->errors.toString(),
                                    "manifest", renderer->manifest.manifest,
                                    "details", renderer->programDetails,
                                    "sampleRate", renderer->sampleRate,
                                    "host", hostDescription));
    }
}

inline const std::unordered_map<std::string, choc::value::Value>& Patch::getStoredStateValues() const
{
    return storedState;
}

inline void Patch::setStoredStateValue (const std::string& key, const choc::value::ValueView& newValue)
{
    auto& v = storedState[key];

    if (v != newValue)
    {
        if (newValue.isVoid())
            storedState.erase (key);
        else
            v = std::move (newValue);

        sendStoredStateValueToViews (key);
    }
}

inline choc::value::Value Patch::getFullStoredState() const
{
    auto values = choc::value::createObject ({});

    for (auto& value : storedState)
        values.addMember (value.first, value.second);

    std::vector<PatchParameter*> paramsToSave;
    paramsToSave.reserve (256);

    for (auto& param : getParameterList())
        if (param->currentValue != param->properties.defaultValue)
            paramsToSave.push_back (param.get());

    auto parameters = choc::value::createArray (static_cast<uint32_t> (paramsToSave.size()),
                                                [&] (uint32_t i)
    {
        return choc::json::create ("name", paramsToSave[i]->properties.endpointID,
                                   "value", paramsToSave[i]->currentValue);
    });

    return choc::json::create ("parameters", parameters,
                               "values", values);
}

inline bool Patch::setFullStoredState (const choc::value::ValueView& newState)
{
    if (! newState.isObject())
        return false;

    uint32_t sendParamTimeoutMillisecs = 10;

    if (auto params = newState["parameters"]; params.isArray() && params.size() != 0)
    {
        std::unordered_map<std::string, float> explicitParamValues;

        for (auto paramValue : params)
            if (paramValue.isObject())
                if (auto name = paramValue["name"].toString(); ! name.empty())
                    if (auto value = paramValue["value"]; value.isFloat() || value.isInt())
                        explicitParamValues[name] = value.getWithDefault<float> (0);

        for (auto& param : getParameterList())
        {
            auto newValue = explicitParamValues.find (param->properties.endpointID);

            if (newValue != explicitParamValues.end())
                param->setValue (newValue->second, true, -1, sendParamTimeoutMillisecs);
            else
                param->resetToDefaultValue (true, -1, sendParamTimeoutMillisecs);
        }
    }
    else
    {
        for (auto& param : getParameterList())
            param->resetToDefaultValue (true, -1, sendParamTimeoutMillisecs);
    }

    std::unordered_set<std::string> storedValuesToRemove;

    for (auto& state : storedState)
        storedValuesToRemove.insert (state.first);

    if (auto values = newState["values"]; values.isObject())
    {
        for (uint32_t i = 0; i < values.size(); ++i)
        {
            auto member = values.getObjectMemberAt (i);
            setStoredStateValue (member.name, member.value);
            storedValuesToRemove.erase (member.name);
        }
    }

    for (auto& key : storedValuesToRemove)
        setStoredStateValue (key, {});

    return true;
}

inline void Patch::sendParameterChangeToViews (const EndpointID& endpointID, float value) const
{
    if (endpointID)
        broadcastMessageToViews ("param_value",
                                 choc::json::create ("endpointID", endpointID.toString(),
                                                     "value", value));
}

inline void Patch::sendCPUInfoToViews (float level) const
{
    broadcastMessageToViews ("cpu_info",
                             choc::json::create ("level", level));
}

inline void Patch::sendStoredStateValueToViews (const std::string& key) const
{
    if (! key.empty())
    {
        auto found = storedState.find (key);
        auto value = found != storedState.end() ? found->second : choc::value::Value();

        broadcastMessageToViews ("state_key_value",
                                 choc::json::create ("key", key,
                                                     "value", value));
    }
}

inline void Patch::sendPatchChange()
{
    if (isLoaded())
        sendPatchStatusChangeToViews();

    if (patchChanged)
        patchChanged();
}

inline void Patch::setNewRenderer (std::shared_ptr<PatchRenderer> newRenderer)
{
    if (renderer == nullptr && newRenderer == nullptr)
        return;

    if (stopPlayback)
        stopPlayback();

    fileChangeChecker.reset();
    renderer.reset();
    sendPatchChange();

    if (newRenderer != nullptr)
    {
        renderer = std::move (newRenderer);
        sendPatchChange();

        if (isPlayable())
        {
            clientEventQueue->prepare (renderer->sampleRate);
            renderer->startPatchWorker();

            if (startPlayback)
                startPlayback();

            if (handleInfiniteLoop)
                renderer->startInfiniteLoopCheck (handleInfiniteLoop);
        }

        if (statusChanged)
        {
            Status s;

            if (renderer->errors.hasErrors())
                s.statusMessage = renderer->errors.toString();
            else
                s.statusMessage = getName().empty() ? std::string() : "Loaded: " + getName();

            s.messageList = renderer->errors;
            statusChanged (s);
        }
    }

    startCheckingForChanges();
}

inline void Patch::addActiveView (PatchView& v)
{
    activeViews.push_back (std::addressof (v));
}

inline void Patch::removeActiveView (PatchView& v)
{
    const auto i = std::find (activeViews.begin(), activeViews.end(), std::addressof (v));

    if (i != activeViews.end())
    {
        activeViews.erase (i);

        if (renderer != nullptr)
            renderer->removeReferencesToView (v);
    }
}

inline PatchView* Patch::findViewForID (uint16_t viewID) const
{
    for (auto& v : activeViews)
        if (v->viewID == viewID)
            return v;

    return {};
}

inline void Patch::sendOutputEventToViews (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
{
    handleOutputEvent (frame, endpointID, v);

    if (renderer != nullptr)
        renderer->endpointListeners.sendOutputEventToViews (*this, endpointID, v);
}

inline bool Patch::sendEventOrValueToPatch (const EndpointID& endpointID, const choc::value::ValueView& value,
                                            int32_t rampFrames, uint32_t timeoutMilliseconds)
{
    if (renderer == nullptr)
        return false;

    if (renderer->sendEventOrValueToPatch (*clientEventQueue, endpointID, value, rampFrames, timeoutMilliseconds))
        return true;

    failedToPushToPatch();
    return false;
}

inline bool Patch::sendMIDIInputEvent (const EndpointID& endpointID, choc::midi::ShortMessage message, uint32_t timeoutMilliseconds)
{
    if (renderer == nullptr)
        return false;

    if (renderer->sendMIDIInputEvent (*clientEventQueue, endpointID, message, timeoutMilliseconds))
        return true;

    failedToPushToPatch();
    return false;
}

inline void Patch::sendGestureStart (const EndpointID& endpointID)
{
    if (renderer != nullptr)
        renderer->sendGestureStart (endpointID);
}

inline void Patch::sendGestureEnd (const EndpointID& endpointID)
{
    if (renderer != nullptr)
        renderer->sendGestureEnd (endpointID);
}

inline void Patch::sendCurrentParameterValueToViews (const EndpointID& endpointID) const
{
    if (auto param = findParameter (endpointID))
        sendParameterChangeToViews (endpointID, param->currentValue);
}

inline bool Patch::startEndpointData (PatchView& view, const EndpointID& endpointID, std::string replyType, uint32_t granularity, bool fullData)
{
    return renderer != nullptr && renderer->startEndpointData (view, endpointID, replyType, granularity, fullData);
}

inline bool Patch::stopEndpointData (PatchView& view, const EndpointID& endpointID, std::string replyType)
{
    return renderer != nullptr && renderer->stopEndpointData (view, endpointID, replyType);
}

inline Patch::CustomAudioSourcePtr Patch::getCustomAudioSourceForInput (const EndpointID& e) const
{
    if (auto s = customAudioInputSources.find (e.toString()); s != customAudioInputSources.end())
        return s->second;

    return {};
}

inline void Patch::setCustomAudioSourceForInput (const EndpointID& e, Patch::CustomAudioSourcePtr source)
{
    if (source != nullptr)
        customAudioInputSources[e.toString()] = source;
    else
        customAudioInputSources.erase (e.toString());

    if (renderer != nullptr)
        renderer->setCustomAudioSource (e, source);
}

inline void Patch::setCPUInfoMonitorChunkSize (uint32_t framesPerCallback)
{
    clientEventQueue->cpu.framesPerCallback = framesPerCallback;
}

inline bool Patch::handleClientMessage (PatchView& sourceView, const choc::value::ValueView& msg)
{
    if (! msg.isObject())
        return false;

    if (auto typeMember = msg["type"]; typeMember.isString())
    {
        auto type = typeMember.getString();

        if (type == "send_value")
        {
            if (! sendEventOrValueToPatch (cmaj::EndpointID::create (msg["id"].toString()),
                                           msg["value"],
                                           msg["rampFrames"].getWithDefault<int32_t> (-1),
                                           static_cast<uint32_t> (std::max (0, msg["timeout"].getWithDefault<int32_t> (0)))))
                failedToPushToPatch();

            return true;
        }

        if (type == "send_gesture_start")
        {
            sendGestureStart (cmaj::EndpointID::create (msg["id"].getString()));
            return true;
        }

        if (type == "send_gesture_end")
        {
            sendGestureEnd (cmaj::EndpointID::create (msg["id"].getString()));
            return true;
        }

        if (type == "req_status")
        {
            sendPatchStatusChangeToViews();
            return true;
        }

        if (type == "req_param_value")
        {
            sendCurrentParameterValueToViews (cmaj::EndpointID::create (msg["id"].getString()));
            return true;
        }

        if (type == "req_reset")
        {
            resetToInitialState();
            return true;
        }

        if (type == "req_state_value")
        {
            sendStoredStateValueToViews (msg["key"].toString());
            return true;
        }

        if (type == "send_state_value")
        {
            setStoredStateValue (msg["key"].toString(), msg["value"]);
            return true;
        }

        if (type == "req_full_state")
        {
            if (auto replyType = msg["replyType"].toString(); ! replyType.empty())
                sendMessageToView (sourceView, replyType, getFullStoredState());

            return true;
        }

        if (type == "send_full_state")
        {
            if (auto value = msg["value"]; value.isObject())
                setFullStoredState (value);

            return true;
        }

        if (type == "load_patch")
        {
            if (auto file = msg["file"].toString(); ! file.empty())
                return loadPatchFromFile (file, false);

            unload();
            return true;
        }

        if (type == "add_endpoint_listener")
        {
            startEndpointData (sourceView,
                               cmaj::EndpointID::create (msg["endpoint"].toString()),
                               msg["replyType"].toString(),
                               static_cast<uint32_t> (msg["granularity"].getWithDefault<int64_t> (0)),
                               msg["fullAudioData"].getWithDefault<bool> (false));
            return true;
        }

        if (type == "remove_endpoint_listener")
        {
            stopEndpointData (sourceView,
                              cmaj::EndpointID::create (msg["endpoint"].toString()),
                              msg["replyType"].toString());
            return true;
        }

        if (type == "set_cpu_info_rate")
        {
            setCPUInfoMonitorChunkSize (static_cast<uint32_t> (msg["framesPerCallback"].getWithDefault<int64_t> (0)));
            return true;
        }

        if (type == "unload")
        {
            unload();
            return true;
        }
    }

    return false;
}

//==============================================================================
inline PatchParameter::PatchParameter (std::shared_ptr<Patch::PatchRenderer> r, const EndpointDetails& details, cmaj::EndpointHandle handle)
    : properties (details), endpointHandle (handle), currentValue (properties.defaultValue), renderer (std::move (r))
{
}

inline bool PatchParameter::setValue (float newValue, bool forceSend, int32_t numRampFrames, uint32_t timeoutMilliseconds)
{
    newValue = properties.snapAndConstrainValue (newValue);

    if (currentValue != newValue || forceSend)
    {
        currentValue = newValue;

        if (auto r = renderer.lock())
            if (! r->postParameterChange (properties, endpointHandle, newValue, numRampFrames, timeoutMilliseconds))
                return false;

        if (valueChanged)
            valueChanged (newValue);
    }

    return true;
}

inline bool PatchParameter::setValue (const choc::value::ValueView& v, bool forceSend, int32_t numRampFrames, uint32_t timeoutMilliseconds)
{
    return setValue (properties.parseValue (v), forceSend, numRampFrames, timeoutMilliseconds);
}

inline bool PatchParameter::resetToDefaultValue (bool forceSend, int32_t numRampFrames, uint32_t timeoutMilliseconds)
{
    return setValue (properties.defaultValue, forceSend, numRampFrames, timeoutMilliseconds);
}

//==============================================================================
inline PatchView::PatchView (Patch& p) : PatchView (p, {})
{}

inline PatchView::PatchView (Patch& p, const PatchManifest::View& view)
    : patch (p), viewID (++p.nextViewID)
{
    update (view);
    setActive (true);
}

inline PatchView::~PatchView()
{
    setActive (false);
}

inline void PatchView::update (const PatchManifest::View& view)
{
    width = view.getWidth();
    height = view.getHeight();
    resizable = view.isResizable();

    if (width < 50  || width > 10000)  width = 600;
    if (height < 50 || height > 10000) height = 400;
}

inline void PatchView::setActive (bool active)
{
    if (active != isActive())
    {
        if (active)
            patch.addActiveView (*this);
        else
            patch.removeActiveView (*this);
    }
}

inline bool PatchView::isActive() const
{
    return std::find (patch.activeViews.begin(), patch.activeViews.end(), this) != patch.activeViews.end();
}

inline bool PatchView::isViewOf (Patch& p) const
{
    return std::addressof (p) == std::addressof (patch);
}

} // namespace cmaj
