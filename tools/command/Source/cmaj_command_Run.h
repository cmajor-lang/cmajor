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

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#include "choc/audio/choc_MIDIFile.h"
#include "choc/containers/choc_ArgumentList.h"
#include "choc/text/choc_Files.h"
#include "choc/text/choc_FloatToString.h"
#include "choc/text/choc_JSON.h"
#include "choc/text/choc_StringUtilities.h"

#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "../../../include/cmajor/API/cmaj_ExternalVariables.h"
#include "../../../include/cmajor/helpers/cmaj_EndpointTypeCoercion.h"
#include "../../../modules/playback/include/cmaj_AudioFileUtils.h"

/// Implements `cmaj run`: compiles some Cmajor code and renders its main processor
/// synchronously, with no patch, message loop, audio device or threads. Audio, events
/// and values go in from files and options; output streams go to audio files, and all
/// events, values and console output go to a JSON report.
namespace cmaj::run_command
{

//==============================================================================
/// A render length, given either as a frame count or as a duration in seconds.
struct Length
{
    /// Parses "1000" (frames), "1.5s" or "250ms".
    static Length parse (const std::string& text)
    {
        auto t = choc::text::trim (text);
        Length length;
        auto numberPart = t;

        if (choc::text::endsWith (t, "ms"))
        {
            numberPart = t.substr (0, t.length() - 2);
            length.isSeconds = true;
            length.scale = 0.001;
        }
        else if (choc::text::endsWith (t, "s"))
        {
            numberPart = t.substr (0, t.length() - 1);
            length.isSeconds = true;
        }

        auto isValid = ! numberPart.empty()
                         && numberPart.find_first_not_of ("0123456789.") == std::string::npos
                         && std::count (numberPart.begin(), numberPart.end(), '.') <= (length.isSeconds ? 1 : 0);

        if (! isValid)
            throw std::runtime_error ("Illegal --length value '" + text + "': expected a number of frames, or a duration such as 2s or 500ms");

        length.amount = std::stod (numberPart);

        if (length.amount <= 0)
            throw std::runtime_error ("Illegal --length value '" + text + "': the length must be greater than zero");

        return length;
    }

    uint64_t toFrames (double sampleRate) const
    {
        if (! isSeconds)
            return static_cast<uint64_t> (amount);

        return static_cast<uint64_t> (std::llround (amount * scale * sampleRate));
    }

    double amount = 0;
    double scale = 1.0;
    bool isSeconds = false;
};

//==============================================================================
/// The parsed command-line options for `cmaj run`.
struct RunOptions
{
    struct Source
    {
        std::string filename, code;
    };

    /// An option of the form [name=]value
    struct NamedValue
    {
        std::string name, value;
    };

    void parseArguments (choc::ArgumentList& args)
    {
        if (auto m = removeSingleValue (args, "--main"))
            mainProcessor = *m;

        if (auto r = removeSingleValue (args, "--rate"))
            sampleRate = parsePositiveNumber (*r, "--rate");

        if (auto l = removeSingleValue (args, "--length"))
            length = Length::parse (*l);

        if (auto b = removeSingleValue (args, "--blockSize"))
        {
            auto size = parsePositiveNumber (*b, "--blockSize");

            if (size != std::floor (size) || size > maxBlockSize)
                throw std::runtime_error ("Illegal --blockSize value '" + *b + "': expected a whole number between 1 and "
                                            + std::to_string (maxBlockSize));

            blockSize = static_cast<uint32_t> (size);
        }

        for (auto& input : removeAllValues (args, "--input"))
            inputs.push_back (splitOptionalName (input));

        for (auto& output : removeAllValues (args, "--output"))
            outputs.push_back (splitOptionalName (output));

        for (auto& external : removeAllValues (args, "--external"))
            externals.push_back (splitRequiredName (external, "--external"));

        for (auto& set : removeAllValues (args, "--set"))
            sets.push_back (splitRequiredName (set, "--set"));

        if (auto s = removeSingleValue (args, "--score"))
            score = *s;

        if (auto m = removeSingleValue (args, "--midi"))
            midiFile = *m;

        if (auto r = removeSingleValue (args, "--report"))
            reportFile = *r;

        if (auto f = removeSingleValue (args, "--format"))
        {
            if (*f == "ndjson")    useNDJSON = true;
            else if (*f != "json") throw std::runtime_error ("Illegal --format value '" + *f + "': expected json or ndjson");
        }

        if (auto m = removeSingleValue (args, "--maxEvents"))
        {
            auto n = parsePositiveNumber (*m, "--maxEvents");

            if (n != std::floor (n))
                throw std::runtime_error ("Illegal --maxEvents value '" + *m + "': expected a whole number");

            maxEvents = static_cast<uint64_t> (n);
        }

        if (args.removeIfFound ("--no-flush"))
            sendFlush = false;

        if (args.removeIfFound ("--quiet"))
            quiet = true;

        parseSources (args);
    }

    std::vector<Source> sources;
    std::string mainProcessor;
    std::optional<double> sampleRate;
    std::optional<Length> length;
    uint32_t blockSize = 512;
    std::vector<NamedValue> inputs, outputs, externals, sets;
    std::string score, midiFile, reportFile;
    bool useNDJSON = false;
    uint64_t maxEvents = 10000;
    bool sendFlush = true;
    bool quiet = false;

    static constexpr uint32_t maxBlockSize = 8192;

    //==============================================================================
    /// Removes every occurrence of an option given as either "--name=value" or
    /// "--name value", returning the values in the order they appeared.
    static std::vector<std::string> removeAllValues (choc::ArgumentList& args, const std::string& option)
    {
        std::vector<std::string> values;
        auto prefix = option + "=";
        auto& tokens = args.tokens;

        for (size_t i = 0; i < tokens.size();)
        {
            if (choc::text::startsWith (tokens[i], prefix))
            {
                values.push_back (tokens[i].substr (prefix.length()));
                tokens.erase (tokens.begin() + static_cast<std::ptrdiff_t> (i));
            }
            else if (tokens[i] == option)
            {
                if (i + 1 >= tokens.size())
                    throw std::runtime_error ("Expected a value after " + option);

                values.push_back (tokens[i + 1]);
                tokens.erase (tokens.begin() + static_cast<std::ptrdiff_t> (i),
                              tokens.begin() + static_cast<std::ptrdiff_t> (i + 2));
            }
            else
            {
                ++i;
            }
        }

        return values;
    }

    /// Like removeAllValues(), but for an option that may only be given once.
    static std::optional<std::string> removeSingleValue (choc::ArgumentList& args, const std::string& option)
    {
        auto values = removeAllValues (args, option);

        if (values.empty())
            return {};

        if (values.size() > 1)
            throw std::runtime_error ("The " + option + " option can only be given once");

        return values.front();
    }

    /// Splits "name=value" into its parts, or returns an empty name if there's no '='.
    static NamedValue splitOptionalName (const std::string& text)
    {
        auto equals = text.find ('=');

        if (equals == std::string::npos)
            return { {}, text };

        return { text.substr (0, equals), text.substr (equals + 1) };
    }

    static NamedValue splitRequiredName (const std::string& text, const std::string& option)
    {
        auto result = splitOptionalName (text);

        if (result.name.empty())
            throw std::runtime_error ("Expected " + option + "=<name>=<value>, but got '" + text + "'");

        return result;
    }

    static double parsePositiveNumber (const std::string& text, const std::string& option)
    {
        auto t = choc::text::trim (text);
        double result = 0;

        try
        {
            size_t charsUsed = 0;
            result = std::stod (t, std::addressof (charsUsed));

            if (charsUsed != t.length())
                result = -1;
        }
        catch (const std::exception&)
        {
            result = -1;
        }

        if (! (result > 0))
            throw std::runtime_error ("Illegal " + option + " value '" + text + "': expected a positive number");

        return result;
    }

private:
    /// Once all the other options are removed, what's left are the sources, which
    /// are kept in the order they were given: files, -c <code>, --code=<code>, and
    /// "-" for stdin.
    void parseSources (choc::ArgumentList& args)
    {
        auto& tokens = args.tokens;
        int inlineCount = 0;

        auto addInlineCode = [&] (std::string code)
        {
            sources.push_back ({ "<inline code " + std::to_string (++inlineCount) + ">", std::move (code) });
        };

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            auto& token = tokens[i];

            if (token == "-c" || token == "--code")
            {
                if (i + 1 >= tokens.size())
                    throw std::runtime_error ("Expected some code after " + token);

                addInlineCode (tokens[++i]);
            }
            else if (choc::text::startsWith (token, "--code="))
            {
                addInlineCode (token.substr (7));
            }
            else if (token == "-")
            {
                std::ostringstream content;
                content << std::cin.rdbuf();
                sources.push_back ({ "<stdin>", content.str() });
            }
            else if (choc::text::startsWith (token, "-"))
            {
                throw std::runtime_error ("Unknown option for cmaj run: '" + token + "'");
            }
            else
            {
                if (! std::filesystem::is_regular_file (token))
                    throw std::runtime_error ("File not found: " + token);

                sources.push_back ({ token, choc::file::loadFileAsString (token) });
            }
        }

        tokens.clear();

        if (sources.empty())
            throw std::runtime_error ("Expected some Cmajor source: give one or more files, -c <code>, or - to read from stdin");
    }
};

//==============================================================================
/// Notes and automation, as described by the --score JSON format. Times are in seconds.
struct Score
{
    struct Note
    {
        double start = 0, duration = 0, pitch = 0, velocity = 1.0;
        int32_t channel = 0;
    };

    struct Automation
    {
        double time = 0;
        std::string endpoint;
        choc::value::Value value;
        uint32_t rampFrames = 0;
        bool hasRamp = false;
    };

    /// Parses either inline JSON or the name of a file containing it.
    static Score parse (const std::string& fileOrJSON)
    {
        auto trimmed = choc::text::trim (fileOrJSON);
        std::string json;

        if (choc::text::startsWith (trimmed, "{"))
        {
            json = trimmed;
        }
        else
        {
            if (! std::filesystem::is_regular_file (trimmed))
                throw std::runtime_error ("--score: expected inline JSON or the name of a JSON file, but no file '" + trimmed + "' exists");

            json = choc::file::loadFileAsString (trimmed);
        }

        choc::value::Value root;

        try
        {
            root = choc::json::parse (json);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error (std::string ("--score: failed to parse the JSON: ") + e.what());
        }

        if (! root.isObject())
            throw std::runtime_error ("--score: expected a JSON object with \"notes\" and/or \"automation\" arrays");

        checkMembers (root, { "notes", "automation" }, "--score");

        Score score;

        if (root.hasObjectMember ("notes"))
        {
            auto notes = getArray (root, "notes", "--score");

            for (uint32_t i = 0; i < notes.size(); ++i)
            {
                auto context = "--score: notes[" + std::to_string (i) + "]";
                auto n = notes[i];

                if (! n.isObject())
                    throw std::runtime_error (context + " should be an object");

                checkMembers (n, { "start", "duration", "pitch", "velocity", "channel" }, context);

                Note note;
                note.start    = getTime (n, "start", context);
                note.duration = getTime (n, "duration", context);
                note.pitch    = getNumber (n, "pitch", context);
                note.velocity = n.hasObjectMember ("velocity") ? getNumber (n, "velocity", context) : 1.0;

                if (n.hasObjectMember ("channel"))
                    note.channel = static_cast<int32_t> (getWholeNumber (n, "channel", context));

                score.notes.push_back (note);
            }
        }

        if (root.hasObjectMember ("automation"))
        {
            auto automation = getArray (root, "automation", "--score");

            for (uint32_t i = 0; i < automation.size(); ++i)
            {
                auto context = "--score: automation[" + std::to_string (i) + "]";
                auto a = automation[i];

                if (! a.isObject())
                    throw std::runtime_error (context + " should be an object");

                checkMembers (a, { "time", "endpoint", "value", "rampFrames" }, context);

                Automation item;
                item.time = getTime (a, "time", context);

                if (! (a.hasObjectMember ("endpoint") && a["endpoint"].isString()))
                    throw std::runtime_error (context + " needs an \"endpoint\" name");

                item.endpoint = a["endpoint"].toString();

                if (! a.hasObjectMember ("value"))
                    throw std::runtime_error (context + " needs a \"value\"");

                item.value = choc::value::Value (a["value"]);

                if (a.hasObjectMember ("rampFrames"))
                {
                    item.rampFrames = static_cast<uint32_t> (getWholeNumber (a, "rampFrames", context));
                    item.hasRamp = true;
                }

                score.automation.push_back (std::move (item));
            }
        }

        return score;
    }

    /// The time in seconds at which the last note ends, or the last automation happens.
    double getEndTime() const
    {
        double end = 0;

        for (auto& n : notes)
            end = std::max (end, n.start + n.duration);

        for (auto& a : automation)
            end = std::max (end, a.time);

        return end;
    }

    std::vector<Note> notes;
    std::vector<Automation> automation;

private:
    static void checkMembers (const choc::value::ValueView& object,
                              std::initializer_list<std::string_view> allowedNames,
                              const std::string& context)
    {
        for (uint32_t i = 0; i < object.size(); ++i)
        {
            auto name = std::string_view (object.getObjectMemberAt (i).name);

            if (std::find (allowedNames.begin(), allowedNames.end(), name) == allowedNames.end())
            {
                std::vector<std::string> names (allowedNames.begin(), allowedNames.end());
                throw std::runtime_error (context + ": unknown property \"" + std::string (name)
                                            + "\" (expected " + choc::text::joinStrings (names, ", ") + ")");
            }
        }
    }

    static choc::value::ValueView getArray (const choc::value::ValueView& object, std::string_view name, const std::string& context)
    {
        auto v = object[name];

        if (! v.isArray())
            throw std::runtime_error (context + ": \"" + std::string (name) + "\" should be an array");

        return v;
    }

    static double getNumber (const choc::value::ValueView& object, std::string_view name, const std::string& context)
    {
        if (! object.hasObjectMember (name))
            throw std::runtime_error (context + " needs a \"" + std::string (name) + "\" value");

        auto v = object[name];

        if (! (v.isFloat() || v.isInt()))
            throw std::runtime_error (context + ": \"" + std::string (name) + "\" should be a number");

        return v.get<double>();
    }

    static double getTime (const choc::value::ValueView& object, std::string_view name, const std::string& context)
    {
        auto t = getNumber (object, name, context);

        if (t < 0)
            throw std::runtime_error (context + ": \"" + std::string (name) + "\" can't be negative");

        return t;
    }

    static int64_t getWholeNumber (const choc::value::ValueView& object, std::string_view name, const std::string& context)
    {
        auto n = getNumber (object, name, context);

        if (n < 0 || n != std::floor (n))
            throw std::runtime_error (context + ": \"" + std::string (name) + "\" should be a whole number, zero or above");

        return static_cast<int64_t> (n);
    }
};

//==============================================================================
/// Owns the engine, performer, files and schedule for one `cmaj run`, and renders it.
struct RunState
{
    RunState (const RunOptions& runOptions,
              const choc::value::Value& engineOptions,
              const cmaj::BuildSettings& buildSettings,
              std::ostream& diagnosticStream)
        : options (runOptions), diagnostics (diagnosticStream)
    {
        openInputFiles();
        chooseSampleRate();
        loadScoreAndMIDI();
        chooseLength();
        compile (engineOptions, buildSettings);
        bindStreams();
        bindEventsAndValues();
        scheduleInputs();
        link();
        openOutputFiles();
    }

    /// Renders the whole program. For ndjson, the lines are written to the stream as
    /// they happen; otherwise the JSON report is written at the end. Either way, the
    /// complete report is left in the `report` member.
    void render (std::ostream& reportStream)
    {
        reportOutput = std::addressof (reportStream);
        uint64_t framesDone = 0;
        size_t nextInput = 0;

        while (framesDone < framesToRender)
        {
            // Inputs scheduled at or before this frame go into this block, and the block
            // stops short of the next one, so that every input lands on its exact frame.
            auto firstInputAfterThisBlock = nextInput;

            while (firstInputAfterThisBlock < schedule.size() && schedule[firstInputAfterThisBlock].frame <= framesDone)
                ++firstInputAfterThisBlock;

            auto blockEnd = std::min (framesDone + blockSize, framesToRender);

            if (firstInputAfterThisBlock < schedule.size())
                blockEnd = std::min (blockEnd, schedule[firstInputAfterThisBlock].frame);

            auto numFrames = static_cast<uint32_t> (blockEnd - framesDone);

            performer.setBlockSize (numFrames);
            provideInputStreams (framesDone, numFrames);

            for (; nextInput < firstInputAfterThisBlock; ++nextInput)
                sendInput (schedule[nextInput]);

            advance();
            writeOutputStreams (numFrames);
            collectOutputEvents (framesDone);
            framesDone = blockEnd;
        }

        if (flushHandle)
        {
            // A single frame is enough for the flush event's handler to run and for
            // anything it emits to be captured, without rendering past the end.
            performer.setBlockSize (1);
            provideInputStreams (framesToRender, 1);
            sendInput (ScheduledInput { framesToRender, 0, *flushHandle, "flush", true, choc::value::Value(), {}, 0 });
            advance();
            collectOutputEvents (framesToRender);
        }

        for (auto& stream : outputStreams)
            if (stream.writer != nullptr)
                if (! stream.writer->flush())
                    throw std::runtime_error ("Failed to write to output file " + stream.file);

        buildReport();

        if (options.useNDJSON)
            writeNDJSONLine (createEndLine());
        else
            reportStream << choc::json::toString (report, true) << std::endl;

        if (! options.quiet)
            diagnostics << "Rendered " << framesToRender << " frames (" << (static_cast<double> (framesToRender) / sampleRate)
                        << " seconds) of " << mainProcessorName << " at " << sampleRate << " Hz" << std::endl;
    }

    double sampleRate = 0;
    uint64_t framesToRender = 0;
    std::string mainProcessorName;
    choc::value::Value report;

private:
    //==============================================================================
    struct InputFile
    {
        std::string endpointName, path;
        std::unique_ptr<choc::audio::AudioFileReader> reader;
        choc::audio::AudioFileProperties properties;
    };

    /// The frame layout of an audio stream: a float32 or float64 scalar or vector.
    struct AudioFrameFormat
    {
        static std::optional<AudioFrameFormat> forType (const choc::value::Type& frameType)
        {
            auto elementType = frameType.isVector() ? frameType.getElementType() : frameType;

            if (! (elementType.isFloat32() || elementType.isFloat64()))
                return {};

            return AudioFrameFormat { frameType.isVector() ? frameType.getNumElements() : 1u,
                                      elementType.isFloat64() };
        }

        uint32_t numChannels = 0;
        bool isFloat64 = false;
    };

    template <typename SampleType>
    struct StreamBuffers
    {
        void allocate (uint32_t numChannels, uint32_t numFrames)
        {
            channels.resize ({ numChannels, numFrames });
            interleaved.resize ({ numChannels, numFrames });
        }

        choc::buffer::ChannelArrayBuffer<SampleType> channels;
        choc::buffer::InterleavedBuffer<SampleType> interleaved;
    };

    struct InputStream
    {
        cmaj::EndpointDetails details;
        cmaj::EndpointHandle handle = {};
        std::optional<AudioFrameFormat> format;
        InputFile* file = nullptr;
        StreamBuffers<float> floatBuffers;
        StreamBuffers<double> doubleBuffers;
        std::vector<std::byte> silence;
    };

    struct OutputStream
    {
        cmaj::EndpointDetails details;
        cmaj::EndpointHandle handle = {};
        AudioFrameFormat format;
        std::string file;
        std::unique_ptr<choc::audio::AudioFileWriter> writer;
        StreamBuffers<float> floatBuffers;
        StreamBuffers<double> doubleBuffers;
    };

    struct EventOutput
    {
        std::string endpointID;
        cmaj::EndpointHandle handle = {};
        bool isConsole = false;
        choc::value::Value items = choc::value::createEmptyArray();
        uint64_t count = 0;
    };

    struct ValueOutput
    {
        std::string endpointID;
        cmaj::EndpointHandle handle = {};
    };

    /// An event or value to send to an input endpoint at a given frame.
    struct ScheduledInput
    {
        uint64_t frame = 0;
        int order = 0;   // breaks ties between inputs at the same frame: lower goes first
        cmaj::EndpointHandle handle = {};
        std::string endpointID;
        bool isEvent = true;
        choc::value::Value value;
        std::optional<uint32_t> exactTypeIndex;   // if set, value already has this event type exactly
        uint32_t framesToReachValue = 0;
    };

    /// The order of inputs that happen on the same frame: parameters are set before notes
    /// start, and notes end before new ones begin.
    static constexpr int setOrder = 0, automationOrder = 1, noteOffOrder = 2, noteOnOrder = 3;

    struct NoteTarget
    {
        cmaj::EndpointHandle handle = {};
        std::string endpointID;
        uint32_t typeIndex = 0;
        choc::value::Type type;
    };

    struct PendingEvent
    {
        uint64_t frame;
        EventOutput* output;
        choc::value::Value value;
    };

    //==============================================================================
    RunOptions options;
    std::ostream& diagnostics;
    std::ostream* reportOutput = nullptr;
    uint32_t blockSize = 512;

    std::vector<std::unique_ptr<InputFile>> inputFiles;
    std::optional<Score> score;
    std::vector<std::pair<double, choc::midi::ShortMessage>> midiEvents;

    cmaj::Engine engine;
    cmaj::Performer performer;
    cmaj::EndpointTypeCoercionHelperList coercion;
    cmaj::EndpointDetailsList inputEndpoints, outputEndpoints;

    std::vector<InputStream> inputStreams;
    std::vector<OutputStream> outputStreams;
    std::vector<std::unique_ptr<EventOutput>> eventOutputs;
    std::vector<ValueOutput> valueOutputs;
    std::optional<cmaj::EndpointHandle> flushHandle;
    std::vector<ScheduledInput> schedule;
    std::vector<PendingEvent> pendingEvents;

    //==============================================================================
    void note (const std::string& message)
    {
        if (! options.quiet)
            diagnostics << message << std::endl;
    }

    void openInputFiles()
    {
        for (auto& input : options.inputs)
        {
            auto file = std::make_unique<InputFile>();
            file->endpointName = input.name;
            file->path = input.value;

            if (! std::filesystem::is_regular_file (file->path))
                throw std::runtime_error ("--input: file not found: " + file->path);

            file->reader = cmaj::audio_utils::createFileReader (file->path);

            if (file->reader == nullptr)
                throw std::runtime_error ("--input: couldn't read " + file->path + " as an audio file");

            file->properties = file->reader->getProperties();
            inputFiles.push_back (std::move (file));
        }
    }

    static std::string describeRate (double rate)
    {
        return choc::text::floatToString (rate, -1, true) + " Hz";
    }

    void chooseSampleRate()
    {
        for (auto& file : inputFiles)
        {
            auto fileRate = file->properties.sampleRate;

            if (options.sampleRate.has_value())
            {
                if (fileRate != *options.sampleRate)
                    throw std::runtime_error ("--input: " + file->path + " has a sample rate of " + describeRate (fileRate)
                                                + ", but --rate is " + describeRate (*options.sampleRate)
                                                + ". Files aren't resampled, so the rates must match");
            }
            else if (sampleRate == 0)
            {
                sampleRate = fileRate;
            }
            else if (fileRate != sampleRate)
            {
                throw std::runtime_error ("--input: " + file->path + " has a sample rate of " + describeRate (fileRate)
                                            + ", but " + inputFiles.front()->path + " is " + describeRate (sampleRate)
                                            + ". Files aren't resampled, so the rates must match");
            }
        }

        if (options.sampleRate.has_value())
            sampleRate = *options.sampleRate;

        if (! (sampleRate > 0))
            throw std::runtime_error ("No sample rate was given: use --rate=<rate>, or give an --input file to take the rate from");
    }

    void loadScoreAndMIDI()
    {
        if (! options.score.empty())
            score = Score::parse (options.score);

        if (! options.midiFile.empty())
        {
            if (! std::filesystem::is_regular_file (options.midiFile))
                throw std::runtime_error ("--midi: file not found: " + options.midiFile);

            try
            {
                auto content = choc::file::loadFileAsString (options.midiFile);

                choc::midi::File midi;
                midi.load (content.data(), content.size());

                for (auto& e : midi.toSequence().events)
                {
                    if (e.message.isShortMessage())
                    {
                        auto bytes = e.message.data();
                        auto length = e.message.length();

                        midiEvents.push_back ({ e.timeStamp,
                                                choc::midi::ShortMessage (static_cast<uint8_t> (bytes[0]),
                                                                          static_cast<uint8_t> (length > 1 ? bytes[1] : 0),
                                                                          static_cast<uint8_t> (length > 2 ? bytes[2] : 0)) });
                    }
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error ("--midi: failed to load " + options.midiFile + ": " + e.what());
            }
        }
    }

    void chooseLength()
    {
        blockSize = options.blockSize;

        if (options.length.has_value())
        {
            framesToRender = options.length->toFrames (sampleRate);
        }
        else if (! inputFiles.empty())
        {
            for (auto& file : inputFiles)
                framesToRender = std::max (framesToRender, file->properties.numFrames);
        }
        else if (score.has_value() || ! options.midiFile.empty())
        {
            double endTime = score.has_value() ? score->getEndTime() : 0.0;

            for (auto& e : midiEvents)
                endTime = std::max (endTime, e.first);

            framesToRender = secondsToFrames (endTime + 1.0);
        }
        else
        {
            throw std::runtime_error ("Can't tell how long to render: use --length=<frames|Ns|Nms>, "
                                      "or give an --input, --score or --midi to take the length from");
        }

        if (framesToRender == 0)
            throw std::runtime_error ("Nothing to render: the length works out as zero frames");
    }

    uint64_t secondsToFrames (double seconds) const
    {
        return static_cast<uint64_t> (std::llround (seconds * sampleRate));
    }

    //==============================================================================
    void compile (const choc::value::Value& engineOptions, cmaj::BuildSettings buildSettings)
    {
        auto engineType = engineOptions.isObject() && engineOptions.hasObjectMember ("engine")
                            ? engineOptions["engine"].toString() : std::string();

        engine = cmaj::Engine::create (engineType, std::addressof (engineOptions));

        if (engine == nullptr)
            throw std::runtime_error ("Failed to create an engine" + (engineType.empty() ? std::string() : " of type '" + engineType + "'"));

        buildSettings.setFrequency (sampleRate)
                     .setMaxFrequency (std::max (sampleRate, cmaj::BuildSettings::defaultMaxFrequency))
                     .setMaxBlockSize (blockSize);

        // A processor emitting an event on every frame needs room for a block's worth
        auto settings = buildSettings.getValue();

        if (! (settings.isObject() && settings.hasObjectMember ("eventBufferSize")))
            buildSettings.setEventBufferSize (std::max (blockSize, cmaj::BuildSettings::defaultEventBufferSize));

        if (! options.mainProcessor.empty())
            buildSettings.setMainProcessor (options.mainProcessor);

        engine.setBuildSettings (buildSettings);

        cmaj::DiagnosticMessageList messages;
        cmaj::Program program;

        for (auto& source : options.sources)
            if (! program.parse (messages, source.filename, source.code))
                throw std::runtime_error (messages.toString());

        std::vector<std::string> missingExternals;
        std::vector<bool> externalsUsed (options.externals.size(), false);
        std::string externalError;

        // This is called from inside the engine, so rather than throwing through it,
        // any problem is noted and reported once load() returns
        auto loaded = engine.load (messages, program,
                                   [&] (const cmaj::ExternalVariable& v) -> choc::value::Value
                                   {
                                       for (size_t i = 0; i < options.externals.size(); ++i)
                                       {
                                           auto& name = options.externals[i].name;

                                           if (v.name == name || choc::text::endsWith (v.name, "::" + name))
                                           {
                                               externalsUsed[i] = true;

                                               try
                                               {
                                                   return resolveExternal (v, options.externals[i].value);
                                               }
                                               catch (const std::exception& e)
                                               {
                                                   if (externalError.empty())
                                                       externalError = e.what();

                                                   return {};
                                               }
                                           }
                                       }

                                       missingExternals.push_back (v.name);
                                       return {};
                                   },
                                   {});

        if (! externalError.empty())
            throw std::runtime_error (externalError);

        if (! missingExternals.empty())
            throw std::runtime_error ("The program needs values for these externals: " + choc::text::joinStrings (missingExternals, ", ")
                                        + ". Use --external=<name>=<value>, where the value is an audio file, @file.json or a JSON literal");

        if (! loaded || messages.hasErrors())
            throw std::runtime_error (messages.toString());

        if (! messages.empty())
            diagnostics << messages.toString() << std::endl;

        for (size_t i = 0; i < options.externals.size(); ++i)
            if (! externalsUsed[i])
                throw std::runtime_error ("--external: the program has no external variable called '" + options.externals[i].name + "'");

        mainProcessorName = engine.getProgramDetails()["mainProcessor"].toString();
        inputEndpoints = engine.getInputEndpoints();
        outputEndpoints = engine.getOutputEndpoints();

        coercion.initialise (engine, blockSize, true, true);
    }

    /// Turns the text of an --external value into a value: @file.json, an audio file, or a JSON literal.
    static choc::value::Value resolveExternal (const cmaj::ExternalVariable& v, const std::string& text)
    {
        auto context = "--external=" + v.name + ": ";

        if (choc::text::startsWith (text, "@"))
        {
            auto file = text.substr (1);

            if (! std::filesystem::is_regular_file (file))
                throw std::runtime_error (context + "file not found: " + file);

            return parseJSONValue (choc::file::loadFileAsString (file), context + "failed to parse " + file + ": ");
        }

        if (std::filesystem::is_regular_file (text))
        {
            choc::value::Value result;
            auto error = cmaj::readAudioFileAsValue (result, cmaj::audio_utils::getAudioFormatListForReading(),
                                                     std::make_shared<std::ifstream> (text, std::ios::binary | std::ios::in),
                                                     v.annotation);

            if (! error.empty())
                throw std::runtime_error (context + "couldn't read " + text + " as an audio file: " + error);

            return result;
        }

        return parseJSONValue (text, context + "the value is neither an existing file nor valid JSON: ");
    }

    static choc::value::Value parseJSONValue (const std::string& text, const std::string& errorPrefix)
    {
        try
        {
            return choc::json::parseValue (text);
        }
        catch (const std::exception& e)
        {
            throw std::runtime_error (errorPrefix + e.what());
        }
    }

    //==============================================================================
    static std::string listEndpoints (const cmaj::EndpointDetailsList& list,
                                      const std::function<bool(const cmaj::EndpointDetails&)>& include)
    {
        std::vector<std::string> names;

        for (auto& e : list)
            if (include (e))
                names.push_back ("'" + e.endpointID.toString() + "'");

        if (names.empty())
            return "(none)";

        return choc::text::joinStrings (names, ", ");
    }

    template <typename StreamList>
    static size_t findStream (const StreamList& streams, const std::string& name,
                              const std::string& option, const std::string& kind,
                              const std::string& availableNames)
    {
        if (name.empty())
        {
            if (streams.size() == 1)
                return 0;

            if (streams.empty())
                throw std::runtime_error (option + ": the program has no " + kind + "s");

            throw std::runtime_error (option + ": the program has " + std::to_string (streams.size()) + " " + kind
                                        + "s, so say which one with " + option + "=<name>=<file>. Available " + kind + "s: " + availableNames);
        }

        for (size_t i = 0; i < streams.size(); ++i)
            if (streams[i].details.endpointID.toString() == name)
                return i;

        throw std::runtime_error (option + ": there's no " + kind + " called '" + name + "'. Available " + kind + "s: " + availableNames);
    }

    void bindStreams()
    {
        for (auto& e : inputEndpoints)
        {
            if (e.isStream())
            {
                InputStream stream;
                stream.details = e;
                stream.handle = engine.getEndpointHandle (e.endpointID.toString().c_str());
                stream.format = AudioFrameFormat::forType (e.dataTypes.front());
                inputStreams.push_back (std::move (stream));
            }
        }

        for (auto& e : outputEndpoints)
        {
            if (e.isStream())
            {
                OutputStream stream;
                stream.details = e;
                stream.handle = engine.getEndpointHandle (e.endpointID.toString().c_str());

                if (auto format = AudioFrameFormat::forType (e.dataTypes.front()))
                    stream.format = *format;

                outputStreams.push_back (std::move (stream));
            }
        }

        auto inputStreamNames  = listEndpoints (inputEndpoints,  [] (auto& e) { return e.isStream(); });
        auto outputStreamNames = listEndpoints (outputEndpoints, [] (auto& e) { return e.isStream(); });

        for (auto& file : inputFiles)
        {
            auto& stream = inputStreams[findStream (inputStreams, file->endpointName, "--input", "input stream", inputStreamNames)];
            auto name = stream.details.endpointID.toString();

            if (stream.file != nullptr)
                throw std::runtime_error ("--input: the input stream '" + name + "' was given more than one file");

            if (! stream.format.has_value())
                throw std::runtime_error ("--input: the input stream '" + name + "' has type " + stream.details.dataTypes.front().getDescription()
                                            + ", but audio files can only be fed to float32 or float64 streams");

            if (file->properties.numChannels != stream.format->numChannels)
                throw std::runtime_error ("--input: " + file->path + " has " + std::to_string (file->properties.numChannels)
                                            + " channel(s), but the input stream '" + name + "' has "
                                            + std::to_string (stream.format->numChannels) + " channel(s). The channel counts must match");

            stream.file = file.get();
        }

        for (auto& stream : inputStreams)
            if (stream.file == nullptr)
                note ("Note: the input stream '" + stream.details.endpointID.toString() + "' has no --input file, so it will receive silence");

        for (auto& output : options.outputs)
        {
            auto& stream = outputStreams[findStream (outputStreams, output.name, "--output", "output stream", outputStreamNames)];
            auto name = stream.details.endpointID.toString();

            if (! stream.file.empty())
                throw std::runtime_error ("--output: the output stream '" + name + "' was given more than one file");

            if (stream.format.numChannels == 0)
                throw std::runtime_error ("--output: the output stream '" + name + "' has type " + stream.details.dataTypes.front().getDescription()
                                            + ", but only float32 or float64 streams can be written to audio files");

            if (choc::text::toLowerCase (std::filesystem::path (output.value).extension().string()) != ".wav")
                throw std::runtime_error ("--output: " + output.value + " should be a .wav file");

            stream.file = output.value;
        }
    }

    void bindEventsAndValues()
    {
        for (auto& e : outputEndpoints)
        {
            auto handle = engine.getEndpointHandle (e.endpointID.toString().c_str());

            if (e.isEvent())
            {
                auto output = std::make_unique<EventOutput>();
                output->endpointID = e.endpointID.toString();
                output->handle = handle;
                output->isConsole = e.isConsole();
                eventOutputs.push_back (std::move (output));
            }
            else if (e.isValue())
            {
                valueOutputs.push_back ({ e.endpointID.toString(), handle });
            }
        }

        if (options.sendFlush)
            for (auto& e : inputEndpoints)
                if (e.isEvent() && e.endpointID.toString() == "flush")
                    for (auto& type : e.dataTypes)
                        if (type.isVoid())
                            flushHandle = engine.getEndpointHandle ("flush");
    }

    //==============================================================================
    const cmaj::EndpointDetails& findEventOrValueInput (const std::string& name, const std::string& option)
    {
        for (auto& e : inputEndpoints)
        {
            if (e.endpointID.toString() == name)
            {
                if (e.isStream())
                    throw std::runtime_error (option + ": '" + name + "' is an input stream, which can only be fed with --input. Available event and value inputs: "
                                                + listEndpoints (inputEndpoints, [] (auto& d) { return ! d.isStream(); }));

                return e;
            }
        }

        throw std::runtime_error (option + ": there's no input event or value called '" + name + "'. Available event and value inputs: "
                                    + listEndpoints (inputEndpoints, [] (auto& d) { return ! d.isStream(); }));
    }

    void addScheduledValue (uint64_t frame, int order, const cmaj::EndpointDetails& endpoint,
                            choc::value::Value value, uint32_t framesToReachValue, const std::string& context)
    {
        auto id = endpoint.endpointID.toString();
        auto handle = engine.getEndpointHandle (id.c_str());

        // check now that the value fits the endpoint, so a mistake is reported before rendering
        auto fits = endpoint.isEvent() ? static_cast<bool> (coercion.coerceValueToMatchingType (handle, value, cmaj::EndpointType::event))
                                       : static_cast<bool> (coercion.coerceValue (handle, value));

        // the coercion happily turns an object or array into zero for a primitive type,
        // which would hide a mistake, so that's rejected here
        auto isAggregate = [] (const choc::value::Type& t) { return t.isObject() || t.isArray() || t.isVector(); };

        if (isAggregate (value.getType())
             && std::none_of (endpoint.dataTypes.begin(), endpoint.dataTypes.end(), isAggregate))
            fits = false;

        if (! fits)
        {
            std::vector<std::string> types;

            for (auto& t : endpoint.dataTypes)
                types.push_back (t.getDescription());

            throw std::runtime_error (context + ": can't convert " + choc::json::toString (value) + " to the type of '"
                                        + id + "', which is " + choc::text::joinStrings (types, " or "));
        }

        schedule.push_back ({ frame, order, handle, id, endpoint.isEvent(), std::move (value), {}, framesToReachValue });
    }

    std::optional<NoteTarget> findNoteTarget (std::string_view typeName)
    {
        for (auto& e : inputEndpoints)
        {
            if (e.isEvent())
            {
                for (uint32_t i = 0; i < e.dataTypes.size(); ++i)
                {
                    auto& type = e.dataTypes[i];

                    if (type.isObject() && choc::text::endsWith (type.getObjectClassName(), typeName))
                        return NoteTarget { engine.getEndpointHandle (e.endpointID.toString().c_str()),
                                            e.endpointID.toString(), i, type };
                }
            }
        }

        return {};
    }

    const cmaj::EndpointDetails* findMIDIInput() const
    {
        for (auto& e : inputEndpoints)
            if (e.isMIDI())
                return std::addressof (e);

        return nullptr;
    }

    /// Creates an event with exactly the type of a NoteOn or NoteOff struct, filling in
    /// whichever of its channel, pitch and velocity members it has.
    static choc::value::Value createNoteEvent (const choc::value::Type& type, int32_t channel, double pitch, double velocity)
    {
        choc::value::Value event (type);

        for (uint32_t i = 0; i < type.getNumElements(); ++i)
        {
            auto& memberName = type.getObjectMember (i).name;
            auto member = event[i];
            double v = 0;

            if (memberName == "channel")        v = channel;
            else if (memberName == "pitch")     v = pitch;
            else if (memberName == "velocity")  v = velocity;
            else continue;

            if (member.isFloat32())     member.set (static_cast<float> (v));
            else if (member.isFloat64()) member.set (v);
            else if (member.isInt32())  member.set (static_cast<int32_t> (v));
            else if (member.isInt64())  member.set (static_cast<int64_t> (v));
        }

        return event;
    }

    void scheduleInputs()
    {
        for (auto& set : options.sets)
            addScheduledValue (0, setOrder, findEventOrValueInput (set.name, "--set"),
                               parseJSONValue (set.value, "--set=" + set.name + ": the value isn't valid JSON: "), 0,
                               "--set=" + set.name);

        if (score.has_value())
        {
            for (size_t i = 0; i < score->automation.size(); ++i)
            {
                auto& a = score->automation[i];
                auto context = "--score: automation[" + std::to_string (i) + "]";
                auto& endpoint = findEventOrValueInput (a.endpoint, context);

                if (a.hasRamp && ! endpoint.isValue())
                    throw std::runtime_error (context + ": rampFrames can only be used with value endpoints, and '" + a.endpoint + "' is an event");

                addScheduledValue (secondsToFrames (a.time), automationOrder, endpoint, a.value, a.rampFrames, context);
            }

            if (! score->notes.empty())
                scheduleNotes();
        }

        if (! midiEvents.empty())
        {
            auto midiInput = findMIDIInput();

            if (midiInput == nullptr)
                throw std::runtime_error ("--midi: the program has no MIDI input (an input event endpoint of type std::midi::Message). Available input events: "
                                            + listEndpoints (inputEndpoints, [] (auto& e) { return e.isEvent(); }));

            for (auto& e : midiEvents)
                addScheduledValue (secondsToFrames (e.first), noteOnOrder, *midiInput,
                                   cmaj::MIDIEvents::createMIDIMessageObject (e.second), 0, "--midi");
        }

        std::stable_sort (schedule.begin(), schedule.end(), [] (const ScheduledInput& a, const ScheduledInput& b)
        {
            return a.frame != b.frame ? a.frame < b.frame : a.order < b.order;
        });
    }

    void scheduleNotes()
    {
        if (auto noteOn = findNoteTarget ("NoteOn"))
        {
            auto noteOff = findNoteTarget ("NoteOff");

            if (! noteOff)
                note ("Note: the program has no input for std::notes::NoteOff, so the score's notes will never end");

            for (auto& n : score->notes)
            {
                schedule.push_back ({ secondsToFrames (n.start), noteOnOrder, noteOn->handle, noteOn->endpointID, true,
                                      createNoteEvent (noteOn->type, n.channel, n.pitch, n.velocity), noteOn->typeIndex, 0 });

                if (noteOff)
                    schedule.push_back ({ secondsToFrames (n.start + n.duration), noteOffOrder, noteOff->handle, noteOff->endpointID, true,
                                          createNoteEvent (noteOff->type, n.channel, n.pitch, 0.0), noteOff->typeIndex, 0 });
            }

            return;
        }

        auto midiInput = findMIDIInput();

        if (midiInput == nullptr)
            throw std::runtime_error ("--score: the score has notes, but the program has no input event accepting std::notes::NoteOn, "
                                      "and no MIDI input. Available input events: "
                                        + listEndpoints (inputEndpoints, [] (auto& e) { return e.isEvent(); }));

        for (size_t i = 0; i < score->notes.size(); ++i)
        {
            auto& n = score->notes[i];
            auto context = "--score: notes[" + std::to_string (i) + "]";

            if (n.pitch != std::floor (n.pitch) || n.pitch < 0 || n.pitch > 127)
                throw std::runtime_error (context + ": the program only has a MIDI input, so the pitch must be a whole number from 0 to 127");

            if (n.channel > 15)
                throw std::runtime_error (context + ": the program only has a MIDI input, so the channel must be from 0 to 15");

            auto noteNumber = static_cast<uint8_t> (n.pitch);
            auto velocity = static_cast<uint8_t> (std::clamp (std::lround (n.velocity * 127.0), 1L, 127L));
            auto channel = static_cast<uint8_t> (n.channel);

            addScheduledValue (secondsToFrames (n.start), noteOnOrder, *midiInput,
                               cmaj::MIDIEvents::createMIDIMessageObject (choc::midi::ShortMessage (static_cast<uint8_t> (0x90 | channel), noteNumber, velocity)),
                               0, context);

            addScheduledValue (secondsToFrames (n.start + n.duration), noteOffOrder, *midiInput,
                               cmaj::MIDIEvents::createMIDIMessageObject (choc::midi::ShortMessage (static_cast<uint8_t> (0x80 | channel), noteNumber, 0)),
                               0, context);
        }
    }

    //==============================================================================
    void link()
    {
        cmaj::DiagnosticMessageList messages;

        if (! engine.link (messages) || messages.hasErrors())
            throw std::runtime_error (messages.toString());

        performer = engine.createPerformer();

        if (performer == nullptr)
            throw std::runtime_error ("Failed to create a performer");

        coercion.initialiseDictionary (performer);
    }

    void openOutputFiles()
    {
        for (auto& stream : inputStreams)
        {
            if (stream.file != nullptr)
            {
                if (stream.format->isFloat64)
                    stream.doubleBuffers.allocate (stream.format->numChannels, blockSize);
                else
                    stream.floatBuffers.allocate (stream.format->numChannels, blockSize);
            }
            else
            {
                stream.silence.resize (stream.details.dataTypes.front().getValueDataSize() * blockSize);
            }
        }

        for (auto& stream : outputStreams)
        {
            if (! stream.file.empty())
            {
                stream.writer = cmaj::audio_utils::createFileWriter (stream.file, sampleRate, stream.format.numChannels);

                if (stream.writer == nullptr)
                    throw std::runtime_error ("--output: couldn't create " + stream.file);

                if (stream.format.isFloat64)
                    stream.doubleBuffers.allocate (stream.format.numChannels, blockSize);
                else
                    stream.floatBuffers.allocate (stream.format.numChannels, blockSize);
            }
        }
    }

    //==============================================================================
    template <typename SampleType>
    void readInputFile (InputStream& stream, StreamBuffers<SampleType>& buffers, uint64_t startFrame, uint32_t numFrames)
    {
        auto channels = buffers.channels.getView().getStart (numFrames);
        channels.clear();

        // past the end of the file, the stream gets silence
        auto fileLength = stream.file->properties.numFrames;

        if (startFrame < fileLength)
        {
            auto framesToRead = static_cast<uint32_t> (std::min<uint64_t> (numFrames, fileLength - startFrame));

            if (! stream.file->reader->readFrames (startFrame, channels.getStart (framesToRead)))
                throw std::runtime_error ("Failed to read from " + stream.file->path);
        }

        auto interleaved = buffers.interleaved.getView().getStart (numFrames);
        choc::buffer::copy (interleaved, channels);
        performer.setInputFrames (stream.handle, interleaved);
    }

    void provideInputStreams (uint64_t startFrame, uint32_t numFrames)
    {
        for (auto& stream : inputStreams)
        {
            if (stream.file == nullptr)
                performer.setInputFrames (stream.handle, stream.silence.data(), numFrames);
            else if (stream.format->isFloat64)
                readInputFile (stream, stream.doubleBuffers, startFrame, numFrames);
            else
                readInputFile (stream, stream.floatBuffers, startFrame, numFrames);
        }
    }

    void sendInput (const ScheduledInput& input)
    {
        if (input.exactTypeIndex.has_value())
        {
            performer.addInputEvent (input.handle, *input.exactTypeIndex, input.value);
            return;
        }

        static constexpr int32_t emptyEventData = 0;

        if (input.isEvent)
        {
            auto coerced = coercion.coerceValueToMatchingType (input.handle, input.value, cmaj::EndpointType::event);

            if (! coerced)
                throw std::runtime_error ("Failed to send an event to '" + input.endpointID + "'");

            // a void event has no data, but the performer still wants a pointer
            auto data = coerced.data.data != nullptr ? coerced.data.data : static_cast<const void*> (std::addressof (emptyEventData));
            performer.addInputEvent (input.handle, coerced.typeIndex, data);
        }
        else
        {
            auto coerced = coercion.coerceValue (input.handle, input.value);

            if (! coerced)
                throw std::runtime_error ("Failed to send a value to '" + input.endpointID + "'");

            performer.setInputValue (input.handle, coerced.data, input.framesToReachValue);
        }
    }

    void advance()
    {
        if (performer.advance() != cmaj::Result::Ok)
            throw std::runtime_error ("The performer failed to render a block");

        if (auto error = performer.getRuntimeError())
            throw std::runtime_error (std::string ("Runtime error: ") + error);
    }

    template <typename SampleType>
    void writeOutputFile (OutputStream& stream, StreamBuffers<SampleType>& buffers, uint32_t numFrames)
    {
        auto interleaved = buffers.interleaved.getView().getStart (numFrames);
        performer.copyOutputFrames (stream.handle, interleaved);

        auto channels = buffers.channels.getView().getStart (numFrames);
        choc::buffer::copy (channels, interleaved);

        if (! stream.writer->appendFrames (channels))
            throw std::runtime_error ("Failed to write to " + stream.file);
    }

    void writeOutputStreams (uint32_t numFrames)
    {
        for (auto& stream : outputStreams)
        {
            if (stream.writer != nullptr)
            {
                if (stream.format.isFloat64)
                    writeOutputFile (stream, stream.doubleBuffers, numFrames);
                else
                    writeOutputFile (stream, stream.floatBuffers, numFrames);
            }
        }
    }

    //==============================================================================
    void collectOutputEvents (uint64_t blockStartFrame)
    {
        pendingEvents.clear();

        for (auto& output : eventOutputs)
        {
            performer.iterateOutputEvents (output->handle,
                                           [&] (cmaj::EndpointHandle handle, uint32_t typeIndex, uint32_t frameOffset,
                                                const void* data, uint32_t size) -> bool
            {
                auto bytes = static_cast<const uint8_t*> (data);
                auto& view = coercion.getViewForOutputData (handle, typeIndex, { bytes, bytes + size });

                auto value = output->isConsole ? choc::value::Value (cmaj::convertConsoleMessageToString (view))
                                               : choc::value::Value (view);

                pendingEvents.push_back ({ blockStartFrame + frameOffset, output.get(), std::move (value) });
                return true;
            });
        }

        // keep events from different endpoints in time order, for the ndjson stream
        std::stable_sort (pendingEvents.begin(), pendingEvents.end(),
                          [] (const PendingEvent& a, const PendingEvent& b) { return a.frame < b.frame; });

        for (auto& e : pendingEvents)
        {
            auto& output = *e.output;
            ++output.count;

            if (output.count > options.maxEvents)
                continue;

            auto item = choc::json::create ("frame", static_cast<int64_t> (e.frame),
                                            "time", static_cast<double> (e.frame) / sampleRate);

            if (! e.value.isVoid())
                item.addMember ("value", e.value);

            output.items.addArrayElement (item);

            if (options.useNDJSON)
            {
                auto line = choc::json::create ("type", output.isConsole ? "console" : "event");

                if (! output.isConsole)
                    line.addMember ("endpoint", output.endpointID);

                for (uint32_t i = 0; i < item.size(); ++i)
                {
                    auto member = item.getObjectMemberAt (i);
                    line.addMember (member.name, member.value);
                }

                writeNDJSONLine (line);
            }
        }

        if (options.useNDJSON && ! pendingEvents.empty())
            reportOutput->flush();
    }

    void writeNDJSONLine (const choc::value::ValueView& line)
    {
        *reportOutput << choc::json::toString (line) << "\n";
    }

    choc::value::Value getEventList (const EventOutput& output) const
    {
        if (output.count <= options.maxEvents)
            return output.items;

        return choc::json::create ("truncated", true,
                                   "count", static_cast<int64_t> (output.count),
                                   "items", output.items);
    }

    void buildReport()
    {
        auto values = choc::value::createObject ({});

        for (auto& output : valueOutputs)
        {
            auto view = coercion.getViewForOutputArray (output.handle, cmaj::EndpointType::value);
            performer.copyOutputValue (output.handle, const_cast<void*> (view.getRawData()));
            values.addMember (output.endpointID, choc::value::Value (view));
        }

        auto events = choc::value::createObject ({});
        auto console = choc::value::createEmptyArray();

        for (auto& output : eventOutputs)
        {
            if (output->isConsole)
                console = getEventList (*output);
            else
                events.addMember (output->endpointID, getEventList (*output));
        }

        auto outputFiles = choc::value::createObject ({});

        for (auto& stream : outputStreams)
            if (! stream.file.empty())
                outputFiles.addMember (stream.details.endpointID.toString(), stream.file);

        report = choc::json::create ("sampleRate", sampleRate,
                                     "framesRendered", static_cast<int64_t> (framesToRender),
                                     "mainProcessor", mainProcessorName,
                                     "values", values,
                                     "events", events,
                                     "console", console,
                                     "outputs", outputFiles);
    }

    choc::value::Value createEndLine() const
    {
        auto line = choc::json::create ("type", "end",
                                        "sampleRate", sampleRate,
                                        "framesRendered", static_cast<int64_t> (framesToRender),
                                        "mainProcessor", mainProcessorName,
                                        "values", report["values"],
                                        "outputs", report["outputs"]);

        auto truncated = choc::value::createObject ({});

        for (auto& output : eventOutputs)
            if (output->count > options.maxEvents)
                truncated.addMember (output->isConsole ? std::string ("console") : output->endpointID,
                                     static_cast<int64_t> (output->count));

        if (truncated.size() != 0)
            line.addMember ("truncatedEventCounts", truncated);

        return line;
    }
};

} // namespace cmaj::run_command

//==============================================================================
/// The entry point for `cmaj run`.
inline void run (choc::ArgumentList& args, const choc::value::Value& engineOptions, cmaj::BuildSettings& buildSettings)
{
    cmaj::run_command::RunOptions options;
    options.parseArguments (args);

    cmaj::run_command::RunState state (options, engineOptions, buildSettings, std::cerr);

    if (options.reportFile.empty() || options.reportFile == "-")
    {
        state.render (std::cout);
        return;
    }

    std::ofstream reportFile (options.reportFile, std::ios::binary | std::ios::out | std::ios::trunc);

    if (! reportFile)
        throw std::runtime_error ("--report: couldn't create " + options.reportFile);

    state.render (reportFile);
}
