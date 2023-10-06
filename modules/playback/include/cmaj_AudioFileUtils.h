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

#include "choc/containers/choc_Value.h"
#include "choc/audio/choc_AudioFileFormat.h"

namespace cmaj::audio_utils
{

choc::audio::AudioFileFormatList getAudioFormatListForReading();
std::unique_ptr<choc::audio::AudioFileReader> createFileReader (const std::string& name);
std::unique_ptr<choc::audio::AudioFileWriter> createFileWriter (const std::string& name, double sampleRate, uint32_t channelCount);

}
