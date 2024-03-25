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

struct File { std::string_view name, content; };

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
                return std::make_shared<std::istringstream> (source);

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

static constexpr bool hasStaticallyLinkedPerformer()
{
   #ifdef CMAJOR_DLL
    return CMAJOR_DLL == 0;
   #else
    return false;
   #endif
}

static void initTestPatch (Patch& patch)
{
    patch.createEngine      = [] { return Engine::create(); };
    patch.createContextForPatchWorker = [] { return choc::javascript::Context(); };
    patch.stopPlayback      = [] {};
    patch.startPlayback     = [] {};
    patch.patchChanged      = [] {};
    patch.statusChanged     = [] (auto&&...) {};
    patch.handleOutputEvent = [] (auto&&...) {};

    patch.setHostDescription ("Cmajor Test");
}

static bool runUnitTests (choc::test::TestProgress& progress)
{
    CMAJ_ASSERT (hasStaticallyLinkedPerformer());

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

        const auto block = choc::audio::AudioMIDIBlockDispatcher::Block
        {
            inputs,
            outputs,
            choc::span<choc::midi::ShortMessage> {},
            choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn {}
        };
        const auto replaceOutput = true;
        patch.process (block, replaceOutput);

        CHOC_EXPECT_NEAR (outputBackingBuffer[0], 0.5f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[1], 0.375f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[2], 0.25f, 0.0001f);
        CHOC_EXPECT_NEAR (outputBackingBuffer[3], 0.125f, 0.0001f);
    }

    return progress.numFails == 0;
}

} // namespace cmaj::patch_helper_tests
