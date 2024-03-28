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

#include "../../../modules/playback/include/cmaj_PatchWindow.h"
#include "../../../modules/scripting/include/cmaj_ScriptEngine.h"
#include "../../../modules/server/include/cmaj_PatchPlayerServer.h"

void printCmajorVersion();

//==============================================================================
static void runPatch (cmaj::PatchPlayer& player, const std::string& filename, int64_t framesToRender, bool stopOnError)
{
    choc::messageloop::Timer checkTimer;
    std::atomic<bool> shouldStop { false };

    if (framesToRender > 0 || stopOnError)
    {
        checkTimer = choc::messageloop::Timer (10, [&player, &shouldStop, framesToRender]
        {
            if (shouldStop || (framesToRender > 0 && player.totalFramesRendered > static_cast<uint64_t> (framesToRender)))
            {
                choc::messageloop::stop();
                return false;
            }

            return true;
        });
    }

    player.onStatusChange = [&shouldStop, stopOnError] (const cmaj::Patch::Status& s)
    {
        std::cout << s.statusMessage << std::endl;

        if (stopOnError && s.messageList.hasErrors())
            shouldStop = true;
    };

    if (! player.loadPatch (filename))
        throw std::runtime_error ("Failed to load this patch");

    choc::messageloop::run();
}

//==============================================================================
void playFile (juce::ArgumentList& args,
               const choc::value::Value& engineOptions,
               cmaj::BuildSettings& buildSettings,
               const cmaj::audio_utils::AudioDeviceOptions& audioOptions)
{
    if (args.size() == 0)
        throw std::runtime_error ("Expected a filename to play");

    bool noGUI       = args.removeOptionIfFound ("--no-gui");
    bool stopOnError = args.removeOptionIfFound ("--stop-on-error");
    bool dryRun      = args.removeOptionIfFound ("--dry-run");

    int64_t framesToRender = 0;

    if (args.containsOption ("--length"))
    {
        framesToRender = args.removeValueForOption ("--length").getLargeIntValue();

        if (framesToRender < 0)
            throw std::runtime_error ("Illegal length");
    }

    auto file = args[0].resolveAsExistingFile();
    auto filename = file.getFullPathName().toStdString();

    if (file.hasFileExtension (".js"))
        return cmaj::javascript::executeScript (filename, engineOptions, buildSettings, audioOptions);

    if (! file.hasFileExtension (".cmajorpatch"))
        throw std::runtime_error ("Expected a .cmajorpatch file");

    if (dryRun)
    {
        cmaj::Patch patch;

        patch.createEngine   = [] { return cmaj::Engine::create(); };
        patch.stopPlayback   = [] {};
        patch.startPlayback  = [] {};
        patch.patchChanged   = [] {};
        patch.statusChanged  = [] (const cmaj::Patch::Status& s)   { std::cout << s.statusMessage << std::endl; };

        {
            cmaj::Patch::PlaybackParams params;
            params.blockSize = audioOptions.blockSize == 0 ? 256 : audioOptions.blockSize;
            params.sampleRate = audioOptions.sampleRate == 0 ? 44100 : audioOptions.sampleRate;
            params.numInputChannels = audioOptions.inputChannelCount == 0 ? 2 : audioOptions.inputChannelCount;
            params.numOutputChannels = audioOptions.outputChannelCount == 0 ? 2 : audioOptions.outputChannelCount;
            patch.setPlaybackParams (params);
        }

        patch.loadPatchFromFile (filename, true);
        return;
    }

    auto audioPlayer = std::make_shared<cmaj::audio_utils::MultiClientAudioMIDIPlayer> (audioOptions);

    if (noGUI)
    {
        cmaj::PatchPlayer player (engineOptions, buildSettings, true);
        player.setAudioMIDIPlayer (std::move (audioPlayer));
        player.startPlayback();
        runPatch (player, filename, framesToRender, stopOnError);
    }
    else
    {
        choc::ui::setWindowsDPIAwareness();
        cmaj::PatchWindow patchWindow (engineOptions, buildSettings);
        patchWindow.player.setAudioMIDIPlayer (std::move (audioPlayer));
        runPatch (patchWindow.player, filename, framesToRender, stopOnError);
    }
}


//==============================================================================
void runServerProcess (juce::ArgumentList& args,
                       const choc::value::Value& engineOptions,
                       cmaj::BuildSettings& buildSettings,
                       const cmaj::audio_utils::AudioDeviceOptions& audioOptions)
{
    std::string address = "127.0.0.1";
    std::string port = "51000";

    if (args.containsOption ("--address"))
    {
        address = choc::text::trim (args.removeValueForOption ("--address").toStdString());

        if (auto lastColon = address.rfind (':'); lastColon != std::string::npos)
        {
            auto isNumber = [] (const std::string& s)
            {
                if (s.empty())
                    return false;

                for (auto c : s)
                    if (! choc::text::isDigit (c))
                        return false;

                return true;
            };

            if (auto possiblePort = choc::text::trim (address.substr (lastColon + 1)); isNumber (possiblePort))
            {
                address = address.substr (0, lastColon);
                port = possiblePort;
            }
        }
    }

    if (args.containsOption ("--port"))
        port = choc::text::trim (args.removeValueForOption ("--port").toStdString());

    auto portNum = std::stoi (port);

    if (portNum <= 0 || portNum >= 65536)
        throw std::runtime_error ("Out of range port number");

    std::vector<std::filesystem::path> patchFolders;

    for (auto& arg : args.arguments)
        patchFolders.push_back (arg.resolveAsExistingFolder().getFullPathName().toStdString());

    std::cout << "------------------------------------------------" << std::endl
              << "Starting server process at " << juce::Time::getCurrentTime().toString (true, true, true, true) << std::endl;

    printCmajorVersion();
    std::cout << "Machine: " << juce::SystemStats::getDeviceDescription() << std::endl
              << "OS: " << juce::SystemStats::getOperatingSystemName() << std::endl;

    cmaj::PatchPlayerServer server (address, static_cast<uint16_t> (portNum),
                                    engineOptions, buildSettings, audioOptions, patchFolders);

    std::cout << std::endl
              << "------------------------------------------------" << std::endl
              << std::endl;

    choc::messageloop::run();
}
