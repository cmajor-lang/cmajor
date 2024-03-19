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

#include "cmajor/API/cmaj_Engine.h"

namespace cmaj::api_tests
{
    static bool throwsException (std::function<void()> f)
    {
        try
        {
            f();
        }
        catch (cmaj::AbortCompilationException)
        {
            return true;
        }

        return false;
    }

    static std::string source()
    {
        return R"(
            graph test
            {
                input stream float32 in1;
                input value float32 in2;
                input event float32 in3;

                output stream float32 out1;
                output value float32 out2;
                output event float32 out3;

                event in3 (float f)
                {
                    out3 <- f;
                }

                external float multiplier;

                connection
                {
                    in1 * multiplier -> out1;
                    in2 * multiplier -> out2;
                }
            }
        )";
    }

    static void checkInvalidEngine (choc::test::TestProgress& progress)
    {
        CHOC_TEST (checkInvalidEngine);

        auto factory = cmaj::EngineFactoryPtr (cmaj::Library::createEngineFactory (nullptr));
        CHOC_EXPECT_TRUE (factory != nullptr);
        CHOC_EXPECT_TRUE (EnginePtr (factory->createEngine ("invalid^^json")) == nullptr);

        auto engine = cmaj::Engine::create ("unknown");
        CHOC_EXPECT_TRUE (engine.engine == nullptr);
        CHOC_EXPECT_FALSE (engine.isLoaded());
        CHOC_EXPECT_FALSE (engine.isLinked());

        cmaj::Program program;
        cmaj::DiagnosticMessageList messages;

        program.parse (messages, "", source());
        CHOC_EXPECT_TRUE (messages.empty());

        std::vector<std::string> requestedExternals;

        bool result = engine.load (messages, program, [&] (const cmaj::ExternalVariable& e) -> choc::value::Value
                                                      {
                                                          requestedExternals.push_back (e.name);
                                                          return {};
                                                      }, {});

        CHOC_EXPECT_FALSE (result);
        CHOC_EXPECT_FALSE (messages.empty());
        CHOC_EXPECT_TRUE (engine.getInputEndpoints().size() == 0);
        CHOC_EXPECT_TRUE (engine.getOutputEndpoints().size() == 0);
        CHOC_EXPECT_TRUE (requestedExternals.size() == 0);

        engine.setBuildSettings (cmaj::BuildSettings().setFrequency (44100.0)
                                                      .setMaxBlockSize (1024));

        messages.clear();
        CHOC_EXPECT_FALSE (engine.link (messages, {}));

        auto performer = engine.createPerformer();
        CHOC_EXPECT_FALSE (performer);

        CHOC_EXPECT_TRUE (throwsException ([&] { performer.setBlockSize (100); }));

        auto inputBlock = choc::buffer::createInterleavedBuffer (1, 5, [] (choc::buffer::ChannelCount, choc::buffer::FrameCount sample) { return float (sample); });
        auto outputBlock = choc::buffer::InterleavedBuffer<float> (1, 5);

        CHOC_EXPECT_TRUE (throwsException ([&] { performer.setBlockSize (5); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.setInputFrames (1, inputBlock.getView()); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.setInputValue (1, choc::value::createInt32 (1), 10); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.addInputEvent (1, 0, choc::value::createInt32 (1)); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.advance(); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.copyOutputValue (2, nullptr);}));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.copyOutputFrames (2, outputBlock);}));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.iterateOutputEvents (2, [&] (uint32_t, uint32_t, uint32_t, const void*, uint32_t) { return false; } ); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.getStringForHandle (12345); }));
        CHOC_EXPECT_EQ (performer.getXRuns(), 0U);
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.getMaximumBlockSize(); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.getLatency(); }));
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.getEventBufferSize(); }));
    }

    static void checkGraph (choc::test::TestProgress& progress)
    {
        CHOC_TEST (checkGraph);

        auto engine = cmaj::Engine::create ({});

        // Pre load
        CHOC_EXPECT_TRUE (engine.engine != nullptr);
        CHOC_EXPECT_FALSE (engine.isLoaded());
        CHOC_EXPECT_FALSE (engine.isLinked());
        CHOC_EXPECT_TRUE (engine.getInputEndpoints().size() == 0);
        CHOC_EXPECT_TRUE (engine.getOutputEndpoints().size() == 0);

        cmaj::Program program;
        cmaj::DiagnosticMessageList messages;

        program.parse (messages, "", source());
        CHOC_EXPECT_TRUE (messages.empty());

        // Post load, pre link
        std::map<std::string, choc::value::Value> externalValues { {"test::multiplier", choc::value::createFloat32 (2.0f) }};

        std::vector<std::string> requestedExternals;

        bool result = engine.load (messages, program, [&] (const cmaj::ExternalVariable& e) -> choc::value::Value
                                                      {
                                                          requestedExternals.push_back (e.name);
                                                          return externalValues[e.name];
                                                      },
                                                      {});

        CHOC_EXPECT_TRUE (result);
        CHOC_EXPECT_TRUE (messages.empty());
        CHOC_EXPECT_TRUE (engine.isLoaded());
        CHOC_EXPECT_FALSE (engine.isLinked());
        CHOC_EXPECT_TRUE (engine.getInputEndpoints().size() == 3);
        CHOC_EXPECT_TRUE (engine.getOutputEndpoints().size() == 3);
        CHOC_EXPECT_TRUE (requestedExternals.size() == 1);

        auto in1Handle = engine.getEndpointHandle ("in1");
        auto in2Handle = engine.getEndpointHandle ("in2");
        auto in3Handle = engine.getEndpointHandle ("in3");
        auto in4Handle = engine.getEndpointHandle ("in4");

        auto out1Handle = engine.getEndpointHandle ("out1");
        auto out2Handle = engine.getEndpointHandle ("out2");
        auto out3Handle = engine.getEndpointHandle ("out3");
        auto out4Handle = engine.getEndpointHandle ("out4");

        CHOC_EXPECT_TRUE (in1Handle != 0);
        CHOC_EXPECT_TRUE (in2Handle != 0);
        CHOC_EXPECT_TRUE (in3Handle != 0);
        CHOC_EXPECT_FALSE (in4Handle != 0);

        CHOC_EXPECT_TRUE (out1Handle != 0);
        CHOC_EXPECT_TRUE (out2Handle != 0);
        CHOC_EXPECT_TRUE (out3Handle != 0);
        CHOC_EXPECT_FALSE (out4Handle != 0);

        CHOC_EXPECT_FALSE (engine.createPerformer());
        // Frequency needs setting before link will succeed
        CHOC_EXPECT_FALSE (engine.link (messages, {}));
        CHOC_EXPECT_FALSE (engine.createPerformer());

        engine.setBuildSettings (cmaj::BuildSettings().setFrequency (44100.0)
                                                      .setMaxBlockSize (10));

        // Post link
        CHOC_EXPECT_TRUE (engine.link (messages, {}));
        auto performer = engine.createPerformer();
        CHOC_EXPECT_TRUE (performer);
        CHOC_EXPECT_TRUE (performer.getLatency() == 0);
        CHOC_EXPECT_EQ (performer.getStringForHandle (12345), "");

        auto inputBlock = choc::buffer::createInterleavedBuffer (1, 5, [] (choc::buffer::ChannelCount, choc::buffer::FrameCount sample) { return float (sample); });
        auto outputBlock = choc::buffer::InterleavedBuffer<float> (1, 5);

        performer.setBlockSize (5);
        performer.setInputFrames (in1Handle, inputBlock.getView());
        performer.setInputValue (in2Handle, 2.0f, 0);
        performer.advance();
        performer.copyOutputFrames (out1Handle, outputBlock);

        for (uint32_t i = 0; i < 5; i++)
            CHOC_EXPECT_NEAR (float (i) * 2, outputBlock.getSample (0, i), 0.0001);

        auto out2Value = -1.0f;
        performer.copyOutputValue (out2Handle, std::addressof (out2Value));
        CHOC_EXPECT_NEAR (4.0f, out2Value, 0.0001);

        // Exceeds maxBlockSize = 10
        CHOC_EXPECT_TRUE (throwsException ([&] { performer.setBlockSize (100); }));
    }

    static void checkOutputEventWithMultipleTypes (choc::test::TestProgress& progress)
    {
        CHOC_TEST (checkOutputEventWithMultipleTypes)

        auto engine = cmaj::Engine::create ({});

        cmaj::Program program;
        cmaj::DiagnosticMessageList messages;

        const auto source = R"(
            graph G
            {
                input event (std::notes::NoteOn, std::notes::NoteOff) note;
                output event int32 out;

                event note (std::notes::NoteOn v)
                {
                    out <- 1;
                }

                event note (std::notes::NoteOff v)
                {
                    out <- 2;
                }
            }
        )";
        program.parse (messages, "", source);
        CHOC_EXPECT_TRUE (messages.empty());

        std::vector<std::string> requestedExternals;

        bool result = engine.load (messages, program, [&] (const cmaj::ExternalVariable& e) -> choc::value::Value
                                                      {
                                                          requestedExternals.push_back (e.name);
                                                          return {};
                                                      },
                                                      {});

        CHOC_EXPECT_TRUE (result);
        CHOC_EXPECT_TRUE (requestedExternals.size() == 0);
        CHOC_EXPECT_TRUE (messages.empty());

        const auto noteHandle = engine.getEndpointHandle ("note");
        const auto outHandle = engine.getEndpointHandle ("out");

        engine.setBuildSettings (cmaj::BuildSettings().setFrequency (44100.0)
                                                      .setMaxBlockSize (1));

        CHOC_EXPECT_TRUE (engine.link (messages, {}));
        CHOC_EXPECT_TRUE (messages.empty());
        auto performer = engine.createPerformer();
        CHOC_EXPECT_TRUE (performer);

        performer.setBlockSize (1);
        const auto noteOff = choc::json::create ("channel", choc::value::createInt32 (0),
                                                 "pitch", choc::value::createFloat32 (60.0f),
                                                 "velocity", choc::value::createFloat32 (0.0f));
        performer.addInputEvent (noteHandle, 1, noteOff.getView());
        performer.advance();

        int32_t value = -1;
        performer.iterateOutputEvents (outHandle, [&] (auto, uint32_t, uint32_t, const void* data, uint32_t)
        {
            value = *reinterpret_cast<const int32_t*> (data);
            return true;
        });
        CHOC_EXPECT_EQ (value, int32_t {2});
    }

    inline void checkExternalFunctions (choc::test::TestProgress& progress)
    {
        CHOC_TEST (checkExternalFunctions)

        auto engine = cmaj::Engine::create ("llvm");

        cmaj::Program program;
        cmaj::DiagnosticMessageList messages;

        const auto source = R"(
            processor P
            {
                output event int32 out;

                external int32 add1 (int32 a, int32 b);
                external int64 add1 (int64 a, int64 b);
                external float32 add2 (float32 a, float32 b);
                external float64 add2 (float64 a, float64 b);
                external bool testBools (bool a, bool b);
                external int32 sum (int32[] value, int num);

                void main()
                {
                    out <- (add1 (2, 3) == 5 ? 1 : 0)
                        <- (add1 (3_i64, 4_i64) == 7_i64 ? 1 : 0)
                        <- (add2 (4.0f, 5.0f) == 9.0f ? 1 : 0)
                        <- (add2 (5.0, 6.0) == 11.0 ? 1 : 0)
                        <- (testBools (true, false) && ! testBools (false, false) ? 1 : 0)
                        <- (sum (int[4] (1, 2, 3, 4), 4) == 10 ? 1 : 0);
                    advance();
                }
            }
        )";

        program.parse (messages, "", source);
        CHOC_EXPECT_TRUE (messages.empty());

        std::vector<std::string> requestedExternals;

        struct Fns
        {
            static int32_t add1_32 (int32_t a, int32_t b)   { return a + b; }
            static int64_t add1_64 (int64_t a, int64_t b)   { return a + b; }
            static float   add2_32 (float a, float b)       { return a + b; }
            static double  add2_64 (double a, double b)     { return a + b; }
            static bool    testBools (bool a, bool b)       { return a || b; }

            static int32_t sum (int* values, int32_t num)
            {
                int total = 0;

                for (int i = 0; i < num; ++i)
                    total += values[i];

                return total;
            }
        };

        bool result = engine.load (messages, program, {},
            [&] (const char* fnName, choc::span<choc::value::Type> paramTypes) -> void*
            {
                auto name = std::string_view (fnName);
                requestedExternals.push_back (std::string (name));
                CMAJ_ASSERT (paramTypes.size() == 2);

                if (choc::text::contains (name, "add1"))
                    return paramTypes[0].isInt32() ? (void*) Fns::add1_32 : (void*) Fns::add1_64;

                if (choc::text::contains (name, "add2"))
                    return paramTypes[0].isFloat32() ? (void*) Fns::add2_32 : (void*) Fns::add2_64;

                if (choc::text::contains (name, "testBools"))
                    return (void*) Fns::testBools;

                if (choc::text::contains (name, "sum"))
                    return (void*) Fns::sum;

                return {};
            });

        CHOC_EXPECT_TRUE (result);
        CHOC_EXPECT_TRUE (messages.empty());

        const auto outHandle = engine.getEndpointHandle ("out");

        engine.setBuildSettings (cmaj::BuildSettings().setFrequency (44100.0)
                                                      .setMaxBlockSize (1));

        CHOC_EXPECT_TRUE (engine.link (messages, {}));
        CHOC_EXPECT_TRUE (messages.empty());
        auto performer = engine.createPerformer();
        CHOC_EXPECT_TRUE (performer);
        CHOC_EXPECT_TRUE (requestedExternals.size() == 6);

        performer.setBlockSize (1);
        performer.advance();
        std::string output;

        performer.iterateOutputEvents (outHandle, [&] (auto, uint32_t, uint32_t, const void* data, uint32_t)
        {
            output += std::to_string (*reinterpret_cast<const int32_t*> (data));
            return true;
        });

        CHOC_EXPECT_EQ (output, "111111");
    }

    static void runUnitTests (choc::test::TestProgress& progress)
    {
        CHOC_CATEGORY (Performer);

        checkExternalFunctions (progress);
        checkGraph (progress);
        checkOutputEventWithMultipleTypes (progress);
        checkInvalidEngine (progress);
    }
}
