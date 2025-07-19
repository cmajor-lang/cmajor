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

#include <filesystem>
#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../../../include/cmajor/API/cmaj_Engine.h"
#include "choc/audio/io/choc_AudioMIDIPlayer.h"
#include "choc/platform/choc_UnitTest.h"


namespace cmaj
{
    using CreateAudioMIDIPlayerFn = std::function<std::unique_ptr<choc::audio::io::AudioMIDIPlayer>(const choc::audio::io::AudioDeviceOptions&)>;

    struct ServerOptions
    {
        std::string address         = "127.0.0.1";
        uint16_t port               = 51000;
        int32_t clientTimeoutMs     = 5000;
        uint32_t maxNumSessions     = 16;

        std::vector<std::filesystem::path> patchLocations;
    };

    void runPatchPlayerServer (const ServerOptions& serverOptions,
                               const choc::value::Value& engineOptions,
                               cmaj::BuildSettings& buildSettings,
                               const choc::audio::io::AudioDeviceOptions& audioOptions,
                               CreateAudioMIDIPlayerFn);

    void runServerUnitTests (choc::test::TestProgress&);
}
