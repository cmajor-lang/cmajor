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

#include "cmajor/helpers/cmaj_Patch.h"

namespace cmaj::patch_helper_tests
{

struct File { std::string name, content; };

static PatchManifest createManifestWithInMemoryFiles (const std::string& manifestSource, const std::vector<File>& cmajorSource)
{
    const auto findFile = [] (const auto& name, const auto& files)
    {
        return std::find_if (files.begin(), files.end(), [&] (const auto& f) { return f.name == name; });
    };

    const auto toSource = [=](const auto& name)
    {
        if (name == "Test.cmajorpatch")
            return manifestSource;

        if (const auto it = findFile (name, cmajorSource); it != cmajorSource.end())
            return std::string { it->content };

        return std::string {};
    };

    PatchManifest m;
    m.initialiseWithVirtualFile ("Test.cmajorpatch",
        [=] (const std::string& name) -> std::shared_ptr<std::istream>
        {
            if (const auto source = toSource (name); ! source.empty())
                return std::make_shared<std::istringstream> (source, std::ios::binary);

            return {};
        },
        [] (const std::string& name) -> std::string { return name; },
        [] (const std::string&) -> std::filesystem::file_time_type { return {}; },
        [=] (const std::string& name)
        {
            return name == "Test.cmajorpatch" || findFile (name, cmajorSource) != cmajorSource.end();
        });

    return m;
}

static void initTestPatch (Patch& patch)
{
    patch.createEngine      = [] { return Engine::create(); };
    patch.createContextForPatchWorker = [] (const std::string&) { return std::unique_ptr<Patch::WorkerContext>(); };
    patch.stopPlayback      = [] {};
    patch.startPlayback     = [] {};
    patch.patchChanged      = [] {};
    patch.statusChanged     = [] (auto&&...) {};
    patch.handleOutputEvent = [] (auto&&...) {};

    patch.setHostDescription ("Cmajor Test");
}

//==============================================================================
/// A manifest containing nothing but the properties that every patch needs, for
/// the tests which don't care about any of the optional metadata.
static std::string createBasicManifest()
{
    return R"({
        "CmajorVersion": 1,
        "ID": "com.your_name.your_patch_ID",
        "version": "1.0",
        "name": "Test",
        "description": "Test",
        "category": "generator",
        "manufacturer": "Your Company Goes Here",
        "isInstrument": true,

        "source": ["Test.cmajor"]
    })";
}

/// Applies a set of playback params and synchronously loads a single-file patch,
/// returning the result of Patch::loadPatch().
static bool loadTestPatch (Patch& patch,
                           const std::string& manifestSource,
                           const std::string& cmajorSource,
                           uint32_t blockSize,
                           double sampleRate,
                           choc::buffer::ChannelCount numInputChannels,
                           choc::buffer::ChannelCount numOutputChannels)
{
    cmaj::Patch::PlaybackParams params;
    params.blockSize = blockSize;
    params.sampleRate = sampleRate;
    params.numInputChannels = numInputChannels;
    params.numOutputChannels = numOutputChannels;
    patch.setPlaybackParams (params);

    return patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true);
}

/// Combines initTestPatch() and loadTestPatch() for the common case where a test
/// doesn't need to override any of the patch's callbacks.
static bool initAndLoadTestPatch (Patch& patch,
                                  const std::string& cmajorSource,
                                  uint32_t blockSize = 4,
                                  double sampleRate = 4,
                                  choc::buffer::ChannelCount numInputChannels = 0,
                                  choc::buffer::ChannelCount numOutputChannels = 1)
{
    initTestPatch (patch);
    return loadTestPatch (patch, createBasicManifest(), cmajorSource,
                          blockSize, sampleRate, numInputChannels, numOutputChannels);
}

//==============================================================================
/// A PatchView which just records everything that the patch sends to it, so that
/// tests can check which messages were dispatched, and what they contained.
struct RecordingPatchView  : public PatchView
{
    RecordingPatchView (Patch& p) : PatchView (p) {}
    RecordingPatchView (Patch& p, const PatchManifest::View& v) : PatchView (p, v) {}

    void sendMessage (const choc::value::ValueView& m) override
    {
        messages.push_back (choc::value::Value (m));
    }

    size_t countMessagesOfType (std::string_view type) const
    {
        size_t total = 0;

        for (auto& m : messages)
            if (m["type"].toString() == type)
                ++total;

        return total;
    }

    /// Returns the payload of the most recent message of this type, or a void
    /// value if no such message was received.
    choc::value::Value findLastMessageOfType (std::string_view type) const
    {
        for (auto m = messages.rbegin(); m != messages.rend(); ++m)
            if ((*m)["type"].toString() == type)
                return choc::value::Value ((*m)["message"]);

        return {};
    }

    void clearMessages()    { messages.clear(); }

    std::vector<choc::value::Value> messages;
};

//==============================================================================
/// Creates (and deletes) a scratch folder, for the tests which need a patch that
/// really does live on the filesystem.
struct TempPatchFolder
{
    TempPatchFolder()
    {
        auto uniqueSuffix = std::chrono::steady_clock::now().time_since_epoch().count();
        folder = std::filesystem::temp_directory_path() / ("cmaj_patch_tests_" + std::to_string (uniqueSuffix));
        std::filesystem::create_directories (folder);
    }

    ~TempPatchFolder()
    {
        try { std::filesystem::remove_all (folder); } catch (...) {}
    }

    std::filesystem::path createFile (const std::string& name, std::string_view content) const
    {
        auto file = folder / name;
        choc::file::replaceFileWithContent (file, content);
        return file;
    }

    std::filesystem::path folder;
};

//==============================================================================
/// Pumps the message loop until the given condition becomes true, or until the
/// timeout expires. Returns true if the condition was met.
/// A few parts of the Patch class (asynchronous builds, and the client event
/// queue which feeds data to views) can only make progress when the message
/// loop is running.
static bool runMessageLoopUntil (const std::function<bool()>& isFinished, uint32_t timeoutMilliseconds = 20000)
{
    if (isFinished())
        return true;

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMilliseconds);
    bool succeeded = false;

    choc::messageloop::Timer timer (1, [&]
    {
        succeeded = isFinished();

        if (succeeded || std::chrono::steady_clock::now() > deadline)
        {
            choc::messageloop::stop();
            return false;
        }

        return true;
    });

    choc::messageloop::run();
    return succeeded;
}

//==============================================================================
//  Some source files which are shared between several of the tests below
//==============================================================================

/// A trivial gain processor with one event parameter and one value parameter.
static constexpr const char* gainPatchSource = R"(
    processor Test [[ main ]]
    {
        input stream float32 in;
        output stream float32 out;

        input event float32 gain    [[ name: "Gain",   min: 0, max: 2, init: 1, unit: "dB", group: "Levels" ]];
        input value float32 offset  [[ name: "Offset", min: -1, max: 1, init: 0 ]];

        float32 currentGain = 1.0f;

        event gain (float32 g)  { currentGain = g; }

        void main()
        {
            loop
            {
                out <- in * currentGain + offset;
                advance();
            }
        }
    }
)";

/// A patch which declares one of everything, for testing the endpoint queries.
static constexpr const char* kitchenSinkPatchSource = R"(
    processor Test [[ main ]]
    {
        input stream float32<2> audioIn;
        output stream float32<2> audioOut;

        input event std::midi::Message midiIn;
        output event std::midi::Message midiOut;

        input event std::timeline::Tempo tempoIn;
        input event std::timeline::TimeSignature timeSigIn;
        input event std::timeline::TransportState transportIn;
        input event std::timeline::Position positionIn;

        input event float32 gain [[ name: "Gain", min: 0, max: 1, init: 0.5 ]];
        input event float32 rawEvent;

        void main() { loop advance(); }
    }
)";

/// Copies its input to its output, so that tests can check what actually reached
/// the patch's audio input endpoint.
static constexpr const char* passThroughPatchSource = R"(
    processor Test [[ main ]]
    {
        input stream float32 in;
        output stream float32 out;

        void main()
        {
            loop
            {
                out <- in;
                advance();
            }
        }
    }
)";

//==============================================================================
static void runPatchStateTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (UnloadedPatchProperties)

        Patch patch;
        initTestPatch (patch);

        CHOC_EXPECT_FALSE (patch.isLoaded());
        CHOC_EXPECT_FALSE (patch.isPlayable());
        CHOC_EXPECT_EQ (patch.getUID(), "cmajor");
        CHOC_EXPECT_EQ (patch.getName(), "Cmajor Patch Loader");
        CHOC_EXPECT_TRUE (patch.getDescription().empty());
        CHOC_EXPECT_TRUE (patch.getManufacturer().empty());
        CHOC_EXPECT_TRUE (patch.getVersion().empty());
        CHOC_EXPECT_TRUE (patch.getCategory().empty());
        CHOC_EXPECT_TRUE (patch.getPatchFile().empty());
        CHOC_EXPECT_TRUE (patch.getManifestFile().empty());
        CHOC_EXPECT_TRUE (patch.getLastBuildLog().empty());
        CHOC_EXPECT_TRUE (patch.getMainProcessorName().empty());
        CHOC_EXPECT_FALSE (patch.isInstrument());
        CHOC_EXPECT_FALSE (patch.hasMIDIInput());
        CHOC_EXPECT_FALSE (patch.hasMIDIOutput());
        CHOC_EXPECT_FALSE (patch.hasAudioInput());
        CHOC_EXPECT_FALSE (patch.hasAudioOutput());
        CHOC_EXPECT_FALSE (patch.wantsTimecodeEvents());
        CHOC_EXPECT_EQ (patch.getFramesLatency(), 0.0);
        CHOC_EXPECT_TRUE (patch.getProgramDetails().isVoid());
        CHOC_EXPECT_EQ (patch.getInputEndpoints().size(), 0u);
        CHOC_EXPECT_EQ (patch.getOutputEndpoints().size(), 0u);
        CHOC_EXPECT_TRUE (patch.getParameterList().empty());
        CHOC_EXPECT_TRUE (patch.findParameter (EndpointID::create (std::string_view ("gain"))) == nullptr);
        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());
        CHOC_EXPECT_TRUE (patch.getCustomAudioSourceForInput (EndpointID::create (std::string_view ("in"))) == nullptr);

        // none of these should do anything (or explode) when nothing is loaded
        patch.resetToInitialState();
        patch.unload();
        patch.rebuild (true);
        patch.sendGestureStart (EndpointID::create (std::string_view ("gain")));
        patch.sendGestureEnd (EndpointID::create (std::string_view ("gain")));
        patch.sendPatchStatusChangeToViews();
        patch.setCPUInfoMonitorChunkSize (256);

        auto unknownEndpoint = EndpointID::create (std::string_view ("gain"));
        CHOC_EXPECT_FALSE (patch.sendEventOrValueToPatch (unknownEndpoint, choc::value::createFloat32 (1.0f), -1, 0));
        CHOC_EXPECT_FALSE (patch.sendMIDIInputEvent (unknownEndpoint, choc::midi::ShortMessage (0x90, 60, 100), 0));

        RecordingPatchView view (patch);
        CHOC_EXPECT_FALSE (patch.startEndpointData (view, unknownEndpoint, "reply", 0, false));
        CHOC_EXPECT_FALSE (patch.stopEndpointData (view, unknownEndpoint, "reply"));
    }

    {
        CHOC_TEST (HostDescription)

        Patch patch;
        CHOC_EXPECT_TRUE (patch.getHostDescription().empty());

        patch.setHostDescription ("Some Host 1.2.3");
        CHOC_EXPECT_EQ (patch.getHostDescription(), "Some Host 1.2.3");

        patch.setHostDescription ({});
        CHOC_EXPECT_TRUE (patch.getHostDescription().empty());
    }

    {
        CHOC_TEST (PlaybackParams)

        cmaj::Patch::PlaybackParams defaultParams;
        CHOC_EXPECT_FALSE (defaultParams.isValid());
        CHOC_EXPECT_EQ (defaultParams.sampleRate, 0.0);
        CHOC_EXPECT_EQ (defaultParams.blockSize, 0u);

        cmaj::Patch::PlaybackParams params (44100.0, 256, 2, 2);
        CHOC_EXPECT_TRUE (params.isValid());
        CHOC_EXPECT_EQ (params.sampleRate, 44100.0);
        CHOC_EXPECT_EQ (params.blockSize, 256u);
        CHOC_EXPECT_EQ (params.numInputChannels, 2u);
        CHOC_EXPECT_EQ (params.numOutputChannels, 2u);

        CHOC_EXPECT_FALSE (cmaj::Patch::PlaybackParams (0.0, 256, 2, 2).isValid());
        CHOC_EXPECT_FALSE (cmaj::Patch::PlaybackParams (44100.0, 0, 2, 2).isValid());
        CHOC_EXPECT_TRUE (cmaj::Patch::PlaybackParams (44100.0, 256, 0, 0).isValid());

        CHOC_EXPECT_TRUE  (params == cmaj::Patch::PlaybackParams (44100.0, 256, 2, 2));
        CHOC_EXPECT_FALSE (params != cmaj::Patch::PlaybackParams (44100.0, 256, 2, 2));
        CHOC_EXPECT_TRUE  (params != cmaj::Patch::PlaybackParams (48000.0, 256, 2, 2));
        CHOC_EXPECT_TRUE  (params != cmaj::Patch::PlaybackParams (44100.0, 512, 2, 2));
        CHOC_EXPECT_TRUE  (params != cmaj::Patch::PlaybackParams (44100.0, 256, 1, 2));
        CHOC_EXPECT_TRUE  (params != cmaj::Patch::PlaybackParams (44100.0, 256, 2, 1));
        CHOC_EXPECT_TRUE  (params != defaultParams);

        Patch patch;
        initTestPatch (patch);
        CHOC_EXPECT_FALSE (patch.getPlaybackParams().isValid());

        patch.setPlaybackParams (params);
        CHOC_EXPECT_TRUE (patch.getPlaybackParams() == params);

        // loading can't work until some valid playback params have been provided
        Patch unpreparedPatch;
        initTestPatch (unpreparedPatch);
        CHOC_EXPECT_FALSE (unpreparedPatch.loadPatch ({ createManifestWithInMemoryFiles (createBasicManifest(),
                                                                                         {{ "Test.cmajor", passThroughPatchSource }}), {} }, true));
        CHOC_EXPECT_FALSE (unpreparedPatch.isLoaded());
    }

    {
        CHOC_TEST (ManifestProperties)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "dev.cmajor.tests.manifest_properties",
            "version": "2.3.4",
            "name": "Manifest Property Test",
            "description": "A patch which fills in all of the manifest properties",
            "category": "effect",
            "manufacturer": "Cmajor Software Ltd",
            "isInstrument": false,
            "mainProcessor": "Test",

            "view": { "src": "index.html", "width": 700, "height": 350, "resizable": false },

            "source": ["Test.cmajor"]
        })";

        Patch patch;
        initTestPatch (patch);

        if (! loadTestPatch (patch, manifestSource, passThroughPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_TRUE (patch.isLoaded());
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_EQ (patch.getUID(), "dev.cmajor.tests.manifest_properties");
        CHOC_EXPECT_EQ (patch.getName(), "Manifest Property Test");
        CHOC_EXPECT_EQ (patch.getDescription(), "A patch which fills in all of the manifest properties");
        CHOC_EXPECT_EQ (patch.getManufacturer(), "Cmajor Software Ltd");
        CHOC_EXPECT_EQ (patch.getVersion(), "2.3.4");
        CHOC_EXPECT_EQ (patch.getCategory(), "effect");
        CHOC_EXPECT_EQ (patch.getPatchFile(), "Test.cmajorpatch");
        CHOC_EXPECT_EQ (patch.getManifestFile(), "Test.cmajorpatch");
        CHOC_EXPECT_FALSE (patch.isInstrument());
        CHOC_EXPECT_EQ (patch.getMainProcessorName(), "Test");
        CHOC_EXPECT_TRUE (patch.getProgramDetails().isObject());

        auto manifest = patch.getManifest();

        if (manifest == nullptr)
        {
            CHOC_FAIL ("Expected a manifest");
            return;
        }

        CHOC_EXPECT_EQ (manifest->ID, "dev.cmajor.tests.manifest_properties");
        CHOC_EXPECT_EQ (manifest->mainProcessor, "Test");
        CHOC_EXPECT_EQ (manifest->sourceFiles.size(), 1u);
        CHOC_EXPECT_EQ (manifest->views.size(), 1u);
        CHOC_EXPECT_TRUE (manifest->manifest.isObject());
        CHOC_EXPECT_TRUE (manifest->getStrippedManifest().isObject());
    }

    {
        CHOC_TEST (EndpointQueries)

        Patch patch;

        if (! initAndLoadTestPatch (patch, kitchenSinkPatchSource, 4, 4, 2, 2))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_TRUE (patch.hasMIDIInput());
        CHOC_EXPECT_TRUE (patch.hasMIDIOutput());
        CHOC_EXPECT_TRUE (patch.hasAudioInput());
        CHOC_EXPECT_TRUE (patch.hasAudioOutput());
        CHOC_EXPECT_TRUE (patch.wantsTimecodeEvents());
        CHOC_EXPECT_EQ (patch.getFramesLatency(), 0.0);
        CHOC_EXPECT_EQ (patch.getMainProcessorName(), "Test");

        auto inputs = patch.getInputEndpoints();
        auto outputs = patch.getOutputEndpoints();

        CHOC_EXPECT_EQ (inputs.size(), 8u);
        CHOC_EXPECT_EQ (outputs.size(), 2u);

        size_t numParameters = 0, numTimeline = 0, numMIDIInputs = 0, numAudioInputs = 0;

        for (auto& e : inputs)
        {
            if (e.isParameter())    ++numParameters;
            if (e.isTimeline())     ++numTimeline;
            if (e.isMIDI())         ++numMIDIInputs;
            if (e.getNumAudioChannels() != 0) ++numAudioInputs;
        }

        CHOC_EXPECT_EQ (numParameters, 1u);
        CHOC_EXPECT_EQ (numTimeline, 4u);
        CHOC_EXPECT_EQ (numMIDIInputs, 1u);
        CHOC_EXPECT_EQ (numAudioInputs, 1u);

        CHOC_EXPECT_EQ (patch.getParameterList().size(), 1u);
        CHOC_EXPECT_TRUE (patch.findParameter (EndpointID::create (std::string_view ("gain"))) != nullptr);
        CHOC_EXPECT_TRUE (patch.findParameter (EndpointID::create (std::string_view ("rawEvent"))) == nullptr);
        CHOC_EXPECT_TRUE (patch.findParameter (EndpointID::create (std::string_view ("nonexistent"))) == nullptr);
        CHOC_EXPECT_TRUE (patch.findParameter (EndpointID()) == nullptr);
    }

    {
        CHOC_TEST (UnloadResetsEverything)

        Patch patch;
        initTestPatch (patch);

        uint32_t numPatchChanges = 0, numStops = 0, numStarts = 0;
        patch.patchChanged  = [&] { ++numPatchChanges; };
        patch.stopPlayback  = [&] { ++numStops; };
        patch.startPlayback = [&] { ++numStarts; };

        if (! loadTestPatch (patch, createBasicManifest(), gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_TRUE (patch.isLoaded());
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_EQ (numStarts, 1u);
        CHOC_EXPECT_TRUE (numPatchChanges > 0);

        patch.unload();

        CHOC_EXPECT_FALSE (patch.isLoaded());
        CHOC_EXPECT_FALSE (patch.isPlayable());
        CHOC_EXPECT_TRUE (numStops > 0);
        CHOC_EXPECT_TRUE (patch.getParameterList().empty());
        CHOC_EXPECT_EQ (patch.getInputEndpoints().size(), 0u);
        CHOC_EXPECT_TRUE (patch.getLastBuildLog().empty());

        // unloading twice should be harmless
        patch.unload();
        CHOC_EXPECT_FALSE (patch.isLoaded());
    }

    {
        CHOC_TEST (PreloadDoesNotCreateAPerformer)

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        CHOC_EXPECT_TRUE (patch.preload (createManifestWithInMemoryFiles (createBasicManifest(),
                                                                          {{ "Test.cmajor", gainPatchSource }})));

        // a preload is enough to report the patch's vital statistics, but not to play it
        CHOC_EXPECT_TRUE (patch.isLoaded());
        CHOC_EXPECT_FALSE (patch.isPlayable());
        CHOC_EXPECT_EQ (patch.getName(), "Test");
        CHOC_EXPECT_EQ (patch.getParameterList().size(), 2u);
        CHOC_EXPECT_TRUE (patch.hasAudioInput());
        CHOC_EXPECT_TRUE (patch.hasAudioOutput());
    }

    {
        CHOC_TEST (BuildFailureIsReported)

        const auto brokenSource = R"(
            processor Test [[ main ]]
            {
                output stream float32 out // missing semicolon

                void main() { loop advance(); }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        Patch::Status lastStatus;
        patch.statusChanged = [&] (const Patch::Status& s) { lastStatus = s; };

        CHOC_EXPECT_FALSE (loadTestPatch (patch, createBasicManifest(), brokenSource, 4, 4, 0, 1));
        CHOC_EXPECT_FALSE (patch.isPlayable());
        CHOC_EXPECT_TRUE (lastStatus.messageList.hasErrors());
        CHOC_EXPECT_FALSE (lastStatus.statusMessage.empty());
        CHOC_EXPECT_TRUE (patch.getParameterList().empty());

        // a subsequent good build should clear the error
        CHOC_EXPECT_TRUE (patch.loadPatch ({ createManifestWithInMemoryFiles (createBasicManifest(),
                                                                              {{ "Test.cmajor", passThroughPatchSource }}), {} }, true));
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_FALSE (lastStatus.messageList.hasErrors());
    }

    {
        CHOC_TEST (RebuildPreservesParameterValues)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain")));

        if (! gain)
        {
            CHOC_FAIL ("Expected to find parameter");
            return;
        }

        CHOC_EXPECT_NEAR (gain->currentValue, 1.0f, 0.0001f);
        CHOC_EXPECT_TRUE (gain->setValue (0.25f, true, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, 0.25f, 0.0001f);

        patch.rebuild (true);

        CHOC_EXPECT_TRUE (patch.isPlayable());

        auto gainAfterRebuild = patch.findParameter (EndpointID::create (std::string_view ("gain")));

        if (! gainAfterRebuild)
        {
            CHOC_FAIL ("Expected to find parameter after the rebuild");
            return;
        }

        CHOC_EXPECT_NEAR (gainAfterRebuild->currentValue, 0.25f, 0.0001f);

        // changing the playback params should also trigger a rebuild
        auto newParams = patch.getPlaybackParams();
        newParams.blockSize = 8;
        patch.setPlaybackParams (newParams, true);

        CHOC_EXPECT_TRUE (patch.getPlaybackParams() == newParams);
        CHOC_EXPECT_TRUE (patch.isPlayable());
    }

    {
        CHOC_TEST (LoadFromFile)

        TempPatchFolder temp;
        auto manifestFile = temp.createFile ("Test.cmajorpatch", createBasicManifest());
        temp.createFile ("Test.cmajor", passThroughPatchSource);

        Patch patch;
        initTestPatch (patch);
        patch.setAutoRebuildOnFileChange (true);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        CHOC_EXPECT_TRUE (patch.loadPatchFromFile (manifestFile.string(), true));
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_EQ (patch.getPatchFile(), "Test.cmajorpatch");
        CHOC_EXPECT_EQ (patch.getManifestFile(), manifestFile.string());
        CHOC_EXPECT_EQ (patch.getName(), "Test");

        patch.setAutoRebuildOnFileChange (false);
        patch.unload();

        // a patch file which isn't there should fail cleanly
        Patch::Status lastStatus;
        patch.statusChanged = [&] (const Patch::Status& s) { lastStatus = s; };

        CHOC_EXPECT_FALSE (patch.loadPatchFromFile ((temp.folder / "DoesNotExist.cmajorpatch").string(), true));
        CHOC_EXPECT_FALSE (patch.isLoaded());
        CHOC_EXPECT_TRUE (lastStatus.messageList.hasErrors());
    }

    {
        CHOC_TEST (LoadFromManifest)

        TempPatchFolder temp;
        auto manifestFile = temp.createFile ("Test.cmajorpatch", createBasicManifest());
        temp.createFile ("Test.cmajor", passThroughPatchSource);

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        PatchManifest manifest;
        manifest.createFileReaderFunctions (manifestFile);

        CHOC_EXPECT_TRUE (patch.loadPatchFromManifest (std::move (manifest), true));
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_EQ (patch.getUID(), "com.your_name.your_patch_ID");

        // a manifest containing invalid JSON should be reported rather than thrown
        auto brokenFile = temp.createFile ("Broken.cmajorpatch", "{ this is not json");

        Patch::Status lastStatus;
        patch.statusChanged = [&] (const Patch::Status& s) { lastStatus = s; };

        PatchManifest brokenManifest;
        brokenManifest.createFileReaderFunctions (brokenFile);

        CHOC_EXPECT_FALSE (patch.loadPatchFromManifest (std::move (brokenManifest), true));
        CHOC_EXPECT_FALSE (patch.isLoaded());
        CHOC_EXPECT_TRUE (lastStatus.messageList.hasErrors());
    }
}

//==============================================================================
static void runPatchStoredStateTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (StoredStateValues)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());

        patch.setStoredStateValue ("someKey", choc::value::createString ("someValue"));
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 1u);
        CHOC_EXPECT_EQ (patch.getStoredStateValues().at ("someKey").toString(), "someValue");
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_key_value"), 1u);
        CHOC_EXPECT_EQ (view.findLastMessageOfType ("state_key_value")["key"].toString(), "someKey");
        CHOC_EXPECT_EQ (view.findLastMessageOfType ("state_key_value")["value"].toString(), "someValue");

        // setting the same value again shouldn't notify anyone
        patch.setStoredStateValue ("someKey", choc::value::createString ("someValue"));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_key_value"), 1u);

        patch.setStoredStateValue ("someKey", choc::value::createInt32 (123));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_key_value"), 2u);
        CHOC_EXPECT_EQ (patch.getStoredStateValues().at ("someKey").getWithDefault<int32_t> (0), 123);

        patch.setStoredStateValue ("anotherKey", choc::value::createBool (true));
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 2u);

        // removing a key that isn't there should be a no-op
        view.clearMessages();
        patch.setStoredStateValue ("neverSet", {});
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 2u);
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_key_value"), 0u);

        // passing a void value removes a key
        patch.setStoredStateValue ("someKey", {});
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 1u);
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_key_value"), 1u);
        CHOC_EXPECT_TRUE (view.findLastMessageOfType ("state_key_value")["value"].isVoid());

        patch.clearAllStoredStateValues();
        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());

        // ...and clearing an already-empty set shouldn't misbehave either
        patch.clearAllStoredStateValues();
        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());
    }

    {
        CHOC_TEST (FullStoredState)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain")));
        auto offset = patch.findParameter (EndpointID::create (std::string_view ("offset")));

        if (! gain || ! offset)
        {
            CHOC_FAIL ("Expected to find the parameters");
            return;
        }

        // with everything at its default, the state should contain no parameters
        {
            auto state = patch.getFullStoredState();
            CHOC_EXPECT_TRUE (state.isObject());
            CHOC_EXPECT_EQ (state["parameters"].size(), 0u);
            CHOC_EXPECT_EQ (state["values"].size(), 0u);
        }

        gain->setValue (0.5f, true, -1, 0);
        patch.setStoredStateValue ("customThing", choc::value::createString ("hello"));

        auto savedState = patch.getFullStoredState();
        CHOC_EXPECT_EQ (savedState["parameters"].size(), 1u);
        CHOC_EXPECT_EQ (savedState["parameters"][0]["name"].toString(), "gain");
        CHOC_EXPECT_NEAR (savedState["parameters"][0]["value"].getWithDefault<float> (0), 0.5f, 0.0001f);
        CHOC_EXPECT_EQ (savedState["values"]["customThing"].toString(), "hello");

        // scribble over everything, then check that restoring puts it all back
        gain->setValue (2.0f, true, -1, 0);
        offset->setValue (0.75f, true, -1, 0);
        patch.setStoredStateValue ("customThing", choc::value::createString ("goodbye"));
        patch.setStoredStateValue ("extraThing", choc::value::createInt32 (7));

        CHOC_EXPECT_TRUE (patch.setFullStoredState (savedState));

        CHOC_EXPECT_NEAR (gain->currentValue, 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (offset->currentValue, offset->properties.defaultValue, 0.0001f);
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 1u);
        CHOC_EXPECT_EQ (patch.getStoredStateValues().at ("customThing").toString(), "hello");

        // a state with no parameters should reset them all to their defaults
        gain->setValue (2.0f, true, -1, 0);
        CHOC_EXPECT_TRUE (patch.setFullStoredState (choc::json::create ("parameters", choc::value::createEmptyArray(),
                                                                        "values", choc::value::createObject ({}))));
        CHOC_EXPECT_NEAR (gain->currentValue, gain->properties.defaultValue, 0.0001f);
        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());

        // anything that isn't an object should be rejected
        CHOC_EXPECT_FALSE (patch.setFullStoredState (choc::value::createInt32 (123)));
        CHOC_EXPECT_FALSE (patch.setFullStoredState ({}));
    }
}

//==============================================================================
static void runPatchViewTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (ViewRegistration)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        Patch otherPatch;

        RecordingPatchView view (patch);
        CHOC_EXPECT_TRUE (view.isActive());
        CHOC_EXPECT_TRUE (view.isViewOf (patch));
        CHOC_EXPECT_FALSE (view.isViewOf (otherPatch));

        // a view with no manifest entry gets the default size
        CHOC_EXPECT_EQ (view.width, 600u);
        CHOC_EXPECT_EQ (view.height, 400u);
        CHOC_EXPECT_TRUE (view.resizable);

        PatchManifest::View sizedView;
        sizedView.view = choc::json::create ("width", 700, "height", 350, "resizable", false);
        view.update (sizedView);
        CHOC_EXPECT_EQ (view.width, 700u);
        CHOC_EXPECT_EQ (view.height, 350u);
        CHOC_EXPECT_FALSE (view.resizable);

        // silly sizes should be replaced with the defaults
        PatchManifest::View sillyView;
        sillyView.view = choc::json::create ("width", 10, "height", 99999);
        view.update (sillyView);
        CHOC_EXPECT_EQ (view.width, 600u);
        CHOC_EXPECT_EQ (view.height, 400u);
        CHOC_EXPECT_TRUE (view.resizable);

        // messages only go to views which are currently active
        patch.sendMessageToView (view, "hello", choc::value::createInt32 (1));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("hello"), 1u);

        view.setActive (false);
        CHOC_EXPECT_FALSE (view.isActive());
        patch.sendMessageToView (view, "hello", choc::value::createInt32 (2));
        patch.broadcastMessageToViews ("hello", choc::value::createInt32 (3));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("hello"), 1u);

        view.setActive (true);
        CHOC_EXPECT_TRUE (view.isActive());
        patch.broadcastMessageToViews ("hello", choc::value::createInt32 (4));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("hello"), 2u);
        CHOC_EXPECT_EQ (view.findLastMessageOfType ("hello").getWithDefault<int32_t> (0), 4);
    }

    {
        CHOC_TEST (MessagesSentToViews)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view1 (patch), view2 (patch);

        patch.broadcastMessageToViews ("everyone", choc::value::createString ("hi"));
        CHOC_EXPECT_EQ (view1.countMessagesOfType ("everyone"), 1u);
        CHOC_EXPECT_EQ (view2.countMessagesOfType ("everyone"), 1u);

        patch.sendMessageToView (view1, "justYou", choc::value::createString ("hi"));
        CHOC_EXPECT_EQ (view1.countMessagesOfType ("justYou"), 1u);
        CHOC_EXPECT_EQ (view2.countMessagesOfType ("justYou"), 0u);

        patch.sendPatchStatusChangeToViews();
        auto status = view1.findLastMessageOfType ("status");
        CHOC_EXPECT_TRUE (status.isObject());
        CHOC_EXPECT_TRUE (status.hasObjectMember ("manifest"));
        CHOC_EXPECT_TRUE (status.hasObjectMember ("details"));
        CHOC_EXPECT_EQ (status["host"].toString(), "Cmajor Test");
        CHOC_EXPECT_EQ (status["sampleRate"].getWithDefault<double> (0), 4.0);

        auto gainID = EndpointID::create (std::string_view ("gain"));
        patch.sendParameterChangeToViews (gainID, 0.75f);
        auto paramChange = view1.findLastMessageOfType ("param_value");
        CHOC_EXPECT_EQ (paramChange["endpointID"].toString(), "gain");
        CHOC_EXPECT_NEAR (paramChange["value"].getWithDefault<float> (0), 0.75f, 0.0001f);

        // an empty endpoint ID shouldn't send anything
        view1.clearMessages();
        patch.sendParameterChangeToViews (EndpointID(), 0.5f);
        CHOC_EXPECT_EQ (view1.countMessagesOfType ("param_value"), 0u);

        if (auto gain = patch.findParameter (gainID))
            gain->setValue (0.25f, true, -1, 0);

        patch.sendCurrentParameterValueToViews (gainID);
        CHOC_EXPECT_NEAR (view1.findLastMessageOfType ("param_value")["value"].getWithDefault<float> (0), 0.25f, 0.0001f);

        // asking for an unknown parameter shouldn't send anything
        view1.clearMessages();
        patch.sendCurrentParameterValueToViews (EndpointID::create (std::string_view ("nonexistent")));
        CHOC_EXPECT_EQ (view1.countMessagesOfType ("param_value"), 0u);

        patch.sendCPUInfoToViews (0.5f);
        CHOC_EXPECT_NEAR (view1.findLastMessageOfType ("cpu_info")["level"].getWithDefault<float> (0), 0.5f, 0.0001f);

        patch.setStoredStateValue ("key", choc::value::createString ("value"));
        CHOC_EXPECT_EQ (view2.findLastMessageOfType ("state_key_value")["key"].toString(), "key");

        // an empty key shouldn't send anything
        view1.clearMessages();
        patch.sendStoredStateValueToViews ({});
        CHOC_EXPECT_EQ (view1.countMessagesOfType ("state_key_value"), 0u);
    }
}

//==============================================================================
static void runPatchParameterTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (ParameterProperties)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain")));
        auto offset = patch.findParameter (EndpointID::create (std::string_view ("offset")));

        if (! gain || ! offset)
        {
            CHOC_FAIL ("Expected to find the parameters");
            return;
        }

        CHOC_EXPECT_EQ (gain->properties.endpointID, "gain");
        CHOC_EXPECT_EQ (gain->properties.name, "Gain");
        CHOC_EXPECT_EQ (gain->properties.unit, "dB");
        CHOC_EXPECT_EQ (gain->properties.group, "Levels");
        CHOC_EXPECT_NEAR (gain->properties.minValue, 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->properties.maxValue, 2.0f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->properties.defaultValue, 1.0f, 0.0001f);
        CHOC_EXPECT_TRUE (gain->properties.isEvent);
        CHOC_EXPECT_FALSE (offset->properties.isEvent);

        CHOC_EXPECT_NEAR (gain->properties.convertTo0to1 (1.0f), 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->properties.convertFrom0to1 (0.5f), 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->properties.convertTo0to1 (-5.0f), 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->properties.convertTo0to1 (5.0f), 1.0f, 0.0001f);
        CHOC_EXPECT_EQ (gain->properties.getNumDiscreteOptions(), 0u);

        // the endpoint handle should be usable for identifying the parameter
        CHOC_EXPECT_TRUE (gain->endpointHandle != offset->endpointHandle);
    }

    {
        CHOC_TEST (ParameterSetValue)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain")));

        if (! gain)
        {
            CHOC_FAIL ("Expected to find parameter");
            return;
        }

        uint32_t numValueChanges = 0;
        float lastValueSeen = -1.0f;
        gain->valueChanged = [&] (float v) { ++numValueChanges; lastValueSeen = v; };

        CHOC_EXPECT_TRUE (gain->setValue (0.5f, false, -1, 0));
        CHOC_EXPECT_EQ (numValueChanges, 1u);
        CHOC_EXPECT_NEAR (lastValueSeen, 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (gain->currentValue, 0.5f, 0.0001f);

        // setting the same value again shouldn't send anything unless it's forced
        CHOC_EXPECT_TRUE (gain->setValue (0.5f, false, -1, 0));
        CHOC_EXPECT_EQ (numValueChanges, 1u);

        CHOC_EXPECT_TRUE (gain->setValue (0.5f, true, -1, 0));
        CHOC_EXPECT_EQ (numValueChanges, 2u);

        // out-of-range values get clamped
        CHOC_EXPECT_TRUE (gain->setValue (100.0f, false, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, 2.0f, 0.0001f);
        CHOC_EXPECT_TRUE (gain->setValue (-100.0f, false, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, 0.0f, 0.0001f);

        // the ValueView overload parses strings as well as numbers
        CHOC_EXPECT_TRUE (gain->setValue (choc::value::createFloat32 (1.5f), false, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, 1.5f, 0.0001f);
        CHOC_EXPECT_TRUE (gain->setValue (choc::value::createString ("0.75"), false, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, 0.75f, 0.0001f);

        CHOC_EXPECT_TRUE (gain->resetToDefaultValue (false, -1, 0));
        CHOC_EXPECT_NEAR (gain->currentValue, gain->properties.defaultValue, 0.0001f);
    }

    {
        CHOC_TEST (ParameterGestures)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto gainID = EndpointID::create (std::string_view ("gain"));
        auto gain = patch.findParameter (gainID);

        if (! gain)
        {
            CHOC_FAIL ("Expected to find parameter");
            return;
        }

        uint32_t numStarts = 0, numEnds = 0;
        gain->gestureStart = [&] { ++numStarts; };
        gain->gestureEnd   = [&] { ++numEnds; };

        patch.sendGestureStart (gainID);
        patch.sendGestureEnd (gainID);
        CHOC_EXPECT_EQ (numStarts, 1u);
        CHOC_EXPECT_EQ (numEnds, 1u);

        // gestures for endpoints which aren't parameters should be ignored
        patch.sendGestureStart (EndpointID::create (std::string_view ("nonexistent")));
        patch.sendGestureEnd (EndpointID::create (std::string_view ("nonexistent")));
        CHOC_EXPECT_EQ (numStarts, 1u);
        CHOC_EXPECT_EQ (numEnds, 1u);

        RecordingPatchView view (patch);

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "send_gesture_start",
                                                                                "id", "gain")));
        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "send_gesture_end",
                                                                                "id", "gain")));
        CHOC_EXPECT_EQ (numStarts, 2u);
        CHOC_EXPECT_EQ (numEnds, 2u);
    }
}

//==============================================================================
static void runPatchProcessingTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (ChunkedProcessing)

        const auto source = R"(
            processor Test [[ main ]]
            {
                output stream float32 out;

                float32 count = 0;

                void main()
                {
                    loop
                    {
                        out <- count;
                        count += 1.0f;
                        advance();
                    }
                }
            }
        )";

        Patch patch;

        if (! initAndLoadTestPatch (patch, source, 8, 8, 0, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        std::array<float, 8> buffer {};
        std::array<float*, 1> buffers { { buffer.data() } };

        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDI = [] (auto&&...) {};

        const auto renderChunk = [&] (uint32_t start, uint32_t numFrames)
        {
            auto inputs = choc::buffer::createChannelArrayView (static_cast<const float* const*> (nullptr), 0u, numFrames);
            auto outputs = choc::buffer::createChannelArrayView (buffers.data(), 1u, 8u)
                             .getFrameRange ({ start, start + numFrames });

            patch.processChunk ({ inputs, outputs, {}, noMIDI }, true);
        };

        patch.beginChunkedProcess();
        renderChunk (0, 3);
        renderChunk (3, 5);
        patch.endChunkedProcess();

        for (uint32_t i = 0; i < 8; ++i)
            CHOC_EXPECT_NEAR (buffer[i], static_cast<float> (i), 0.0001f);

        // and the whole-block version should carry on from where the chunks left off
        buffer.fill (0.0f);
        patch.process (buffers.data(), 8, [] (auto&&...) {});

        for (uint32_t i = 0; i < 8; ++i)
            CHOC_EXPECT_NEAR (buffer[i], static_cast<float> (i + 8), 0.0001f);

        // ...and resetting should take it back to the start again
        patch.resetToInitialState();
        buffer.fill (0.0f);
        patch.process (buffers.data(), 8, [] (auto&&...) {});

        for (uint32_t i = 0; i < 8; ++i)
            CHOC_EXPECT_NEAR (buffer[i], static_cast<float> (i), 0.0001f);
    }

    {
        CHOC_TEST (MIDIInputAndOutput)

        const auto source = R"(
            processor Test [[ main ]]
            {
                input event std::midi::Message midiIn;
                output event std::midi::Message midiOut;
                output stream float32 out;

                event midiIn (std::midi::Message m)
                {
                    midiOut <- m;
                }

                void main()
                {
                    loop
                    {
                        out <- 0.0f;
                        advance();
                    }
                }
            }
        )";

        Patch patch;

        if (! initAndLoadTestPatch (patch, source, 4, 4, 0, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_TRUE (patch.hasMIDIInput());
        CHOC_EXPECT_TRUE (patch.hasMIDIOutput());

        std::array<float, 4> buffer {};
        std::array<float*, 1> buffers { { buffer.data() } };

        std::vector<choc::midi::ShortMessage> midiOut;

        const uint8_t noteOn[] = { 0x90, 60, 100 };
        patch.addMIDIMessage (0, noteOn, 3);

        patch.process (buffers.data(), 4, [&] (uint32_t, choc::midi::MessageView m)
        {
            midiOut.push_back (choc::midi::ShortMessage (m.data(), m.size()));
        });

        if (midiOut.size() != 1)
        {
            CHOC_FAIL ("Expected a single MIDI output message");
            return;
        }

        CHOC_EXPECT_TRUE (midiOut.front().isNoteOn());
        CHOC_EXPECT_EQ (static_cast<int> (midiOut.front().getVelocity()), 100);

        // the MIDI queue should have been flushed, so a second block produces nothing
        midiOut.clear();
        patch.process (buffers.data(), 4, [&] (uint32_t, choc::midi::MessageView m)
        {
            midiOut.push_back (choc::midi::ShortMessage (m.data(), m.size()));
        });

        CHOC_EXPECT_EQ (midiOut.size(), 0u);

        // sendMIDIInputEvent() pushes straight into the endpoint queue
        midiOut.clear();
        CHOC_EXPECT_TRUE (patch.sendMIDIInputEvent (EndpointID::create (std::string_view ("midiIn")),
                                                     choc::midi::ShortMessage (0x80, 60, 0), 0));

        patch.process (buffers.data(), 4, [&] (uint32_t, choc::midi::MessageView m)
        {
            midiOut.push_back (choc::midi::ShortMessage (m.data(), m.size()));
        });

        if (midiOut.size() != 1)
        {
            CHOC_FAIL ("Expected a single MIDI output message");
            return;
        }

        CHOC_EXPECT_TRUE (midiOut.front().isNoteOff());
    }

    {
        CHOC_TEST (TimelineEvents)

        const auto source = R"(
            processor Test [[ main ]]
            {
                input event std::timeline::Tempo tempoIn;
                input event std::timeline::TimeSignature timeSigIn;
                input event std::timeline::Position positionIn;

                output stream float32 out;

                float32 lastValue = 0;

                event tempoIn (std::timeline::Tempo t)            { lastValue = t.bpm; }
                event timeSigIn (std::timeline::TimeSignature t)  { lastValue = float32 (t.numerator) * 100.0f + float32 (t.denominator); }
                event positionIn (std::timeline::Position p)      { lastValue = float32 (p.quarterNote); }

                void main()
                {
                    loop
                    {
                        out <- lastValue;
                        advance();
                    }
                }
            }
        )";

        Patch patch;

        if (! initAndLoadTestPatch (patch, source, 4, 4, 0, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_TRUE (patch.wantsTimecodeEvents());

        std::array<float, 4> buffer {};
        std::array<float*, 1> buffers { { buffer.data() } };

        patch.sendBPM (120.0f, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});
        CHOC_EXPECT_NEAR (buffer[3], 120.0f, 0.0001f);

        patch.sendTimeSig (7, 8, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});
        CHOC_EXPECT_NEAR (buffer[3], 708.0f, 0.0001f);

        patch.sendPosition (1000, 2.5, 2.0, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});
        CHOC_EXPECT_NEAR (buffer[3], 2.5f, 0.0001f);
    }

    {
        CHOC_TEST (CustomAudioSourceForInput)

        struct ConstantSource  : public Patch::CustomAudioSource
        {
            ConstantSource (float v) : value (v) {}

            void prepare (double rate) override
            {
                preparedRate = rate;
                ++numPrepareCalls;
            }

            void read (choc::buffer::InterleavedView<float> block) override
            {
                choc::buffer::setAllSamples (block, [v = value] { return v; });
            }

            void read (choc::buffer::InterleavedView<double> block) override
            {
                choc::buffer::setAllSamples (block, [v = static_cast<double> (value)] { return v; });
            }

            float value;
            double preparedRate = 0;
            uint32_t numPrepareCalls = 0;
        };

        auto source = std::make_shared<ConstantSource> (0.75f);

        Patch patch;
        initTestPatch (patch);

        auto inputID = EndpointID::create (std::string_view ("in"));

        // a source can be attached before the patch is built
        patch.setCustomAudioSourceForInput (inputID, source);
        CHOC_EXPECT_TRUE (patch.getCustomAudioSourceForInput (inputID) == source);

        if (! loadTestPatch (patch, createBasicManifest(), passThroughPatchSource, 4, 44100, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        CHOC_EXPECT_EQ (source->numPrepareCalls, 1u);
        CHOC_EXPECT_EQ (source->preparedRate, 44100.0);

        std::array<float, 4> inputBacking { { 0.25f, 0.25f, 0.25f, 0.25f } };
        std::array<float, 4> outputBacking {};
        std::array<const float*, 1> inputBuffers { { inputBacking.data() } };
        std::array<float*, 1> outputBuffers { { outputBacking.data() } };

        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDI = [] (auto&&...) {};

        const auto render = [&]
        {
            outputBacking.fill (0.0f);

            auto inputs = choc::buffer::createChannelArrayView (inputBuffers.data(), 1u, 4u);
            auto outputs = choc::buffer::createChannelArrayView (outputBuffers.data(), 1u, 4u);

            patch.process ({ inputs, outputs, {}, noMIDI }, true);
        };

        // the custom source should override whatever the host provides
        render();

        for (auto s : outputBacking)
            CHOC_EXPECT_NEAR (s, 0.75f, 0.0001f);

        // swapping the source over while the patch is loaded should take effect
        auto otherSource = std::make_shared<ConstantSource> (0.5f);
        patch.setCustomAudioSourceForInput (inputID, otherSource);
        CHOC_EXPECT_TRUE (patch.getCustomAudioSourceForInput (inputID) == otherSource);
        CHOC_EXPECT_EQ (otherSource->numPrepareCalls, 1u);

        render();

        for (auto s : outputBacking)
            CHOC_EXPECT_NEAR (s, 0.5f, 0.0001f);

        // ...and removing it should hand the input back to the host
        patch.setCustomAudioSourceForInput (inputID, {});
        CHOC_EXPECT_TRUE (patch.getCustomAudioSourceForInput (inputID) == nullptr);

        render();

        for (auto s : outputBacking)
            CHOC_EXPECT_NEAR (s, 0.25f, 0.0001f);
    }

    {
        CHOC_TEST (EndpointDataListenerRegistration)

        Patch patch;

        if (! initAndLoadTestPatch (patch, kitchenSinkPatchSource, 64, 48000, 2, 2))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        auto audioOut = EndpointID::create (std::string_view ("audioOut"));
        auto audioIn = EndpointID::create (std::string_view ("audioIn"));
        auto rawEvent = EndpointID::create (std::string_view ("rawEvent"));
        auto midiIn = EndpointID::create (std::string_view ("midiIn"));
        auto unknown = EndpointID::create (std::string_view ("nonexistent"));

        CHOC_EXPECT_TRUE (patch.startEndpointData (view, audioOut, "levels", 64, false));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, audioIn, "inputLevels", 64, true));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, rawEvent, "events", 0, false));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, midiIn, "midi", 0, false));
        CHOC_EXPECT_FALSE (patch.startEndpointData (view, unknown, "nope", 0, false));

        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, audioOut, "levels"));
        CHOC_EXPECT_FALSE (patch.stopEndpointData (view, audioOut, "levels"));
        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, rawEvent, "events"));
        CHOC_EXPECT_FALSE (patch.stopEndpointData (view, rawEvent, "events"));
        CHOC_EXPECT_FALSE (patch.stopEndpointData (view, unknown, "nope"));

        // destroying a view should clean up any listeners it still has registered
        {
            RecordingPatchView temporaryView (patch);
            CHOC_EXPECT_TRUE (patch.startEndpointData (temporaryView, audioOut, "levels", 64, false));
            CHOC_EXPECT_TRUE (patch.startEndpointData (temporaryView, rawEvent, "events", 0, false));
        }

        std::array<std::array<float, 64>, 2> backingBuffers {};
        std::array<float*, 2> buffers { { backingBuffers[0].data(), backingBuffers[1].data() } };
        patch.process (buffers.data(), 64, [] (auto&&...) {});
    }
}

//==============================================================================
static void runPatchClientMessageTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (ClientMessageRejection)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, {}));
        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, choc::value::createInt32 (123)));
        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, choc::value::createEmptyArray()));
        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, choc::value::createObject ({})));
        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, choc::json::create ("type", 123)));
        CHOC_EXPECT_FALSE (patch.handleClientMessage (view, choc::json::create ("type", "not_a_real_message_type")));
    }

    {
        CHOC_TEST (ClientMessageStateHandling)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "send_state_value",
                                                                                "key", "abc",
                                                                                "value", choc::value::createString ("def"))));
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 1u);

        view.clearMessages();
        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_state_value",
                                                                                "key", "abc")));
        CHOC_EXPECT_EQ (view.findLastMessageOfType ("state_key_value")["value"].toString(), "def");

        // the full state should come back addressed to the view which asked for it
        if (auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain"))))
            gain->setValue (0.25f, true, -1, 0);

        RecordingPatchView otherView (patch);
        view.clearMessages();

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_full_state",
                                                                                "replyType", "state_reply")));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("state_reply"), 1u);
        CHOC_EXPECT_EQ (otherView.countMessagesOfType ("state_reply"), 0u);

        auto fullState = view.findLastMessageOfType ("state_reply");
        CHOC_EXPECT_EQ (fullState["parameters"].size(), 1u);
        CHOC_EXPECT_EQ (fullState["values"]["abc"].toString(), "def");

        // a request with no replyType is accepted, but shouldn't send anything
        view.clearMessages();
        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_full_state")));
        CHOC_EXPECT_EQ (view.messages.size(), 0u);

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "send_full_state",
                                                                                "value", fullState)));
        CHOC_EXPECT_EQ (patch.getStoredStateValues().size(), 1u);

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "clear_all_state_values")));
        CHOC_EXPECT_TRUE (patch.getStoredStateValues().empty());
    }

    {
        CHOC_TEST (ClientMessageUnloadAndReset)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        if (auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain"))))
        {
            gain->setValue (0.25f, true, -1, 0);
            CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_reset")));
            CHOC_EXPECT_NEAR (gain->currentValue, gain->properties.defaultValue, 0.0001f);
        }

        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_status")));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("status"), 1u);

        // a load_patch message with no filename just unloads
        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "load_patch")));
        CHOC_EXPECT_FALSE (patch.isLoaded());

        // ...and req_status on an unloaded patch shouldn't send anything
        view.clearMessages();
        CHOC_EXPECT_TRUE (patch.handleClientMessage (view, choc::json::create ("type", "req_status")));
        CHOC_EXPECT_EQ (view.countMessagesOfType ("status"), 0u);
    }
}

//==============================================================================
static void runPatchAsyncTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (AsynchronousLoad)

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        CHOC_EXPECT_TRUE (patch.loadPatch ({ createManifestWithInMemoryFiles (createBasicManifest(),
                                                                              {{ "Test.cmajor", gainPatchSource }}), {} }, false));

        CHOC_EXPECT_TRUE (runMessageLoopUntil ([&] { return patch.isPlayable(); }));
        CHOC_EXPECT_EQ (patch.getParameterList().size(), 2u);
    }

    {
        CHOC_TEST (EndpointDataDeliveredToViews)

        Patch patch;

        if (! initAndLoadTestPatch (patch, kitchenSinkPatchSource, 64, 48000, 2, 2))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        auto audioOut = EndpointID::create (std::string_view ("audioOut"));
        auto rawEvent = EndpointID::create (std::string_view ("rawEvent"));

        CHOC_EXPECT_TRUE (patch.startEndpointData (view, audioOut, "levels", 64, false));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, rawEvent, "events", 0, false));

        CHOC_EXPECT_TRUE (patch.sendEventOrValueToPatch (rawEvent, choc::value::createFloat32 (0.5f), -1, 0));

        std::array<std::array<float, 64>, 2> backingBuffers {};
        std::array<float*, 2> buffers { { backingBuffers[0].data(), backingBuffers[1].data() } };
        patch.process (buffers.data(), 64, [] (auto&&...) {});

        CHOC_EXPECT_TRUE (runMessageLoopUntil ([&] { return view.countMessagesOfType ("levels") != 0
                                                             && view.countMessagesOfType ("events") != 0; }));

        auto levels = view.findLastMessageOfType ("levels");
        CHOC_EXPECT_TRUE (levels.isObject());
        CHOC_EXPECT_EQ (levels["min"].size(), 2u);
        CHOC_EXPECT_EQ (levels["max"].size(), 2u);

        CHOC_EXPECT_NEAR (view.findLastMessageOfType ("events").getWithDefault<float> (0), 0.5f, 0.0001f);

        // once removed, no more data should arrive
        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, audioOut, "levels"));
        view.clearMessages();
        patch.process (buffers.data(), 64, [] (auto&&...) {});
        runMessageLoopUntil ([] { return false; }, 200);
        CHOC_EXPECT_EQ (view.countMessagesOfType ("levels"), 0u);
    }

    {
        CHOC_TEST (ParameterChangesDeliveredToViews)

        Patch patch;

        if (! initAndLoadTestPatch (patch, gainPatchSource, 4, 4, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);

        auto gain = patch.findParameter (EndpointID::create (std::string_view ("gain")));

        if (! gain)
        {
            CHOC_FAIL ("Expected to find parameter");
            return;
        }

        gain->setValue (0.25f, true, -1, 0);

        CHOC_EXPECT_TRUE (runMessageLoopUntil ([&] { return view.countMessagesOfType ("param_value") != 0; }));

        auto message = view.findLastMessageOfType ("param_value");
        CHOC_EXPECT_EQ (message["endpointID"].toString(), "gain");
        CHOC_EXPECT_NEAR (message["value"].getWithDefault<float> (0), 0.25f, 0.0001f);
    }

    {
        CHOC_TEST (CPUInfoDeliveredToViews)

        Patch patch;

        if (! initAndLoadTestPatch (patch, passThroughPatchSource, 64, 48000, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);
        patch.setCPUInfoMonitorChunkSize (64);

        std::array<float, 64> inputBacking {};
        std::array<float, 64> outputBacking {};
        std::array<const float*, 1> inputBuffers { { inputBacking.data() } };
        std::array<float*, 1> outputBuffers { { outputBacking.data() } };

        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDI = [] (auto&&...) {};

        const auto renderBlocks = [&] (uint32_t numBlocks)
        {
            auto inputs = choc::buffer::createChannelArrayView (inputBuffers.data(), 1u, 64u);
            auto outputs = choc::buffer::createChannelArrayView (outputBuffers.data(), 1u, 64u);

            for (uint32_t i = 0; i < numBlocks; ++i)
                patch.process ({ inputs, outputs, {}, noMIDI }, true);
        };

        renderBlocks (32);

        CHOC_EXPECT_TRUE (runMessageLoopUntil ([&] { return view.countMessagesOfType ("cpu_info") != 0; }));

        auto cpuInfo = view.findLastMessageOfType ("cpu_info");
        CHOC_EXPECT_TRUE (cpuInfo.isObject() && cpuInfo.hasObjectMember ("level"));

        // setting the rate to zero should stop the callbacks
        patch.setCPUInfoMonitorChunkSize (0);
        view.clearMessages();
        renderBlocks (32);

        runMessageLoopUntil ([] { return false; }, 200);
        CHOC_EXPECT_EQ (view.countMessagesOfType ("cpu_info"), 0u);
    }

    {
        CHOC_TEST (CodeGeneration)

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        Patch::LoadParams loadParams;
        loadParams.manifest = createManifestWithInMemoryFiles (createBasicManifest(),
                                                               {{ "Test.cmajor", gainPatchSource }});

        // calling this on the message thread isn't allowed, and should be reported
        auto badResult = patch.generateCode (loadParams, "cpp", {});
        CHOC_EXPECT_TRUE (badResult.messages.hasErrors());
        CHOC_EXPECT_TRUE (badResult.generatedCode.empty());

        // ...but on a background thread it should work
        cmaj::Engine::CodeGenOutput result;

        auto thread = std::thread ([&]
        {
            result = patch.generateCode (loadParams, "cpp", {});
            choc::messageloop::stop();
        });

        choc::messageloop::run();
        thread.join();

        CHOC_EXPECT_FALSE (result.messages.hasErrors());
        CHOC_EXPECT_FALSE (result.generatedCode.empty());
        CHOC_EXPECT_FALSE (result.mainClassName.empty());

        // an unknown target type should come back with an error rather than throwing
        cmaj::Engine::CodeGenOutput badTarget;

        auto thread2 = std::thread ([&]
        {
            badTarget = patch.generateCode (loadParams, "not_a_real_target", {});
            choc::messageloop::stop();
        });

        choc::messageloop::run();
        thread2.join();

        CHOC_EXPECT_TRUE (badTarget.generatedCode.empty());
    }
}

//==============================================================================
static bool runUnitTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchUtilities);

    {
        CHOC_TEST (LatencyReporting)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,
            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph G [[ main ]]
            {
                input stream float in;
                output stream float out;
                connection in -> P.in;
                connection P.out -> out;
            }

            processor P
            {
                input stream float in;
                output stream float out;
                processor.latency = 32;
                void main() { loop { out <- in; advance(); } }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 1;
        params.sampleRate = 1;
        params.numInputChannels = 0;
        params.numOutputChannels = 0;
        patch.setPlaybackParams (params);

        CHOC_EXPECT_TRUE (patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true));
        CHOC_EXPECT_EQ (patch.getFramesLatency(), 32.0);
    }

    {
        CHOC_TEST (StringValueConversion/CustomRange)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Silence  [[ main ]]
            {
                input event float scaledOptions  [[ name: "Scaled Options", min: 0, max: 100, init:  80, text: "One|Two|Three|Four|Five" ]];

                output stream float out;

                connection 0.0f -> out;
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 1;
        params.sampleRate = 1;
        params.numInputChannels = 0;
        params.numOutputChannels = 0;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        auto parameter = patch.findParameter (EndpointID::create (std::string_view ("scaledOptions")));

        if (! parameter)
        {
            CHOC_FAIL ("Expected to find parameter");
            return false;
        }

        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (0.0f), "One");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (19.9f), "One");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (20.0f), "Two");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (39.0f), "Two");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (40.0f), "Three");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (59.9f), "Three");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (60.0f), "Four");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (79.9f), "Four");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (80.0f), "Five");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (100.0f), "Five");

        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("One"), 0.0f, 0.0001f);
        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("Two"), 25.0f, 0.0001f);
        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("Three"), 50.0f, 0.0001f);
        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("Four"), 75.0f, 0.0001f);
        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("Five"), 100.0f, 0.0001f);
    }

    {
        CHOC_TEST (StringValueConversion/CustomRangeNonZeroMinValue)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Silence  [[ main ]]
            {
                input event float scaledOptions  [[ name: "Scaled Options", min: 1, max: 2, init:  0, text: "One|Two" ]];

                output stream float out;

                connection 0.0f -> out;
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 1;
        params.sampleRate = 1;
        params.numInputChannels = 0;
        params.numOutputChannels = 0;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        auto parameter = patch.findParameter (EndpointID::create (std::string_view ("scaledOptions")));

        if (! parameter)
        {
            CHOC_FAIL ("Expected to find parameter");
            return false;
        }

        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (1.0f), "One");
        CHOC_EXPECT_EQ (parameter->properties.getValueAsString (2.0f), "Two");

        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("One"), 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (*parameter->properties.getStringAsValue ("Two"), 2.0f, 0.0001f);
    }

    {
        CHOC_TEST (UpdateValueEndpoint/NoExplicitRampFrames)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test  [[ main ]]
            {
                input value float multiplier [[ name: "multiplier" ]];
                output stream float out;

                node multiplierStream = ValueToStream;

                connection
                {
                    multiplier -> multiplierStream.in;
                    (1.0f * multiplierStream.out) -> out;
                }
            }

            processor ValueToStream
            {
                input value float in;
                output stream float out;

                void main()
                {
                    loop
                    {
                        out <- in;
                        advance();
                    }
                }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 0;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<float, 4> buffer { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float*, 1> buffers { { buffer.data() } };

        patch.sendEventOrValueToPatch (EndpointID::create (std::string_view ("multiplier")),
                                       choc::value::Value (1.0f).getView(), -1, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});

        CHOC_EXPECT_NEAR (buffer[0], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (buffer[1], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (buffer[2], 1.0f, 0.0001f);
        CHOC_EXPECT_NEAR (buffer[3], 1.0f, 0.0001f);
    }

    {
        CHOC_TEST (UpdateValueEndpoint/ExplicitRampFramesViaView)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test  [[ main ]]
            {
                input value float multiplier [[ name: "multiplier", init: 1.0f ]];
                output stream float out;

                node multiplierStream = ValueToStream;

                connection
                {
                    multiplier -> multiplierStream.in;
                    (1.0f * multiplierStream.out) -> out;
                }
            }

            processor ValueToStream
            {
                input value float in;
                output stream float out;

                void main()
                {
                    loop
                    {
                        out <- in;
                        advance();
                    }
                }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 0;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<float, 4> buffer { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float*, 1> buffers { { buffer.data() } };
        patch.process (buffers.data(), 4, [] (auto&&...) {}); // process one buffer to flush through initial values

        patch.sendEventOrValueToPatch (EndpointID::create (std::string_view ("multiplier")),
                                       choc::value::Value (0.5f).getView(),
                                       4, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});

        // values should be decreasing from 1.0f
        CHOC_EXPECT_TRUE (buffer[0] > buffer[1]);
        CHOC_EXPECT_TRUE (buffer[1] > buffer[2]);
        CHOC_EXPECT_NEAR (buffer[3], 0.5f, 0.0001f);
    }

    {
        CHOC_TEST (UpdateValueEndpoint/ExplicitRampFramesViaAnnotation)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test  [[ main ]]
            {
                input value float multiplier [[ name: "multiplier", init: 1.0f, rampFrames: 4 ]];
                output stream float out;

                node multiplierStream = ValueToStream;

                connection
                {
                    multiplier -> multiplierStream.in;
                    (1.0f * multiplierStream.out) -> out;
                }
            }

            processor ValueToStream
            {
                input value float in;
                output stream float out;

                void main()
                {
                    loop
                    {
                        out <- in;
                        advance();
                    }
                }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 0;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<float, 4> buffer { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float*, 1> buffers { { buffer.data() } };
        patch.process (buffers.data(), 4, [] (auto&&...) {}); // process one buffer to flush through initial values

        patch.sendEventOrValueToPatch (EndpointID::create (std::string_view ("multiplier")),
                                       choc::value::Value (0.5f).getView(), -1, 0);
        patch.process (buffers.data(), 4, [] (auto&&...) {});

        // values should be decreasing from 1.0f
        CHOC_EXPECT_TRUE (buffer[0] > buffer[1]);
        CHOC_EXPECT_TRUE (buffer[1] > buffer[2]);
        CHOC_EXPECT_NEAR (buffer[3], 0.5f, 0.0001f);
    }

    // N.B. verifies messages can be dispatched without crashing, doesn't verify any side effects
    const auto runBasicClientMessageDispatchTests = [&] (bool shouldCompile)
    {
        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto validCmajorSource = R"(
            processor G  [[ main ]]
            {
                input value float inValueParameter [[ name: "Value" ]];
                input event float inEventParameter [[ name: "Event" ]];
                input value float inValue;
                input event float inEvent;
                output stream float out;

                void main() { loop advance(); }
            }
        )";

        const auto invalidCmajorSource = R"(
            processor G  [[ main ]]
            {
                input value float inValueParameter [[ name: "Value" ]];
                input event float inEventParameter [[ name: "Event" ]];
                input value float inValue;
                input event float inEvent;
                output stream float out // missing semicolon

                void main() { loop advance(); }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 1;
        params.sampleRate = 1;
        params.numInputChannels = 0;
        params.numOutputChannels = 0;
        patch.setPlaybackParams (params);

        const File testSource { "Test.cmajor", shouldCompile ? validCmajorSource : invalidCmajorSource };
        CHOC_EXPECT_EQ (patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, { testSource }), {} }, true), shouldCompile);

        const std::vector<choc::value::Value> messages
        {
            choc::json::create ("type", "send_value",
                                "id", "inValueParameter",
                                "value", choc::value::createFloat32 (0.5)),
           choc::json::create ("type", "send_value",
                               "id", "inValueParameter",
                               "value", choc::value::createFloat32 (0.0),
                               "rampFrames", 4),
           choc::json::create ("type", "send_value",
                               "id", "inEventParameter",
                               "value", choc::value::createFloat32 (0.5)),
           choc::json::create ("type", "send_value",
                               "id", "inValue",
                               "value", choc::value::createFloat32 (0.5)),
           choc::json::create ("type", "send_value",
                               "id", "inValue",
                               "value", choc::value::createFloat32 (1.0),
                               "rampFrames", choc::value::createInt32 (0)),
           choc::json::create ("type", "send_value",
                               "id", "inEvent",
                               "value", choc::value::createFloat32 (0.5)),

           choc::json::create ("type", "send_gesture_start",
                               "id", "inValueParameter"),
           choc::json::create ("type", "send_gesture_end",
                               "id", "inValueParameter"),
           choc::json::create ("type", "send_gesture_start",
                               "id", "inEventParameter"),
           choc::json::create ("type", "send_gesture_end",
                               "id", "inEventParameter"),

           choc::json::create ("type", "req_status"),

           choc::json::create ("type", "req_param_value",
                               "id", "inValueParameter"),
           choc::json::create ("type", "req_param_value",
                               "id", "inEventParameter"),

           choc::json::create ("type", "req_reset"),
           choc::json::create ("type", "req_state_value",
                               "key", "doesnotexist"),
           choc::json::create ("type", "send_state_value",
                               "key", "exists",
                               "value", choc::value::createBool (true)),
           choc::json::create ("type", "req_full_state",
                               "replyType", "fullstate_response_0"),

           choc::json::create ("type", "send_full_state",
                               "value", choc::json::create()),

           // N.B. load_patch omitted for now, as it either uses the actual file system, or unloads the patch

           choc::json::create ("type", "add_endpoint_listener",
                               "endpoint", "inEvent",
                               "replyType", "xyz"),
           choc::json::create ("type", "add_endpoint_listener",
                               "endpoint", "inEvent",
                               "replyType", "xyz",
                               "granularity", choc::value::createInt32 (10)),
           choc::json::create ("type", "remove_endpoint_listener",
                               "endpoint", "inEvent",
                               "replyType", "xyz"),

           choc::json::create ("type", "set_cpu_info_rate"),
           choc::json::create ("type", "set_cpu_info_rate",
                               "framesPerCallback", choc::value::createInt64 (4)),
           choc::json::create ("type", "set_cpu_info_rate",
                               "framesPerCallback", choc::value::createInt64 (0)),

           choc::json::create ("type", "unload"),
        };

        struct ProxyPatchView  : public cmaj::PatchView
        {
            ProxyPatchView (cmaj::Patch& p) : PatchView (p) {}
            void sendMessage (const choc::value::ValueView&) override {}
        };

        ProxyPatchView view (patch);

        for (const auto& message : messages)
            CHOC_EXPECT_TRUE (patch.handleClientMessage (view, message));
    };

    {
        CHOC_TEST (DispatchClientMessages/ValidPatch)

        const auto shouldCompile = true;
        runBasicClientMessageDispatchTests (shouldCompile);
    }

    {
        CHOC_TEST (DispatchClientMessages/InvalidPatch)

        const auto shouldCompile = false;
        runBasicClientMessageDispatchTests (shouldCompile);
    }

    {
        CHOC_TEST (ParameterSnapping)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
            {
                output stream float32 out;

                input gain.options;
                input gain.scaledOptions;
                input event float32 formatted [[ name: "Formatted", text: "%+d", max: 10.f ]];
                input value float32 snappedMultiplier [[ name: "Snapped Multiplier", step: 0.5, init: 1.0f ]];
                input value float32 discreteMultiplier [[ name: "Discrete Multiplier", step: 0.5, init: 1.0f, discrete ]];

                node gain = IndexGain;

                connection 1.0f -> gain.in;
                connection gain.out * snappedMultiplier * discreteMultiplier -> out;
            }

            processor IndexGain
            {
                input stream float32 in;
                output stream float32 out;

                input event float32 options [[ name: "Options", text: "I|II|III" ]];
                input event float32 scaledOptions [[ name: "Scaled Options", text: "I|II|III", min: 10, max: 110 ]];

                let targets = float32[] (0.0f, 0.25f, 1.0f);
                wrap<targets.size> currentTargetIndex = 0;

                event options (float32 e)
                {
                    currentTargetIndex = currentTargetIndex.type (e);
                }

                event scaledOptions (float32 e)
                {
                    currentTargetIndex = currentTargetIndex.type (((e - 10.0f) / 100.f) * (targets.size - 1));
                }

                void main()
                {
                    loop
                    {
                        out <- in * targets[currentTargetIndex];
                        advance();
                    }
                }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 0;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<float, 4> buffer { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float*, 1> buffers { { buffer.data() } };

        const auto reset = [&]
        {
            patch.resetToInitialState();
            buffer.fill (0.0f);
        };

        {
            reset();

            auto parameter = patch.findParameter (EndpointID::create (std::string_view ("options")));

            if (! parameter)
            {
                CHOC_FAIL ("Expected to find parameter");
                return false;
            }

            const auto valueToSnap = 1.34f;

            const auto forceSend = true;
            parameter->setValue (choc::value::Value (valueToSnap).getView(), forceSend, -1, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 1.0f, 0.0001f);
        }

        {
            reset();

            auto parameter = patch.findParameter (EndpointID::create (std::string_view ("scaledOptions")));

            if (! parameter)
            {
                CHOC_FAIL ("Expected to find parameter");
                return false;
            }

            const auto valueToSnap = 70.0f;

            const auto forceSend = true;
            parameter->setValue (choc::value::Value (valueToSnap).getView(), forceSend, -1, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.25f, 0.0001f);
        }

        {
            reset();

            auto parameter = patch.findParameter (EndpointID::create (std::string_view ("formatted")));

            if (! parameter)
            {
                CHOC_FAIL ("Expected to find parameter");
                return false;
            }

            CHOC_EXPECT_EQ (parameter->properties.getValueAsString (3.14f), "+3");
        }

        {
            reset();

            auto snappedMultiplierParameter = patch.findParameter (EndpointID::create (std::string_view ("snappedMultiplier")));

            if (! snappedMultiplierParameter)
            {
                CHOC_FAIL ("Expected to find parameter");
                return false;
            }

            auto optionsParameter = patch.findParameter (EndpointID::create (std::string_view ("options")));

            if (! optionsParameter)
            {
                CHOC_FAIL ("Expected to find optionsParameter");
                return false;
            }

            const auto forceSend = true;
            optionsParameter->setValue (choc::value::Value (2.0f).getView(), forceSend, -1, 0);

            const auto requestedValue = 0.7f;
            snappedMultiplierParameter->setValue (choc::value::Value (requestedValue).getView(), forceSend, -1, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            const auto displayValue = snappedMultiplierParameter->properties.getValueAsString (requestedValue);

            CHOC_EXPECT_EQ (displayValue, "0.5");

            const auto displayValueFloat = std::stof (displayValue);
            CHOC_EXPECT_NEAR (buffer[0], displayValueFloat, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], displayValueFloat, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], displayValueFloat, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], displayValueFloat, 0.0001f);
        }

        {
            reset();

            auto discreteMultiplier = patch.findParameter (EndpointID::create (std::string_view ("discreteMultiplier")));

            if (! discreteMultiplier)
            {
                CHOC_FAIL ("Expected to find parameter");
                return false;
            }

            CHOC_EXPECT_EQ (discreteMultiplier->properties.getNumDiscreteOptions(), 3u);

            auto optionsParameter = patch.findParameter (EndpointID::create (std::string_view ("options")));

            if (! optionsParameter)
            {
                CHOC_FAIL ("Expected to find optionsParameter");
                return false;
            }

            const auto forceSend = true;
            optionsParameter->setValue (choc::value::Value (2.0f).getView(), forceSend, -1, 0);

            const auto setValueAndVerifyPerformerAndDisplayValuesMatch = [&] (const auto requestedValue, const auto& expectedDisplayValue)
            {
                discreteMultiplier->setValue (choc::value::Value (requestedValue).getView(), forceSend, -1, 0);
                patch.process (buffers.data(), 4, [] (auto&&...) {});

                const auto displayValue = discreteMultiplier->properties.getValueAsString (requestedValue);

                CHOC_EXPECT_EQ (displayValue, expectedDisplayValue);

                const auto displayValueFloat = std::stof (displayValue);
                CHOC_EXPECT_NEAR (buffer[0], displayValueFloat, 0.0001f);
                CHOC_EXPECT_NEAR (buffer[1], displayValueFloat, 0.0001f);
                CHOC_EXPECT_NEAR (buffer[2], displayValueFloat, 0.0001f);
                CHOC_EXPECT_NEAR (buffer[3], displayValueFloat, 0.0001f);
            };

            setValueAndVerifyPerformerAndDisplayValuesMatch (0.00f, "0.0");
            setValueAndVerifyPerformerAndDisplayValuesMatch (0.32f, "0.0");
            setValueAndVerifyPerformerAndDisplayValuesMatch (0.34f, "0.5");
            setValueAndVerifyPerformerAndDisplayValuesMatch (0.65f, "0.5");
            setValueAndVerifyPerformerAndDisplayValuesMatch (0.67f, "1.0");
            setValueAndVerifyPerformerAndDisplayValuesMatch (1.00f, "1.0");
        }
    }

    {
        CHOC_TEST (SendTransportState)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "generator",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": true,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            graph Test [[ main ]]
             {
                 output stream float out;
                 input event std::timeline::TransportState transportStateIn;

                 // N.B. work around `std::timeline::TransportState` not working as a value
                 node cachedEvent = EventToValue (std::timeline::TransportState, std::timeline::TransportState ());

                 connection
                 {
                     transportStateIn -> cachedEvent;
                     float32 (cachedEvent.out.isRecording()) * 0.55f -> out;
                     float32 (cachedEvent.out.isPlaying()) * 0.25f -> out;
                     float32 (cachedEvent.out.isLooping()) * 0.2f -> out;
                 }
             }

             processor EventToValue (using T, T initialValue)
             {
                 input event T in;
                 output value T out;

                 T cached = initialValue;

                 event in (T e)
                 {
                     cached = e;
                 }

                 void main()
                 {
                     loop
                     {
                         out <- cached;
                         advance();
                     }
                 }
             }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 0;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<float, 4> buffer { { 0.0f, 0.0f, 0.0f, 0.0f } };
        std::array<float*, 1> buffers { { buffer.data() } };

        const auto reset = [&]
        {
            patch.resetToInitialState();
            buffer.fill (0.0f);
        };

        {
            reset();

            const auto isRecording = true;
            const auto isPlaying = false;
            const auto isLooping = false;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.55f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.55f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = false;
            const auto isPlaying = true;
            const auto isLooping = false;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.25f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.25f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = false;
            const auto isPlaying = false;
            const auto isLooping = true;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.2f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.2f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = true;
            const auto isPlaying = true;
            const auto isLooping = true;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 1.0f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 1.0f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = false;
            const auto isPlaying = true;
            const auto isLooping = true;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.45f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.45f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = true;
            const auto isPlaying = false;
            const auto isLooping = true;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.75f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.75f, 0.0001f);
        }

        {
            reset();

            const auto isRecording = true;
            const auto isPlaying = true;
            const auto isLooping = false;
            patch.sendTransportState (isRecording, isPlaying, isLooping, 0);
            patch.process (buffers.data(), 4, [] (auto&&...) {});

            CHOC_EXPECT_NEAR (buffer[0], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[1], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[2], 0.8f, 0.0001f);
            CHOC_EXPECT_NEAR (buffer[3], 0.8f, 0.0001f);
        }
    }

    {
        CHOC_TEST (MultipleAudioInputsWithDifferentChannelCounts)

        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "com.your_name.your_patch_ID",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Your Company Goes Here",
            "isInstrument": false,

            "source": ["Test.cmajor"]
        })";

        const auto cmajorSource = R"(
            processor Test [[ main ]]
            {
                input stream float32<2> mainIn;
                input stream float32 sidechain;

                output stream float32 out;

                void main()
                {
                    loop
                    {
                        out <- (mainIn[0] + mainIn[1]) * 0.5f * sidechain;

                        advance();
                    }
                }
            }
        )";

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 4;
        params.sampleRate = 4;
        params.numInputChannels = 3;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        if (! patch.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", cmajorSource }}), {} }, true))
        {
            CHOC_FAIL ("Failed to load patch");
            return false;
        }

        std::array<std::array<float, 4>, 2> mainBackingBuffer
        {{
            {{ 0.1f, 0.2f, 0.3f, 0.4f }},
            {{ 0.9f, 0.8f, 0.7f, 0.6f }},
        }};
        std::array<float, 4> sidechainBackingBuffer { { 1.0f, 0.75f, 0.5f, 0.25f } };
        std::array<const float*, 3> inputBuffers
        {{
            mainBackingBuffer[0].data(),
            mainBackingBuffer[1].data(),
            sidechainBackingBuffer.data()
        }};

        std::array<float, 4> outputBackingBuffer {{}};
        std::array<float*, 1> outputBuffers { { outputBackingBuffer.data() } };

        auto inputs = choc::buffer::createChannelArrayView (inputBuffers.data(), static_cast<uint32_t> (inputBuffers.size()), params.blockSize);
        auto outputs = choc::buffer::createChannelArrayView (outputBuffers.data(), static_cast<uint32_t> (outputBuffers.size()), params.blockSize);

        const auto block = choc::audio::AudioMIDIBlockDispatcher::Block { inputs, outputs, {}, {} };
        const auto replaceOutput = true;
        patch.process (block, replaceOutput);

        CHOC_EXPECT_NEAR (outputBackingBuffer[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[1], 0.375f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[2], 0.25f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[3], 0.125f, 0.0001f);
    }

    runPatchStateTests (progress);
    runPatchStoredStateTests (progress);
    runPatchViewTests (progress);
    runPatchParameterTests (progress);
    runPatchProcessingTests (progress);
    runPatchClientMessageTests (progress);
    runPatchAsyncTests (progress);

    return progress.numFails == 0;
}

} // namespace cmaj::patch_helper_tests
