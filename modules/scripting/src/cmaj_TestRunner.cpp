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

#include <future>
#include <iomanip>
#include <optional>

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../include/cmaj_ScriptEngine.h"
#include "choc/platform/choc_Platform.h"
#include "choc/audio/choc_MIDIFile.h"
#include "cmaj_javascript_ObjectHandle.h"
#include "cmaj_javascript_Helpers.h"

#ifdef CHOC_WINDOWS
 #include <io.h>
#else
 #include <unistd.h>
#endif


//==============================================================================
namespace cmaj::test
{
    static constexpr std::string_view thinDivider  = "--------------------------------------------------------";
    static constexpr std::string_view thickDivider = "========================================================";

    //==============================================================================
    struct TestSection
    {
        std::string header, body;
        int testNum = 1;
        int lineNum = 0;
        bool headerHasChanged = false;

        void setNewHeader (const std::string& newHeader)
        {
            if (newHeader != header)
            {
                header = newHeader;
                headerHasChanged = true;
            }
        }
    };

    //==============================================================================
    struct TestSuite
    {
        TestSuite (std::string file)
        {
            filename = file;
            scanForTests();
        }

        void runTests (std::ostream& console,
                       const cmaj::BuildSettings& buildSettings, std::optional<int> testToRun,
                       bool runDisabled, const choc::value::Value& engineOptions,
                       std::string testScriptPath);

        bool needsResaving() const
        {
            for (auto& test : tests)
                if (test.section.headerHasChanged)
                    return true;

            return false;
        }

        void resave() const
        {
            std::ostringstream oss;
            oss << userScript;

            if (! globalSource.empty())
            {
                oss << "## global" << std::endl;
                oss << globalSource;
            }

            for (auto& test : tests)
            {
                oss << test.section.header << std::endl;
                oss << test.section.body;
            }

            choc::file::replaceFileWithContent (filename, oss.str());
        }

        //==============================================================================
        struct TestCase
        {
            TestCase (TestSuite& s, TestSection&& ts)
                : suite (s), section (std::move (ts))
            {
            }

            TestCase (TestCase&&) = default;

            void start (std::ostream* consoleToUse)
            {
                console = consoleToUse;

                if (console != nullptr)
                {
                    *console << getPrefix();
                    needsNewLine = true;
                }

                startTime = std::chrono::steady_clock::now();
            }

            void end()
            {
                auto endTime = std::chrono::steady_clock::now();
                time = (endTime - startTime);
                suite.time += time;

                if (needsNewLine)
                    newline();
            }

            void printErrors (std::ostream& out)
            {
                if (! failed.empty())
                {
                    out << thinDivider << std::endl;

                    for (auto& e : errorReports)
                        out << e << std::endl;
                }
            }

            void logMessage (std::string_view comment)
            {
                writeLine (std::string (comment));
                log.emplace_back (comment);
                errorReports.emplace_back (comment);
            }

            void reportTestPassed (std::string_view comment)
            {
                suite.passed++;
                writeResult ("OK", comment);
                passed.emplace_back (comment);
            }

            void reportTestFailed (std::string_view comment)
            {
                suite.failed++;
                writeResult ("FAILED", comment);
                errorReports.emplace_back (comment);
                errorReports.emplace_back (suite.filename + ":" + std::to_string (section.lineNum) + ":1: error: Failed test");
                failed.emplace_back (comment);
            }

            void reportTestDisabled (std::string_view comment)
            {
                suite.disabled++;
                writeResult ("DISABLED", comment);
                disabled.emplace_back (comment);
            }

            void reportTestUnsupported (std::string_view comment)
            {
                suite.unsupported++;
                writeResult ("UNSUPPORTED", comment);
                unsupported.emplace_back (comment);
            }

            TestSuite& suite;
            TestSection section;
            std::vector<std::string> log, passed, failed, disabled, unsupported, errorReports;
            std::chrono::duration<double> time;

        private:
            std::ostream* console = nullptr;
            bool needsNewLine = false;
            std::chrono::steady_clock::time_point startTime;

            std::string getPrefix() const
            {
                return "Test " + std::to_string (section.testNum) + " (line " + std::to_string (section.lineNum) + ")";
            }

            void newline()
            {
                if (console != nullptr)
                {
                    *console << std::endl;
                    needsNewLine = false;
                }
            }

            void writeLine (std::string_view s)
            {
                if (console != nullptr)
                {
                    if (needsNewLine)
                        newline();

                    *console << s << std::endl;
                }
            }

            void writeResult (std::string_view result, std::string_view comment)
            {
                if (console != nullptr)
                {
                    if (! needsNewLine)
                        *console << getPrefix();

                    *console << "  " << result;

                    if (! comment.empty())
                        *console << " " << comment;

                    newline();
                }
            }
        };

        //==============================================================================
        std::string filename, userScript, globalSource;
        std::atomic<int> passed { 0 }, failed { 0 }, disabled { 0 }, unsupported { 0 };
        std::chrono::duration<double> time;
        std::vector<TestCase> tests;

    private:
        void addTest (TestSection ts)
        {
            tests.emplace_back (TestCase (*this, std::move (ts)));
        }

        void scanForTests()
        {
            auto contents = choc::file::loadFileAsString (filename);
            std::istringstream s (contents);

            TestSection testSection;

            std::ostringstream body;
            std::string line;
            int lineNum = 0;

            while (std::getline (s, line))
            {
                lineNum++;

                if (line.length() > 2 && line[0] == '#' && line[1] == '#')
                {
                    if (! testSection.header.empty())
                    {
                        if (testSection.header.substr (0, 9) == "## global")
                        {
                            if (! globalSource.empty())
                                throw std::runtime_error ("Multiple global sections declared in test file");

                            globalSource = body.str();
                        }
                        else
                        {
                            testSection.body = body.str();
                            addTest (testSection);
                            testSection.testNum++;
                        }
                    }
                    else
                    {
                        userScript = body.str();
                    }

                    testSection.header = line;
                    testSection.lineNum = lineNum;
                    body.str("");
                }
                else
                {
                    body << line << "\n";
                }
            }

            if (! testSection.header.empty())
            {
                testSection.body = body.str();
                addTest (testSection);
            }
        }
    };

    //==============================================================================
    struct ThreadPool
    {
        ThreadPool (uint32_t threads) : availableThreads (threads)
        {}

        void acquire()
        {
            std::unique_lock<std::mutex> lock (mutex);
            cv.wait (lock, [this] { return availableThreads != 0; });
            availableThreads--;
        }

        void release()
        {
            std::unique_lock<std::mutex> lock (mutex);
            availableThreads++;
            cv.notify_one();
        }

    private:
        std::mutex mutex;
        std::condition_variable cv;
        uint32_t availableThreads;
    };

    struct ThreadAllocator
    {
        ThreadAllocator (ThreadPool& p) : pool (p)    { pool.acquire(); }
        ~ThreadAllocator()                            { pool.release(); }

        ThreadPool& pool;
    };

    //==============================================================================
    struct TestResult
    {
        static TestResult getTotal (const std::vector<std::unique_ptr<TestSuite>>& suites)
        {
            TestResult r;

            for (auto& s : suites)
                r.add (*s);

            return r;
        }

        void add (const TestSuite& s)
        {
            ++files;
            total += s.tests.size();
            passed += s.passed;
            failed += s.failed;
            disabled += s.disabled;
            unsupported += s.unsupported;
            time += s.time;
        }

        int passed = 0;
        int failed = 0;
        int disabled = 0;
        int unsupported = 0;
        int files = 0;
        size_t total = 0;
        std::chrono::duration<double> time;

        bool noFailures() const
        {
            return failed == 0;
        }

        size_t getNumComplete() const
        {
            return static_cast<size_t> (passed + failed + disabled + unsupported);
        }

        void printSummary (std::ostream& out)
        {
            if (files > 0)
                out << "Total files: " << files << std::endl
                    << std::endl;

            out << "Passed:      " << passed << std::endl
                << "Failed:      " << failed << std::endl
                << "Disabled:    " << disabled << std::endl
                << "Unsupported: " << unsupported << std::endl
                << std::endl;
        }

        static void writeJUnitXML (std::ostream& oss, const std::vector<std::unique_ptr<TestSuite>>& testSuites)
        {
            auto total = TestResult::getTotal (testSuites);

            oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>" << std::endl
                << "<testsuites disabled=\"" << total.disabled
                << "\" skipped=\"" << total.unsupported + total.disabled
                << "\" errors=\"" << total.failed
                << "\" name=\"cmaj\" tests=\"" << total.total
                << "\" time=\"" << total.time.count() << "\">" << std::endl;

            for (auto& ts : testSuites)
            {
                auto getClassName = [] (const std::string& filename) -> std::string
                {
                    auto trimmed = filename.substr (filename.find ("/tests/") + 7);
                    trimmed = trimmed.substr (0, trimmed.find (".cmajtest"));
                    return choc::text::replace (trimmed, "/", ".");
                };

                oss << "    <testsuite disabled=\"" << ts->disabled
                    << "\" skipped=\"" << ts->unsupported + ts->disabled
                    << "\" errors=\"" << ts->failed
                    << "\" name=\"" << ts->filename
                    << "\" tests=\"" << ts->tests.size()
                    << "\" time=\"" << ts->time.count() << "\">" << std::endl;

                for (auto& t : ts->tests)
                {
                    oss << "        <testcase name=\"Test " <<  std::setw (3) << t.section.testNum
                        << "\" classname=\"" << getClassName (ts->filename) << "\" time=\"" << t.time.count() << "\">" << std::endl;

                    if (! t.failed.empty())
                        oss << "            <failure message=\"" << t.failed.front() << "\"/>" << std::endl;

                    if (! t.disabled.empty())
                        oss << "            <skipped message=\"DISABLED: " << t.disabled.front() << "\"/>" << std::endl;

                    if (! t.unsupported.empty())
                        oss << "            <skipped message=\"UNSUPPORTED: " << t.unsupported.front() << "\"/>" << std::endl;

                    oss << "        </testcase>" << std::endl;
                }

                oss << "    </testsuite>" << std::endl;
            }

            oss << "</testsuites>" << std::endl;
        }
    };

    //==============================================================================
    struct TestJavascriptEngine
    {
        TestJavascriptEngine (cmaj::BuildSettings buildSettings,
                              TestSuite& suite,
                              std::ostream& out,
                              const choc::value::Value& engineOptions,
                              std::string testScriptPathToUse)
           : testFile (suite.filename), output (out),
             defaultEngineOptions (engineOptions), testScriptPath (std::move (testScriptPathToUse))
        {
            javascriptEngine = std::make_shared<javascript::JavascriptEngine> (buildSettings.setFrequency (44100),
                                                                               engineOptions);

            auto& context = getContext();

            CMAJ_JAVASCRIPT_BINDING_METHOD (getCurrentTestSection)
            CMAJ_JAVASCRIPT_BINDING_METHOD (getDefaultEngineOptions)
            CMAJ_JAVASCRIPT_BINDING_METHOD (getEngineName)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReportFail)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReportSuccess)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReportDisabled)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReportUnsupported)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testLogCompilerError)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testLogMessage)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testUpdateTestHeader)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReadStreamData)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReadEventData)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testReadMidiData)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testWriteStreamData)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testWriteEventData)
            CMAJ_JAVASCRIPT_BINDING_METHOD (testGetAbsolutePath)

            context.run (getWrapperScript());
            context.run (getTestLibrary());
            context.run (suite.userScript);
        }

        choc::javascript::Context& getContext()     { return javascriptEngine->getContext(); }

        void runTest (std::ostream* con, TestSuite::TestCase& test, bool runDisabled)
        {
            auto name = "Test " + std::to_string (test.section.testNum)
                          + " (line " + std::to_string (test.section.lineNum) + ")";

            auto header = choc::text::trim (test.section.header);

            javascriptEngine->resetPerformerLibrary();

            currentSectionInfo = choc::value::createObject ("Test",
                                                            "source", test.section.body,
                                                            "globalSource", test.suite.globalSource,
                                                            "header", test.section.header,
                                                            "lineNum", test.section.lineNum);

            currentTest = std::addressof (test);
            test.start (con);

            DiagnosticMessageList errors;
            catchAllErrors (errors, [&]
            {
                if (header.substr (0, 2) == "##")
                    performCommand (runDisabled, choc::text::trimStart (header.substr (2)));
            });

            if (errors.hasErrors())
                test.reportTestFailed (choc::text::trim (errors.toString()));

            test.end();
            currentTest = nullptr;
        }

        std::shared_ptr<javascript::JavascriptEngine> javascriptEngine;

    private:
        //==============================================================================
        std::filesystem::path testFile;
        std::ostream& output;
        TestSuite::TestCase* currentTest = nullptr;
        choc::value::Value currentSectionInfo, defaultEngineOptions;

        void performCommand (bool runDisabled, std::string header)
        {
            std::string command, remainingHeader;
            auto space = header.find (' ');

            if (space != std::string::npos)
            {
                command = header.substr (0, space);
                remainingHeader = choc::text::trim (header.substr (space));
            }
            else
            {
                command = header;
            }

            if (command == "disabled" || command == "DISABLED")
            {
                if (runDisabled)
                    return performCommand (runDisabled, remainingHeader);

                getContext().run ("getCurrentTestSection().reportDisabled();");
            }
            else
            {
                if (header.find ("(") == std::string::npos)
                {
                    header += "()";
                    currentTest->section.setNewHeader ("## " + header);
                }

                getContext().run (header, [&] (const std::string& error, const choc::value::ValueView& result)
                {
                    (void) result;

                    if (! error.empty())
                        currentTest->reportTestFailed (error);
                });
            }
        }

        choc::value::Value getCurrentTestSection (choc::javascript::ArgumentList)
        {
            return currentSectionInfo;
        }

        choc::value::Value getDefaultEngineOptions (choc::javascript::ArgumentList)
        {
            return defaultEngineOptions;
        }

        choc::value::Value getEngineName (choc::javascript::ArgumentList)
        {
            return choc::value::createString (javascriptEngine->getEngineTypeName());
        }

        std::string getErrorString (choc::javascript::ArgumentList args, size_t index)
        {
            if (auto value = args[index])
                return getErrorString (*value);

            return {};
        }

        std::string getErrorString (const choc::value::ValueView& error)
        {
            if (error.isVoid())
                return {};

            try
            {
                if (error.isObject())
                {
                    std::string fullDescription, annotatedLine;

                    if (error.hasObjectMember ("fullDescription"))
                        fullDescription = error["fullDescription"].toString();

                    if (error.hasObjectMember ("annotatedLine"))
                        annotatedLine = error["annotatedLine"].toString();

                    return choc::text::trim (fullDescription + "\n" + annotatedLine);
                }
                else if (error.isArray())
                {
                    std::vector<std::string> descriptions;

                    for (auto e : error)
                        descriptions.push_back (getErrorString(e));

                    return choc::text::joinStrings (descriptions, "\n");
                }

                return error.get<std::string>();
            }
            catch (const std::exception& e)
            {
                return e.what();
            }

            return "?";
        }

        choc::value::Value testReportFail (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);
            currentTest->reportTestFailed (getErrorString (args, 0));
            return {};
        }

        choc::value::Value testReportSuccess (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);
            currentTest->reportTestPassed (getErrorString (args, 0));
            return {};
        }

        choc::value::Value testReportDisabled (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);
            currentTest->reportTestDisabled (getErrorString (args, 0));
            return {};
        }

        choc::value::Value testReportUnsupported (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);
            currentTest->reportTestUnsupported (getErrorString (args, 0));
            return {};
        }

        choc::value::Value testLogCompilerError (choc::javascript::ArgumentList args)
        {
            // We have a string representing the error text. This starts line:col and we need to update line to reflect the
            // correct line number in the currentTestSection, as well as the filename

            CMAJ_ASSERT (currentTest != nullptr);
            auto errorText = args.get<std::string> (0);

            if (! errorText.empty())
            {
                try
                {
                    auto lineNum = currentTest->section.lineNum + std::stol (errorText);
                    errorText = errorText.substr (errorText.find (":"));
                    output << testFile << ":" << lineNum << errorText << std::endl;
                }
                catch (std::invalid_argument&)
                {
                    output << testFile << ":" << errorText << std::endl;
                }
            }

            return {};
        }

        choc::value::Value testLogMessage (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);

            for (auto& arg : args)
            {
                if (arg.isString())
                    currentTest->logMessage (arg.getString());
                else
                    currentTest->logMessage (choc::json::toString (arg));
            }

            return {};
        }

        choc::value::Value testUpdateTestHeader (choc::javascript::ArgumentList args)
        {
            CMAJ_ASSERT (currentTest != nullptr);
            currentTest->section.setNewHeader (args.get<std::string> (0));
            return {};
        }

        choc::value::Value testReadStreamData (choc::javascript::ArgumentList args)
        {
            auto name = args.get<std::string> (0);

            if (! name.empty())
            {
                auto filename = getFullPath (name);

                if (auto reader = cmaj::audio_utils::createFileReader (filename))
                {
                    auto buffer = reader->readEntireStream<float>();

                    return choc::value::createObject ("sampleData",
                                                      "sampleRate", choc::value::createFloat64 (reader->getProperties().sampleRate),
                                                      "channelCount", choc::value::createInt32 (static_cast<int32_t> (buffer.getNumChannels())),
                                                      "frameCount", choc::value::createInt64 (static_cast<int64_t> (buffer.getNumFrames())),
                                                      "data", channelArrayToValue (buffer));
                }
            }

            return javascript::createErrorObject ("File not found");
        }

        choc::value::Value testReadEventData (choc::javascript::ArgumentList args)
        {
            auto name = args.get<std::string> (0);

            if (! name.empty())
            {
                try
                {
                    auto filename = getFullPath (name);
                    auto contents = choc::file::loadFileAsString (filename);
                    return choc::json::parse (contents);
                }
                catch (const std::exception& e)
                {
                    return javascript::createErrorObject (e.what());
                }
            }

            return {};
        }

        choc::value::Value testReadMidiData (choc::javascript::ArgumentList args)
        {
            auto name = args.get<std::string> (0);

            if (! name.empty())
            {
                try
                {
                    auto filename = getFullPath (name);
                    auto contents = choc::file::loadFileAsString (filename);

                    choc::midi::File midi;
                    midi.load (contents.data(), contents.size());

                    auto midiEvents = choc::value::createEmptyArray();

                    midi.iterateEvents ([&] (const choc::midi::LongMessage& m, double timeInSeconds)
                    {
                        (void) m;

                        if (m.isShortMessage())
                        {
                            int v = (m.data()[0] << 16) + (m.data()[1] << 8) + m.data()[2];
                            double sampleRate = 48000.0;

                            midiEvents.addArrayElement(choc::value::createObject ("midiEvent",
                                                                                  "frameOffset", choc::value::createFloat64 (sampleRate * timeInSeconds),
                                                                                  "event", choc::value::createObject ("event",
                                                                                                                      "message", choc::value::createInt32 (v))));
                        }
                    });

                    return midiEvents;
                }
                catch (const std::exception& e)
                {
                    return javascript::createErrorObject (e.what());
                }
            }

            return {};
        }


        choc::value::Value testWriteStreamData (choc::javascript::ArgumentList args)
        {
            auto name = args.get<std::string> (0);

            if (! name.empty())
            {
                if (auto params = args[1])
                {
                    try
                    {
                        auto sampleRate = (*params)["sampleRate"].getFloat64();
                        auto data = valueToChannelArray ((*params)["data"]);

                        auto filename = getFullPath (name);

                        if (auto writer = cmaj::audio_utils::createFileWriter (filename, sampleRate,
                                                                               data.getNumChannels()))
                            if (writer->appendFrames (data))
                                return {};

                        return javascript::createErrorObject ("Cannot write to audio file: " + filename);
                    }
                    catch (const std::exception& e)
                    {
                        return javascript::createErrorObject (e.what());
                    }
                }
            }

            return {};
        }

        choc::value::Value testWriteEventData (choc::javascript::ArgumentList args)
        {
            auto name = args.get<std::string> (0);

            if (! name.empty())
            {
                if (auto jsonArg = args[1])
                {
                    auto filename = getFullPath (name);
                    auto json = choc::json::toString (*jsonArg);

                    try
                    {
                        choc::file::replaceFileWithContent (filename, json);
                        return {};
                    }
                    catch (const std::exception& e)
                    {
                        return javascript::createErrorObject (e.what());
                    }
                }
            }

            return javascript::createErrorObject ("Failed to write");
        }

        choc::value::Value testGetAbsolutePath (choc::javascript::ArgumentList args)
        {
            return choc::value::createString (getFullPath (args.get<std::string> (0)));
        }

        std::string getFullPath (const std::string& relativeFilename)
        {
            return (testFile.parent_path() / relativeFilename).string();
        }

        choc::value::Value channelArrayToValue (choc::buffer::ChannelArrayBuffer<float>& value)
        {
            auto result = choc::value::createEmptyArray();

            auto channelCount = value.getNumChannels();
            auto frameCount = value.getNumFrames();

            for (uint32_t i = 0; i < frameCount; i++)
            {
                if (channelCount == 1)
                {
                    result.addArrayElement (value.getSample (0, i));
                }
                else
                {
                    auto frame = choc::value::createEmptyArray();

                    for (uint32_t j = 0; j < channelCount; j++)
                        frame.addArrayElement (value.getSample (j, i));

                    result.addArrayElement (frame);
                }
            }

            return result;
        }

        choc::buffer::ChannelArrayBuffer<float> valueToChannelArray (const choc::value::ValueView& value)
        {
            auto bufferSize = value.size();

            uint32_t channelCount = 1;

            if (value[0].isArray())         channelCount = value[0].size();
            else if (value[0].isObject())   channelCount = value[0]["real"].size() * 2;

            choc::buffer::ChannelArrayBuffer<float> arrayBuffer (channelCount, bufferSize);

            for (uint32_t i = 0; i < bufferSize; i++)
            {
                if (value[0].isArray())
                {
                    for (uint32_t j = 0; j < channelCount; j++)
                        arrayBuffer.getSample (j, i) = static_cast<float> (value[i][j].getFloat64());
                }
                else if (value[0].isObject())
                {
                    if (channelCount == 2)
                    {
                        arrayBuffer.getSample (0, i) = static_cast<float> (value[i]["real"].getFloat64());
                        arrayBuffer.getSample (1, i) = static_cast<float> (value[i]["imag"].getFloat64());
                    }
                    else
                    {
                        for (uint32_t j = 0; j < channelCount/2; j++)
                        {
                            arrayBuffer.getSample (j * 2, i)     = static_cast<float> (value[i]["real"][j].getFloat64());
                            arrayBuffer.getSample (j * 2 + 1, i) = static_cast<float> (value[i]["imag"][j].getFloat64());
                        }
                    }
                }
                else
                {
                    arrayBuffer.getSample (0, i) = static_cast<float> (value[i].getFloat64());
                }
            }

            return arrayBuffer;
        }

        static std::string getWrapperScript()
        {
            return R"WRAPPER_SCRIPT(

function TestSection (ts)
{
    this.source = ts.source;
    this.globalSource = ts.globalSource;
    this.header = ts.header;

    this.reportFail         = function (msg)     { return _testReportFail (msg); }
    this.reportSuccess      = function (msg)     { return _testReportSuccess (msg); }
    this.reportDisabled     = function (msg)     { return _testReportDisabled (msg); }
    this.reportUnsupported  = function (msg)     { return _testReportUnsupported (msg); }
    this.logCompilerError   = function (error)   { return _testLogCompilerError (error); }
    this.logMessage         = function (msg)     { return _testLogMessage (msg); }
    this.updateTestHeader   = function (h)       { return _testUpdateTestHeader (h); }
    this.writeStreamData    = function (n, d)    { return _testWriteStreamData (n, d); }
    this.writeEventData     = function (n, d)    { return _testWriteEventData (n, d); }
    this.readStreamData     = function (n)       { return _testReadStreamData (n); }
    this.readEventData      = function (n)       { return _testReadEventData (n); }
    this.readMidiData       = function (n)       { return _testReadMidiData (n); }
    this.getAbsolutePath    = function (path)    { return _testGetAbsolutePath (path); }
}

function getCurrentTestSection()                     { return new TestSection (_getCurrentTestSection()); }
function getDefaultEngineOptions()                   { return _getDefaultEngineOptions(); }
function getEngineName()                             { return _getEngineName(); }
)WRAPPER_SCRIPT";
        }

        std::string testScriptPath;

        std::string getTestLibrary()
        {
            std::string module = "cmaj_test_functions.js";

            if (! testScriptPath.empty())
            {
                try
                {
                    auto file = std::filesystem::path (testScriptPath) / module;

                    if (auto content = choc::file::loadFileAsString (file.string()); ! content.empty())
                        return content;
                }
                catch (const std::exception&) {}
            }

            #include "cmaj_TestRunnerLibrary.h"
            return std::string (testFunctionLibrary);
        }
    };

    //==============================================================================
    inline void TestSuite::runTests (std::ostream& console,
                                     const cmaj::BuildSettings& buildSettings, std::optional<int> testToRun,
                                     bool runDisabled, const choc::value::Value& engineOptions,
                                     std::string testScriptPath)
    {
        console << thickDivider << std::endl
                << "Running: " << std::filesystem::path (filename).filename().string() << "   (" << filename << ")" << std::endl
                << std::endl;

        TestJavascriptEngine testEngine (buildSettings, *this, console, engineOptions, testScriptPath);

        for (auto& test : tests)
        {
            if (testToRun.has_value() && test.section.testNum != *testToRun)
                continue;

            testEngine.runTest (std::addressof (console), test, runDisabled);
        }
    }

    //==============================================================================
    static void runSuites (const std::vector<std::unique_ptr<TestSuite>>& testSuites,
                           const cmaj::BuildSettings& buildSettings,
                           std::ostream& output,
                           std::optional<int> testToRun,
                           bool runDisabled,
                           bool showProgressBar,
                           bool printOnlyErrors,
                           uint32_t threadLimit,
                           const choc::value::Value& engineOptions,
                           std::string testScriptPath)
    {
        if (threadLimit > 1 && ! testToRun.has_value())
        {
            ThreadPool pool (threadLimit);

            std::vector<std::future<std::string>> futures;
            size_t totalNumTests = 0;

            if (testSuites.size() == 1)
            {
                auto& suite = *testSuites.front();

                for (int testIndex = 0; testIndex < (int) suite.tests.size(); ++testIndex)
                {
                    futures.emplace_back (std::async (std::launch::async,
                                                      [&suite, testIndex, &pool, &buildSettings,
                                                       runDisabled, &engineOptions, testScriptPath] () -> std::string
                                                      {
                                                          ThreadAllocator allocateThread (pool);
                                                          std::ostringstream testOutput;
                                                          suite.runTests (testOutput, buildSettings, testIndex + 1, runDisabled, engineOptions, testScriptPath);
                                                          return testOutput.str();
                                                      }));

                    ++totalNumTests;
                }
            }
            else
            {
                for (auto& suite : testSuites)
                {
                    futures.emplace_back (std::async (std::launch::async,
                                                      [&] () -> std::string
                                                      {
                                                          ThreadAllocator allocateThread (pool);
                                                          std::ostringstream testOutput;
                                                          suite->runTests (testOutput, buildSettings, {}, runDisabled, engineOptions, testScriptPath);
                                                          return testOutput.str();
                                                      }));

                    totalNumTests += suite->tests.size();
                }
            }

            if (showProgressBar)
            {
                size_t lastPercentage = 0;

                auto printBar = [&]
                {
                    auto totalResults = TestResult::getTotal (testSuites);
                    auto percentage = (totalResults.getNumComplete() * 100) / totalNumTests;

                    if (lastPercentage != percentage)
                    {
                        lastPercentage = percentage;

                        size_t barLength = 60;
                        auto barSize = (percentage * barLength) / 100;

                        std::cout << "\x1b[1000DRunning tests:  "
                                    << std::string (barSize, '#') << std::string (barLength - barSize, '.')
                                    << "  " << percentage << "%  "
                                    << "  pass: " << totalResults.passed
                                    << "  fail: " << totalResults.failed
                                    << "  "
                                    << std::flush;
                    }
                };

                for (auto& f : futures)
                {
                    for (;;)
                    {
                        if (f.wait_for (std::chrono::milliseconds (100)) == std::future_status::ready)
                            break;

                        printBar();
                    }
                }

                printBar();
                std::cout << std::endl;
            }
            else
            {
                for (auto& f : futures)
                {
                    f.wait();

                    if (! printOnlyErrors)
                        output << f.get();
                }
            }
        }
        else
        {
            for (auto& suite : testSuites)
            {
                if (printOnlyErrors)
                {
                    std::ostringstream testOutput;
                    suite->runTests (testOutput, buildSettings, testToRun, runDisabled, engineOptions, testScriptPath);
                }
                else
                {
                    suite->runTests (output, buildSettings, testToRun, runDisabled, engineOptions, testScriptPath);
                }
            }
        }
    }

    //==============================================================================
    bool runTestFiles (const cmaj::BuildSettings& buildSettings,
                       std::ostream& output,
                       const std::string& xml,
                       choc::span<std::string> testFiles,
                       std::optional<int> testToRun,
                       bool runDisabled,
                       uint32_t threadLimit,
                       int iterations,
                       const choc::value::Value& engineOptions,
                       std::string testScriptPath)
    {
        auto startTime = std::chrono::steady_clock::now();

        if (threadLimit == 0)
        {
            threadLimit = std::thread::hardware_concurrency();
            output << "Setting thread count to " << threadLimit << std::endl
                   << std::endl;
        }

       #if CHOC_WINDOWS
        bool isRunningInRealTerminal = _isatty (_fileno (stdout));
       #else
        bool isRunningInRealTerminal = isatty (fileno (stdout));
       #endif

        // may want to allow these to be overridden
        bool showProgressBar = isRunningInRealTerminal && threadLimit > 1 && testFiles.size() > 1;
        bool printOnlyErrors = isRunningInRealTerminal && threadLimit > 1;

        TestResult totalResults;

        try
        {
            std::vector<std::unique_ptr<TestSuite>> testSuites;
            testSuites.reserve (testFiles.size());

            for (int iteration = 1; iteration <= iterations; iteration++)
            {
                testSuites.clear();

                for (auto& file : testFiles)
                    testSuites.emplace_back (std::make_unique<TestSuite> (file));

                runSuites (testSuites, buildSettings, output,
                           testToRun, runDisabled, showProgressBar, printOnlyErrors,
                           threadLimit, engineOptions, testScriptPath);
            }

            auto endTime = std::chrono::steady_clock::now();

            for (auto& suite : testSuites)
            {
                if (suite->needsResaving())
                {
                    output << "Updating test headers in file: " << suite->filename << std::endl;
                    suite->resave();
                }
            }

            output << std::endl
                   << thickDivider << std::endl
                   << "Total time: " << choc::text::getDurationDescription (endTime - startTime) << std::endl;

            totalResults = TestResult::getTotal (testSuites);
            totalResults.printSummary (output);

            if (printOnlyErrors)
            {
                for (auto& suite : testSuites)
                    for (auto& test : suite->tests)
                        test.printErrors (output);
            }

            if (! xml.empty())
            {
                std::ofstream xmlFile (xml);
                TestResult::writeJUnitXML (xmlFile, testSuites);
            }
        }
        catch (const std::exception& e)
        {
            output << e.what() << std::endl;
        }
        catch (...)
        {
            output << "caught unknown exception" << std::endl;
        }

        return totalResults.noFailures();
    }
}
