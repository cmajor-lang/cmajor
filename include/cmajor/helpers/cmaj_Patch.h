//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

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
    Patch (bool buildSynchronously, bool keepCheckingFilesForChanges);
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
    bool loadPatch (const LoadParams&);

    /// Tries to load a patch from a file path
    bool loadPatchFromFile (const std::string& patchFilePath);

    /// Tries to load a patch from a manifest
    bool loadPatchFromManifest (PatchManifest&&);

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

    PlaybackParams getPlaybackParams() const        { return currentPlaybackParams; }

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
    void sendTimeSig (int numerator, int denominator);
    /// Can be called before process() to update the BPM
    void sendBPM (float bpm);
    /// Can be called before process() to update the transport status
    void sendTransportState (bool isRecording, bool isPlaying, bool isLooping);
    /// Can be called before process() to update the playhead time
    void sendPosition (int64_t currentFrame, double ppq, double ppqBar);

    /// Sets a persistent string that should be saved and restored for this
    /// patch by the host.
    void setStoredStateValue (const std::string& key, std::string newValue);

    /// Iterates any persistent state values that have been stored
    const std::unordered_map<std::string, std::string>& getStoredStateValues() const;

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
    void sendMessageToViews (std::string_view type, const choc::value::ValueView&) const;
    void sendPatchStatusChangeToViews() const;
    void sendParameterChangeToViews (const EndpointID&, float value) const;
    void sendCurrentParameterValueToViews (const EndpointID&) const;
    void sendOutputEventToViews (std::string_view endpointID, const choc::value::ValueView&) const;
    void sendCPUInfoToViews (float level) const;
    void sendStoredStateValueToViews (const std::string& key) const;

    // These can be called by things like the GUI to control the patch
    bool handleClientMessage (const choc::value::ValueView&);
    void sendEventOrValueToPatch (const EndpointID&, const choc::value::ValueView&, int32_t rampFrames = -1) const;
    void sendMIDIInputEvent (const EndpointID&, choc::midi::ShortMessage) const;

    void sendGestureStart (const EndpointID&) const;
    void sendGestureEnd (const EndpointID&) const;

    void setCPUInfoMonitorChunkSize (uint32_t);

    /// Enables/disables monitoring data for an endpoint.
    /// If granularity == 0, monitoring is disabled.
    /// For audio endpoints, granularity == 1 sends complete blocks of all incoming data
    /// For audio endpoints, granularity > 1 sends min/max ranges for each chunk of this many frames
    /// For MIDI endpoints, granularity != 0 sends all MIDI events
    /// For other types of endpoint, granularity != 0 sends all events as objects
    void setEndpointAudioMinMaxNeeded (const EndpointID&, uint32_t granularity);

    /// Enables/disables monitoring of events going to an endpoint. This moves a listener
    /// count up/down, so each call that enables events must be matched by a call to
    /// disable them.
    void setEndpointEventsNeeded (const EndpointID&, bool active);

    /// This base class is used for defining audio sources which can be attached to
    /// an audio input endpoint with `setCustomAudioSourceForInput()`
    struct CustomAudioSource
    {
        virtual ~CustomAudioSource() = default;
        virtual void prepare (double sampleRate) = 0;
        virtual void read (choc::buffer::InterleavedView<float>) = 0;
    };

    using CustomAudioSourcePtr = std::shared_ptr<CustomAudioSource>;

    /// Sets (or clears) the custom audio source to attach to an audio input endpoint.
    void setCustomAudioSourceForInput (const EndpointID&, CustomAudioSourcePtr);

    CustomAudioSourcePtr getCustomAudioSourceForInput (const EndpointID&) const;

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
    struct Build;
    struct BuildThread;
    friend struct PatchView;
    friend struct PatchParameter;

    const bool scanFilesForChanges;
    LoadParams lastLoadParams;
    std::shared_ptr<PatchRenderer> renderer;
    PlaybackParams currentPlaybackParams;
    std::unordered_map<std::string, CustomAudioSourcePtr> customAudioInputSources;
    std::unique_ptr<PatchFileChangeChecker> fileChangeChecker;
    std::vector<PatchView*> activeViews;
    std::unordered_map<std::string, std::string> storedState;

    struct ClientEventQueue;
    std::unique_ptr<ClientEventQueue> clientEventQueue;

    std::vector<choc::midi::ShortMessage> midiMessages;
    std::vector<int> midiMessageTimes;

    std::unique_ptr<BuildThread> buildThread;

    void sendPatchChange();
    void setNewRenderer (std::shared_ptr<PatchRenderer>);
    void sendOutputEvent (uint64_t frame, std::string_view endpointID, const choc::value::ValueView&) const;
    void startCheckingForChanges();
    void handleFileChange (PatchFileChangeChecker::ChangeType);
    void setStatus (std::string);
    void setErrorStatus (const std::string& error, const std::string& file, choc::text::LineAndColumn, bool unloadFirst);
};

//==============================================================================
/// Represents a patch parameter, and provides various helpers to deal with
/// its range, text conversion, etc.
struct PatchParameter  : public std::enable_shared_from_this<PatchParameter>
{
    PatchParameter (std::shared_ptr<Patch::PatchRenderer>, const EndpointDetails&, EndpointHandle);

    void setValue (float newValue, bool forceSend, int32_t explicitRampFrames = -1);
    void setValue (const choc::value::ValueView&, bool forceSend, int32_t explicitRampFrames = -1);
    void resetToDefaultValue (bool forceSend);

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
        struct FakeSerialiser
        {
            FakeSerialiser (const choc::value::Type& t) { t.serialise (*this); }
            size_t size = 0;
            void write (const void*, size_t s) { size += s; }
        };

        auto midiMessage = cmaj::MIDIEvents::createMIDIMessageObject (choc::midi::ShortMessage (0x90, 0, 0));
        serialisedMIDIMessage = midiMessage.serialise();
        serialisedMIDIContent = serialisedMIDIMessage.data.data() + FakeSerialiser (midiMessage.getType()).size;

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
        fifo.reset (8192);
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

    void postAudioMinMaxUpdate (const std::string& endpointID, const choc::buffer::ChannelArrayBuffer<float>& levels)
    {
        triggerDispatchOnEndOfBlock = true;
        auto numChannels = levels.getNumChannels();

        auto endpointChars = endpointID.data();
        auto endpointLen = static_cast<uint32_t> (endpointID.length());

        fifo.push (2 + numChannels * sizeof (float) * 2 + endpointLen, [&] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            *d++ = static_cast<char> (EventType::audioLevels);
            *d++ = static_cast<char> (numChannels);

            for (uint32_t chan = 0; chan < numChannels; ++chan)
            {
                auto i = levels.getIterator (chan);
                choc::memory::writeNativeEndian (d, *i);
                d += sizeof (float);
                ++i;
                choc::memory::writeNativeEndian (d, *i);
                d += sizeof (float);
            }

            memcpy (d, endpointChars, endpointLen);
        });
    }

    void dispatchAudioMinMaxUpdate (const char* d, const char* end)
    {
        ++d;
        auto numChannels = static_cast<uint32_t> (static_cast<uint8_t> (*d++));
        choc::SmallVector<float, 8> mins, maxs;

        for (uint32_t chan = 0; chan < numChannels; ++chan)
        {
            mins.push_back (choc::memory::readNativeEndian<float> (d));
            d += sizeof (float);
            maxs.push_back (choc::memory::readNativeEndian<float> (d));
            d += sizeof (float);
        }

        CMAJ_ASSERT (end > d);

        patch.sendMessageToViews ("audio_data_" + std::string (d, static_cast<std::string_view::size_type> (end - d)),
                                  choc::json::create (
                                     "min", choc::value::createArrayView (mins.data(), static_cast<uint32_t> (mins.size())),
                                     "max", choc::value::createArrayView (maxs.data(), static_cast<uint32_t> (maxs.size()))));
    }

    void postEndpointEvent (const std::string& endpointID, const void* messageData, uint32_t messageSize)
    {
        auto endpointChars = endpointID.data();
        auto endpointLen = static_cast<uint32_t> (endpointID.length());

        fifo.push (2 + endpointLen + messageSize, [=] (void* dest)
        {
            auto d = static_cast<char*> (dest);
            d[0] = static_cast<char> (EventType::endpointEvent);
            d[1] = static_cast<char> (endpointLen);
            memcpy (d + 2, endpointChars, endpointLen);
            memcpy (d + 2 + endpointLen, messageData, messageSize);
        });

        triggerDispatchOnEndOfBlock = true;
    }

    void postEndpointEvent (const std::string& endpointID, const choc::value::ValueView& message)
    {
        auto serialisedMessage = message.serialise();
        postEndpointEvent (endpointID, serialisedMessage.data.data(), static_cast<uint32_t> (serialisedMessage.data.size()));
    }

    void postEndpointMIDI (const std::string& endpointID, choc::midi::ShortMessage message)
    {
        auto packedMIDI = cmaj::MIDIEvents::midiMessageToPackedInt (message);
        memcpy (serialisedMIDIContent, std::addressof (packedMIDI), 4);
        postEndpointEvent (endpointID, serialisedMIDIMessage.data.data(), static_cast<uint32_t> (serialisedMIDIMessage.data.size()));
    }

    void dispatchEndpointEvent (const char* d, uint32_t size)
    {
        auto endpointLen = d[1] == 0 ? 256u : static_cast<uint32_t> (static_cast<uint8_t> (d[1]));
        CMAJ_ASSERT (endpointLen + 4 < size);
        auto valueData = choc::value::InputData { reinterpret_cast<const uint8_t*> (d + 2 + endpointLen),
                                                  reinterpret_cast<const uint8_t*> (d + size) };

        patch.sendMessageToViews ("event_" + std::string (d + 2, endpointLen),
                                  choc::value::Value::deserialise (valueData));
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
                case EventType::audioLevels:            dispatchAudioMinMaxUpdate (d, d + size); break;
                case EventType::endpointEvent:          dispatchEndpointEvent (d, size); break;
                case EventType::cpuLevel:               dispatchCPULevel (d); break;
                default:                                break;
            }
        });
    }

    enum class EventType  : char
    {
        paramChange,
        audioLevels,
        endpointEvent,
        cpuLevel
    };

    Patch& patch;
    choc::fifo::VariableSizeFIFO fifo;
    choc::threading::TaskThread clientEventHandlerThread;
    choc::threading::ThreadSafeFunctor<std::function<void()>> dispatchClientEventsCallback;
    choc::value::SerialisedData serialisedMIDIMessage;
    void* serialisedMIDIContent = {};
    bool triggerDispatchOnEndOfBlock = false;
    uint32_t framesProcessedInBlock = 0;

    CPUMonitor cpu;
};

//==============================================================================
struct Patch::PatchRenderer  : public std::enable_shared_from_this<PatchRenderer>
{
    PatchRenderer (const Patch& p) : patch (p)
    {
        handleOutputEvent = [&p] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
        {
            p.sendOutputEvent (frame, endpointID, v);
        };
    }

    ~PatchRenderer()
    {
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
        AudioLevelMonitor (const EndpointDetails& endpoint)
            : endpointID (endpoint.endpointID.toString())
        {
            auto numChannels = endpoint.getNumAudioChannels();
            CMAJ_ASSERT (numChannels > 0);

            levels.resize ({ numChannels, 2 });
        }

        template <typename SampleType>
        void process (ClientEventQueue& queue, const choc::buffer::InterleavedView<SampleType>& data)
        {
            if (auto framesPerChunk = granularity.load())
            {
                // handle min/max chunks
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

                    if (++frameCount == framesPerChunk)
                    {
                        frameCount = 0;
                        queue.postAudioMinMaxUpdate (endpointID, levels);
                    }
                }
            }
        }

        std::atomic<uint32_t> granularity { 0 };
        std::string endpointID;

    private:
        choc::buffer::ChannelArrayBuffer<float> levels;
        uint32_t frameCount = 0;
    };

    //==============================================================================
    struct EventMonitor
    {
        EventMonitor (const EndpointDetails& endpoint)
           : endpointID (endpoint.endpointID.toString()), isMIDI (endpoint.isMIDI())
        {
        }

        bool process (ClientEventQueue& queue, const std::string& endpoint, const choc::value::ValueView& message)
        {
            if (activeListeners > 0 && endpointID == endpoint)
            {
                queue.postEndpointEvent (endpointID, message);
                return true;
            }

            return false;
        }

        bool process (ClientEventQueue& queue, const std::string& endpoint, choc::midi::ShortMessage message)
        {
            if (isMIDI && activeListeners > 0 && endpointID == endpoint)
            {
                queue.postEndpointMIDI (endpointID, message);
                return true;
            }

            return false;
        }

        std::string endpointID;
        bool isMIDI;
        std::atomic<int32_t> activeListeners { 0 };
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
                audioEndpointMonitors.push_back (std::make_unique<AudioLevelMonitor> (e));
            }
            else
            {
                eventEndpointMonitors.push_back (std::make_unique<EventMonitor> (e));

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
                audioEndpointMonitors.push_back (std::make_unique<AudioLevelMonitor> (e));
            }
            else if (e.isMIDI())
            {
                hasMIDIOutputs = true;
            }
        }
    }

    //==============================================================================
    struct DataListener  : public AudioMIDIPerformer::AudioDataListener
    {
        DataListener (ClientEventQueue& c, AudioLevelMonitor& m) : queue (c), monitor (m) {}

        void process (const choc::buffer::InterleavedView<float>&  block) override
        {
            if (customSource != nullptr)
                customSource->read (block);

            monitor.process (queue, block);
        }

        void process (const choc::buffer::InterleavedView<double>& block) override
        {
            // TODO: handle custom source here
            monitor.process (queue, block);
        }

        CustomAudioSourcePtr customSource;
        ClientEventQueue& queue;
        AudioLevelMonitor& monitor;
    };

    //==============================================================================
    void build (cmaj::Engine& engine,
                LoadParams& loadParams,
                const PlaybackParams& playbackParams,
                bool shouldResolveExternals,
                bool shouldLink,
                const cmaj::CacheDatabaseInterface::Ptr& c,
                const std::function<void()>& checkForStopSignal)
    {
        try
        {
            manifest = std::move (loadParams.manifest);

            if (! loadProgram (engine, playbackParams, shouldResolveExternals, checkForStopSignal))
                return;

            if (! shouldResolveExternals)
                return;

            checkForStopSignal();
            AudioMIDIPerformer::Builder performerBuilder (engine);
            scanEndpointList (engine);
            checkForStopSignal();
            connectPerformerEndpoints (playbackParams, performerBuilder);
            checkForStopSignal();

            if (! shouldLink)
                return;

            if (! engine.link (errors, c.get()))
                return;

            sampleRate = playbackParams.sampleRate;
            lastBuildLog = engine.getLastBuildLog();

            if (performerBuilder.setEventOutputHandler ([this] { outputEventsReady(); }))
                startOutputEventThread();

            if (createPerformer (performerBuilder))
                applyParameterValues (loadParams.parameterValues);
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

                auto content = manifest.readFileContent (file);

                if (content.empty()
                    && manifest.getFileModificationTime (file) == std::filesystem::file_time_type())
                {
                    errors.add (cmaj::DiagnosticMessage::createError ("Could not open source file: " + file, {}));
                    return false;
                }

                if (! program.parse (errors, manifest.getFullPathForFile (file), std::move (content)))
                    return false;
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
                                                      createDataListener (e.endpointID));
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
                                                       createDataListener (e.endpointID));
            }
            else if (e.isMIDI())
            {
                performerBuilder.connectMIDIOutputTo (e);
            }
        }
    }

    std::shared_ptr<DataListener> createDataListener (const EndpointID& endpointID)
    {
        for (auto& m : audioEndpointMonitors)
        {
            if (endpointID.toString() == m->endpointID)
            {
                auto l = std::make_shared<PatchRenderer::DataListener> (*patch.clientEventQueue, *m);

                if (auto s = patch.getCustomAudioSourceForInput (endpointID))
                {
                    l->customSource = s;
                    s->prepare (sampleRate);
                }

                dataListeners[endpointID.toString()] = l;
                return l;
            }
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    bool setCustomAudioSource (const EndpointID& e, CustomAudioSourcePtr source)
    {
        auto l = dataListeners.find (e.toString());

        if (l == dataListeners.end())
            return false;

        if (source != nullptr)
            source->prepare (sampleRate);

        std::lock_guard<decltype(processLock)> lock (processLock);
        l->second->customSource = source;
        return true;
    }

    //==============================================================================
    void sendTimeSig (int numerator, int denominator)
    {
        if (timeSigEventID)
            performer->postEvent (timeSigEventID, timelineEvents.getTimeSigEvent (numerator, denominator));
    }

    void sendBPM (float bpm)
    {
        if (tempoEventID)
            performer->postEvent (tempoEventID, timelineEvents.getBPMEvent (bpm));
    }

    void sendTransportState (bool isRecording, bool isPlaying, bool isLooping)
    {
        if (transportStateEventID)
            performer->postEvent (transportStateEventID, timelineEvents.getTransportStateEvent (isRecording, isPlaying, isLooping));
    }

    void sendPosition (int64_t currentFrame, double quarterNote, double barStartQuarterNote)
    {
        if (positionEventID)
            performer->postEvent (positionEventID, timelineEvents.getPositionEvent (currentFrame, quarterNote, barStartQuarterNote));
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

    void applyParameterValues (const std::unordered_map<std::string, float>& values) const
    {
        for (auto& p : parameterIDMap)
        {
            auto oldValue = values.find (p.first);

            if (oldValue != values.end())
                p.second->setValue (oldValue->second, true);
            else
                p.second->resetToDefaultValue (true);
        }
    }

    void sendEventOrValueToPatch (ClientEventQueue& queue, const EndpointID& endpointID,
                                  const choc::value::ValueView& value, int32_t rampFrames = -1)
    {
        if (performer == nullptr)
            return;

        if (auto param = findParameter (endpointID))
            return param->setValue (value, false, rampFrames);

        if (! performer->postEvent (endpointID, value))
            performer->postValue (endpointID, value, rampFrames > 0 ? (uint32_t) rampFrames : 0);

        for (auto& m : eventEndpointMonitors)
            if (m->process (queue, endpointID.toString(), value))
                break;
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
            param->resetToDefaultValue (true);
    }

    void beginProcessBlock()
    {
        processLock.lock();
    }

    void endProcessBlock()
    {
        processLock.unlock();
    }

    //==============================================================================
    void postParameterChange (const PatchParameterProperties& properties, EndpointHandle endpointHandle,
                              float newValue, int32_t explicitRampFrames)
    {
        if (performer)
        {
            if (properties.isEvent)
                performer->postEvent (endpointHandle, choc::value::createFloat32 (newValue));
            else
                performer->postValue (endpointHandle, choc::value::createFloat32 (newValue),
                                      explicitRampFrames >= 0 ? static_cast<uint32_t> (explicitRampFrames)
                                                              : properties.rampFrames);
        }

        patch.clientEventQueue->postParameterChange (properties.endpointID, newValue);
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

    const Patch& patch;
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

    std::vector<std::unique_ptr<EventMonitor>> eventEndpointMonitors;
    std::vector<std::unique_ptr<AudioLevelMonitor>> audioEndpointMonitors;
    std::unordered_map<std::string, std::shared_ptr<DataListener>> dataListeners;

    choc::threading::ThreadSafeFunctor<HandleOutputEventFn> handleOutputEvent;

private:
    std::unique_ptr<cmaj::AudioMIDIPerformer> performer;

    std::unordered_map<std::string, PatchParameterPtr> parameterIDMap;

    TimelineEventGenerator timelineEvents;
    cmaj::EndpointID timeSigEventID, tempoEventID, transportStateEventID, positionEventID;

    choc::threading::TaskThread outputEventThread;
    choc::messageloop::Timer infiniteLoopCheckTimer;

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
                         resolveExternals, performLink, patch.cache, checkForStopSignal);
        return engine;
    }

    std::shared_ptr<PatchRenderer> takeRenderer()
    {
        CMAJ_ASSERT (renderer != nullptr);
        return std::move (renderer);
    }

private:
    const Patch& patch;
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
inline Patch::Patch (bool buildSynchronously, bool keepCheckingFilesForChanges)
    : scanFilesForChanges (keepCheckingFilesForChanges)
{
    const size_t midiBufferSize = 256;
    midiMessageTimes.reserve (midiBufferSize);
    midiMessages.reserve (midiBufferSize);

    clientEventQueue = std::make_unique<ClientEventQueue> (*this);

    if (! buildSynchronously)
        buildThread = std::make_unique<BuildThread> (*this);
}

inline Patch::~Patch()
{
    unload();
    clientEventQueue.reset();
}

inline bool Patch::preload (const PatchManifest& m)
{
    LoadParams params;
    params.manifest = m;

    auto build = std::make_unique<Build> (*this, params, false, false);
    build->build ([] {});
    setNewRenderer (build->takeRenderer());
    return renderer != nullptr && ! renderer->errors.hasErrors();
}

inline bool Patch::loadPatch (const LoadParams& params)
{
    if (! currentPlaybackParams.isValid())
        return false;

    fileChangeChecker.reset();

    if (std::addressof (lastLoadParams) != std::addressof (params))
        lastLoadParams = params;

    auto build = std::make_unique<Build> (*this, params, true, true);

    setStatus ("Loading: " + params.manifest.manifestFile);

    if (buildThread != nullptr)
    {
        buildThread->startBuild (std::move (build));
        return true;
    }

    build->build ([] {});
    setNewRenderer (build->takeRenderer());
    return isPlayable();
}

inline bool Patch::loadPatchFromManifest (PatchManifest&& m)
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

    return loadPatch (params);
}

inline bool Patch::loadPatchFromFile (const std::string& patchFile)
{
    PatchManifest manifest;
    manifest.createFileReaderFunctions (patchFile);
    return loadPatchFromManifest (std::move (manifest));
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
            for (auto& param : renderer->parameterList)
                lastLoadParams.parameterValues[param->properties.endpointID] = param->currentValue;

        if (lastLoadParams.manifest.reload())
        {
            loadPatch (lastLoadParams);
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
    if (renderer)
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
inline std::string Patch::getDescription() const            { return isLoaded() ? renderer->manifest.description : std::string(); }
inline std::string Patch::getManufacturer() const           { return isLoaded() ? renderer->manifest.manufacturer : std::string(); }
inline std::string Patch::getVersion() const                { return isLoaded() ? renderer->manifest.version : std::string(); }
inline std::string Patch::getCategory() const               { return isLoaded() ? renderer->manifest.category : std::string(); }
inline std::string Patch::getPatchFile() const              { return isLoaded() ? renderer->manifest.manifestFile : std::string(); }
inline bool Patch::isInstrument() const                     { return isLoaded() && renderer->manifest.isInstrument; }
inline bool Patch::hasMIDIInput() const                     { return isLoaded() && renderer->hasMIDIInputs; }
inline bool Patch::hasMIDIOutput() const                    { return isLoaded() && renderer->hasMIDIOutputs; }
inline bool Patch::hasAudioInput() const                    { return isLoaded() && renderer->numAudioInputChans != 0; }
inline bool Patch::hasAudioOutput() const                   { return isLoaded() && renderer->numAudioOutputChans != 0; }
inline bool Patch::wantsTimecodeEvents() const              { return renderer->hasTimecodeInputs; }
inline double Patch::getFramesLatency() const               { return isLoaded() ? renderer->framesLatency : 0.0; }
inline choc::value::Value Patch::getProgramDetails() const  { return isLoaded() ? renderer->programDetails : choc::value::Value(); }

inline std::string Patch::getMainProcessorName() const
{
    if (renderer && renderer->programDetails.isObject())
        return renderer->programDetails["mainProcessor"].toString();

    return {};
}

inline EndpointDetailsList Patch::getInputEndpoints() const
{
    if (renderer)
        return renderer->inputEndpoints;

    return {};
}

inline EndpointDetailsList Patch::getOutputEndpoints() const
{
    if (renderer)
        return renderer->outputEndpoints;

    return {};
}

inline choc::span<PatchParameterPtr> Patch::getParameterList() const
{
    if (renderer)
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

        if (! renderer->eventEndpointMonitors.empty())
            for (auto& m : renderer->eventEndpointMonitors)
                if (m->isMIDI)
                    m->process (*clientEventQueue, m->endpointID, message);
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

    if (! block.midiMessages.empty())
        for (auto& monitor : renderer->eventEndpointMonitors)
            if (monitor->isMIDI)
                for (auto& m : block.midiMessages)
                    monitor->process (*clientEventQueue, monitor->endpointID, m);
}

inline void Patch::endChunkedProcess()
{
    clientEventQueue->endOfProcessCallback();
    renderer->endProcessBlock();
}

inline void Patch::sendTimeSig (int numerator, int denominator)
{
    renderer->sendTimeSig (numerator, denominator);
}

inline void Patch::sendBPM (float bpm)
{
    renderer->sendBPM (bpm);
}

inline void Patch::sendTransportState (bool isRecording, bool isPlaying, bool isLooping)
{
    renderer->sendTransportState (isRecording, isPlaying, isLooping);
}

inline void Patch::sendPosition (int64_t currentFrame, double ppq, double ppqBar)
{
    renderer->sendPosition (currentFrame, ppq, ppqBar);
}

inline void Patch::sendMessageToViews (std::string_view type, const choc::value::ValueView& message) const
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
        sendMessageToViews ("status",
                            choc::json::create (
                                "error", renderer->errors.toString(),
                                "manifest", renderer->manifest.manifest,
                                "details", renderer->programDetails,
                                "sampleRate", currentPlaybackParams.sampleRate));
    }
}

inline const std::unordered_map<std::string, std::string>& Patch::getStoredStateValues() const
{
    return storedState;
}

inline void Patch::setStoredStateValue (const std::string& key, std::string newValue)
{
    auto& v = storedState[key];

    if (v != newValue)
    {
        if (newValue.empty())
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
                param->setValue (newValue->second, true);
            else
                param->resetToDefaultValue (true);
        }
    }
    else
    {
        for (auto& param : getParameterList())
            param->resetToDefaultValue (true);
    }

    std::unordered_set<std::string> storedValuesToRemove;

    for (auto& state : storedState)
        storedValuesToRemove.insert (state.first);

    if (auto values = newState["values"]; values.isObject())
    {
        for (uint32_t i = 0; i < values.size(); ++i)
        {
            auto member = values.getObjectMemberAt (i);
            setStoredStateValue (member.name, member.value.toString());
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
        sendMessageToViews ("param_value",
                            choc::json::create ("endpointID", endpointID.toString(),
                                                "value", value));
}

inline void Patch::sendCPUInfoToViews (float level) const
{
    sendMessageToViews ("cpu_info",
                        choc::json::create ("level", level));
}

inline void Patch::sendOutputEventToViews (std::string_view endpointID, const choc::value::ValueView& value) const
{
    if (! (value.isVoid() || endpointID.empty()))
        sendMessageToViews ("event_" + std::string (endpointID), value);
}

inline void Patch::sendStoredStateValueToViews (const std::string& key) const
{
    if (! key.empty())
        if (auto found = storedState.find (key); found != storedState.end())
            sendMessageToViews ("state_key_value",
                                choc::json::create ("key", key,
                                                    "value", found->second));
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

            if (startPlayback)
                startPlayback();

            if (handleInfiniteLoop)
                renderer->startInfiniteLoopCheck (handleInfiniteLoop);
        }
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

    startCheckingForChanges();
}

inline void Patch::sendOutputEvent (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v) const
{
    handleOutputEvent (frame, endpointID, v);
    sendOutputEventToViews (endpointID, v);
}

inline void Patch::sendEventOrValueToPatch (const EndpointID& endpointID, const choc::value::ValueView& value, int32_t rampFrames) const
{
    if (renderer != nullptr)
        renderer->sendEventOrValueToPatch (*clientEventQueue, endpointID, value, rampFrames);
}

inline void Patch::sendMIDIInputEvent (const EndpointID& endpointID, choc::midi::ShortMessage message) const
{
    sendEventOrValueToPatch (endpointID, cmaj::MIDIEvents::createMIDIMessageObject (message));
}

inline void Patch::sendGestureStart (const EndpointID& endpointID) const
{
    if (renderer != nullptr)
        renderer->sendGestureStart (endpointID);
}

inline void Patch::sendGestureEnd (const EndpointID& endpointID) const
{
    if (renderer != nullptr)
        renderer->sendGestureEnd (endpointID);
}

inline void Patch::sendCurrentParameterValueToViews (const EndpointID& endpointID) const
{
    if (auto param = findParameter (endpointID))
        sendParameterChangeToViews (endpointID, param->currentValue);
}

inline void Patch::setEndpointEventsNeeded (const EndpointID& endpointID, bool active)
{
    if (renderer != nullptr)
        for (auto& m : renderer->eventEndpointMonitors)
            if (endpointID.toString() == m->endpointID)
                m->activeListeners += (active ? 1 : -1);
}

inline void Patch::setEndpointAudioMinMaxNeeded (const EndpointID& endpointID, uint32_t granularity)
{
    if (renderer != nullptr)
        for (auto& m : renderer->audioEndpointMonitors)
            if (endpointID.toString() == m->endpointID)
                m->granularity = granularity;
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

inline bool Patch::handleClientMessage (const choc::value::ValueView& msg)
{
    if (! msg.isObject())
        return false;

    if (auto typeMember = msg["type"]; typeMember.isString())
    {
        auto type = typeMember.getString();

        if (type == "send_value")
        {
            auto endpointID = cmaj::EndpointID::create (msg["id"].toString());
            sendEventOrValueToPatch (endpointID, msg["value"], msg["rampFrames"].getWithDefault<int32_t> (-1));
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
            setStoredStateValue (msg["key"].toString(), msg["value"].toString());
            return true;
        }

        if (type == "req_full_state")
        {
            if (auto replyType = msg["replyType"].toString(); ! replyType.empty())
                sendMessageToViews (replyType, getFullStoredState());

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
                return loadPatchFromFile (file);

            unload();
            return true;
        }

        if (type == "set_endpoint_event_monitoring")
        {
            setEndpointEventsNeeded (cmaj::EndpointID::create (msg["endpoint"].toString()),
                                     msg["active"].getWithDefault<bool> (false));
            return true;
        }

        if (type == "set_endpoint_audio_monitoring")
        {
            setEndpointAudioMinMaxNeeded (cmaj::EndpointID::create (msg["endpoint"].toString()),
                                          static_cast<uint32_t> (msg["granularity"].getWithDefault<int64_t> (0)));
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

inline void PatchParameter::setValue (float newValue, bool forceSend, int32_t explicitRampFrames)
{
    newValue = properties.snapAndConstrainValue (newValue);

    if (currentValue != newValue || forceSend)
    {
        currentValue = newValue;

        if (auto r = renderer.lock())
            r->postParameterChange (properties, endpointHandle, newValue, explicitRampFrames);

        if (valueChanged)
            valueChanged (newValue);
    }
}

inline void PatchParameter::setValue (const choc::value::ValueView& v, bool forceSend, int32_t explicitRampFrames)
{
    setValue (properties.parseValue (v), forceSend, explicitRampFrames);
}

inline void PatchParameter::resetToDefaultValue (bool forceSend)
{
    setValue (properties.defaultValue, forceSend);
}

//==============================================================================
inline PatchView::PatchView (Patch& p) : PatchView (p, {})
{}

inline PatchView::PatchView (Patch& p, const PatchManifest::View& view) : patch (p)
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
    const auto i = std::find (patch.activeViews.begin(), patch.activeViews.end(), this);
    const auto isCurrentlyActive = i != patch.activeViews.end();

    if (active == isCurrentlyActive)
        return;

    if (active)
        patch.activeViews.push_back (this);
    else
        patch.activeViews.erase (i);
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
