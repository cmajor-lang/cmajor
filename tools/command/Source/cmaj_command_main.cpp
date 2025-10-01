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

#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "../../../modules/scripting/include/cmaj_ScriptEngine.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedAssets.h"

#include "choc/containers/choc_ArgumentList.h"
#include "choc/text/choc_Files.h"
#include "choc/text/choc_OpenSourceLicenseList.h"

#include "cmaj_command_Generate.h"
#include "cmaj_command_Render.h"
#include "cmaj_command_CreatePatch.h"
#include "cmaj_command_RunTests.h"

void runUnitTests (choc::ArgumentList&, const choc::value::Value&, cmaj::BuildSettings&);

void playFile (choc::ArgumentList&, const choc::value::Value& engineOptions,
               cmaj::BuildSettings&, const choc::audio::io::AudioDeviceOptions&);

void runServerProcess (choc::ArgumentList&, const choc::value::Value& engineOptions,
                       cmaj::BuildSettings&, const choc::audio::io::AudioDeviceOptions&);

void printCmajorVersion()
{
    std::cout << "Cmajor Version: " << cmaj::Library::getVersion() << std::endl
              << "Build date: " <<  __DATE__ << " " << __TIME__ << std::endl;
}

static void showHelp()
{
    std::cout << R"(
   ,ad888ba,                                88
  d8"'    "8b
 d8            88,dPba,,adPba,   ,adPPYba,  88
 88            88P'  "88"   "8a        '88  88
 Y8,           88     88     88  ,adPPPP88  88
  Y8a.   .a8P  88     88     88  88,   ,88  88
   '"Y888Y"'   88     88     88  '"8bbP"Y8  88
                                           ,88
                                         888P"
Cmajor Tools (C)2024 Cmajor Software Ltd.
https://cmajor.dev
)";

    printCmajorVersion();

    auto help = R"(

cmaj <command> [options]    Runs the given command. Options can include the following:

    -O0|1|2|3|4             Set the optimisation level to the given value
    --debug                 Turn on debug output from the performer
    --sessionID=n           Set the session id to the given value
    --eventBufferSize=n     Set the max number of events per buffer
    --engine=<type>         Use the specified engine - e.g. llvm, webview, cpp
                            When using cpp, the additional options --overrideCompiler=<v>,
                            --extraCompileArgs=<v> and --extraLinkerArgs=<v> can be specified to 
                            alter the compiler and arguments used
    --simd                  WASM generation uses SIMD/non-SIMD at runtime (default)
    --no-simd               WASM generation does not emit SIMD
    --simd-only             WASM generation only emits SIMD
    --worker=<webview|quickjs>  Specify the type of javascript engine to use for patch workers

Supported commands:

cmaj help                   Displays this help output

cmaj version                Displays the current Cmajor version

cmaj licenses               Print out legally required licensing details for 3rd-party
                            libraries that are used by this application.

cmaj play file [opts]       Plays a .cmajorpatch, or executes a .js javascript file.

    --no-gui                Disable automatic launching of the UI in a web browser
    --stop-on-error         Exits the app if there's a compile error (default is to keep
                            running and retry when files are modified)
    --dry-run               Doesn't attempt to play any audio, just builds the patch, emits
                            any errors that are found, and exits
    --rate=<rate>           Use the specified sample rate
    --block-size=<size>     Request the given block size

cmaj server [opts] dir      Run cmaj as an http service, serving the patches within the given
                            directory. Connect to the server using a browser to the http address
                            given

    --address=<addr>:<port> Serve from the specified address, defaults to 127.0.0.1:51000
    --timeoutMs=<ms>        Alters the client timeout time, defaults to 5000ms

cmaj test [opts] <files>    Runs one or more .cmajtest scripts, and print the aggregate results
                            for the tests. See the documentation for writing tests for more info.

    --singleThread          Use a single thread to run the tests
    --threads=n             Run with the given number of threads, defaults to the available cores
    --runDisabled           Run all tests including any marked disabled
    --testToRun=n           Only run the specified test number in the test files
    --xmlOutput=file        Generate a JUNIT compatible xml file containing the test results
    --iterations=n          How many times to repeat the tests

cmaj render [opts] <file>   Renders the given file or patch

    --length=<frames>       The number of frames to render (optional if an input audio file is provided)
    --rate=<rate>           Use the specified sample rate (optional if an input audio file is provided)
    --channels=<num>        Number of output audio channels to render (default is 2 if omitted)
    --blockSize=<size>      Render in the given block size
    --output=<file>         Write the output to the given file
    --input=<file>          Use input from the given file
    --midi=<file>           Use input MIDI data from the given file

cmaj generate [opts] <file> Generates some code from the given file or patch

    Performs various types of code-gen output. Targets are:

CODE_GEN_TARGETS
    Other arguments for code-gen include:

    --output=<file-path>    Write the generated runtime to the given file (or folder for a plugin)
    --jucePath=<folder>     If generating a JUCE plugin, this is the path to your JUCE folder
    --clapIncludePath=<folder>  If generating a CLAP plugin, this is the path to your CLAP include folder
    --cmajorIncludePath=<folder>  If generating a plugin, this is the path to your cmajor/include folder
    --maxFramesPerBlock=n   Specify the maximum block size when generating code

cmaj create [opts] <folder> Creates a folder containing files for a new empty patch

    --name="name"           Provides a name for the patch

cmaj unit-test              Runs internal unit tests.

    --iterations=n          How many times to repeat the tests

)";

    std::cout << choc::text::replace (help, "CODE_GEN_TARGETS", getCodeGenTargetHelp())
              << std::endl;
}

//==============================================================================
static cmaj::BuildSettings parseBuildArgs (choc::ArgumentList& args)
{
    cmaj::BuildSettings buildSettings;

    if (args.removeIfFound ("-O0")    || args.removeIfFound ("--O0"))     buildSettings.setOptimisationLevel (0);
    if (args.removeIfFound ("-O1")    || args.removeIfFound ("--O1"))     buildSettings.setOptimisationLevel (1);
    if (args.removeIfFound ("-O2")    || args.removeIfFound ("--O2"))     buildSettings.setOptimisationLevel (2);
    if (args.removeIfFound ("-O3")    || args.removeIfFound ("--O3"))     buildSettings.setOptimisationLevel (3);
    if (args.removeIfFound ("-O4")    || args.removeIfFound ("--O4"))     buildSettings.setOptimisationLevel (4);
    if (args.removeIfFound ("-Ofast") || args.removeIfFound ("--Ofast"))  buildSettings.setOptimisationLevel (4);

    if (args.removeIfFound ("-debug") || args.removeIfFound ("--debug"))
        buildSettings.setDebugFlag (true);

    if (auto sessID = args.removeIntValue<int32_t> ("--sessionID"))
        buildSettings.setSessionID (*sessID);

    if (auto bufferSize = args.removeIntValue<uint32_t> ("--eventBufferSize"))
        buildSettings.setEventBufferSize (*bufferSize);

    if (auto maxFrequency = args.removeIntValue<uint32_t> ("--maxFrequency"))
        buildSettings.setMaxFrequency (*maxFrequency);

    return buildSettings;
}


//==============================================================================
static choc::audio::io::AudioDeviceOptions parseAudioDeviceArgs (choc::ArgumentList& args)
{
    choc::audio::io::AudioDeviceOptions options;

    options.sampleRate = args.removeIntValue<uint32_t> ("--rate", 0);
    options.blockSize  = args.removeIntValue<uint32_t> ("--block-size", 0);

    options.audioAPI = args.removeValueFor ("--audio-device-type", {});

    options.inputChannelCount  = args.removeIntValue<uint32_t> ("--inputs", 256);
    options.outputChannelCount = args.removeIntValue<uint32_t> ("--outputs", 256);

    options.outputDeviceID = args.removeValueFor ("--output-device", {});
    options.inputDeviceID  = args.removeValueFor ("--input-device", {});

    return options;
}

//==============================================================================
static choc::value::Value parseEngineArgs (choc::ArgumentList& args)
{
    auto engineOptions = choc::value::createObject ("options");

    if (auto engine = args.removeValueFor ("--engine"))
        engineOptions.addMember ("engine", *engine);

    if (args.removeIfFound ("--validatePrint"))
        engineOptions.addMember ("validatePrint", true);

    if (args.removeIfFound ("--simd"))      engineOptions.addMember ("SIMD", "enable");
    if (args.removeIfFound ("--no-simd"))   engineOptions.addMember ("SIMD", "disable");
    if (args.removeIfFound ("--simd-only")) engineOptions.addMember ("SIMD", "simd-only");

    if (auto worker = args.removeValueFor ("--worker"))
        engineOptions.addMember ("worker", *worker);

    if (auto overrideCompiler = args.removeValueFor ("--overrideCompiler"))
        engineOptions.addMember ("overrideCompiler", *overrideCompiler);

    if (auto extraCompileArgs = args.removeValueFor ("--extraCompileArgs"))
        engineOptions.addMember ("extraCompileArgs", *extraCompileArgs);

    if (auto extraLinkerArgs = args.removeValueFor ("--extraLinkerArgs"))
        engineOptions.addMember ("extraLinkerArgs", *extraLinkerArgs);

    return engineOptions;
}


//==============================================================================
static bool isCommand (choc::ArgumentList& args, std::string_view name)
{
    if (args.indexOf (name) == 0)
    {
        args.removeIndex (0);
        return true;
    }

    return false;
}

static void performCommandLineTask (choc::ArgumentList& args)
{
    if (isCommand (args, "version"))
        return printCmajorVersion();

    if (isCommand (args, "licenses") || isCommand (args, "licences"))
    {
        std::cout << choc::text::OpenSourceLicenseList::getAllLicenseText() << std::endl;
        return;
    }

    auto buildSettings = parseBuildArgs (args);
    auto engine = parseEngineArgs (args);

    if (auto assets = args.removeExistingFolderIfPresent ("--assetFolder"))
    {
        cmaj::EmbeddedAssets::getInstance().setLocalAssetFolder (*assets);
        args.removeIfFound  ("--assetFolder");
    }

    if (isCommand (args, "play"))      return playFile (args, engine, buildSettings, parseAudioDeviceArgs (args));
    if (isCommand (args, "server"))    return runServerProcess (args, engine, buildSettings, parseAudioDeviceArgs (args));
    if (isCommand (args, "generate"))  return generate (args, engine, buildSettings);
    if (isCommand (args, "render"))    return render (args, engine, buildSettings);
    if (isCommand (args, "test"))      return runTests (args, engine, buildSettings);
    if (isCommand (args, "create"))    return createPatch (args);
    if (isCommand (args, "unit-test")) return runUnitTests (args, engine, buildSettings);

    showHelp();
}


//==============================================================================
int main (int argc, char** argv)
{
    try
    {
        choc::messageloop::initialise();

        choc::ArgumentList args (argc, argv);
        performCommandLineTask (args);
        return 0;
    }
    catch (const std::exception& e)
    {
        if (e.what() != std::string ("std::exception"))
            std::cerr << e.what() << std::endl;
    }

    return 1;
}
