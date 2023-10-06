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

namespace cmaj::test
{
    bool runTestFiles (const BuildSettings& buildSettings,
                       std::ostream& output,
                       const std::string& xml,
                       choc::span<std::string> testFiles,
                       std::optional<int> testToRun,
                       bool runDisabled,
                       uint32_t threadLimit,
                       int iterations,
                       const choc::value::Value& engineOptions,
                       std::string testScriptPath);
}

static void findTestFiles (std::vector<juce::File>& testFiles, const juce::File& file)
{
    if (file.isDirectory())
    {
        for (auto& f : file.findChildFiles (juce::File::findFilesAndDirectories, true, "*.cmajtest"))
            testFiles.push_back (f);
    }
    else
    {
        testFiles.push_back (file);
    }
}

void runTests (juce::ArgumentList& args,
               const choc::value::Value& engineOptions,
               cmaj::BuildSettings& buildSettings)
{
    std::optional<int> testToRun;
    int iterations = 1;
    std::string testScriptPath;

    if (args.containsOption ("--scriptFolder"))
        testScriptPath = args.getExistingFolderForOptionAndRemove ("--scriptFolder").getFullPathName().toStdString();

    if (args.containsOption ("--testToRun"))
        testToRun = args.removeValueForOption ("--testToRun").getIntValue();

    if (args.containsOption ("--iterations"))
        iterations = args.removeValueForOption ("--iterations").getIntValue();

    bool runDisabled = args.removeOptionIfFound ("--runDisabled");

    uint32_t threadCount = 0;

    if (args.removeOptionIfFound ("--singleThread"))
        threadCount = 1;
    else if (args.containsOption ("--threads"))
        threadCount = static_cast<uint32_t> (args.removeValueForOption ("--threads").getIntValue());

    std::string xmlFile;

    if (args.containsOption ("--xmlOutput"))
        xmlFile = args.removeValueForOption ("--xmlOutput").toStdString();

    std::vector<juce::File> testFiles;

    for (auto& arg : args.arguments)
        findTestFiles (testFiles, arg.resolveAsFile());

    std::sort (testFiles.begin(), testFiles.end(), [] (const juce::File& lhs, const juce::File& rhs)
    {
        return lhs.getFileName() < rhs.getFileName();
    });

    std::vector<std::string> paths;

    for (auto& f : testFiles)
        paths.push_back (f.getFullPathName().toStdString());

    std::cout << "Cmajor Version: " << cmaj::Library::getVersion() << std::endl
              << "Engine: " << choc::json::toString (engineOptions, false) << std::endl;

    choc::messageloop::initialise();

    std::optional<std::exception> exceptionThrown;

    auto t = std::thread ([&]
    {
        try
        {
            if (! cmaj::test::runTestFiles (buildSettings, std::cerr, xmlFile, paths,
                                            testToRun, runDisabled, threadCount, iterations,
                                            engineOptions, testScriptPath))
                throw std::exception();
        }
        catch (const std::exception& e)
        {
            exceptionThrown = e;
        }
        catch (...)
        {
            exceptionThrown = std::runtime_error ("unknown exception");
        }

        choc::messageloop::stop();
    });

    choc::messageloop::run();

    t.join();

    if (exceptionThrown)
        throw *exceptionThrown;
}
