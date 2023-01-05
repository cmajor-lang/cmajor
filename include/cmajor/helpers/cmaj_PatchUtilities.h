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

#include "../../choc/text/choc_Files.h"
#include "../../choc/audio/choc_AudioFileFormat_WAV.h"
#include "../../choc/audio/choc_AudioFileFormat_Ogg.h"
#include "../../choc/audio/choc_AudioFileFormat_FLAC.h"
#include "../../choc/audio/choc_AudioFileFormat_MP3.h"
#include "../../choc/gui/choc_MessageLoop.h"
#include "../../choc/threading/choc_TaskThread.h"
#include "../../choc/threading/choc_ThreadSafeFunctor.h"

#include "cmaj_AudioMIDIPerformer.h"

namespace cmaj
{

struct PatchView;
struct PatchParameter;
using PatchParameterPtr = std::shared_ptr<PatchParameter>;

static constexpr int32_t currentPatchCompatibilityVersion = 1;

//==============================================================================
/// Parses and represents a .cmajorpatch file
struct PatchManifest
{
    /// Initialises this manifest object by reading a given patch from the
    /// filesystem.
    /// This will throw an exception if there are errors parsing the file.
    void initialiseWithFile (std::filesystem::path manifestFile);

    /// Initialises this manifest object by reading a given patch using a set
    /// of custom file-reading functors.
    /// This will throw an exception if there are errors parsing the file.
    void initialiseWithVirtualFile (std::string patchFileLocation,
                                    std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReader,
                                    std::function<std::string(const std::string&)> getFullPathForFile,
                                    std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTime,
                                    std::function<bool(const std::string&)> fileExists);

    /// Refreshes the content by re-reading from the original source
    bool reload();

    choc::value::Value manifest;
    std::string manifestFile, ID, name, description, category, manufacturer, version;
    bool isInstrument = false;
    uint32_t framesLatency = 0;
    std::vector<std::string> sourceFiles;
    choc::value::Value externals;
    bool needsToBuildSource = true;

    // These functors are used for all file access, as the patch may be loaded from
    // all sorts of virtual filesystems
    std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReader;
    std::function<std::string(const std::string&)> getFullPathForFile;
    std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTime;
    std::function<bool(const std::string&)> fileExists;
    std::string readFileContent (const std::string& name) const;

    /// Represents one of the GUI views in the patch
    struct View
    {
        /// A (possibly relative) URL for the view content
        std::string source;
        uint32_t width = 0, height = 0;
        bool resizable = false;
    };

    std::vector<View> views;

private:
    void addSource (const choc::value::ValueView&);
    void addView (const choc::value::ValueView&);
};

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

    /// Triggers a rebuild of the current patch, which may be needed if the code
    /// or playback parameters change.
    void rebuild();

    /// Unloads any currently loaded patch
    void unload();

    /// Checks whether a patch has been selected
    bool isLoaded() const                               { return currentPatch != nullptr; }

    /// Checks whether a patch is currently loaded and ready to play.
    bool isPlayable() const;

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
    uint32_t getFramesLatency() const;

    EndpointDetailsList getInputEndpoints() const;
    EndpointDetailsList getOutputEndpoints() const;

    choc::span<PatchParameterPtr> getParameterList() const;
    PatchParameterPtr findParameter (const EndpointID&) const;

    //==============================================================================
    /// Processes the next block, optionally adding or replacing the audio output data
    void process (const choc::audio::AudioMIDIBlockDispatcher::Block&, bool replaceOutput);

    /// Renders a block using a juce-style single array of input + output audio channels.
    /// For this one, make calls to addMIDIMessage() beforehand to provide the MIDI.
    template <typename HandleMIDIOutFn>
    void process (float* const* audioChannels, uint32_t numFrames, HandleMIDIOutFn&&);

    /// Queues a MIDI message for use by the next call to process(). This isn't
    /// needed if you use the version of process that takes a Block object.
    void addMIDIMessage (int frameIndex, const void* data, uint32_t length);

    /// Can be called before process() to update the time sig details
    void sendTimeSig (int numerator, int denominator);
    /// Can be called before process() to update the BPM
    void sendBPM (float bpm);
    /// Can be called before process() to update the transport status
    void sendTransportState (bool isRecording, bool isPlaying);
    /// Can be called before process() to update the playhead time
    void sendPosition (int64_t currentFrame, double ppq, double ppqBar);

    /// Sets a persistent string that should be saved and restored for this
    /// patch by the host.
    void setStoredStateValue (const std::string& key, std::string newValue);

    /// Iterates any persistent state values that have been stored
    const std::unordered_map<std::string, std::string>& getStoredStateValues() const;

    //==============================================================================
    /// This must be supplied by the client using this class before trying to load a patch.
    std::function<cmaj::Engine()> createEngine;

    using HandleOutputEventFn = std::function<void(uint64_t frame, std::string_view endpointID, const choc::value::ValueView&)>;
    /// This must be supplied by the client before processing any data
    HandleOutputEventFn handleOutputEvent;

    // These are optional callbacks that the client can supply to be called when
    // various events occur:
    std::function<void()> stopPlayback       = []{},
                          startPlayback      = []{},
                          handlePatchChange  = []{};

    /// The client can set this callback to be given patch status updates, such as
    /// build failures, etc.
    std::function<void(const std::string&, bool)> setStatusMessage  = [] (const std::string&, bool) {};

    /// This object can optionally be provided if you have a build cache that you'd like
    /// the engine to use when compiling code.
    cmaj::CacheDatabaseInterface::Ptr cache;

    // These dispatch various types of event to any active views that the patch has open.
    void sendMessageToViews (const choc::value::ValueView&);
    void sendPatchStatusChangeToViews();
    void sendSampleRateChangeToViews (double newRate);
    void sendParameterChangeToViews (const EndpointID&, float value);
    void sendRealtimeParameterChangeToViews (const std::string&, float value);
    void sendCurrentParameterValueToViews (const EndpointID&);
    void sendOutputEventToViews (std::string_view endpointID, const choc::value::ValueView&);
    void sendStoredStateToViews (const std::string& key);

    // These can be called by things like the GUI to control the patch
    bool handleCientMessage (const choc::value::ValueView&);
    void sendEventOrValueToPatch (const EndpointID&, const choc::value::ValueView&, int32_t rampFrames = -1);
    void sendMIDIInputEvent (choc::midi::ShortMessage);

    void sendGestureStart (const EndpointID&);
    void sendGestureEnd (const EndpointID&);

private:
    //==============================================================================
    struct LoadedPatch;
    struct Build;
    struct BuildThread;
    struct FileChangeChecker;
    friend struct PatchView;
    friend struct PatchParameter;

    const bool scanFilesForChanges;
    LoadParams lastLoadParams;
    std::shared_ptr<LoadedPatch> currentPatch;
    PlaybackParams currentPlaybackParams;
    std::unique_ptr<FileChangeChecker> fileChangeChecker;
    std::vector<PatchView*> activeViews;
    std::unordered_map<std::string, std::string> storedState;

    choc::fifo::VariableSizeFIFO paramChangeQueue;
    choc::threading::TaskThread paramChangeHandler;
    choc::threading::ThreadSafeFunctor<std::function<void()>> deliverParamChangeMessage;

    std::vector<choc::midi::ShortMessage> midiMessages;
    std::vector<int> midiMessageTimes;

    LoadedPatch& getLoadedPatch()
    {
        CHOC_ASSERT (currentPatch != nullptr);
        return *currentPatch;
    }

    void sendPatchChange();
    void applyFinishedBuild (std::shared_ptr<LoadedPatch>);
    void sendOutputEvent (uint64_t frame, std::string_view endpointID, const choc::value::ValueView&);
    void startCheckingForChanges();
    void dispatchParameterChanges();

    //==============================================================================
    std::unique_ptr<BuildThread> buildThread;
};


//==============================================================================
/// Represents a patch parameter, and provides various helpers to deal with
/// its range, text conversion, etc.
struct PatchParameter  : public std::enable_shared_from_this<PatchParameter>
{
    PatchParameter (std::shared_ptr<Patch::LoadedPatch>, const EndpointDetails&, EndpointHandle);

    void setValue (float newValue, bool forceSend, int32_t explicitRampFrames = -1);
    void resetToDefaultValue (bool forceSend);

    float getStringAsValue (std::string_view text) const;
    std::string getValueAsString (float value) const;

    //==============================================================================
    std::weak_ptr<Patch::LoadedPatch> patch;
    EndpointID endpointID;
    EndpointHandle endpointHandle;

    float currentValue = 0, minValue = 0, maxValue = 0, step = 0, defaultValue = 0;
    std::string name, unit, group;
    std::vector<std::string> valueStrings;
    bool isEvent = false, boolean = false, automatable = false, hidden = false;
    uint64_t numSteps = 0;
    uint32_t rampFrames = 0;

    // optional callback that's invoked when the value is changed
    std::function<void(float)> valueChanged;
    std::function<void()> gestureStart, gestureEnd;
};

//==============================================================================
/// Base class for a GUI for a patch.
struct PatchView
{
    PatchView (Patch&, uint32_t width, uint32_t height);
    virtual ~PatchView();

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

inline void PatchManifest::initialiseWithVirtualFile (std::string patchFileLocation,
                                                      std::function<std::shared_ptr<std::istream>(const std::string&)> createFileReaderFn,
                                                      std::function<std::string(const std::string&)> getFullPathForFileFn,
                                                      std::function<std::filesystem::file_time_type(const std::string&)> getFileModificationTimeFn,
                                                      std::function<bool(const std::string&)> fileExistsFn)
{
    createFileReader = std::move (createFileReaderFn);
    getFullPathForFile = std::move (getFullPathForFileFn);
    getFileModificationTime = std::move (getFileModificationTimeFn);
    fileExists = std::move (fileExistsFn);
    CHOC_ASSERT (createFileReader && getFullPathForFile && getFileModificationTime && fileExists);

    manifestFile = std::move (patchFileLocation);
    reload();
}

inline void PatchManifest::initialiseWithFile (std::filesystem::path file)
{
    auto folder = file.parent_path();

    const auto getFileAsPath = [folder] (const std::string& f) -> std::filesystem::path
    {
        return folder / std::filesystem::path (f).relative_path();
    };

    initialiseWithVirtualFile (file.filename().string(),
        [getFileAsPath] (const std::string& f) -> std::shared_ptr<std::istream>
        {
            try
            {
                return std::make_shared<std::ifstream> (getFileAsPath (f), std::ios::binary | std::ios::in);
            }
            catch (...) {}

            return {};
        },
        [getFileAsPath] (const std::string& f) -> std::string
        {
            return getFileAsPath (f).string();
        },
        [getFileAsPath] (const std::string& f) -> std::filesystem::file_time_type
        {
            try
            {
                return last_write_time (getFileAsPath (f));
            }
            catch (...) {}

            return {};
        },
        [getFileAsPath] (const std::string& f) { return exists (getFileAsPath (f)); }
    );
}

inline bool PatchManifest::reload()
{
    if (createFileReader == nullptr || manifestFile.empty())
        return false;

    manifest = choc::json::parse (readFileContent (manifestFile));

    ID = {};
    name = {};
    description = {};
    category = {};
    manufacturer = {};
    version = {};
    isInstrument = false;
    framesLatency = 0;
    sourceFiles.clear();
    externals = choc::value::Value();
    views.clear();

    if (manifest.isObject())
    {
        if (! manifest.hasObjectMember ("CmajorVersion"))
            throw std::runtime_error ("The manifest must contain a property \"CmajorVersion\"");

        if (auto cmajVersion = manifest["CmajorVersion"].getWithDefault<int64_t> (0);
            cmajVersion < 1 || cmajVersion > currentPatchCompatibilityVersion)
            throw std::runtime_error ("Incompatible value for CmajorVersion");

        ID             = manifest["ID"].toString();
        name           = manifest["name"].toString();
        description    = manifest["description"].toString();
        category       = manifest["category"].toString();
        manufacturer   = manifest["manufacturer"].toString();
        version        = manifest["version"].toString();
        isInstrument   = manifest["isInstrument"].getWithDefault<bool> (false);
        externals      = manifest["externals"];

        if (ID.length() < 4)
            throw std::runtime_error ("The manifest must contain a valid and globally unique \"ID\" property");

        if (name.length() > 128 || name.length() < 3)
            throw std::runtime_error ("The manifest must contain a valid \"name\" property");

        if (version.length() > 24 || version.empty())
            throw std::runtime_error ("The manifest must contain a valid \"version\" property");

        addSource (manifest["source"]);
        addView (manifest["view"]);

        return true;
    }

    throw std::runtime_error ("The patch file did not contain a valid JSON object");
}

inline std::string PatchManifest::readFileContent (const std::string& file) const
{
    if (auto stream = createFileReader (file))
    {
        try
        {
            stream->seekg (0, std::ios_base::end);
            auto fileSize = stream->tellg();

            if (fileSize > 0)
            {
                std::string result;
                result.resize (static_cast<std::string::size_type> (fileSize));
                stream->seekg (0);

                if (stream->read (reinterpret_cast<std::ifstream::char_type*> (result.data()), static_cast<std::streamsize> (fileSize)))
                    return result;
            }
        }
        catch (...) {}
    }

    return {};
}

inline void PatchManifest::addSource (const choc::value::ValueView& source)
{
    if (source.isString())
    {
        sourceFiles.push_back (source.get<std::string>());
    }
    else if (source.isArray())
    {
        for (auto f : source)
            addSource (f);
    }
}

inline void PatchManifest::addView (const choc::value::ValueView& view)
{
    if (view.isArray())
    {
        for (auto e : view)
            if (e.isObject())
                addView (e);
    }
    else if (view.isObject())
    {
        View v;

        v.source    = view["src"].toString();
        v.width     = view["width"].getWithDefault<uint32_t> (0);
        v.height    = view["height"].getWithDefault<uint32_t> (0);
        v.resizable = view["resizable"].getWithDefault<bool> (true);
        views.push_back (std::move (v));
    }
}

//==============================================================================
struct Patch::FileChangeChecker
{
    FileChangeChecker (const PatchManifest& m, std::function<void()>&& onChange)
        : manifest (m), currentState (manifest), callback (std::move (onChange))
    {
        fileChangeCheckThread.start (1500, [this]
        {
            if (checkAndReset())
                choc::messageloop::postMessage ([cb = callback] { cb(); });
        });
    }

    ~FileChangeChecker()
    {
        fileChangeCheckThread.stop();
        callback.reset();
    }

    bool checkAndReset()
    {
        auto newState = SourceFilesWithTimes (manifest);

        if (newState != currentState)
        {
            currentState = std::move (newState);
            return true;
        }

        return false;
    }

private:
    struct SourceFilesWithTimes
    {
        SourceFilesWithTimes (const PatchManifest& m)
        {
            add (m, m.manifestFile);

            for (auto& f : m.sourceFiles)
                add (m, f);

            for (auto& v : m.views)
                add (m, v.source);
        }

        SourceFilesWithTimes (SourceFilesWithTimes&&) = default;
        SourceFilesWithTimes& operator= (SourceFilesWithTimes&&) = default;

        struct File
        {
            std::string file;
            std::filesystem::file_time_type lastWriteTime;

            bool operator== (const File& other) const   { return file == other.file && lastWriteTime == other.lastWriteTime; }
            bool operator!= (const File& other) const   { return ! operator== (other); }
        };

        void add (const PatchManifest& m, const std::string& file)
        {
            files.push_back ({ file, m.getFileModificationTime (file) });
        }

        bool operator== (const SourceFilesWithTimes& other) const { return files == other.files; }
        bool operator!= (const SourceFilesWithTimes& other) const { return ! (files == other.files); }

        std::vector<File> files;
    };

    const PatchManifest& manifest;
    SourceFilesWithTimes currentState;
    choc::threading::ThreadSafeFunctor<std::function<void()>> callback;
    choc::threading::TaskThread fileChangeCheckThread;
};

//==============================================================================
struct Patch::LoadedPatch
{
    ~LoadedPatch()
    {
        handleOutputEvent.reset();
    }

    PatchManifest manifest;
    cmaj::DiagnosticMessageList errors;
    std::unique_ptr<cmaj::AudioMIDIPerformer> performer;
    std::vector<PatchParameterPtr> parameterList;
    std::unordered_map<std::string, PatchParameterPtr> parameterIDMap;
    std::vector<EndpointID> midiInputEndpointIDs;
    std::function<void(const EndpointID&, float newValue)> handleParameterChange;
    choc::threading::ThreadSafeFunctor<HandleOutputEventFn> handleOutputEvent;

    bool hasMIDIInputs = false, hasMIDIOutputs = false;
    bool hasAudioInputs = false, hasAudioOutputs = false;
    bool hasTimecodeInputs = false;
    double sampleRate = 0, latencySamples = 0;
    cmaj::EndpointID timeSigEventID, tempoEventID, transportStateEventID, positionEventID;
    cmaj::EndpointDetailsList inputEndpoints, outputEndpoints;

    //==============================================================================
    void sendTimeSig (int numerator, int denominator)
    {
        timeSigEvent.setMember ("numerator", numerator);
        timeSigEvent.setMember ("denominator", denominator);
        performer->postEvent (timeSigEventID, timeSigEvent);
    }

    void sendBPM (float bpm)
    {
        tempoEvent.setMember ("bpm", bpm);
        performer->postEvent (tempoEventID, tempoEvent);
    }

    void sendTransportState (bool isRecording, bool isPlaying)
    {
        transportState.setMember ("state", isRecording ? 2 : isPlaying ? 1 : 0);
        performer->postEvent (transportStateEventID, transportState);
    }

    void sendPosition (int64_t currentFrame, double ppq, double ppqBar)
    {
        positionEvent.setMember ("currentFrame", currentFrame);
        positionEvent.setMember ("currentQuarterNote", ppq);
        positionEvent.setMember ("lastBarStartQuarterNote", ppqBar);
        performer->postEvent (positionEventID, positionEvent);
    }

    choc::value::Value timeSigEvent     { choc::value::createObject ("TimeSignature",
                                                                     "numerator", 0,
                                                                     "denominator", 0) };
    choc::value::Value tempoEvent       { choc::value::createObject ("Tempo",
                                                                     "bpm", 0.0f) };
    choc::value::Value transportState   { choc::value::createObject ("TransportState",
                                                                     "state", 0) };
    choc::value::Value positionEvent    { choc::value::createObject ("Position",
                                                                     "currentFrame", (int64_t) 0,
                                                                     "currentQuarterNote", 0.0,
                                                                     "lastBarStartQuarterNote", 0.0) };

    //==============================================================================
    PatchParameter* findParameter (const EndpointID& endpointID)
    {
        auto param = parameterIDMap.find (endpointID.toString());

        if (param != parameterIDMap.end())
            return param->second.get();

        return {};
    }

    void sendEventOrValueToPatch (const EndpointID& endpointID, const choc::value::ValueView& value, int32_t rampFrames = -1)
    {
        if (auto param = findParameter (endpointID))
        {
            float v = value.isString() ? param->getStringAsValue (value.getString())
                                       : value.getWithDefault<float> (0);

            param->setValue (v, false, rampFrames);

            if (handleParameterChange)
                handleParameterChange (endpointID, param->currentValue);

            return;
        }

        if (! performer->postEvent (endpointID, value))
            performer->postValue (endpointID, value, rampFrames > 0 ? (uint32_t) rampFrames : 0);
    }

    void sendMIDIInputEvent (choc::midi::ShortMessage value)
    {
        for (auto& endpointID : midiInputEndpointIDs)
            performer->postEvent (endpointID, MIDIEvents::createMIDIMessageObject (value));
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

    //==============================================================================
    void process (const choc::buffer::ChannelArrayView<float> inputView,
                  const choc::buffer::ChannelArrayView<float> outputView,
                  const choc::midi::ShortMessage* midiInMessages,
                  const int* midiInMessageTimes,
                  uint32_t totalNumMIDIMessages,
                  const std::function<void(uint32_t frame, choc::midi::ShortMessage)>& sendMidiOut)
    {
        if (totalNumMIDIMessages == 0)
        {
            performer->process (choc::audio::AudioMIDIBlockDispatcher::Block
            {
                inputView, outputView, {}, sendMidiOut
            }, true);
        }
        else
        {
            auto remainingChunk = outputView.getFrameRange();
            uint32_t midiStartIndex = 0;

            while (remainingChunk.start < remainingChunk.end)
            {
                auto chunkToDo = remainingChunk;
                auto endOfMIDI = midiStartIndex;

                while (endOfMIDI < totalNumMIDIMessages)
                {
                    auto eventTime = midiInMessageTimes[endOfMIDI];

                    if (eventTime > (int) chunkToDo.start)
                    {
                        chunkToDo.end = static_cast<choc::buffer::FrameCount> (eventTime);
                        break;
                    }

                    ++endOfMIDI;
                }

                performer->process (choc::audio::AudioMIDIBlockDispatcher::Block
                {
                    inputView.getFrameRange (chunkToDo),
                    outputView.getFrameRange (chunkToDo),
                    choc::span<const choc::midi::ShortMessage> (midiInMessages + midiStartIndex,
                                                                midiInMessages + endOfMIDI),
                    [&] (uint32_t frame, choc::midi::ShortMessage m)
                    {
                        sendMidiOut (chunkToDo.start + frame, m);
                    }
                }, true);

                remainingChunk.start = chunkToDo.end;
                midiStartIndex = endOfMIDI;
            }
        }
    }
};

//==============================================================================
struct Patch::Build
{
    Build (cmaj::Engine e, LoadParams lp, PlaybackParams pp, cmaj::CacheDatabaseInterface::Ptr c)
       : engine (e), loadParams (std::move (lp)), playbackParams (pp), cache (std::move (c))
    {}

    Engine engine;
    LoadParams loadParams;
    PlaybackParams playbackParams;
    std::unique_ptr<AudioMIDIPerformer::Builder> performerBuilder;
    std::shared_ptr<LoadedPatch> result;
    cmaj::CacheDatabaseInterface::Ptr cache;

    bool loadProgram (const std::function<void()>& checkForStopSignal)
    {
        try
        {
            result = std::make_shared<LoadedPatch>();
            result->manifest = std::move (loadParams.manifest);

            cmaj::Program program;

            if (result->manifest.needsToBuildSource)
            {
                for (auto& file : result->manifest.sourceFiles)
                {
                    checkForStopSignal();

                    auto content = result->manifest.readFileContent (file);

                    if (content.empty()
                         && result->manifest.getFileModificationTime (file) == std::filesystem::file_time_type())
                    {
                        result->errors.add (cmaj::DiagnosticMessage::createError ("Could not open source file: " + file, {}));
                        return false;
                    }

                    if (! program.parse (result->errors, result->manifest.getFullPathForFile (file), std::move (content)))
                        return false;
                }
            }

            engine.setBuildSettings (engine.getBuildSettings()
                                       .setFrequency (playbackParams.sampleRate)
                                       .setMaxBlockSize (playbackParams.blockSize));

            checkForStopSignal();

            if (engine.load (result->errors, program))
            {
                result->inputEndpoints = engine.getInputEndpoints();
                result->outputEndpoints = engine.getOutputEndpoints();
                return true;
            }
        }
        catch (const choc::json::ParseError& e)
        {
            result->errors.add (cmaj::DiagnosticMessage::createError (std::string (e.what()) + ":" + e.lineAndColumn.toString(), {}));
        }
        catch (const std::runtime_error& e)
        {
            result->errors.add (cmaj::DiagnosticMessage::createError (e.what(), {}));
        }

        return false;
    }

    void build (const std::function<void()>& checkForStopSignal, bool stopBeforeLink = false)
    {
        if (! loadProgram (checkForStopSignal))
            return;

        try
        {
            checkForStopSignal();

            if (! resolvePatchExternals())
            {
                result->errors.add (cmaj::DiagnosticMessage::createError ("Failed to resolve external variables", {}));
                return;
            }

            checkForStopSignal();
            performerBuilder = std::make_unique<AudioMIDIPerformer::Builder> (engine);
            scanEndpointList();
            checkForStopSignal();
            findEndpointIDs();
            connectPerformerEndpoints();
            checkForStopSignal();

            if (stopBeforeLink)
                return;

            if (! engine.link (result->errors, cache.get()))
                return;

            result->sampleRate = playbackParams.sampleRate;

            performerBuilder->setEventOutputHandler ([p = result.get()] (uint64_t frame, std::string_view endpointID,
                                                                         const choc::value::ValueView& value)
            {
                choc::messageloop::postMessage ([handler = p->handleOutputEvent,
                                                frame, endpointID = std::string (endpointID),
                                                value = choc::value::Value (value)]
                                                {
                                                    handler (frame, endpointID, value);
                                                });
            });

            result->performer = performerBuilder->createPerformer();

            if (result->performer->prepareToStart())
            {
                applyParameterValues();
                result->latencySamples = result->performer->performer.getLatency();
            }
        }
        catch (const choc::json::ParseError& e)
        {
            result->errors.add (cmaj::DiagnosticMessage::createError (std::string (e.what()) + ":" + e.lineAndColumn.toString(), {}));
        }
        catch (const std::runtime_error& e)
        {
            result->errors.add (cmaj::DiagnosticMessage::createError (e.what(), {}));
        }
    }

private:
    bool resolvePatchExternals()
    {
        if (result->manifest.externals.isVoid())
            return true;

        if (! result->manifest.externals.isObject())
            return false;

        auto externals = engine.getExternalVariables();

        for (auto& ev : externals.externals)
        {
            auto value = result->manifest.externals[ev.name];

            if (! engine.setExternalVariable (ev.name.c_str(), replaceStringsWithAudioData (value, ev.annotation)))
                return false;
        }

        return true;
    }

    choc::value::Value replaceStringsWithAudioData (const choc::value::ValueView& v,
                                                    const choc::value::ValueView& annotation)
    {
        if (v.isVoid())
            return {};

        if (v.isString())
        {
            try
            {
                auto s = v.get<std::string>();

                if (auto reader = result->manifest.createFileReader (s))
                {
                    choc::value::Value audioFileContent;

                    choc::audio::AudioFileFormatList formats;
                    formats.addFormat<choc::audio::OggAudioFileFormat<false>>();
                    formats.addFormat<choc::audio::MP3AudioFileFormat>();
                    formats.addFormat<choc::audio::FLACAudioFileFormat<false>>();
                    formats.addFormat<choc::audio::WAVAudioFileFormat<true>>();

                    auto error = cmaj::readAudioFileAsValue (audioFileContent, formats, reader, annotation);

                    if (error.empty())
                        return audioFileContent;
                }
            }
            catch (...)
            {}
        }

        if (v.isArray())
        {
            auto copy = choc::value::createEmptyArray();

            for (auto element : v)
                copy.addArrayElement (replaceStringsWithAudioData (element, annotation));

            return copy;
        }

        if (v.isObject())
        {
            auto copy = choc::value::createObject ({});

            for (uint32_t i = 0; i < v.size(); ++i)
            {
                auto m = v.getObjectMemberAt (i);
                copy.setMember (m.name, replaceStringsWithAudioData (m.value, annotation));
            }

            return copy;
        }

        return choc::value::Value (v);
    }

    //==============================================================================
    void scanEndpointList()
    {
        for (auto& e : result->inputEndpoints)
        {
            if (e.isParameter())
            {
                auto patchParam = std::make_shared<PatchParameter> (result, e, engine.getEndpointHandle (e.endpointID));
                result->parameterList.push_back (patchParam);
                result->parameterIDMap[e.endpointID.toString()] = std::move (patchParam);
            }
            else if (e.isMIDI())
            {
                result->midiInputEndpointIDs.push_back (e.endpointID);
            }
        }
    }

    void findEndpointIDs()
    {
        result->hasMIDIInputs = false;
        result->hasMIDIOutputs = false;
        result->hasAudioInputs = false;
        result->hasAudioOutputs = false;
        result->hasTimecodeInputs = false;

        result->timeSigEventID = {};
        result->tempoEventID = {};
        result->transportStateEventID = {};
        result->positionEventID = {};

        for (auto& e : result->inputEndpoints)
        {
            if (e.getNumAudioChannels() != 0)       result->hasAudioInputs = true;
            else if (e.isMIDI())                    result->hasMIDIInputs = true;
            else if (e.isTimelineTimeSignature())   { result->timeSigEventID = e.endpointID;        result->hasTimecodeInputs = true; }
            else if (e.isTimelinePosition())        { result->positionEventID = e.endpointID;       result->hasTimecodeInputs = true; }
            else if (e.isTimelineTransportState())  { result->transportStateEventID = e.endpointID; result->hasTimecodeInputs = true; }
            else if (e.isTimelineTempo())           { result->tempoEventID = e.endpointID;          result->hasTimecodeInputs = true; }
        }

        for (auto& e : result->outputEndpoints)
        {
            if (e.getNumAudioChannels() != 0)
                result->hasAudioOutputs = true;
            else if (e.isMIDI())
                result->hasMIDIOutputs = true;
        }
    }

    void connectPerformerEndpoints()
    {
        uint32_t inputChanIndex = 0;

        for (auto& e : result->inputEndpoints)
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

                performerBuilder->connectAudioInputTo (inChans, e, endpointChans);
            }
            else if (e.isMIDI())
            {
                performerBuilder->connectMIDIInputTo (e);
            }
        }

        uint32_t outputChanIndex = 0;
        uint32_t totalOutputChans = 0;
        auto outputEndpoints = result->outputEndpoints;

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

                performerBuilder->connectAudioOutputTo (e, endpointChans, outChans);
            }
            else if (e.isMIDI())
            {
                performerBuilder->connectMIDIOutputTo (e);
            }
        }
    }

    void applyParameterValues()
    {
        for (auto& p : result->parameterIDMap)
        {
            auto oldValue = loadParams.parameterValues.find (p.first);

            if (oldValue != loadParams.parameterValues.end())
                p.second->setValue (oldValue->second, true);
            else
                p.second->resetToDefaultValue (true);
        }
    }
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
            owner.applyFinishedBuild (std::move (finishedTask->build->result));
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

    if (! buildSynchronously)
        buildThread = std::make_unique<BuildThread> (*this);
}

inline Patch::~Patch()
{
    unload();
}

inline bool Patch::preload (const PatchManifest& m)
{
    CHOC_ASSERT (createEngine);

    LoadParams params;
    params.manifest = m;

    if (auto engine = createEngine())
    {
        auto build = std::make_unique<Build> (std::move (engine), params, currentPlaybackParams, cache);
        build->loadProgram ([] {});
        applyFinishedBuild (std::move (build->result));
        return ! currentPatch->errors.hasErrors();
    }

    return false;
}

inline bool Patch::loadPatch (const LoadParams& params)
{
    if (std::addressof (lastLoadParams) != std::addressof (params))
        lastLoadParams = params;

    if (! currentPlaybackParams.isValid())
        return false;

    CHOC_ASSERT (createEngine);
    auto build = std::make_unique<Build> (createEngine(), params, currentPlaybackParams, cache);

    if (buildThread != nullptr)
    {
        buildThread->startBuild (std::move (build));
        setStatusMessage ("Loading: " + params.manifest.manifestFile, false);
    }
    else
    {
        build->build ([] {});
        applyFinishedBuild (std::move (build->result));
    }

    return true;
}

inline bool Patch::loadPatchFromFile (const std::string& patchFile)
{
    LoadParams params;

    try
    {
        params.manifest.initialiseWithFile (patchFile);
    }
    catch (const choc::json::ParseError& e)
    {
        setStatusMessage ("error: " + std::string (e.what()) + ":" + e.lineAndColumn.toString(), true);
        return false;
    }
    catch (const std::runtime_error& e)
    {
        setStatusMessage ("error: " + std::string (e.what()), true);
        return false;
    }

    return loadPatch (params);
}

inline Engine::CodeGenOutput Patch::generateCode (const LoadParams& params, const std::string& target, const std::string& options)
{
    CHOC_ASSERT (createEngine);
    unload();
    auto engine = createEngine();
    CHOC_ASSERT (engine);

    auto build = std::make_unique<Build> (std::move (engine), params, currentPlaybackParams, cache);
    build->build ([] {}, true);

    if (build->result->errors.hasErrors())
    {
        Engine::CodeGenOutput result;
        result.messages = build->result->errors;
        return result;
    }

    return engine.generateCode (target, options);
}

inline void Patch::unload()
{
    deliverParamChangeMessage.reset();
    paramChangeHandler.stop();

    if (currentPatch)
    {
        stopPlayback();
        currentPatch.reset();
        sendPatchChange();
    }
}

inline void Patch::startCheckingForChanges()
{
    fileChangeChecker.reset();

    if (scanFilesForChanges && lastLoadParams.manifest.needsToBuildSource)
        if (lastLoadParams.manifest.getFileModificationTime != nullptr)
            fileChangeChecker = std::make_unique<FileChangeChecker> (lastLoadParams.manifest, [this] { rebuild(); });
}

inline void Patch::rebuild()
{
    try
    {
        if (isPlayable())
            for (auto& param : currentPatch->parameterList)
                lastLoadParams.parameterValues[param->endpointID.toString()] = param->currentValue;

        if (lastLoadParams.manifest.reload())
            loadPatch (lastLoadParams);
        else
            startCheckingForChanges();
    }
    catch (const choc::json::ParseError& e)
    {
        setStatusMessage ("error: " + std::string (e.what()) + ":" + e.lineAndColumn.toString(), true);
    }
    catch (const std::runtime_error& e)
    {
        setStatusMessage ("error: " + std::string (e.what()), true);
    }
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
    return isLoaded() ? currentPatch->manifest.ID
                      : "cmajor";
}

inline std::string Patch::getName() const
{
    return isLoaded() && ! currentPatch->manifest.name.empty()
            ? currentPatch->manifest.name
            : "Cmajor Patch Loader";
}

inline const PatchManifest* Patch::getManifest() const    { return currentPatch != nullptr ? std::addressof (currentPatch->manifest) : nullptr; }
inline bool Patch::isPlayable() const                     { return currentPatch != nullptr && currentPatch->performer != nullptr; }
inline std::string Patch::getDescription() const          { return isLoaded() ? currentPatch->manifest.description : std::string(); }
inline std::string Patch::getManufacturer() const         { return isLoaded() ? currentPatch->manifest.manufacturer : std::string(); }
inline std::string Patch::getVersion() const              { return isLoaded() ? currentPatch->manifest.version : std::string(); }
inline std::string Patch::getCategory() const             { return isLoaded() ? currentPatch->manifest.category : std::string(); }
inline std::string Patch::getPatchFile() const            { return isLoaded() ? currentPatch->manifest.manifestFile : std::string(); }
inline bool Patch::isInstrument() const                   { return isLoaded() && currentPatch->manifest.isInstrument; }
inline bool Patch::hasMIDIInput() const                   { return isLoaded() && currentPatch->hasMIDIInputs; }
inline bool Patch::hasMIDIOutput() const                  { return isLoaded() && currentPatch->hasMIDIOutputs; }
inline bool Patch::hasAudioInput() const                  { return isLoaded() && currentPatch->hasAudioInputs; }
inline bool Patch::hasAudioOutput() const                 { return isLoaded() && currentPatch->hasAudioOutputs; }
inline bool Patch::wantsTimecodeEvents() const            { return currentPatch->hasTimecodeInputs; }
inline uint32_t Patch::getFramesLatency() const           { return isLoaded() ? currentPatch->manifest.framesLatency : 0; }

inline EndpointDetailsList Patch::getInputEndpoints() const
{
    if (currentPatch)
        return currentPatch->inputEndpoints;

    return {};
}

inline EndpointDetailsList Patch::getOutputEndpoints() const
{
    if (currentPatch)
        return currentPatch->outputEndpoints;

    return {};
}

inline choc::span<PatchParameterPtr> Patch::getParameterList() const
{
    if (currentPatch)
        return currentPatch->parameterList;

    return {};
}

inline PatchParameterPtr Patch::findParameter (const EndpointID& endpointID) const
{
    if (currentPatch)
        if (auto p = currentPatch->findParameter (endpointID))
            return p->shared_from_this();

    return {};
}

inline void Patch::addMIDIMessage (int frameIndex, const void* data, uint32_t length)
{
    if (length < 4)
    {
        midiMessages.push_back (choc::midi::ShortMessage (data, static_cast<size_t> (length)));
        midiMessageTimes.push_back (frameIndex);
    }
}

template <typename HandleMIDIOutFn>
void Patch::process (float* const* audioChannels, uint32_t numFrames, HandleMIDIOutFn&& handleMIDIOut)
{
    currentPatch->process (choc::buffer::createChannelArrayView (audioChannels, currentPlaybackParams.numInputChannels, numFrames),
                           choc::buffer::createChannelArrayView (audioChannels, currentPlaybackParams.numOutputChannels, numFrames),
                           midiMessages.data(), midiMessageTimes.data(), static_cast<uint32_t> (midiMessages.size()), handleMIDIOut);

    midiMessages.clear();
    midiMessageTimes.clear();
}

inline void Patch::process (const choc::audio::AudioMIDIBlockDispatcher::Block& block, bool replaceOutput)
{
    currentPatch->performer->process (block, replaceOutput);
}

inline void Patch::sendTimeSig (int numerator, int denominator)
{
    if (currentPatch->timeSigEventID)
        currentPatch->sendTimeSig (numerator, denominator);
}

inline void Patch::sendBPM (float bpm)
{
    if (currentPatch->tempoEventID)
        currentPatch->sendBPM (bpm);
}

inline void Patch::sendTransportState (bool isRecording, bool isPlaying)
{
    if (currentPatch->transportStateEventID)
        currentPatch->sendTransportState (isRecording, isPlaying);
}

inline void Patch::sendPosition (int64_t currentFrame, double ppq, double ppqBar)
{
    if (currentPatch->positionEventID)
        currentPatch->sendPosition (currentFrame, ppq, ppqBar);
}

inline void Patch::sendMessageToViews (const choc::value::ValueView& msg)
{
    for (auto pv : activeViews)
        pv->sendMessage (msg);
}

inline void Patch::sendPatchStatusChangeToViews()
{
    if (currentPatch)
    {
        sendMessageToViews (choc::value::createObject ({},
                                                       "type", "status",
                                                       "error", currentPatch->errors.toString(),
                                                       "manifest", currentPatch->manifest.manifest,
                                                       "inputs", currentPatch->inputEndpoints.toJSON(),
                                                       "outputs", currentPatch->outputEndpoints.toJSON()));

        sendSampleRateChangeToViews (currentPatch->sampleRate);
    }
}

inline void Patch::sendSampleRateChangeToViews (double newRate)
{
    sendMessageToViews (choc::value::createObject ({},
                                                    "type", "sample_rate",
                                                    "rate", newRate));
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
        v = std::move (newValue);
        sendStoredStateToViews (key);
    }
}

inline void Patch::sendRealtimeParameterChangeToViews (const std::string& endpointID, float value)
{
    auto endpointChars = endpointID.data();
    auto endpointLen = static_cast<uint32_t> (endpointID.length());

    paramChangeQueue.push (4 + endpointLen, [=] (void* dest)
    {
        choc::memory::writeNativeEndian (dest, value);
        memcpy (static_cast<char*> (dest) + 4, endpointChars, endpointLen);
    });

    paramChangeHandler.trigger();
}

inline void Patch::dispatchParameterChanges()
{
    paramChangeQueue.popAllAvailable ([this] (const void* data, uint32_t size)
    {
        float value = choc::memory::readNativeEndian<float> (data);
        auto endpointID = std::string_view (static_cast<const char*> (data) + 4, size - 4);
        sendParameterChangeToViews (EndpointID::create (endpointID), value);
    });
}

inline void Patch::sendParameterChangeToViews (const EndpointID& endpointID, float value)
{
    if (endpointID)
        sendMessageToViews (choc::value::createObject ({},
                                                        "type", "param_value",
                                                        "ID", endpointID.toString(),
                                                        "value", value));
}

inline void Patch::sendOutputEventToViews (std::string_view endpointID, const choc::value::ValueView& value)
{
    if (! (value.isVoid() || endpointID.empty()))
        sendMessageToViews (choc::value::createObject ({},
                                                        "type", "output_event",
                                                        "ID", endpointID,
                                                        "value", value));
}

inline void Patch::sendStoredStateToViews (const std::string& key)
{
    if (! key.empty())
        if (auto found = storedState.find (key); found != storedState.end())
            sendMessageToViews (choc::value::createObject ({},
                                                           "type", "state_changed",
                                                           "key", key,
                                                           "value", found->second));
}

inline void Patch::sendPatchChange()
{
    if (isLoaded())
    {
        sendPatchStatusChangeToViews();
        sendSampleRateChangeToViews (currentPlaybackParams.sampleRate);
    }

    handlePatchChange();
}

inline void Patch::applyFinishedBuild (std::shared_ptr<LoadedPatch> newPatch)
{
    CHOC_ASSERT (newPatch != nullptr);

    stopPlayback();

    currentPatch.reset();
    sendPatchChange();
    currentPatch = std::move (newPatch);

    currentPatch->handleOutputEvent = [this] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
    {
        sendOutputEvent (frame, endpointID, v);
    };

    currentPatch->handleParameterChange = [this] (const EndpointID& endpointID, float value)
    {
        sendRealtimeParameterChangeToViews (endpointID.toString(), value);
    };

    sendPatchChange();

    if (isPlayable())
    {
        paramChangeQueue.reset (4096);
        deliverParamChangeMessage = [this] { dispatchParameterChanges(); };
        paramChangeHandler.start (0, [this] { choc::messageloop::postMessage ([=] { deliverParamChangeMessage(); }); });
        startPlayback();
    }

    std::string status;

    if (! getName().empty())
        status = "Loaded: " + getName();

    if (! currentPatch->errors.empty())
    {
        status += "\n\n" + currentPatch->errors.toString();
        setStatusMessage (status, currentPatch->errors.hasErrors());
    }
    else
    {
        setStatusMessage (status, false);
    }

    startCheckingForChanges();
}

inline void Patch::sendOutputEvent (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
{
    handleOutputEvent (frame, endpointID, v);
    sendOutputEventToViews (endpointID, v);
}

inline void Patch::sendEventOrValueToPatch (const EndpointID& endpointID, const choc::value::ValueView& value, int32_t rampFrames)
{
    if (currentPatch != nullptr)
        currentPatch->sendEventOrValueToPatch (endpointID, value, rampFrames);
}

inline void Patch::sendMIDIInputEvent (choc::midi::ShortMessage message)
{
    if (currentPatch != nullptr)
        currentPatch->sendMIDIInputEvent (message);
}

inline void Patch::sendGestureStart (const EndpointID& endpointID)
{
    if (currentPatch != nullptr)
        currentPatch->sendGestureStart (endpointID);
}

inline void Patch::sendGestureEnd (const EndpointID& endpointID)
{
    if (currentPatch != nullptr)
        currentPatch->sendGestureEnd (endpointID);
}

inline void Patch::sendCurrentParameterValueToViews (const EndpointID& endpointID)
{
    if (auto param = findParameter (endpointID))
        sendParameterChangeToViews (endpointID, param->currentValue);
}

inline bool Patch::handleCientMessage (const choc::value::ValueView& msg)
{
    if (! msg.isObject())
        return false;

    if (auto typeMember = msg["type"]; typeMember.isString())
    {
        auto type = typeMember.getString();

        if (type == "send_value")
        {
            if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
            {
                auto endpointID = cmaj::EndpointID::create (endpointIDMember.getString());
                sendEventOrValueToPatch (endpointID, msg["value"], msg["rampFrames"].getWithDefault<int32_t> (-1));
            }

            return true;
        }

        if (type == "midi_input")
        {
            if (auto midiEvent = msg["midiEvent"]; ! midiEvent.isVoid())
                if (auto value = midiEvent.getWithDefault<int32_t> (0))
                    sendMIDIInputEvent (MIDIEvents::packedMIDIDataToMessage (value));

            return true;
        }

        if (type == "send_gesture_start")
        {
            if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                sendGestureStart (cmaj::EndpointID::create (endpointIDMember.getString()));

            return true;
        }

        if (type == "send_gesture_end")
        {
            if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                sendGestureEnd (cmaj::EndpointID::create (endpointIDMember.getString()));

            return true;
        }

        if (type == "req_status")
        {
            sendPatchStatusChangeToViews();
            return true;
        }

        if (type == "req_endpoint")
        {
            if (auto endpointIDMember = msg["id"]; endpointIDMember.isString())
                sendCurrentParameterValueToViews (cmaj::EndpointID::create (endpointIDMember.getString()));

            return true;
        }

        if (type == "req_state")
        {
            if (auto key = msg["key"]; key.isString())
                sendStoredStateToViews (key.toString());

            return true;
        }

        if (type == "send_state")
        {
            if (auto key = msg["key"]; key.isString())
                if (auto value = msg["value"]; value.isString() || value.isVoid())
                    setStoredStateValue (key.toString(), value.get<std::string>());

            return true;
        }

        if (type == "load_patch")
        {
            if (auto file = msg["file"].getWithDefault<std::string>({}); ! file.empty())
                loadPatchFromFile (file);

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
inline PatchParameter::PatchParameter (std::shared_ptr<Patch::LoadedPatch> p, const EndpointDetails& details, cmaj::EndpointHandle handle)
    : patch (p), endpointHandle (handle)
{
    endpointID = details.endpointID;
    isEvent = details.isEvent();

    name  = details.annotation["name"].getWithDefault<std::string> (details.endpointID.toString());
    unit  = details.annotation["unit"].toString();
    group = details.annotation["group"].toString();

    minValue = details.annotation["min"].getWithDefault<float> (0);
    maxValue = details.annotation["max"].getWithDefault<float> (1.0f);
    step     = details.annotation["step"].getWithDefault<float> (0);
    numSteps = 1000;

    if (step > 0)
        numSteps = static_cast<uint64_t> ((maxValue - minValue) / step) + 1u;
    else
        step = (maxValue - minValue) / (float) numSteps;

    if (auto text = details.annotation["text"].toString(); ! text.empty())
    {
        valueStrings = choc::text::splitString (choc::text::removeDoubleQuotes (std::string (text)), '|', false);
        auto numStrings = valueStrings.size();

        if (numStrings > 1)
        {
            numSteps = (uint64_t) numStrings - 1u;

            const auto hasUserDefinedRange = [](const auto& annotation)
            {
                return annotation.hasObjectMember ("min") && annotation.hasObjectMember ("max");
            };

            if (! hasUserDefinedRange (details.annotation))
            {
                minValue = 0.0f;
                maxValue = (float) numSteps;
            }
        }
    }

    defaultValue  = details.annotation["init"].getWithDefault<float> (minValue);
    automatable   = details.annotation["automatable"].getWithDefault<bool> (true);
    boolean       = details.annotation["boolean"].getWithDefault<bool> (false);
    hidden        = details.annotation["hidden"].getWithDefault<bool> (false);
    rampFrames    = details.annotation["rampFrames"].getWithDefault<uint32_t> (0);

    currentValue = defaultValue;
}

inline void PatchParameter::setValue (float newValue, bool forceSend, int32_t explicitRampFrames)
{
    if (step > 0)
        newValue = std::round (newValue / step) * step;

    newValue = std::max (minValue, std::min (maxValue, newValue));

    if (currentValue != newValue || forceSend)
    {
        currentValue = newValue;

        if (auto p = patch.lock())
        {
            if (isEvent)
                p->performer->postEvent (endpointHandle, choc::value::createFloat32 (newValue));
            else
                p->performer->postValue (endpointHandle, choc::value::createFloat32 (newValue),
                                         explicitRampFrames >= 0 ? (uint32_t) explicitRampFrames : rampFrames);

            if (valueChanged)
                valueChanged (newValue);

            if (p->handleParameterChange)
                p->handleParameterChange (endpointID, newValue);
        }
    }
}

inline void PatchParameter::resetToDefaultValue (bool forceSend)
{
    setValue (defaultValue, forceSend);
}

inline static std::string parseFormatString (choc::text::UTF8Pointer text, float value)
{
    std::string result;

    for (;;)
    {
        auto c = text.popFirstChar();

        if (c == 0)
            return result;

        if (c == '%')
        {
            auto t = text;
            char sign = 0;

            if (value < 0)
                sign = '-';

            if (*t == '+')
            {
                ++t;

                if (value >= 0)
                    sign = '+';
            }

            value = std::abs (value);

            uint32_t numDigits = 0;
            bool isPadded = (*t == '0');

            for (;;)
            {
                auto digit = static_cast<uint32_t> (*t) - (uint32_t) '0';

                if (digit > 9)
                    break;

                numDigits = 10 * numDigits + digit;
                ++t;
            }

            bool isInt   = (*t == 'd');
            bool isFloat = (*t == 'f');

            if (isInt || isFloat)
            {
                if (sign != 0)
                    result += sign;

                if (isInt)
                {
                    auto n = std::to_string (static_cast<int64_t> (value + 0.5f));

                    if (isPadded && n.length() < numDigits)
                        result += std::string (numDigits - n.length(), '0');

                    result += n;
                }
                else
                {
                    result += choc::text::floatToString (value, numDigits != 0 ? (int) numDigits : -1, numDigits == 0);
                }

                text = ++t;
                continue;
            }
        }

        choc::text::appendUTF8 (result, c);
    }

    return result;
}

inline std::string PatchParameter::getValueAsString (float value) const
{
    if (valueStrings.empty())
        return choc::text::floatToString (value);

    auto numStrings = static_cast<int> (valueStrings.size());
    int index = 0;

    if (numStrings > 1)
    {
        auto value0to1 = (value - minValue) / (maxValue - minValue);
        index = static_cast<int> (value0to1 * (numStrings - 1));
        index = std::max (0, std::min (numStrings - 1, index));
    }

    return parseFormatString (choc::text::UTF8Pointer (valueStrings[static_cast<size_t> (index)].c_str()), value);
}

inline float PatchParameter::getStringAsValue (std::string_view text) const
{
    auto target = std::string (choc::text::trim (text));

    if (auto numStrings = valueStrings.size())
    {
        for (size_t i = 0; i < numStrings; ++i)
        {
            if (choc::text::toLowerCase (choc::text::trim (valueStrings[i])) == choc::text::toLowerCase (target))
            {
                auto index0to1 = static_cast<double> (i) / static_cast<double> (numStrings - 1);
                return static_cast<float> (minValue + index0to1 * (maxValue - minValue));
            }
        }
    }

    try
    {
        return static_cast<float> (std::stod (target));
    }
    catch (...) {}

    return 0;
}

//==============================================================================
inline PatchView::PatchView (Patch& p, uint32_t viewWidth, uint32_t viewHeight)
  : patch (p)
{
    width  = viewWidth;
    height = viewHeight;

    if (width < 50  || width > 10000)  width = 600;
    if (height < 50 || height > 10000) height = 400;

    patch.activeViews.push_back (this);
}

inline PatchView::~PatchView()
{
    auto i = std::find (patch.activeViews.begin(), patch.activeViews.end(), this);
    CHOC_ASSERT (i != patch.activeViews.end());
    patch.activeViews.erase (i);
}

inline bool PatchView::isViewOf (Patch& p) const
{
    return std::addressof (p) == std::addressof (patch);
}

} // namespace cmaj
