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

#include <set>
#include "cmajor/helpers/cmaj_Patch.h"
#include "../../../../modules/playback/include/cmaj_AudioSources.h"
#include "../../../../modules/playback/include/cmaj_PatchPlayer.h"   // (which includes cmaj_AllocationChecker.h, which has no include guard)

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

        uint32_t numPatchChanges = 0, numStops = 0, numStarts = 0;

        Patch patch;
        initTestPatch (patch);

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

        Patch::Status lastStatus;

        Patch patch;
        initTestPatch (patch);

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
        CHOC_TEST (GraphStructureIsCachedPerBuild)

        // (the external's value matters: without it, the program wouldn't load)
        const auto manifestSource = R"({
            "CmajorVersion": 1,
            "ID": "dev.cmajor.tests.graph_structure",
            "version": "1.0",
            "name": "Test",
            "description": "Test",
            "category": "effect",
            "manufacturer": "Cmajor Software Ltd",
            "isInstrument": false,
            "source": ["Test.cmajor"],
            "externals": { "Gain::scale": 0.5 }
        })";

        const auto createSource = [] (const std::string& extraNode)
        {
            return R"(
                processor Gain
                {
                    input stream float32 in;
                    output stream float32 out;
                    external float32 scale;

                    void main() { loop { out <- in * scale; advance(); } }
                }

                graph Test [[ main ]]
                {
                    input stream float32 in;
                    output stream float32 out;

                    node first = Gain;
                    )" + extraNode + R"(

                    connection in -> first.in;
                    connection first.out -> out;
                }
            )";
        };

        const auto load = [&] (Patch& p, const std::string& source)
        {
            return p.loadPatch ({ createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", source }}), {} }, true);
        };

        const auto hasNode = [] (const std::string& json, std::string_view path)
        {
            auto graph = choc::json::parse (json);

            for (auto node : graph["root"]["nodes"])
                if (node["path"].toString() == path)
                    return true;

            return false;
        };

        // it's off by default
        {
            Patch patch;
            initTestPatch (patch);
            patch.setPlaybackParams ({ 4, 4, 1, 1 });
            CHOC_EXPECT_TRUE (load (patch, createSource ({})));
            CHOC_EXPECT_TRUE (patch.getGraphStructure().empty());
            CHOC_EXPECT_TRUE (patch.getGraphStructureVersion().empty());
        }

        Patch patch;
        initTestPatch (patch);
        patch.generateGraphStructure = true;
        patch.setPlaybackParams ({ 4, 4, 1, 1 });

        if (! load (patch, createSource ({})))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        auto firstGraph = patch.getGraphStructure();
        auto firstVersion = patch.getGraphStructureVersion();
        CHOC_EXPECT_FALSE (firstGraph.empty());
        CHOC_EXPECT_FALSE (firstVersion.empty());
        CHOC_EXPECT_TRUE (hasNode (firstGraph, "first"));
        CHOC_EXPECT_FALSE (hasNode (firstGraph, "second"));

        // the same as the "graph" code generation target gives
        {
            Patch codeGenPatch;
            initTestPatch (codeGenPatch);
            codeGenPatch.setPlaybackParams ({ 4, 4, 1, 1 });
            Patch::LoadParams params;
            params.manifest = createManifestWithInMemoryFiles (manifestSource, {{ "Test.cmajor", createSource ({}) }});

            // (generateCode() refuses to run on the message thread)
            auto generated = std::async (std::launch::async, [&] { return codeGenPatch.generateCode (params, "graph", "{}"); }).get();
            CHOC_EXPECT_EQ (generated.generatedCode, firstGraph);
        }

        // a rebuild with an unchanged graph has the same version
        CHOC_EXPECT_TRUE (load (patch, createSource ({})));
        CHOC_EXPECT_EQ (patch.getGraphStructureVersion(), firstVersion);

        // a change to the graph's structure gives a new version
        CHOC_EXPECT_TRUE (load (patch, createSource ("node second = Gain;")));
        CHOC_EXPECT_TRUE (hasNode (patch.getGraphStructure(), "second"));
        CHOC_EXPECT_NE (patch.getGraphStructureVersion(), firstVersion);

        // a failed build has none
        CHOC_EXPECT_FALSE (load (patch, createSource ("this won't compile")));
        CHOC_EXPECT_TRUE (patch.getGraphStructure().empty());
        CHOC_EXPECT_TRUE (patch.getGraphStructureVersion().empty());

        CHOC_EXPECT_TRUE (load (patch, createSource ({})));
        CHOC_EXPECT_EQ (patch.getGraphStructureVersion(), firstVersion);
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

        Patch::Status lastStatus;

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

        Patch::Status lastStatus;

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

        uint32_t numValueChanges = 0;
        float lastValueSeen = -1.0f;

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

        uint32_t numStarts = 0, numEnds = 0;

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
/// Copies everything that arrives at its timeline inputs to output channels, along with a
/// count of each kind of event, so that the tests can see what a PatchPlayer sent it.
static constexpr const char* timelineRecorderPatchSource = R"(
    processor Test [[ main ]]
    {
        input event std::timeline::Tempo tempoIn;
        input event std::timeline::TimeSignature timeSigIn;
        input event std::timeline::TransportState transportIn;
        input event std::timeline::Position positionIn;

        output stream float32 bpmOut, tempoCountOut, timeSigOut, timeSigCountOut,
                              transportFlagsOut, transportCountOut, frameOut, quarterNoteOut, barStartOut;

        float32 bpm, timeSig, flags, frame, quarterNote, barStart;
        float32 tempoCount, timeSigCount, transportCount;

        event tempoIn (std::timeline::Tempo t)              { bpm = t.bpm; tempoCount += 1.0f; }
        event timeSigIn (std::timeline::TimeSignature t)    { timeSig = float32 (t.numerator) * 100.0f + float32 (t.denominator); timeSigCount += 1.0f; }
        event transportIn (std::timeline::TransportState t) { flags = float32 (t.flags); transportCount += 1.0f; }

        event positionIn (std::timeline::Position p)
        {
            frame = float32 (p.frameIndex);
            quarterNote = float32 (p.quarterNote);
            barStart = float32 (p.barStartQuarterNote);
        }

        void main()
        {
            loop
            {
                bpmOut <- bpm;                  tempoCountOut <- tempoCount;
                timeSigOut <- timeSig;          timeSigCountOut <- timeSigCount;
                transportFlagsOut <- flags;     transportCountOut <- transportCount;
                frameOut <- frame;              quarterNoteOut <- quarterNote;      barStartOut <- barStart;
                advance();
            }
        }
    }
)";

/// Drives a PatchPlayer's audio callback directly (as an audio device would), with the
/// timeline recorder patch loaded.
struct TimelineTestPlayer
{
    enum Channel { bpm, tempoCount, timeSig, timeSigCount, transportFlags, transportCount, frame, quarterNote, barStart, numChannels };

    static constexpr double sampleRate = 44100.0;   // (the PatchPlayer's own default, so it never rebuilds)
    static constexpr uint32_t blockSize = 256;

    bool load()
    {
        callback.sampleRateChanged (sampleRate);

        Patch::PlaybackParams params;
        params.blockSize = blockSize;
        params.sampleRate = sampleRate;
        params.numInputChannels = 0;
        params.numOutputChannels = numChannels;
        player.patch.setPlaybackParams (params);

        return player.patch.loadPatch ({ createManifestWithInMemoryFiles (createBasicManifest(), {{ "Test.cmajor", timelineRecorderPatchSource }}), {} }, true)
                && player.patch.isPlayable();
    }

    void renderBlocks (uint32_t numBlocks = 1)
    {
        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDI = [] (auto&&...) {};

        for (uint32_t i = 0; i < numBlocks; ++i)
        {
            std::array<float*, numChannels> channels {};

            for (size_t c = 0; c < numChannels; ++c)
                channels[c] = output[c].data();

            auto inputs = choc::buffer::createChannelArrayView (static_cast<const float* const*> (nullptr), 0u, blockSize);
            auto outputs = choc::buffer::createChannelArrayView (channels.data(), static_cast<uint32_t> (numChannels), blockSize);

            callback.startBlock();
            callback.processSubBlock ({ inputs, outputs, {}, noMIDI }, true);
            callback.endBlock();
        }
    }

    /// The value that the patch was outputting at the end of the last block.
    double get (Channel c) const    { return output[c][blockSize - 1]; }

    PatchPlayer player { choc::value::Value(), BuildSettings(), false };
    choc::audio::io::AudioMIDICallback& callback = player;
    std::array<std::array<float, blockSize>, numChannels> output {};
};

static void runPatchPlayerTransportTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (PatchPlayer);

    constexpr auto block = static_cast<double> (TimelineTestPlayer::blockSize);
    constexpr auto oneSecond = static_cast<int64_t> (TimelineTestPlayer::sampleRate);

    {
        CHOC_TEST (TransportDefaults)

        // a player that's never told about a transport behaves as it always has: the
        // position counts up, and nothing else is sent
        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.renderBlocks (3);
        CHOC_EXPECT_NEAR (t.get (t.frame), 2 * block, 0.001);
        CHOC_EXPECT_NEAR (t.get (t.quarterNote), 0.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 0.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSigCount), 0.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportCount), 0.0, 0.0001);

        auto info = t.player.getTransportInfo();
        CHOC_EXPECT_TRUE (info.playing);
        CHOC_EXPECT_EQ (info.frame, static_cast<int64_t> (3 * block));
        CHOC_EXPECT_EQ (info.tempo, 0.0f);
        CHOC_EXPECT_EQ (info.sampleRate, TimelineTestPlayer::sampleRate);
    }

    {
        CHOC_TEST (FrameAdvancesOnlyWhilePlaying)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTempo (120.0f);
        t.player.setTimeSig (4, 4);
        t.player.setTransportState (true, false);

        t.renderBlocks (4);
        CHOC_EXPECT_NEAR (t.get (t.frame), 3 * block, 0.001);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 1.0, 0.0001);
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, static_cast<int64_t> (4 * block));

        t.player.setTransportPlaying (false);
        t.renderBlocks (3);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 0.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.frame), 4 * block, 0.001);
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, static_cast<int64_t> (4 * block));
        CHOC_EXPECT_FALSE (t.player.getTransportInfo().playing);

        // ...while the total keeps counting
        CHOC_EXPECT_EQ (t.player.totalFramesRendered, static_cast<uint64_t> (7 * block));

        t.player.setTransportPlaying (true);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.frame), 5 * block, 0.001);
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, static_cast<int64_t> (6 * block));
    }

    {
        CHOC_TEST (SetPosition)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTempo (120.0f);
        t.player.setTimeSig (4, 4);
        t.player.setTransportState (false, false);
        t.renderBlocks (2);

        // one second at 120 bpm is 2 quarter-notes, in the first 4/4 bar
        t.player.setPosition (oneSecond);
        auto info = t.player.getTransportInfo();     // (reported before the audio thread has seen it)
        CHOC_EXPECT_EQ (info.frame, oneSecond);
        CHOC_EXPECT_NEAR (info.quarterNote, 2.0, 0.0001);
        CHOC_EXPECT_NEAR (info.barStartQuarterNote, 0.0, 0.0001);

        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.frame), static_cast<double> (oneSecond), 0.001);
        CHOC_EXPECT_NEAR (t.get (t.quarterNote), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.barStart), 0.0, 0.0001);
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, oneSecond);

        // 3 seconds is 6 quarter-notes, in the bar that starts at 4
        t.player.setPosition (3 * oneSecond);
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.quarterNote), 6.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.barStart), 4.0, 0.0001);

        // playing carries on from the new position
        t.player.setTransportPlaying (true);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.frame), static_cast<double> (3 * oneSecond) + block, 0.5);

        t.player.rewind();
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, static_cast<int64_t> (0));
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.frame), 0.0, 0.001);

        t.player.setPosition (-100);
        CHOC_EXPECT_EQ (t.player.getTransportInfo().frame, static_cast<int64_t> (0));
    }

    {
        CHOC_TEST (RecordingDoesNotClearPlaying)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTransportState (true, true);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 3.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.frame), block, 0.001);

        auto info = t.player.getTransportInfo();
        CHOC_EXPECT_TRUE (info.playing);
        CHOC_EXPECT_TRUE (info.recording);

        // ...and recording doesn't imply playing
        t.player.setTransportState (false, true);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.frame), 2 * block, 0.001);
        CHOC_EXPECT_FALSE (t.player.getTransportInfo().playing);
        CHOC_EXPECT_TRUE (t.player.getTransportInfo().recording);

        t.player.setTransportRecording (false);
        t.player.setTransportPlaying (true);
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 1.0, 0.0001);
    }

    {
        CHOC_TEST (LoopingIsReported)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTransportState (true, false, true);
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 5.0, 0.0001);
        CHOC_EXPECT_TRUE (t.player.getTransportInfo().looping);

        // the two-flag version leaves the looping flag alone
        t.player.setTransportState (true, true);
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 7.0, 0.0001);

        t.player.setTransportLooping (false);
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 3.0, 0.0001);
        CHOC_EXPECT_FALSE (t.player.getTransportInfo().looping);
    }

    {
        CHOC_TEST (TempoAndTimeSigReachThePatch)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTempo (120.0f);
        t.player.setTimeSig (4, 4);
        t.player.setTransportState (false, false);
        t.renderBlocks (3);
        CHOC_EXPECT_NEAR (t.get (t.bpm), 120.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSig), 404.0, 0.0001);

        // each is only sent when it changes
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSigCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportCount), 1.0, 0.0001);

        t.player.setTempo (90.0f);
        t.player.setTimeSig (7, 8);
        t.player.setPosition (oneSecond * 4);   // 6 quarter-notes at 90 bpm; 7/8 bars are 3.5 long
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.bpm), 90.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSig), 708.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSigCount), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.quarterNote), 6.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.barStart), 3.5, 0.0001);

        auto info = t.player.getTransportInfo();
        CHOC_EXPECT_EQ (info.tempo, 90.0f);
        CHOC_EXPECT_EQ (info.numerator, 7u);
        CHOC_EXPECT_EQ (info.denominator, 8u);
        CHOC_EXPECT_NEAR (info.quarterNote, 6.0, 0.0001);
        CHOC_EXPECT_NEAR (info.barStartQuarterNote, 3.5, 0.0001);
    }

    {
        CHOC_TEST (ResetAndSampleRateChangeResendTheInfo)

        TimelineTestPlayer t;

        if (! t.load())
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        t.player.setTempo (100.0f);
        t.player.setTimeSig (3, 4);
        t.player.setTransportState (true, false);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 1.0, 0.0001);

        // the device reporting its rate (again) sends everything again
        t.callback.sampleRateChanged (TimelineTestPlayer::sampleRate);
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSigCount), 2.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportCount), 2.0, 0.0001);

        // a reset patch forgets everything, so it's all sent again
        RecordingPatchView view (t.player.patch);
        CHOC_EXPECT_TRUE (t.player.handleClientMessage (view, choc::json::parse (R"({ "type": "req_reset" })")));
        t.renderBlocks (2);
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSigCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.bpm), 100.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSig), 304.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 1.0, 0.0001);

        // ...as does a rebuild, which carries on from the same position
        auto frameBeforeRebuild = t.player.getTransportInfo().frame;
        t.player.patch.rebuild (true);
        CHOC_EXPECT_TRUE (t.player.patch.isPlayable());
        t.renderBlocks();
        CHOC_EXPECT_NEAR (t.get (t.tempoCount), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.bpm), 100.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.timeSig), 304.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.transportFlags), 1.0, 0.0001);
        CHOC_EXPECT_NEAR (t.get (t.frame), static_cast<double> (frameBeforeRebuild), 0.5);
    }
}

//==============================================================================
using audio_utils::SignalGeneratorSource;

/// Renders a generator's output in blocks, returning its first channel, and checking
/// that every other channel is the same. The reads happen under the allocation tracker
/// that the PatchPlayer uses on its audio thread (which only checks in debug builds).
static std::vector<float> renderGenerator (SignalGeneratorSource& source, uint32_t numFrames,
                                           uint32_t numChannels = 1, bool* channelsMatch = nullptr,
                                           bool* allocated = nullptr)
{
    constexpr uint32_t blockSize = 512;
    choc::buffer::InterleavedBuffer<float> block (numChannels, blockSize);
    std::vector<float> result;
    result.reserve (numFrames);

    if (channelsMatch != nullptr)
        *channelsMatch = true;

    for (uint32_t start = 0; start < numFrames; start += blockSize)
    {
        auto view = block.getView().getStart (std::min (blockSize, numFrames - start));

        try
        {
            cmaj::ScopedAllocationTracker allocationTracker;
            source.read (view);
        }
        catch (...)
        {
            if (allocated != nullptr)
                *allocated = true;
        }

        for (uint32_t frame = 0; frame < view.getNumFrames(); ++frame)
        {
            result.push_back (view.getSample (0, frame));

            for (uint32_t channel = 1; channel < numChannels; ++channel)
                if (channelsMatch != nullptr && view.getSample (channel, frame) != view.getSample (0, frame))
                    *channelsMatch = false;
        }
    }

    return result;
}

static std::shared_ptr<SignalGeneratorSource> createGenerator (const char* settingsJSON, double sampleRate)
{
    SignalGeneratorSource::Settings settings;
    settings.applyChanges (choc::json::parse (settingsJSON));
    auto source = std::make_shared<SignalGeneratorSource> (settings);
    source->prepare (sampleRate);
    return source;
}

static double getRMS (const std::vector<float>& samples, size_t start, size_t end)
{
    double sum = 0;

    for (auto i = start; i < end; ++i)
        sum += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);

    return std::sqrt (sum / static_cast<double> (end - start));
}

/// The biggest difference between neighbouring samples in [start, end)
static double getMaxStep (const std::vector<float>& samples, size_t start, size_t end)
{
    double maxStep = 0;

    for (auto i = std::max (start, size_t (1)); i < end; ++i)
        maxStep = std::max (maxStep, std::abs (static_cast<double> (samples[i]) - static_cast<double> (samples[i - 1])));

    return maxStep;
}

/// Estimates the frequency of a sine from its rising zero crossings in [start, end)
static double getZeroCrossingFrequency (const std::vector<float>& samples, size_t start, size_t end, double sampleRate)
{
    uint32_t numCrossings = 0;

    for (auto i = std::max (start, size_t (1)); i < end; ++i)
        if (samples[i - 1] < 0 && samples[i] >= 0)
            ++numCrossings;

    return numCrossings * sampleRate / static_cast<double> (end - start);
}

static void runSignalGeneratorTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (SignalGenerator);

    constexpr double rate = 48000.0;
    constexpr auto oneSecond = static_cast<uint32_t> (rate);
    auto at = [&] (double seconds) { return static_cast<size_t> (seconds * rate); };

    {
        CHOC_TEST (Settings)

        SignalGeneratorSource source;
        CHOC_EXPECT_EQ (source.getName(), "generator");

        auto defaults = source.getSettings().toValue();
        CHOC_EXPECT_EQ (choc::json::toString (defaults),
                        choc::json::toString (choc::json::parse (R"({ "type": "sine", "level": -12.0, "frequency": 440.0, "startFrequency": 20.0, "endFrequency": 20000.0, "sweepSeconds": 5.0, "impulseRate": 2.0 })")));

        // partial changes are merged
        source.applyChanges (choc::json::parse (R"({ "type": "sweep", "endFrequency": 5000 })"));
        auto s = source.getSettings();
        CHOC_EXPECT_TRUE (s.type == SignalGeneratorSource::SignalType::sweep);
        CHOC_EXPECT_EQ (s.endFrequency, 5000.0);
        CHOC_EXPECT_EQ (s.frequency, 440.0);
        CHOC_EXPECT_EQ (s.level, -12.0);

        // out-of-range values are clamped, and bad ones ignored
        source.applyChanges (choc::json::parse (R"({ "type": "nonsense", "level": 6, "frequency": -5, "startFrequency": 1e7,
                                                     "sweepSeconds": 0, "impulseRate": 100000, "endFrequency": "loud" })"));
        s = source.getSettings();
        CHOC_EXPECT_TRUE (s.type == SignalGeneratorSource::SignalType::sweep);
        CHOC_EXPECT_EQ (s.level, 0.0);
        CHOC_EXPECT_EQ (s.frequency, SignalGeneratorSource::Settings::minFrequency);
        CHOC_EXPECT_EQ (s.startFrequency, SignalGeneratorSource::Settings::maxFrequency);
        CHOC_EXPECT_EQ (s.endFrequency, 5000.0);
        CHOC_EXPECT_EQ (s.sweepSeconds, SignalGeneratorSource::Settings::minSweepSeconds);
        CHOC_EXPECT_EQ (s.impulseRate, SignalGeneratorSource::Settings::maxImpulseRate);

        source.applyChanges (choc::json::parse (R"({ "level": -200 })"));
        CHOC_EXPECT_EQ (source.getSettings().level, SignalGeneratorSource::Settings::minLevel);

        // every type name round-trips
        for (auto name : { "sine", "sweep", "white", "pink", "impulse", "silence" })
        {
            source.applyChanges (choc::json::create ("type", std::string (name)));
            CHOC_EXPECT_EQ (source.getSettings().toValue()["type"].toString(), name);
        }
    }

    {
        CHOC_TEST (CreateOrUpdate)

        auto first = SignalGeneratorSource::createOrUpdate ({}, choc::json::parse (R"({ "frequency": 1000 })"));
        CHOC_EXPECT_EQ (first->getSettings().frequency, 1000.0);

        // an existing generator is updated in place, keeping its other settings
        auto second = SignalGeneratorSource::createOrUpdate (first, choc::json::parse (R"({ "level": -3 })"));
        CHOC_EXPECT_TRUE (second == first);
        CHOC_EXPECT_EQ (second->getSettings().frequency, 1000.0);
        CHOC_EXPECT_EQ (second->getSettings().level, -3.0);

        // any other source is replaced, with the changes merged into the base settings
        SignalGeneratorSource::Settings base;
        base.type = SignalGeneratorSource::SignalType::pink;
        base.level = -30.0;

        Patch::CustomAudioSourcePtr mute = std::make_shared<audio_utils::MuteAudioSource>();
        auto third = SignalGeneratorSource::createOrUpdate (mute, choc::json::parse (R"({ "level": -6 })"), base);
        CHOC_EXPECT_TRUE (third != nullptr && third.get() != static_cast<void*> (mute.get()));
        CHOC_EXPECT_TRUE (third->getSettings().type == SignalGeneratorSource::SignalType::pink);
        CHOC_EXPECT_EQ (third->getSettings().level, -6.0);
    }

    {
        CHOC_TEST (SineFrequencyAndLevel)

        auto source = createGenerator (R"({ "type": "sine", "frequency": 1000, "level": -6 })", rate);
        bool channelsMatch = false, allocated = false;
        auto out = renderGenerator (*source, oneSecond, 2, std::addressof (channelsMatch), std::addressof (allocated));

        CHOC_EXPECT_TRUE (channelsMatch);
        CHOC_EXPECT_FALSE (allocated);

        // (it fades in when it starts, so skip the beginning)
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (out, at (0.2), at (1.0), rate), 1000.0, 2.0);
        CHOC_EXPECT_NEAR (getRMS (out, at (0.2), at (1.0)), std::pow (10.0, -6.0 / 20.0) / std::sqrt (2.0), 0.002);
        CHOC_EXPECT_TRUE (std::abs (out.front()) < 0.01f);

        // the double version renders the same signal
        auto doubleSource = createGenerator (R"({ "type": "sine", "frequency": 1000, "level": -6 })", rate);
        choc::buffer::InterleavedBuffer<double> doubleBlock (1, 512);
        renderGenerator (*doubleSource, oneSecond - 512);
        doubleSource->read (doubleBlock.getView());
        CHOC_EXPECT_NEAR (doubleBlock.getSample (0, 100), static_cast<double> (out[oneSecond - 512 + 100]), 0.0001);

        // a frequency change is smoothed towards the new frequency
        source->applyChanges (choc::json::parse (R"({ "frequency": 250 })"));
        out = renderGenerator (*source, oneSecond);
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (out, at (0.2), at (1.0), rate), 250.0, 2.0);
        CHOC_EXPECT_TRUE (getMaxStep (out, 0, at (0.2)) < 0.07);

        // a frequency above Nyquist is held just below it (rather than aliasing: 30kHz would sound at 18kHz)
        source->applyChanges (choc::json::parse (R"({ "frequency": 30000 })"));
        out = renderGenerator (*source, oneSecond);
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (out, at (0.5), at (1.0), rate), rate * 0.49, 50.0);
    }

    {
        CHOC_TEST (LevelChangesAreSmoothed)

        auto source = createGenerator (R"({ "type": "sine", "frequency": 50, "level": -40 })", rate);
        renderGenerator (*source, at (0.3));

        // jumping from -40 to 0 dB would be a step of up to ~1 without smoothing
        source->applyChanges (choc::json::parse (R"({ "level": 0 })"));
        auto out = renderGenerator (*source, oneSecond);

        // a 50Hz sine at full level moves by up to 0.0065 per sample
        CHOC_EXPECT_TRUE (getMaxStep (out, 0, out.size()) < 0.01);
        CHOC_EXPECT_TRUE (std::abs (out[0]) < 0.03f);
        CHOC_EXPECT_NEAR (getRMS (out, at (0.5), at (1.0)), 1.0 / std::sqrt (2.0), 0.002);
    }

    {
        CHOC_TEST (TypeChangesCrossfade)

        auto source = createGenerator (R"({ "type": "sine", "frequency": 50, "level": 0 })", rate);
        auto before = renderGenerator (*source, at (0.505));   // (stops near the sine's peak)
        CHOC_EXPECT_TRUE (std::abs (before.back()) > 0.9f);

        source->applyChanges (choc::json::parse (R"({ "type": "silence" })"));
        auto out = renderGenerator (*source, at (0.1));

        CHOC_EXPECT_TRUE (std::abs (static_cast<double> (out[0]) - static_cast<double> (before.back())) < 0.01);
        CHOC_EXPECT_TRUE (getMaxStep (out, 0, out.size()) < 0.01);

        // once the crossfade has finished, silence is exactly zero
        auto fadeEnd = at (SignalGeneratorSource::crossfadeSeconds) + 1;
        CHOC_EXPECT_TRUE (std::all_of (out.begin() + static_cast<std::ptrdiff_t> (fadeEnd), out.end(), [] (float s) { return s == 0.0f; }));

        // noise fades in rather than starting abruptly
        source->applyChanges (choc::json::parse (R"({ "type": "sine" })"));
        before = renderGenerator (*source, at (0.505));
        source->applyChanges (choc::json::parse (R"({ "type": "white" })"));
        out = renderGenerator (*source, at (0.1));
        CHOC_EXPECT_TRUE (std::abs (static_cast<double> (out[0]) - static_cast<double> (before.back())) < 0.01);
        CHOC_EXPECT_TRUE (getMaxStep (out, 0, 16) < 0.05);
        CHOC_EXPECT_TRUE (getMaxStep (out, at (0.05), at (0.1)) > 0.5);
    }

    {
        CHOC_TEST (Sweep)

        auto source = createGenerator (R"({ "type": "sweep", "startFrequency": 100, "endFrequency": 10000, "sweepSeconds": 2, "level": 0 })", rate);
        auto out = renderGenerator (*source, 3 * oneSecond);

        // log sweep: f(t) = 100 * 100^(t / 2)
        auto expectedAt = [] (double t) { return 100.0 * std::pow (100.0, t / 2.0); };
        auto early = getZeroCrossingFrequency (out, at (0.1), at (0.3), rate);
        auto middle = getZeroCrossingFrequency (out, at (0.95), at (1.05), rate);
        auto late = getZeroCrossingFrequency (out, at (1.85), at (1.95), rate);
        auto restarted = getZeroCrossingFrequency (out, at (2.1), at (2.3), rate);

        CHOC_EXPECT_NEAR (early, expectedAt (0.2), 15.0);
        CHOC_EXPECT_NEAR (middle, expectedAt (1.0), 50.0);
        CHOC_EXPECT_NEAR (late, expectedAt (1.9), 300.0);
        CHOC_EXPECT_NEAR (restarted, expectedAt (0.2), 15.0);

        // the phase carries on across the restart, so no step is bigger than a 10kHz sine's
        CHOC_EXPECT_TRUE (getMaxStep (out, at (1.99), at (2.01)) < 2.0 * std::sin (3.14159265 * 10000.0 / rate) + 0.01);
        CHOC_EXPECT_TRUE (getMaxStep (out, at (2.0) + 5, at (2.0) + 200) < 0.05);
    }

    {
        CHOC_TEST (WhiteAndPinkNoise)

        auto white = renderGenerator (*createGenerator (R"({ "type": "white", "level": 0 })", rate), 2 * oneSecond);
        auto pink = renderGenerator (*createGenerator (R"({ "type": "pink", "level": 0 })", rate), 2 * oneSecond);

        auto whiteRMS = getRMS (white, at (0.2), at (2.0));
        auto pinkRMS = getRMS (pink, at (0.2), at (2.0));

        // uniform noise in [-1, 1] has an RMS of 1 / sqrt (3), and pink is normalised to match
        CHOC_EXPECT_NEAR (whiteRMS, 1.0 / std::sqrt (3.0), 0.01);
        CHOC_EXPECT_NEAR (20.0 * std::log10 (pinkRMS / whiteRMS), 0.0, 1.0);

        auto countDistinct = [] (const std::vector<float>& v) { return std::set<float> (v.begin(), v.end()).size(); };
        CHOC_EXPECT_TRUE (countDistinct (white) > 10000);
        CHOC_EXPECT_TRUE (countDistinct (pink) > 10000);

        // pink has more energy at low frequencies than high: compare a crude low-pass (the mean
        // of 16 samples) with a crude high-pass (the difference between neighbours)
        auto getLowToHighRatio = [&] (const std::vector<float>& v)
        {
            double low = 0, high = 0;

            for (auto i = at (0.2); i + 16 < v.size(); ++i)
            {
                double mean = 0;

                for (size_t j = 0; j < 16; ++j)
                    mean += static_cast<double> (v[i + j]) / 16.0;

                auto diff = static_cast<double> (v[i + 1]) - static_cast<double> (v[i]);
                low += mean * mean;
                high += diff * diff;
            }

            return low / high;
        };

        auto whiteRatio = getLowToHighRatio (white);
        auto pinkRatio = getLowToHighRatio (pink);
        CHOC_EXPECT_TRUE (pinkRatio > 1.0);
        CHOC_EXPECT_TRUE (whiteRatio < 0.1);
        CHOC_EXPECT_TRUE (pinkRatio > 20.0 * whiteRatio);
    }

    {
        CHOC_TEST (ImpulseTrain)

        auto source = createGenerator (R"({ "type": "impulse", "impulseRate": 10, "level": 0 })", rate);
        auto out = renderGenerator (*source, 2 * oneSecond);

        std::vector<size_t> impulses;

        for (size_t i = 0; i < out.size(); ++i)
            if (out[i] != 0.0f)
                impulses.push_back (i);

        CHOC_EXPECT_EQ (impulses.size(), size_t (20));
        CHOC_EXPECT_TRUE (! impulses.empty() && impulses.front() == 0);

        for (size_t i = 1; i < impulses.size(); ++i)
            CHOC_EXPECT_NEAR (static_cast<double> (impulses[i] - impulses[i - 1]), rate / 10.0, 1.0);

        CHOC_EXPECT_NEAR (out[impulses.back()], 1.0f, 0.001f);

        // a rate that doesn't divide the sample rate still averages out correctly
        source->applyChanges (choc::json::parse (R"({ "impulseRate": 7 })"));
        out = renderGenerator (*source, 2 * oneSecond);
        auto numImpulses = std::count_if (out.begin(), out.end(), [] (float s) { return s != 0.0f; });
        CHOC_EXPECT_TRUE (numImpulses == 14 || numImpulses == 15);
    }

    {
        CHOC_TEST (Silence)

        bool allocated = false;
        auto out = renderGenerator (*createGenerator (R"({ "type": "silence", "level": 0 })", rate), oneSecond, 2, nullptr, std::addressof (allocated));
        CHOC_EXPECT_TRUE (std::all_of (out.begin(), out.end(), [] (float s) { return s == 0.0f; }));
        CHOC_EXPECT_FALSE (allocated);
    }

    {
        CHOC_TEST (ChangingSettingsDoesNotAllocateOnTheAudioThread)

        auto source = createGenerator (R"({ "type": "sine" })", rate);
        bool allocated = false;

        for (auto change : { R"({ "type": "sweep" })", R"({ "type": "white", "level": -3 })", R"({ "type": "pink" })",
                             R"({ "type": "impulse", "impulseRate": 50 })", R"({ "type": "silence" })", R"({ "type": "sine", "frequency": 80 })" })
        {
            source->applyChanges (choc::json::parse (change));
            renderGenerator (*source, at (0.05), 2, nullptr, std::addressof (allocated));
        }

        // ...including a sample-rate change
        source->prepare (96000.0);
        renderGenerator (*source, at (0.05), 2, nullptr, std::addressof (allocated));
        CHOC_EXPECT_FALSE (allocated);
    }

    {
        CHOC_TEST (SampleRateChange)

        auto source = createGenerator (R"({ "type": "sine", "frequency": 1000, "level": 0 })", 44100.0);
        renderGenerator (*source, 44100);
        source->prepare (96000.0);
        auto out = renderGenerator (*source, 96000);
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (out, 0, out.size(), 96000.0), 1000.0, 2.0);
    }

    {
        CHOC_TEST (GeneratorAsPatchInputSurvivesRebuild)

        Patch patch;
        initTestPatch (patch);

        auto inputID = EndpointID::create (std::string_view ("in"));
        auto source = createGenerator (R"({ "type": "sine", "frequency": 1000, "level": 0 })", rate);
        patch.setCustomAudioSourceForInput (inputID, source);

        if (! loadTestPatch (patch, createBasicManifest(), passThroughPatchSource, 512, rate, 1, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        std::vector<float> inputBacking (512, 0.0f), outputBacking (512, 0.0f), output;
        std::array<const float*, 1> inputBuffers { { inputBacking.data() } };
        std::array<float*, 1> outputBuffers { { outputBacking.data() } };
        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDI = [] (auto&&...) {};

        auto render = [&] (uint32_t numBlocks)
        {
            output.clear();

            for (uint32_t i = 0; i < numBlocks; ++i)
            {
                auto inputs = choc::buffer::createChannelArrayView (inputBuffers.data(), 1u, 512u);
                auto outputs = choc::buffer::createChannelArrayView (outputBuffers.data(), 1u, 512u);
                patch.process ({ inputs, outputs, {}, noMIDI }, true);
                output.insert (output.end(), outputBacking.begin(), outputBacking.end());
            }
        };

        render (100);
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (output, output.size() / 2, output.size(), rate), 1000.0, 5.0);
        CHOC_EXPECT_NEAR (getRMS (output, output.size() / 2, output.size()), 1.0 / std::sqrt (2.0), 0.01);

        // an in-place update and a rebuild keep the same source and its settings
        source->applyChanges (choc::json::parse (R"({ "frequency": 500 })"));
        patch.rebuild (true);
        CHOC_EXPECT_TRUE (patch.isPlayable());
        CHOC_EXPECT_TRUE (patch.getCustomAudioSourceForInput (inputID) == source);
        CHOC_EXPECT_EQ (source->getSettings().frequency, 500.0);

        render (100);
        CHOC_EXPECT_NEAR (getZeroCrossingFrequency (output, output.size() / 2, output.size(), rate), 500.0, 5.0);
        CHOC_EXPECT_NEAR (getRMS (output, output.size() / 2, output.size()), 1.0 / std::sqrt (2.0), 0.01);
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
        CHOC_TEST (EventListenersChangeWhileEventsAreDelivered)

        // Output events reach the views on the message thread, while a client's own thread adds
        // and removes its event listeners (as the server's does): the message thread must never
        // see the list of listeners half-changed (this used to crash, reading a null monitor)
        const auto eventSource = R"(
            processor Test [[ main ]]
            {
                output stream float out;
                output event float32 eventOut;

                void main()
                {
                    loop
                    {
                        eventOut <- 1.0f;
                        advance();
                    }
                }
            }
        )";

        Patch patch;

        if (! initAndLoadTestPatch (patch, eventSource, 16, 48000, 0, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);
        auto eventOut = EndpointID::create (std::string_view ("eventOut"));
        std::atomic<bool> finished { false };

        std::thread audioThread ([&]
        {
            std::array<float, 16> backing {};
            std::array<float*, 1> buffers { { backing.data() } };

            while (! finished)
            {
                patch.process (buffers.data(), 16, [] (auto&&...) {});
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
        });

        std::thread clientThread ([&]
        {
            for (uint32_t i = 0; ! finished; ++i)
            {
                auto replyType = "events" + std::to_string (i % 8);

                if (! patch.stopEndpointData (view, eventOut, replyType))
                    patch.startEndpointData (view, eventOut, replyType, 0, false);
            }
        });

        runMessageLoopUntil ([] { return false; }, 1500);
        finished = true;
        audioThread.join();
        clientThread.join();
        runMessageLoopUntil ([] { return false; }, 200);

        size_t numEvents = 0;

        for (int i = 0; i < 8; ++i)
            numEvents += view.countMessagesOfType ("events" + std::to_string (i));

        CHOC_EXPECT_TRUE (numEvents > 0);
    }

    {
        CHOC_TEST (AudioDataMessagesCarryTheirFrame)

        // (outputs the index of each frame it renders, so a chunk's data says where it came from)
        const auto rampSource = R"(
            processor Test [[ main ]]
            {
                output stream float32 out;
                float32 n;
                void main() { loop { out <- n; n += 1.0f; advance(); } }
            }
        )";

        Patch patch;

        if (! initAndLoadTestPatch (patch, rampSource, 512, 48000, 0, 1))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);
        auto out = EndpointID::create (std::string_view ("out"));
        uint64_t framesRendered = 0;

        // renders in blocks whose sizes don't divide any of the granularities, including one
        // that's longer than the performer's maximum block, so that it gets split up
        const auto render = [&] (uint32_t totalFrames)
        {
            static constexpr std::array<uint32_t, 4> blockSizes { 100, 37, 700, 1 };
            std::vector<float> samples (1024);
            std::array<float*, 1> buffers { { samples.data() } };

            for (size_t i = 0; totalFrames != 0; ++i)
            {
                auto numFrames = std::min (totalFrames, blockSizes[i % blockSizes.size()]);
                patch.process (buffers.data(), numFrames, [] (auto&&...) {});
                totalFrames -= numFrames;
                framesRendered += numFrames;
            }
        };

        const auto getMessages = [&] (std::string_view type)
        {
            std::vector<choc::value::Value> result;

            for (auto& m : view.messages)
                if (m["type"].toString() == type)
                    result.push_back (choc::value::Value (m["message"]));

            return result;
        };

        // checks that each chunk starts where the last one ended, beginning at firstFrame, and
        // that its data is the patch's output for the frames it says (offset by rampStart)
        const auto checkChunks = [&] (std::string_view type, uint32_t granularity, uint64_t firstFrame,
                                      uint64_t endFrame, uint64_t rampStart, bool fullData)
        {
            auto messages = getMessages (type);
            CHOC_EXPECT_EQ (messages.size(), static_cast<size_t> ((endFrame - firstFrame) / granularity));
            auto expectedFrame = firstFrame;

            for (auto& m : messages)
            {
                CHOC_EXPECT_TRUE (m["frame"].isFloat64());
                auto frame = static_cast<uint64_t> (m["frame"].getWithDefault<double> (-1.0));
                CHOC_EXPECT_EQ (frame, expectedFrame);

                if (fullData)
                {
                    auto data = m["data"];
                    CHOC_EXPECT_EQ (data.size(), 1u);
                    CHOC_EXPECT_EQ (data[0].size(), granularity);
                    bool dataMatchesFrame = true;

                    for (uint32_t i = 0; i < data[0].size(); ++i)
                        if (static_cast<uint64_t> (data[0][i].getWithDefault<float> (-1.0f)) + rampStart != frame + i)
                            dataMatchesFrame = false;

                    CHOC_EXPECT_TRUE (dataMatchesFrame);
                }
                else
                {
                    CHOC_EXPECT_EQ (static_cast<uint64_t> (m["min"][0].getWithDefault<float> (-1.0f)) + rampStart, frame);
                    CHOC_EXPECT_EQ (static_cast<uint64_t> (m["max"][0].getWithDefault<float> (-1.0f)) + rampStart, frame + granularity - 1);
                }

                expectedFrame = frame + granularity;
            }
        };

        const auto dispatch = [&] (size_t expectedTotal)
        {
            return runMessageLoopUntil ([&] { return view.messages.size() >= expectedTotal; });
        };

        const auto chunksBetween = [] (uint64_t start, uint64_t end, uint32_t granularity)
        {
            return static_cast<size_t> ((end - start) / granularity);
        };

        // the frames count from the start of the patch's rendering, not from when a listener is added
        render (150);
        auto firstListenerStart = framesRendered;
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "wide", 256, true));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "narrow", 100, true));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "levels", 64, false));

        render (1000);
        auto lateListenerStart = framesRendered;
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "late", 64, true));

        render (2000);
        CHOC_EXPECT_TRUE (dispatch (chunksBetween (firstListenerStart, framesRendered, 256)
                                     + chunksBetween (firstListenerStart, framesRendered, 100)
                                     + chunksBetween (firstListenerStart, framesRendered, 64)
                                     + chunksBetween (lateListenerStart, framesRendered, 64)));

        checkChunks ("wide",   256, firstListenerStart, framesRendered, 0, true);
        checkChunks ("narrow", 100, firstListenerStart, framesRendered, 0, true);
        checkChunks ("levels",  64, firstListenerStart, framesRendered, 0, false);
        checkChunks ("late",    64, lateListenerStart,  framesRendered, 0, true);

        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, out, "narrow"));
        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, out, "levels"));
        CHOC_EXPECT_TRUE (patch.stopEndpointData (view, out, "late"));

        // a reset keeps counting (the patch's own output restarts from zero, though)
        view.clearMessages();
        patch.resetToInitialState();
        auto resetStart = framesRendered;
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "wide", 256, true));
        render (600);
        CHOC_EXPECT_TRUE (dispatch (chunksBetween (resetStart, framesRendered, 256)));
        checkChunks ("wide", 256, resetStart, framesRendered, resetStart, true);

        // a rebuild re-creates the listeners, and its frames carry on from the last build's
        view.clearMessages();
        auto rebuildStart = framesRendered;
        CHOC_EXPECT_TRUE (loadTestPatch (patch, createBasicManifest(), rampSource, 512, 48000, 0, 1));
        render (600);
        CHOC_EXPECT_TRUE (dispatch (chunksBetween (rebuildStart, framesRendered, 256)));
        checkChunks ("wide", 256, rebuildStart, framesRendered, rebuildStart, true);
    }

    {
        CHOC_TEST (EndpointMonitorsSurviveRebuilds)

        const auto createSource = [] (bool withEventInput)
        {
            return std::string (R"(
                processor Test [[ main ]]
                {
                    input stream float32 in;
                    output stream float32 out;
                    )") + (withEventInput ? "input event float32 rawEvent;" : "") + R"(

                    void main() { loop { out <- in; advance(); } }
                }
            )";
        };

        Patch patch;
        initTestPatch (patch);

        cmaj::Patch::PlaybackParams params;
        params.blockSize = 64;
        params.sampleRate = 48000;
        params.numInputChannels = 1;
        params.numOutputChannels = 1;
        patch.setPlaybackParams (params);

        const auto load = [&] (const std::string& source)
        {
            return patch.loadPatch ({ createManifestWithInMemoryFiles (createBasicManifest(), {{ "Test.cmajor", source }}), {} }, true);
        };

        if (! load (createSource (true)))
        {
            CHOC_FAIL ("Failed to load patch");
            return;
        }

        RecordingPatchView view (patch);
        auto out = EndpointID::create (std::string_view ("out"));
        auto rawEvent = EndpointID::create (std::string_view ("rawEvent"));

        // processes one granularity-sized block (sending an event first, if there's an input for
        // it), and returns how many "levels", "levels2" and "events" messages arrive
        const auto countMessages = [&] () -> std::array<size_t, 3>
        {
            view.clearMessages();
            patch.sendEventOrValueToPatch (rawEvent, choc::value::createFloat32 (0.5f), -1, 0);

            std::array<float, 64> samples {};
            std::array<float*, 1> buffers { { samples.data() } };
            patch.process (buffers.data(), 64, [] (auto&&...) {});

            runMessageLoopUntil ([] { return false; }, 200);
            return { view.countMessagesOfType ("levels"), view.countMessagesOfType ("levels2"), view.countMessagesOfType ("events") };
        };

        using Counts = std::array<size_t, 3>;
        auto firstBuild = patch.getBuildNumber();

        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "levels", 64, false));
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, rawEvent, "events", 0, false));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 1 }));

        // asking again replaces the monitor rather than adding another one
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "levels", 64, false));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 1 }));

        // a rebuild keeps them
        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_EQ (patch.getBuildNumber(), firstBuild + 1);
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 1 }));

        // so does a failed build in between, and one asked for while it has failed arrives
        // with the next good build
        CHOC_EXPECT_FALSE (load ("this won't compile"));
        CHOC_EXPECT_EQ (patch.getBuildNumber(), firstBuild + 2);
        CHOC_EXPECT_TRUE (patch.startEndpointData (view, out, "levels2", 64, false));
        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 1, 1 }));

        // one that's removed while the build has failed doesn't come back
        CHOC_EXPECT_FALSE (load ("this won't compile"));
        patch.stopEndpointData (view, out, "levels2");
        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 1 }));

        // a build without its endpoint drops it, and it stays dropped when the endpoint returns
        CHOC_EXPECT_TRUE (load (createSource (false)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 0 }));
        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 0 }));

        // a view that's gone doesn't leave any behind
        {
            RecordingPatchView temporaryView (patch);
            CHOC_EXPECT_TRUE (patch.startEndpointData (temporaryView, out, "levels", 64, false));
        }

        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 1, 0, 0 }));

        // unloading removes them all
        patch.unload();
        CHOC_EXPECT_TRUE (load (createSource (true)));
        CHOC_EXPECT_TRUE (countMessages() == (Counts { 0, 0, 0 }));
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
        CHOC_EXPECT_TRUE (cpuInfo.hasObjectMember ("peak"));
        CHOC_EXPECT_TRUE (cpuInfo["peak"].getWithDefault<float> (-1.0f) >= 0.0f);

        // setting the rate to zero should stop the callbacks
        patch.setCPUInfoMonitorChunkSize (0);
        view.clearMessages();
        renderBlocks (32);

        runMessageLoopUntil ([] { return false; }, 200);
        CHOC_EXPECT_EQ (view.countMessagesOfType ("cpu_info"), 0u);
    }

    {
        CHOC_TEST (CPUMonitorPeak)

        // 480 frames at 48kHz gives each block a 10ms budget
        CPUMonitor monitor;
        monitor.reset (48000.0);
        monitor.framesPerCallback = 480;

        std::vector<std::pair<float, float>> reports;
        monitor.handleCPULevelAndPeak = [&] (float level, float peak) { reports.push_back ({ level, peak }); };

        // a block that takes twice its budget: the peak shows the spike, the smoothed level doesn't
        monitor.startProcess();
        std::this_thread::sleep_for (std::chrono::milliseconds (20));
        monitor.endProcess (480);

        CHOC_EXPECT_EQ (reports.size(), size_t (1));

        if (reports.size() == 1)
        {
            CHOC_EXPECT_TRUE (reports[0].second >= 1.9f);
            CHOC_EXPECT_TRUE (reports[0].second >= reports[0].first);
            CHOC_EXPECT_TRUE (reports[0].first < 0.5f);
        }

        // the peak is reset after each report, so a fast block reports a small peak
        // (the level is still decaying from the spike, so it changes enough to be sent)
        monitor.startProcess();
        monitor.endProcess (480);

        CHOC_EXPECT_EQ (reports.size(), size_t (2));

        if (reports.size() == 2)
        {
            CHOC_EXPECT_TRUE (reports[1].second < 0.5f);
            CHOC_EXPECT_TRUE (reports[1].first > 0.0f);
        }

        // with only the older callback set, it still gets the level
        CPUMonitor oldStyle;
        oldStyle.reset (48000.0);
        oldStyle.framesPerCallback = 480;
        float lastLevel = -1.0f;
        oldStyle.handleCPULevel = [&] (float level) { lastLevel = level; };
        oldStyle.startProcess();
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
        oldStyle.endProcess (480);
        CHOC_EXPECT_TRUE (lastLevel > 0.0f);
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

        // NB: Block holds its MIDI-output callback by reference, so the functor it refers to
        // must be kept alive - passing a temporary `{}` here would leave a dangling reference
        const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn noMIDIOutput = [] (auto&&...) {};

        const auto block = choc::audio::AudioMIDIBlockDispatcher::Block { inputs, outputs, {}, noMIDIOutput };
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
    runPatchPlayerTransportTests (progress);
    runSignalGeneratorTests (progress);
    runPatchClientMessageTests (progress);
    runPatchAsyncTests (progress);

    return progress.numFails == 0;
}

} // namespace cmaj::patch_helper_tests
