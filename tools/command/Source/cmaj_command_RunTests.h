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

static void findTestFiles (std::vector<std::filesystem::path>& testFiles, const std::filesystem::path& file)
{
    if (is_directory (file))
    {
        for (auto& f : std::filesystem::recursive_directory_iterator (file))
            if (f.path().extension() == ".cmajtest")
                testFiles.push_back (f);
    }
    else
    {
        testFiles.push_back (file);
    }
}

void runTests (ArgumentList& args,
               const choc::value::Value& engineOptions,
               cmaj::BuildSettings& buildSettings)
{
    auto testScriptPath = args.removeExistingFolderIfPresent ("--scriptFolder");
    auto testToRun = args.removeIntValue<int32_t> ("--testToRun");
    auto iterations = args.removeIntValue<int32_t> ("--iterations", 1);
    bool runDisabled = args.removeIfFound ("--runDisabled");

    uint32_t threadCount = 0;

    if (args.removeIfFound ("--singleThread"))
        threadCount = 1;
    else if (auto threads = args.removeIntValue<uint32_t> ("--threads"))
        threadCount = *threads;

    std::string xmlFile;

    if (auto xml = args.removeValueFor ("--xmlOutput"))
        xmlFile = *xml;

    std::vector<std::filesystem::path> testFiles;

    for (auto& file : args.getAllAsFiles())
        findTestFiles (testFiles, file);

    std::sort (testFiles.begin(), testFiles.end(), [] (const std::filesystem::path& lhs, const std::filesystem::path& rhs)
    {
        return lhs.filename() < rhs.filename();
    });

    std::vector<std::string> paths;

    for (auto& f : testFiles)
        paths.push_back (f.string());

    std::cout << "Cmajor Version: " << cmaj::Library::getVersion() << std::endl
              << "Engine: " << choc::json::toString (engineOptions, false) << std::endl;

    choc::messageloop::initialise();

    std::optional<std::exception> exceptionThrown;

    auto t = std::thread ([&]
    {
        try
        {
            if (! cmaj::test::runTestFiles (buildSettings, std::cerr, xmlFile, paths,
                                            testToRun, runDisabled, threadCount, iterations, engineOptions,
                                            testScriptPath ? testScriptPath->string() : std::string()))
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
