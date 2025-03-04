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

#include <iomanip>
#include "../../../modules/playback/include/cmaj_PatchWindow.h"
#include "../../../modules/scripting/include/cmaj_ScriptEngine.h"
#include "../../../modules/server/include/cmaj_PatchPlayerServer.h"
#include "choc/containers/choc_ArgumentList.h"
#include "../../../modules/playback/include/cmaj_RtAudioPlayer.h"

void printCmajorVersion();

static std::unique_ptr<cmaj::audio_utils::AudioMIDIPlayer> createDefaultAudioDevice (const cmaj::audio_utils::AudioDeviceOptions& audioOptions)
{
    return cmaj::audio_utils::createRtAudioMIDIPlayer (audioOptions);
}


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
void playFile (choc::ArgumentList& args,
               const choc::value::Value& engineOptions,
               cmaj::BuildSettings& buildSettings,
               const cmaj::audio_utils::AudioDeviceOptions& audioOptions)
{
    if (args.size() == 0)
        throw std::runtime_error ("Expected a filename to play");

    bool noGUI       = args.removeIfFound ("--no-gui");
    bool stopOnError = args.removeIfFound ("--stop-on-error");
    bool dryRun      = args.removeIfFound ("--dry-run");

    int64_t framesToRender = 0;

    if (auto length = args.removeIntValue<int64_t> ("--length"))
    {
        framesToRender = *length;

        if (framesToRender < 0)
            throw std::runtime_error ("Illegal length");
    }

    auto files = args.getAllAsExistingFiles();

    if (files.size() != 1)
        throw std::runtime_error ("Expected a .cmajorpatch file");

    auto file = files[0];

    if (file.extension() == ".js")
        return cmaj::javascript::executeScript (file.string(), engineOptions, buildSettings, audioOptions);

    if (file.extension() != ".cmajorpatch")
        throw std::runtime_error ("Expected a .cmajorpatch file");

    if (dryRun)
    {
        cmaj::Patch patch;

        patch.createEngine                = [] { return cmaj::Engine::create(); };
        patch.stopPlayback                = [] {};
        patch.startPlayback               = [] {};
        patch.patchChanged                = [] {};
        patch.statusChanged               = [] (const cmaj::Patch::Status& s)   { std::cout << s.statusMessage << std::endl; };
        patch.createContextForPatchWorker = [] (const std::string&)             { return std::unique_ptr<cmaj::Patch::WorkerContext>(); };

        patch.setHostDescription ("Cmajor Player");

        {
            cmaj::Patch::PlaybackParams params;
            params.blockSize = audioOptions.blockSize == 0 ? 256 : audioOptions.blockSize;
            params.sampleRate = audioOptions.sampleRate == 0 ? 44100 : audioOptions.sampleRate;
            params.numInputChannels = audioOptions.inputChannelCount == 0 ? 2 : audioOptions.inputChannelCount;
            params.numOutputChannels = audioOptions.outputChannelCount == 0 ? 2 : audioOptions.outputChannelCount;
            patch.setPlaybackParams (params);
        }

        patch.loadPatchFromFile (file.string(), true);
        return;
    }

    auto audioPlayer = createDefaultAudioDevice (audioOptions);

    if (noGUI)
    {
        cmaj::PatchPlayer player (engineOptions, buildSettings, true);
        player.setAudioMIDIPlayer (std::move (audioPlayer));
        player.startPlayback();
        runPatch (player, file.string(), framesToRender, stopOnError);
    }
    else
    {
        choc::ui::setWindowsDPIAwareness();
        cmaj::PatchWindow patchWindow (engineOptions, buildSettings);
        patchWindow.player.setAudioMIDIPlayer (std::move (audioPlayer));
        runPatch (patchWindow.player, file.string(), framesToRender, stopOnError);
    }
}


//==============================================================================
void runServerProcess (choc::ArgumentList& args,
                       const choc::value::Value& engineOptions,
                       cmaj::BuildSettings& buildSettings,
                       const cmaj::audio_utils::AudioDeviceOptions& audioOptions)
{
    cmaj::ServerOptions serverOptions;

    std::string port = "51000";

    if (auto addr = args.removeValueFor ("--address"))
    {
        serverOptions.address = choc::text::trim (*addr);

        if (auto lastColon = serverOptions.address.rfind (':'); lastColon != std::string::npos)
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

            if (auto possiblePort = choc::text::trim (serverOptions.address.substr (lastColon + 1)); isNumber (possiblePort))
            {
                serverOptions.address = serverOptions.address.substr (0, lastColon);
                port = possiblePort;
            }
        }
    }

    if (auto p = args.removeValueFor ("--port"))
        port = choc::text::trim (*p);

    if (auto t = args.removeIntValue<int32_t> ("--timeoutMs"))
        serverOptions.clientTimeoutMs = *t;

    auto portNum = std::stoi (port);

    if (portNum <= 0 || portNum >= 65536)
        throw std::runtime_error ("Out of range port number");

    serverOptions.port           = static_cast<uint16_t> (portNum);
    serverOptions.patchLocations = args.getAllAsExistingFiles();

    auto timeNow = std::chrono::system_clock::to_time_t (std::chrono::system_clock::now());

    std::cout << "------------------------------------------------" << std::endl
              << "Starting server process at " << std::put_time (std::localtime (&timeNow), "%c") << std::endl;

    printCmajorVersion();
    std::cout << "OS: " << CHOC_OPERATING_SYSTEM_NAME << std::endl;

    cmaj::runPatchPlayerServer (serverOptions, engineOptions, buildSettings, audioOptions,
                                [] (const auto& options) { return createDefaultAudioDevice (options); });
}
