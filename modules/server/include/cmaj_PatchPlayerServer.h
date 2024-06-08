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
#include "../../playback/include/cmaj_AudioPlayer.h"
#include "choc/tests/choc_UnitTest.h"


namespace cmaj
{
    using CreateAudioMIDIPlayerFn = std::function<std::unique_ptr<cmaj::audio_utils::AudioMIDIPlayer>(const cmaj::audio_utils::AudioDeviceOptions&)>;

    void runPatchPlayerServer (std::string address,
                               uint16_t port,
                               const choc::value::Value& engineOptions,
                               cmaj::BuildSettings& buildSettings,
                               const cmaj::audio_utils::AudioDeviceOptions& audioOptions,
                               CreateAudioMIDIPlayerFn,
                               std::vector<std::filesystem::path> patchLocationsToScan);

    void runServerUnitTests (choc::test::TestProgress&);
}
