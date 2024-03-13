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

#include "juce/cmaj_JUCEHeaders.h"
#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "../../../modules/scripting/include/cmaj_ScriptEngine.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedAssets.h"

#include "choc/text/choc_Files.h"
#include "choc/text/choc_OpenSourceLicenseList.h"

#include "cmaj_command_Generate.h"
#include "cmaj_command_Render.h"
#include "cmaj_command_CreatePatch.h"
#include "cmaj_command_RunTests.h"
#include "cmaj_JUCEAudioPlayer.h"
#include "cmaj_command_OpenSourceLicenses.h"

void runUnitTests (juce::ArgumentList&, const choc::value::Value&, cmaj::BuildSettings&);

void playFile (juce::ArgumentList&, const choc::value::Value& engineOptions,
               cmaj::BuildSettings&, const cmaj::audio_utils::AudioDeviceOptions&);

void runServerProcess (juce::ArgumentList&, const choc::value::Value& engineOptions,
                       cmaj::BuildSettings&, const cmaj::audio_utils::AudioDeviceOptions&);

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
    --engine=<type>         Use the specified engine - e.g. llvm, webview, cpp

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

cmaj server [opts] dir      Run cmaj as an http service, serving the patches within the given
                            directory. Connect to the server using a browser to the http address
                            given

    --address=<addr>:<port> Serve from the specified address, defaults to 127.0.0.1:51000

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
    --eventBufferSize=n     Specify an event buffer size when generating code

cmaj create [opts] <folder> Creates a folder containing files for a new empty patch

    --name="name"           Provides a name for the patch

cmaj unit-test              Runs internal unit tests.

    --iterations=n          How many times to repeat the tests

)";

    std::cout << choc::text::replace (help, "CODE_GEN_TARGETS", getCodeGenTargetHelp())
              << std::endl;
}

//==============================================================================
static cmaj::BuildSettings parseBuildArgs (juce::ArgumentList& args)
{
    cmaj::BuildSettings buildSettings;

    if (args.removeOptionIfFound("-O0") || args.removeOptionIfFound("--O0"))  buildSettings.setOptimisationLevel (0);
    if (args.removeOptionIfFound("-O1") || args.removeOptionIfFound("--O1"))  buildSettings.setOptimisationLevel (1);
    if (args.removeOptionIfFound("-O2") || args.removeOptionIfFound("--O2"))  buildSettings.setOptimisationLevel (2);
    if (args.removeOptionIfFound("-O3") || args.removeOptionIfFound("--O3"))  buildSettings.setOptimisationLevel (3);
    if (args.removeOptionIfFound("-O4") || args.removeOptionIfFound("--O4"))  buildSettings.setOptimisationLevel (4);
    if (args.removeOptionIfFound("-Ofast") || args.removeOptionIfFound("--Ofast"))  buildSettings.setOptimisationLevel (4);

    if (args.removeOptionIfFound ("-debug") || args.removeOptionIfFound ("--debug"))
        buildSettings.setDebugFlag (true);

    if (args.containsOption ("--sessionID"))
        buildSettings.setSessionID (args.removeValueForOption ("--sessionID").getIntValue());

    return buildSettings;
}


//==============================================================================
static cmaj::audio_utils::AudioDeviceOptions parseAudioDeviceArgs (juce::ArgumentList& args)
{
    cmaj::audio_utils::AudioDeviceOptions options;

    auto getIntArg = [&] (juce::StringRef name, uint32_t defaultValue) -> uint32_t
    {
        if (args.containsOption (name))
            return static_cast<uint32_t> (std::max (0, args.removeValueForOption (name).getIntValue()));

        return defaultValue;
    };

    auto getStringArg = [&] (juce::StringRef name, std::string defaultValue = {}) -> std::string
    {
        if (args.containsOption (name))
            return args.removeValueForOption (name).unquoted().toStdString();

        return defaultValue;
    };

    options.sampleRate = getIntArg ("--rate", 0);
    options.blockSize  = getIntArg ("--block-size", 0);
    options.audioAPI = getStringArg ("--audio-device-type");

    options.inputChannelCount  = getIntArg ("--inputs", 256);
    options.outputChannelCount = getIntArg ("--outputs", 256);

    options.outputDeviceName = getStringArg ("--output-device");
    options.inputDeviceName  = getStringArg ("--input-device");

    options.createPlayer = cmaj::JUCEAudioMIDIPlayer::create;

    return options;
}

//==============================================================================
static choc::value::Value parseEngineArgs (juce::ArgumentList& args)
{
    auto engineOptions = choc::value::createObject ("options");

    if (args.containsOption ("--engine"))
        engineOptions.addMember ("engine", args.removeValueForOption ("--engine").toStdString());

    if (args.removeOptionIfFound ("--validatePrint"))
        engineOptions.addMember ("validatePrint", true);

    return engineOptions;
}

//==============================================================================
static bool isCommand (juce::ArgumentList& args, juce::StringRef name)
{
    if (args.indexOfOption (name) == 0)
    {
        args.arguments.remove (0);
        return true;
    }

    return false;
}

static void performCommandLineTask (juce::ArgumentList& args)
{
    if (isCommand (args, "version"))
        return printCmajorVersion();

    if (isCommand (args, "licenses") || isCommand (args, "licences"))
        return printOpenSourceLicenses();

    auto buildSettings = parseBuildArgs (args);
    auto engine = parseEngineArgs (args);

    if (args.containsOption ("--assetFolder"))
    {
        auto localAssetFolder = choc::text::trim (args.getExistingFolderForOption ("--assetFolder").getFullPathName().toStdString());
        cmaj::EmbeddedAssets::getInstance().setLocalAssetFolder (localAssetFolder);
        args.removeOptionIfFound ("--assetFolder");
    }

    if (isCommand (args, "play"))      return playFile (args, engine, buildSettings, parseAudioDeviceArgs (args));
    if (isCommand (args, "server"))    return runServerProcess (args, engine, buildSettings, parseAudioDeviceArgs (args));
    if (isCommand (args, "generate"))  return generate (args, buildSettings);
    if (isCommand (args, "render"))    return render (args, engine, buildSettings);
    if (isCommand (args, "test"))      return runTests (args, engine, buildSettings);
    if (isCommand (args, "create"))    return createPatch (args);
    if (isCommand (args, "unit-test")) return runUnitTests (args, engine, buildSettings);

    showHelp();
}


//==============================================================================
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiser;
    juce::ArgumentList args (argc, argv);

   #if JUCE_MAC
    auto macJunkArgs = args.indexOfOption ("-NSDocumentRevisionsDebugMode");

    if (macJunkArgs >= 0)
        args.arguments.removeRange (macJunkArgs, 2);
   #endif

    try
    {
        return juce::ConsoleApplication::invokeCatchingFailures ([&args]
        {
            performCommandLineTask (args);
            return 0;
        });
    }
    catch (const std::exception& e)
    {
        if (e.what() != std::string ("std::exception"))
            std::cerr << e.what() << std::endl;
    }

    return 1;
}
