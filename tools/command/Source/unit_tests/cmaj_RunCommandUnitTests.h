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

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"

#include "choc/platform/choc_UnitTest.h"
#include "choc/audio/choc_MIDIFile.h"

#include "../cmaj_command_Run.h"

#include <atomic>
#include <chrono>
#include <sstream>

namespace cmaj::run_command_tests
{

//==============================================================================
/// Runs `cmaj run` with the given tokens (as they'd appear after "cmaj run"), capturing
/// the diagnostics, the report text, and any error that stopped it.
struct TestRun
{
    explicit TestRun (std::vector<std::string> tokens)
    {
        try
        {
            choc::ArgumentList args (std::move (tokens));
            cmaj::run_command::RunOptions options;
            options.parseArguments (args);

            cmaj::BuildSettings buildSettings;
            cmaj::run_command::RunState state (options, choc::value::createObject ({}), buildSettings, diagnostics);
            state.render (output);
            report = state.report;
            succeeded = true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }
    }

    bool errorContains (std::string_view text) const    { return choc::text::contains (error, text); }

    choc::value::ValueView getEvents (std::string_view endpoint) const      { return report["events"][endpoint]; }
    int64_t getEventFrame (std::string_view endpoint, uint32_t index) const { return getEvents (endpoint)[index]["frame"].getWithDefault<int64_t> (-1); }
    double getEventValue (std::string_view endpoint, uint32_t index) const  { return getEvents (endpoint)[index]["value"].getWithDefault<double> (-1.0); }
    double getValue (std::string_view endpoint) const                       { return report["values"][endpoint].getWithDefault<double> (-1.0); }
    int64_t getFramesRendered() const                                       { return report["framesRendered"].getWithDefault<int64_t> (-1); }

    choc::value::Value report;
    std::ostringstream diagnostics, output;
    std::string error;
    bool succeeded = false;
};

/// Fails the test with the run's error message if it didn't succeed, and returns, since
/// there's no report to check.
#define CMAJ_EXPECT_RUN_OK(run)      if (! (run).succeeded) { CHOC_FAIL ("Run failed: " + (run).error); return; }

/// Fails the test unless the run failed with an error containing the given text.
#define CMAJ_EXPECT_RUN_ERROR(run, text) \
    if ((run).succeeded) { CHOC_FAIL ("Expected an error containing: " + std::string (text)); } \
    else if (! (run).errorContains (text)) { CHOC_FAIL ("Expected an error containing \"" + std::string (text) + "\", but got: " + (run).error); }

//==============================================================================
/// A uniquely-named folder in the temp directory, deleted along with its contents
/// when this goes out of scope.
struct TempFolder
{
    TempFolder()
    {
        static std::atomic<uint32_t> counter { 0 };
        auto time = std::chrono::steady_clock::now().time_since_epoch().count();

        folder = std::filesystem::temp_directory_path()
                   / ("cmaj_run_tests_" + std::to_string (time) + "_" + std::to_string (++counter));

        std::filesystem::create_directories (folder);
    }

    ~TempFolder()
    {
        std::error_code ignored;
        std::filesystem::remove_all (folder, ignored);
    }

    std::string getPath (const std::string& name) const    { return (folder / name).string(); }

    std::filesystem::path folder;
};

/// The sample written to test WAV files: a ramp, inverted on odd channels, so that
/// every frame and channel is distinguishable.
static float getTestSample (uint32_t channel, uint32_t frame)
{
    auto ramp = static_cast<float> (frame % 1000) / 1000.0f;
    return (channel % 2) == 0 ? ramp : -ramp;
}

static std::string writeTestWAV (const TempFolder& temp, const std::string& name, double sampleRate,
                                 uint32_t numChannels, uint32_t numFrames)
{
    auto path = temp.getPath (name);
    auto writer = cmaj::audio_utils::createFileWriter (path, sampleRate, numChannels);

    auto buffer = choc::buffer::createChannelArrayBuffer (numChannels, numFrames,
                                                          [] (choc::buffer::ChannelCount channel, choc::buffer::FrameCount frame)
                                                          {
                                                              return getTestSample (channel, frame);
                                                          });

    writer->appendFrames (buffer.getView());
    writer->flush();
    return path;
}

static choc::buffer::ChannelArrayBuffer<float> readWAV (const std::string& path)
{
    auto reader = cmaj::audio_utils::createFileReader (path);

    if (reader == nullptr)
        return {};

    return reader->readEntireStream<float>();
}

/// Writes a MIDI file holding the given (seconds, message) events, at the default 120bpm.
static std::string writeTestMIDIFile (const TempFolder& temp, const std::string& name,
                                      const std::vector<std::pair<double, choc::midi::ShortMessage>>& events)
{
    choc::midi::Sequence sequence;

    for (auto& e : events)
        sequence.events.push_back ({ e.first, choc::midi::LongMessage (e.second.data()[0], e.second.data()[1], e.second.data()[2]) });

    auto data = choc::midi::File (sequence).save();
    auto path = temp.getPath (name);
    choc::file::replaceFileWithContent (path, std::string_view (reinterpret_cast<const char*> (data.data()), data.size()));
    return path;
}

//==============================================================================
/// A minimal synth: it outputs its note's velocity as a level, reports note starts
/// and ends as events carrying the pitch, and echoes its cutoff parameter.
static constexpr auto synthCode = R"(
processor Synth
{
    input event (std::notes::NoteOn, std::notes::NoteOff) eventIn;
    input value float cutoff;
    output stream float out;
    output event float noteStarted, noteEnded, velocityStarted;
    output value float cutoffSeen;

    float level;

    event eventIn (std::notes::NoteOn n)    { level = n.velocity; noteStarted <- n.pitch; velocityStarted <- n.velocity; }
    event eventIn (std::notes::NoteOff n)   { level = 0.0f; noteEnded <- n.pitch; }

    void main()
    {
        loop
        {
            out <- level;
            cutoffSeen <- cutoff;
            advance();
        }
    }
}
)";

/// Copies a stereo input stream to a stereo output stream.
static constexpr auto passthroughCode = R"(
processor Thru
{
    input stream float<2> in;
    output stream float<2> out;

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

/// Reports every MIDI message it receives as its packed int32 value.
static constexpr auto midiProbeCode = R"(
processor MidiProbe
{
    input event std::midi::Message midiIn;
    output event int32 packed;

    event midiIn (std::midi::Message m)     { packed <- m.message; }

    void main()     { loop advance(); }
}
)";

/// Emits a tick every 100 frames, and records when it gets the flush event.
static constexpr auto tickerCode = R"(
processor Ticker
{
    input event void flush;
    output event int32 tick, flushed;
    output value int32 framesSeen;

    int32 frame;

    event flush()   { flushed <- frame; }

    void main()
    {
        loop
        {
            if (frame % 100 == 0)
                tick <- frame;

            ++frame;
            framesSeen <- frame;
            advance();
        }
    }
}
)";

/// A value and an event input, each echoed back out.
static constexpr auto paramsCode = R"(
processor Params
{
    input value float level;
    input event float trigger;
    output event float triggered;
    output value float levelSeen;

    event trigger (float f)     { triggered <- f; }

    void main()
    {
        loop
        {
            levelSeen <- level;
            advance();
        }
    }
}
)";

/// Outputs its gain value as a stream, so that ramps can be checked sample by sample.
static constexpr auto rampCode = R"(
processor Ramp
{
    input value float gain;
    input event float marker;
    output event float markerSeen;
    output stream float out;

    event marker (float f)      { markerSeen <- f; }

    void main()
    {
        loop
        {
            out <- gain;
            advance();
        }
    }
}
)";

//==============================================================================
static void testOptionParsing (choc::test::TestProgress& progress)
{
    CHOC_TEST (OptionParsing)

    using cmaj::run_command::Length;
    using cmaj::run_command::RunOptions;

    CHOC_EXPECT_EQ (Length::parse ("1000").toFrames (44100.0), static_cast<uint64_t> (1000));
    CHOC_EXPECT_EQ (Length::parse ("2s").toFrames (44100.0), static_cast<uint64_t> (88200));
    CHOC_EXPECT_EQ (Length::parse ("1.5s").toFrames (48000.0), static_cast<uint64_t> (72000));
    CHOC_EXPECT_EQ (Length::parse ("250ms").toFrames (48000.0), static_cast<uint64_t> (12000));

    CHOC_EXPECT_THROW (Length::parse ("abc"));
    CHOC_EXPECT_THROW (Length::parse ("1.5"));
    CHOC_EXPECT_THROW (Length::parse ("0"));
    CHOC_EXPECT_THROW (Length::parse ("-5s"));
    CHOC_EXPECT_THROW (Length::parse ("s"));

    auto named = RunOptions::splitOptionalName ("in=file.wav");
    CHOC_EXPECT_EQ (named.name, std::string ("in"));
    CHOC_EXPECT_EQ (named.value, std::string ("file.wav"));

    auto unnamed = RunOptions::splitOptionalName ("file.wav");
    CHOC_EXPECT_TRUE (unnamed.name.empty());
    CHOC_EXPECT_EQ (unnamed.value, std::string ("file.wav"));

    // repeated options keep their order, in either the --x=v or --x v form, and sources
    // keep their order relative to each other
    choc::ArgumentList args (std::vector<std::string> { "-c", "processor A { output stream float out; void main() { advance(); } }",
                                                        "--set=a=1", "--set", "b=[1,2]", "--input=x.wav",
                                                        "--code=processor B { output stream float out; void main() { advance(); } }",
                                                        "--rate=48000", "--length=2s", "--blockSize=64", "--format=ndjson",
                                                        "--maxEvents=5", "--no-flush", "--quiet" });
    RunOptions options;
    options.parseArguments (args);

    CHOC_EXPECT_EQ (options.sets.size(), static_cast<size_t> (2));
    CHOC_EXPECT_EQ (options.sets[0].name, std::string ("a"));
    CHOC_EXPECT_EQ (options.sets[1].value, std::string ("[1,2]"));
    CHOC_EXPECT_EQ (options.inputs.size(), static_cast<size_t> (1));
    CHOC_EXPECT_TRUE (options.inputs[0].name.empty());
    CHOC_EXPECT_EQ (options.sources.size(), static_cast<size_t> (2));
    CHOC_EXPECT_TRUE (choc::text::contains (options.sources[0].code, "processor A"));
    CHOC_EXPECT_TRUE (choc::text::contains (options.sources[1].code, "processor B"));
    CHOC_EXPECT_EQ (*options.sampleRate, 48000.0);
    CHOC_EXPECT_EQ (options.blockSize, 64u);
    CHOC_EXPECT_TRUE (options.useNDJSON);
    CHOC_EXPECT_EQ (options.maxEvents, static_cast<uint64_t> (5));
    CHOC_EXPECT_FALSE (options.sendFlush);
    CHOC_EXPECT_TRUE (options.quiet);

    auto parse = [] (std::vector<std::string> tokens)
    {
        choc::ArgumentList a (std::move (tokens));
        RunOptions o;
        o.parseArguments (a);
    };

    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--bogus=1" }));
    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--score={}", "--score={}" }));
    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--set=noValue" }));
    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--format=xml" }));
    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--blockSize=100000" }));
    CHOC_EXPECT_THROW (parse ({ "-c", "x", "--rate=fast" }));
    CHOC_EXPECT_THROW (parse ({ "--rate=44100" }));
    CHOC_EXPECT_THROW (parse ({ "no_such_file.cmajor" }));
}

//==============================================================================
static void testOutputStreamToFile (choc::test::TestProgress& progress)
{
    CHOC_TEST (OutputStreamToFile)

    TempFolder temp;
    auto outFile = temp.getPath ("sine.wav");

    TestRun run ({ "--rate=44100", "--length=1000", "--blockSize=300", "--output=" + outFile, "--quiet",
                   "-c", R"(graph G { output stream float<2> out;
                                     node osc = std::oscillators::Sine (float<2>, 440.0f);
                                     connection osc -> out; })" });
    CMAJ_EXPECT_RUN_OK (run)

    CHOC_EXPECT_EQ (run.getFramesRendered(), static_cast<int64_t> (1000));
    CHOC_EXPECT_EQ (run.report["sampleRate"].getWithDefault<double> (0), 44100.0);
    CHOC_EXPECT_EQ (run.report["mainProcessor"].toString(), std::string ("G"));
    CHOC_EXPECT_EQ (run.report["outputs"]["out"].toString(), outFile);

    auto audio = readWAV (outFile);
    CHOC_EXPECT_EQ (audio.getNumChannels(), 2u);
    CHOC_EXPECT_EQ (audio.getNumFrames(), 1000u);

    // a 440Hz sine at 44.1kHz: a quarter of a cycle in is near the peak
    CHOC_EXPECT_NEAR (audio.getSample (0, 25), 1.0f, 0.01f);
    CHOC_EXPECT_NEAR (audio.getSample (1, 25), 1.0f, 0.01f);

    // only .wav files can be written, and float output streams
    TestRun notWav ({ "--rate=44100", "--length=100", "--output=" + temp.getPath ("x.mp3"), "-c", passthroughCode });
    CMAJ_EXPECT_RUN_ERROR (notWav, ".wav")
}

//==============================================================================
static void testMainProcessorResolution (choc::test::TestProgress& progress)
{
    CHOC_TEST (MainProcessorResolution)

    auto twoProcessors = R"(
        processor A { output event int32 which; void main() { which <- 1; loop advance(); } }
        processor B { output event int32 which; void main() { which <- 2; loop advance(); } }
    )";

    TestRun ambiguous ({ "--rate=1000", "--length=10", "--quiet", "-c", twoProcessors });
    CMAJ_EXPECT_RUN_ERROR (ambiguous, "multiple candidates")

    TestRun chosen ({ "--rate=1000", "--length=10", "--quiet", "--main=B", "-c", twoProcessors });
    CMAJ_EXPECT_RUN_OK (chosen)
    CHOC_EXPECT_EQ (chosen.getEventValue ("which", 0), 2.0);

    TestRun marked ({ "--rate=1000", "--length=10", "--quiet", "-c",
                      R"(processor A { output event int32 which; void main() { which <- 1; loop advance(); } }
                         processor B [[ main ]] { output event int32 which; void main() { which <- 2; loop advance(); } })" });
    CMAJ_EXPECT_RUN_OK (marked)
    CHOC_EXPECT_EQ (marked.getEventValue ("which", 0), 2.0);

    TestRun compileError ({ "--rate=1000", "--length=10", "-c", "processor P { output stream float out; void main() { out <- nonsense; } }" });
    CMAJ_EXPECT_RUN_ERROR (compileError, "nonsense")
}

//==============================================================================
static void testEventsValuesAndConsole (choc::test::TestProgress& progress)
{
    CHOC_TEST (EventsValuesAndConsole)

    TestRun run ({ "--rate=1000", "--length=1000", "--blockSize=64", "--quiet", "-c", R"(
        namespace N
        {
            struct Info { int32 index; float32 amount; }
        }

        processor P
        {
            output event N::Info info;
            output value N::Info latest;

            void main()
            {
                console <- "starting";

                for (wrap<1000> i)
                {
                    if (i % 250 == 7)
                    {
                        let x = N::Info (int32 (i), float32 (i) * 0.5f);
                        info <- x;
                        latest <- x;
                    }

                    advance();
                }

                loop advance();
            }
        }
    )" });
    CMAJ_EXPECT_RUN_OK (run)

    // events land on their exact frames, regardless of the block size
    auto info = run.getEvents ("info");
    CHOC_EXPECT_EQ (info.size(), 4u);
    CHOC_EXPECT_EQ (run.getEventFrame ("info", 0), static_cast<int64_t> (7));
    CHOC_EXPECT_EQ (run.getEventFrame ("info", 3), static_cast<int64_t> (757));
    CHOC_EXPECT_NEAR (info[3]["time"].getWithDefault<double> (0), 0.757, 0.000001);
    CHOC_EXPECT_EQ (info[1]["value"]["index"].getWithDefault<int32_t> (0), 257);
    CHOC_EXPECT_NEAR (info[1]["value"]["amount"].getWithDefault<double> (0), 128.5, 0.0001);

    CHOC_EXPECT_EQ (run.report["values"]["latest"]["index"].getWithDefault<int32_t> (0), 757);

    auto console = run.report["console"];
    CHOC_EXPECT_EQ (console.size(), 1u);
    CHOC_EXPECT_EQ (console[0]["value"].toString(), std::string ("starting"));
    CHOC_EXPECT_EQ (console[0]["frame"].getWithDefault<int64_t> (-1), static_cast<int64_t> (0));
}

//==============================================================================
static void testMaxEventsAndNDJSON (choc::test::TestProgress& progress)
{
    CHOC_TEST (MaxEventsAndNDJSON)

    TestRun truncated ({ "--rate=1000", "--length=1000", "--maxEvents=3", "--quiet", "-c", tickerCode });
    CMAJ_EXPECT_RUN_OK (truncated)

    auto ticks = truncated.getEvents ("tick");
    CHOC_EXPECT_TRUE (ticks["truncated"].getWithDefault<bool> (false));
    CHOC_EXPECT_EQ (ticks["count"].getWithDefault<int64_t> (0), static_cast<int64_t> (11));   // ten ticks, and one at the flush frame
    CHOC_EXPECT_EQ (ticks["items"].size(), 3u);

    TestRun streamed ({ "--rate=1000", "--length=300", "--format=ndjson", "--quiet", "-c", tickerCode });
    CMAJ_EXPECT_RUN_OK (streamed)

    auto lines = choc::text::splitIntoLines (streamed.output.str(), false);
    std::vector<choc::value::Value> parsed;

    for (auto& line : lines)
        if (! choc::text::trim (line).empty())
            parsed.push_back (choc::json::parse (line));

    // three ticks, the flush event and its tick, then the end line
    CHOC_EXPECT_EQ (parsed.size(), static_cast<size_t> (6));

    if (parsed.size() == 6)
    {
        CHOC_EXPECT_EQ (parsed[0]["type"].toString(), std::string ("event"));
        CHOC_EXPECT_EQ (parsed[0]["endpoint"].toString(), std::string ("tick"));
        CHOC_EXPECT_EQ (parsed[2]["frame"].getWithDefault<int64_t> (-1), static_cast<int64_t> (200));

        int64_t lastFrame = 0;

        for (size_t i = 0; i < 5; ++i)
        {
            auto frame = parsed[i]["frame"].getWithDefault<int64_t> (-1);
            CHOC_EXPECT_TRUE (frame >= lastFrame);
            lastFrame = frame;
        }

        CHOC_EXPECT_EQ (parsed[5]["type"].toString(), std::string ("end"));
        CHOC_EXPECT_EQ (parsed[5]["framesRendered"].getWithDefault<int64_t> (-1), static_cast<int64_t> (300));
        CHOC_EXPECT_EQ (parsed[5]["values"]["framesSeen"].getWithDefault<int32_t> (-1), 301);
    }
}

//==============================================================================
static void testFlush (choc::test::TestProgress& progress)
{
    CHOC_TEST (Flush)

    TestRun run ({ "--rate=1000", "--length=1000", "--blockSize=512", "--quiet", "-c", tickerCode });
    CMAJ_EXPECT_RUN_OK (run)

    // the flush is a single extra frame: it arrives at the end frame, and nothing
    // is rendered beyond it
    CHOC_EXPECT_EQ (run.getFramesRendered(), static_cast<int64_t> (1000));
    CHOC_EXPECT_EQ (run.getEvents ("flushed").size(), 1u);
    CHOC_EXPECT_EQ (run.getEventFrame ("flushed", 0), static_cast<int64_t> (1000));
    CHOC_EXPECT_EQ (run.getEventValue ("flushed", 0), 1000.0);
    CHOC_EXPECT_EQ (run.getEvents ("tick").size(), 11u);
    CHOC_EXPECT_EQ (run.getEventFrame ("tick", 10), static_cast<int64_t> (1000));
    CHOC_EXPECT_EQ (run.getValue ("framesSeen"), 1001.0);

    TestRun noFlush ({ "--rate=1000", "--length=1000", "--blockSize=512", "--no-flush", "--quiet", "-c", tickerCode });
    CMAJ_EXPECT_RUN_OK (noFlush)
    CHOC_EXPECT_EQ (noFlush.getEvents ("flushed").size(), 0u);
    CHOC_EXPECT_EQ (noFlush.getEvents ("tick").size(), 10u);
    CHOC_EXPECT_EQ (noFlush.getValue ("framesSeen"), 1000.0);
}

//==============================================================================
static void testExternals (choc::test::TestProgress& progress)
{
    CHOC_TEST (Externals)

    TempFolder temp;
    auto jsonFile = temp.getPath ("table.json");
    choc::file::replaceFileWithContent (jsonFile, "[10, 20, 30]");
    auto audioFile = writeTestWAV (temp, "sample.wav", 22050.0, 1, 123);

    auto code = R"(
        processor Ext
        {
            output value float result;
            output value int32 sampleLength;
            output value float64 sampleRate;

            external float gain;
            external int32[3] table;
            external std::audio_data::Mono sample;

            void main()
            {
                result <- gain * float (table[1]);
                sampleLength <- int32 (sample.frames.size);
                sampleRate <- sample.sampleRate;
                loop advance();
            }
        }
    )";

    TestRun run ({ "--rate=1000", "--length=10", "--quiet", "--external=gain=0.5", "--external=Ext::table=@" + jsonFile,
                   "--external=sample=" + audioFile, "-c", code });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.getValue ("result"), 10.0);
    CHOC_EXPECT_EQ (run.getValue ("sampleLength"), 123.0);
    CHOC_EXPECT_EQ (run.getValue ("sampleRate"), 22050.0);

    TestRun missing ({ "--rate=1000", "--length=10", "--external=gain=0.5", "-c", code });
    CMAJ_EXPECT_RUN_ERROR (missing, "Ext::table")

    TestRun unused ({ "--rate=1000", "--length=10", "--external=gain=0.5", "--external=Ext::table=[1,2,3]",
                      "--external=sample=" + audioFile, "--external=nothing=1", "-c", code });
    CMAJ_EXPECT_RUN_ERROR (unused, "nothing")

    TestRun badJSON ({ "--rate=1000", "--length=10", "--external=gain={oops", "--external=Ext::table=[1,2,3]",
                       "--external=sample=" + audioFile, "-c", code });
    CMAJ_EXPECT_RUN_ERROR (badJSON, "gain")
}

//==============================================================================
static void testInputPassthrough (choc::test::TestProgress& progress)
{
    CHOC_TEST (InputPassthrough)

    TempFolder temp;
    auto inFile = writeTestWAV (temp, "in.wav", 48000.0, 2, 1500);
    auto outFile = temp.getPath ("out.wav");

    // no --rate or --length: both come from the input file
    TestRun run ({ "--input=" + inFile, "--output=out=" + outFile, "--blockSize=256", "--quiet", "-c", passthroughCode });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.report["sampleRate"].getWithDefault<double> (0), 48000.0);
    CHOC_EXPECT_EQ (run.getFramesRendered(), static_cast<int64_t> (1500));

    auto audio = readWAV (outFile);
    CHOC_EXPECT_EQ (audio.getNumChannels(), 2u);
    CHOC_EXPECT_EQ (audio.getNumFrames(), 1500u);

    bool allMatch = audio.getNumFrames() == 1500;

    for (uint32_t frame = 0; frame < audio.getNumFrames() && allMatch; ++frame)
        for (uint32_t channel = 0; channel < 2; ++channel)
            if (audio.getSample (channel, frame) != getTestSample (channel, frame))
                allMatch = false;

    CHOC_EXPECT_TRUE (allMatch);

    // rendering beyond the end of the input file feeds silence
    TestRun longer ({ "--input=in=" + inFile, "--output=" + outFile, "--length=2000", "--quiet", "-c", passthroughCode });
    CMAJ_EXPECT_RUN_OK (longer)

    auto longerAudio = readWAV (outFile);
    CHOC_EXPECT_EQ (longerAudio.getNumFrames(), 2000u);

    if (longerAudio.getNumFrames() == 2000)
    {
        CHOC_EXPECT_EQ (longerAudio.getSample (0, 1499), getTestSample (0, 1499));
        CHOC_EXPECT_EQ (longerAudio.getSample (0, 1500), 0.0f);
        CHOC_EXPECT_EQ (longerAudio.getSample (1, 1999), 0.0f);
    }

    // an input stream with no file gets silence, and says so
    TestRun silent ({ "--rate=48000", "--length=100", "-c", passthroughCode });
    CMAJ_EXPECT_RUN_OK (silent)
    CHOC_EXPECT_TRUE (choc::text::contains (silent.diagnostics.str(), "'in' has no --input file"));
}

//==============================================================================
static void testFloat64Input (choc::test::TestProgress& progress)
{
    CHOC_TEST (Float64Input)

    TempFolder temp;
    auto inFile = writeTestWAV (temp, "mono.wav", 1000.0, 1, 100);

    TestRun run ({ "--input=" + inFile, "--quiet", "-c", R"(
        processor P
        {
            input stream float64 in;
            output event float64 sample;

            void main()
            {
                loop
                {
                    sample <- in;
                    advance();
                }
            }
        }
    )" });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.getEvents ("sample").size(), 100u);
    CHOC_EXPECT_NEAR (run.getEventValue ("sample", 42), static_cast<double> (getTestSample (0, 42)), 0.000001);
}

//==============================================================================
static void testInputRules (choc::test::TestProgress& progress)
{
    CHOC_TEST (InputRules)

    TempFolder temp;
    auto stereo48k = writeTestWAV (temp, "stereo48k.wav", 48000.0, 2, 1000);
    auto stereo44k = writeTestWAV (temp, "stereo44k.wav", 44100.0, 2, 1000);
    auto mono48k   = writeTestWAV (temp, "mono48k.wav", 48000.0, 1, 1000);

    // channel counts must match, and the error names both
    TestRun channels ({ "--input=" + mono48k, "-c", passthroughCode });
    CMAJ_EXPECT_RUN_ERROR (channels, "has 1 channel(s), but the input stream 'in' has 2 channel(s)")

    // --rate must agree with the files
    TestRun rate ({ "--input=" + stereo44k, "--rate=48000", "-c", passthroughCode });
    CMAJ_EXPECT_RUN_ERROR (rate, "44100")

    auto twoInputs = R"(
        processor Two
        {
            input stream float<2> a, b;
            output stream float<2> out;
            void main() { loop { out <- a + b; advance(); } }
        }
    )";

    // ...and with each other
    TestRun mixedRates ({ "--input=a=" + stereo48k, "--input=b=" + stereo44k, "-c", twoInputs });
    CMAJ_EXPECT_RUN_ERROR (mixedRates, "rates must match")

    // with more than one input stream, the name is needed, and the error lists them
    TestRun unnamed ({ "--input=" + stereo48k, "-c", twoInputs });
    CMAJ_EXPECT_RUN_ERROR (unnamed, "'a', 'b'")

    TestRun wrongName ({ "--input=c=" + stereo48k, "-c", twoInputs });
    CMAJ_EXPECT_RUN_ERROR (wrongName, "no input stream called 'c'. Available input streams: 'a', 'b'")

    TestRun twice ({ "--input=a=" + stereo48k, "--input=a=" + stereo48k, "-c", twoInputs });
    CMAJ_EXPECT_RUN_ERROR (twice, "more than one file")

    TestRun missingFile ({ "--input=" + temp.getPath ("nope.wav"), "-c", passthroughCode });
    CMAJ_EXPECT_RUN_ERROR (missingFile, "file not found")

    TestRun wrongOutput ({ "--input=a=" + stereo48k, "--output=nope=" + temp.getPath ("o.wav"), "-c", twoInputs });
    CMAJ_EXPECT_RUN_ERROR (wrongOutput, "no output stream called 'nope'. Available output streams: 'out'")

    // the longest input decides the length
    auto shorter = writeTestWAV (temp, "short.wav", 48000.0, 2, 400);
    TestRun longest ({ "--input=a=" + shorter, "--input=b=" + stereo48k, "--quiet", "-c", twoInputs });
    CMAJ_EXPECT_RUN_OK (longest)
    CHOC_EXPECT_EQ (longest.getFramesRendered(), static_cast<int64_t> (1000));

    // with nothing to infer them from, the rate and length must be given
    TestRun noRate ({ "--length=100", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (noRate, "--rate")

    TestRun noLength ({ "--rate=1000", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (noLength, "--length")
}

//==============================================================================
static void testSet (choc::test::TestProgress& progress)
{
    CHOC_TEST (Set)

    TestRun run ({ "--rate=1000", "--length=100", "--quiet", "--set=level=0.25", "--set=trigger=3", "--set=trigger=4.5", "-c", paramsCode });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.getValue ("levelSeen"), 0.25);
    CHOC_EXPECT_EQ (run.getEvents ("triggered").size(), 2u);
    CHOC_EXPECT_EQ (run.getEventFrame ("triggered", 0), static_cast<int64_t> (0));
    CHOC_EXPECT_EQ (run.getEventValue ("triggered", 0), 3.0);
    CHOC_EXPECT_EQ (run.getEventValue ("triggered", 1), 4.5);

    TestRun wrongName ({ "--rate=1000", "--length=100", "--set=volume=1", "-c", paramsCode });
    CMAJ_EXPECT_RUN_ERROR (wrongName, "no input event or value called 'volume'. Available event and value inputs: 'level', 'trigger'")

    TestRun wrongType ({ "--rate=1000", "--length=100", "--set=level={\"a\":[1,2]}", "-c", paramsCode });
    CMAJ_EXPECT_RUN_ERROR (wrongType, "can't convert")

    TestRun numberForStruct ({ "--rate=1000", "--length=100", "--set=eventIn=5", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (numberForStruct, "can't convert 5 to the type of 'eventIn'")

    TestRun noteAsJSON ({ "--rate=1000", "--length=100", "--quiet",
                          R"(--set=eventIn={"_type":"NoteOn","channel":0,"pitch":72,"velocity":0.5})", "-c", synthCode });
    CMAJ_EXPECT_RUN_OK (noteAsJSON)
    CHOC_EXPECT_EQ (noteAsJSON.getEventValue ("noteStarted", 0), 72.0);

    TestRun notJSON ({ "--rate=1000", "--length=100", "--set=level=loud", "-c", paramsCode });
    CMAJ_EXPECT_RUN_ERROR (notJSON, "isn't valid JSON")

    TestRun stream ({ "--rate=1000", "--length=100", "--set=in=1", "-c", passthroughCode });
    CMAJ_EXPECT_RUN_ERROR (stream, "is an input stream")
}

//==============================================================================
static void testScoreNotes (choc::test::TestProgress& progress)
{
    CHOC_TEST (ScoreNotes)

    // block size 512 doesn't divide any of the note times, so they're only right if
    // the blocks are split at them
    TestRun run ({ "--rate=48000", "--length=1s", "--blockSize=512", "--quiet", "--set=cutoff=0.35",
                   R"(--score={"notes":[{"start":0.25,"duration":0.25,"pitch":60,"velocity":0.8},
                                        {"start":0.5,"duration":0.1,"pitch":67.5,"channel":2}]})",
                   "-c", synthCode });
    CMAJ_EXPECT_RUN_OK (run)

    CHOC_EXPECT_EQ (run.getEvents ("noteStarted").size(), 2u);
    CHOC_EXPECT_EQ (run.getEventFrame ("noteStarted", 0), static_cast<int64_t> (12000));
    CHOC_EXPECT_EQ (run.getEventValue ("noteStarted", 0), 60.0);
    CHOC_EXPECT_NEAR (run.getEventValue ("velocityStarted", 0), 0.8, 0.00001);
    CHOC_EXPECT_EQ (run.getEventFrame ("noteStarted", 1), static_cast<int64_t> (24000));
    CHOC_EXPECT_EQ (run.getEventValue ("noteStarted", 1), 67.5);
    CHOC_EXPECT_EQ (run.getEventValue ("velocityStarted", 1), 1.0);   // the default

    // the first note ends on the same frame the second starts, and goes first
    CHOC_EXPECT_EQ (run.getEvents ("noteEnded").size(), 2u);
    CHOC_EXPECT_EQ (run.getEventFrame ("noteEnded", 0), static_cast<int64_t> (24000));
    CHOC_EXPECT_EQ (run.getEventValue ("noteEnded", 0), 60.0);
    CHOC_EXPECT_EQ (run.getEventFrame ("noteEnded", 1), static_cast<int64_t> (28800));

    CHOC_EXPECT_NEAR (run.getValue ("cutoffSeen"), 0.35, 0.00001);

    // without --length, the score's end plus a second
    TestRun inferred ({ "--rate=1000", "--quiet", R"(--score={"notes":[{"start":0.5,"duration":1,"pitch":60}]})", "-c", synthCode });
    CMAJ_EXPECT_RUN_OK (inferred)
    CHOC_EXPECT_EQ (inferred.getFramesRendered(), static_cast<int64_t> (2500));
}

//==============================================================================
static void testScoreFromFile (choc::test::TestProgress& progress)
{
    CHOC_TEST (ScoreFromFile)

    TempFolder temp;
    auto scoreFile = temp.getPath ("score.json");
    choc::file::replaceFileWithContent (scoreFile, R"({ "notes": [ { "start": 0.1, "duration": 0.2, "pitch": 48 } ] })");

    TestRun run ({ "--rate=1000", "--quiet", "--score=" + scoreFile, "-c", synthCode });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.getEventFrame ("noteStarted", 0), static_cast<int64_t> (100));
    CHOC_EXPECT_EQ (run.getEventFrame ("noteEnded", 0), static_cast<int64_t> (300));

    TestRun missing ({ "--rate=1000", "--score=" + temp.getPath ("nope.json"), "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (missing, "no file")
}

//==============================================================================
static void testScoreAutomation (choc::test::TestProgress& progress)
{
    CHOC_TEST (ScoreAutomation)

    TempFolder temp;
    auto outFile = temp.getPath ("ramp.wav");

    TestRun run ({ "--rate=1000", "--length=400", "--blockSize=64", "--output=" + outFile, "--quiet",
                   R"(--score={"automation":[{"time":0.1,"endpoint":"gain","value":1.0,"rampFrames":100},
                                             {"time":0.123,"endpoint":"marker","value":7}]})",
                   "-c", rampCode });
    CMAJ_EXPECT_RUN_OK (run)

    CHOC_EXPECT_EQ (run.getEventFrame ("markerSeen", 0), static_cast<int64_t> (123));
    CHOC_EXPECT_EQ (run.getEventValue ("markerSeen", 0), 7.0);

    auto audio = readWAV (outFile);
    CHOC_EXPECT_EQ (audio.getNumFrames(), 400u);

    if (audio.getNumFrames() == 400)
    {
        CHOC_EXPECT_EQ (audio.getSample (0, 99), 0.0f);
        CHOC_EXPECT_NEAR (audio.getSample (0, 150), 0.5f, 0.02f);
        CHOC_EXPECT_NEAR (audio.getSample (0, 250), 1.0f, 0.0001f);
    }

    TestRun rampOnEvent ({ "--rate=1000", "--length=100", R"(--score={"automation":[{"time":0,"endpoint":"marker","value":1,"rampFrames":10}]})", "-c", rampCode });
    CMAJ_EXPECT_RUN_ERROR (rampOnEvent, "rampFrames can only be used with value endpoints")

    TestRun unknownEndpoint ({ "--rate=1000", "--length=100", R"(--score={"automation":[{"time":0,"endpoint":"volume","value":1}]})", "-c", rampCode });
    CMAJ_EXPECT_RUN_ERROR (unknownEndpoint, "Available event and value inputs: 'gain', 'marker'")
}

//==============================================================================
static void testScoreErrors (choc::test::TestProgress& progress)
{
    CHOC_TEST (ScoreErrors)

    TestRun badJSON ({ "--rate=1000", "--score={notes:", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (badJSON, "failed to parse")

    TestRun typo ({ "--rate=1000", R"(--score={"notes":[{"start":0,"duration":1,"pich":60}]})", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (typo, "notes[0]: unknown property \"pich\"")

    TestRun missingPitch ({ "--rate=1000", R"(--score={"notes":[{"start":0,"duration":1}]})", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (missingPitch, "needs a \"pitch\"")

    TestRun negativeTime ({ "--rate=1000", R"(--score={"notes":[{"start":-1,"duration":1,"pitch":60}]})", "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (negativeTime, "can't be negative")

    TestRun noNoteInput ({ "--rate=1000", R"(--score={"notes":[{"start":0,"duration":1,"pitch":60}]})", "-c", paramsCode });
    CMAJ_EXPECT_RUN_ERROR (noNoteInput, "no input event accepting std::notes::NoteOn, and no MIDI input. Available input events: 'trigger'")
}

//==============================================================================
static void testScoreToMIDIInput (choc::test::TestProgress& progress)
{
    CHOC_TEST (ScoreToMIDIInput)

    TestRun run ({ "--rate=1000", "--length=1000", "--quiet",
                   R"(--score={"notes":[{"start":0.2,"duration":0.3,"pitch":64,"velocity":1,"channel":3}]})",
                   "-c", midiProbeCode });
    CMAJ_EXPECT_RUN_OK (run)

    CHOC_EXPECT_EQ (run.getEvents ("packed").size(), 2u);
    CHOC_EXPECT_EQ (run.getEventFrame ("packed", 0), static_cast<int64_t> (200));
    CHOC_EXPECT_EQ (run.getEventValue ("packed", 0), static_cast<double> (0x93407f));
    CHOC_EXPECT_EQ (run.getEventFrame ("packed", 1), static_cast<int64_t> (500));
    CHOC_EXPECT_EQ (run.getEventValue ("packed", 1), static_cast<double> (0x834000));

    TestRun fractional ({ "--rate=1000", "--length=1000", R"(--score={"notes":[{"start":0,"duration":0.3,"pitch":64.5}]})", "-c", midiProbeCode });
    CMAJ_EXPECT_RUN_ERROR (fractional, "whole number from 0 to 127")
}

//==============================================================================
static void testMIDIFile (choc::test::TestProgress& progress)
{
    CHOC_TEST (MIDIFile)

    TempFolder temp;
    auto midiFile = writeTestMIDIFile (temp, "test.mid", { { 0.25, choc::midi::ShortMessage (0x90, 60, 100) },
                                                           { 0.75, choc::midi::ShortMessage (0x80, 60, 0) },
                                                           { 1.0,  choc::midi::ShortMessage (0xb0, 1, 64) } });

    // without --length, the MIDI file's end plus a second
    TestRun run ({ "--rate=1000", "--blockSize=100", "--quiet", "--midi=" + midiFile, "-c", midiProbeCode });
    CMAJ_EXPECT_RUN_OK (run)
    CHOC_EXPECT_EQ (run.getFramesRendered(), static_cast<int64_t> (2000));

    CHOC_EXPECT_EQ (run.getEvents ("packed").size(), 3u);
    CHOC_EXPECT_EQ (run.getEventFrame ("packed", 0), static_cast<int64_t> (250));
    CHOC_EXPECT_EQ (run.getEventValue ("packed", 0), static_cast<double> (0x903c64));
    CHOC_EXPECT_EQ (run.getEventFrame ("packed", 1), static_cast<int64_t> (750));
    CHOC_EXPECT_EQ (run.getEventFrame ("packed", 2), static_cast<int64_t> (1000));
    CHOC_EXPECT_EQ (run.getEventValue ("packed", 2), static_cast<double> (0xb00140));

    TestRun noMIDIInput ({ "--rate=1000", "--midi=" + midiFile, "-c", synthCode });
    CMAJ_EXPECT_RUN_ERROR (noMIDIInput, "no MIDI input")

    TestRun missing ({ "--rate=1000", "--midi=" + temp.getPath ("nope.mid"), "-c", midiProbeCode });
    CMAJ_EXPECT_RUN_ERROR (missing, "file not found")
}

//==============================================================================
inline void runUnitTests (choc::test::TestProgress& progress)
{
    CHOC_CATEGORY (RunCommand);

    testOptionParsing (progress);
    testOutputStreamToFile (progress);
    testMainProcessorResolution (progress);
    testEventsValuesAndConsole (progress);
    testMaxEventsAndNDJSON (progress);
    testFlush (progress);
    testExternals (progress);
    testInputPassthrough (progress);
    testFloat64Input (progress);
    testInputRules (progress);
    testSet (progress);
    testScoreNotes (progress);
    testScoreFromFile (progress);
    testScoreAutomation (progress);
    testScoreErrors (progress);
    testScoreToMIDIInput (progress);
    testMIDIFile (progress);
}

#undef CMAJ_EXPECT_RUN_OK
#undef CMAJ_EXPECT_RUN_ERROR

} // namespace cmaj::run_command_tests
