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

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../include/cmaj_AudioFileUtils.h"

#ifdef __EMSCRIPTEN__
namespace cmaj::audio_utils
{
choc::audio::AudioFileFormatList getAudioFormatListForReading()                                       { return {}; }
std::unique_ptr<choc::audio::AudioFileReader> createFileReader (const std::string&)                   { return {}; }
std::unique_ptr<choc::audio::AudioFileWriter> createFileWriter (const std::string&, double, uint32_t) { return {}; }
}

#else

#include "choc/audio/choc_AudioFileFormat_WAV.h"
#include "choc/audio/choc_AudioFileFormat_Ogg.h"
#include "choc/audio/choc_AudioFileFormat_FLAC.h"
#include "choc/audio/choc_AudioFileFormat_MP3.h"

namespace cmaj::audio_utils
{

choc::audio::AudioFileFormatList getAudioFormatListForReading()
{
    choc::audio::AudioFileFormatList formats;
    formats.addFormat<choc::audio::OggAudioFileFormat<false>>();
    formats.addFormat<choc::audio::MP3AudioFileFormat>();
    formats.addFormat<choc::audio::FLACAudioFileFormat<false>>();
    formats.addFormat<choc::audio::WAVAudioFileFormat<true>>();
    return formats;
}

std::unique_ptr<choc::audio::AudioFileReader> createFileReader (const std::string& name)
{
    return getAudioFormatListForReading().createReader (name);
}

std::unique_ptr<choc::audio::AudioFileWriter> createFileWriter (const std::string& name, double sampleRate, uint32_t channelCount)
{
    choc::audio::WAVAudioFileFormat<true> wav;

    choc::audio::AudioFileProperties props;
    props.bitDepth = choc::audio::BitDepth::float32;
    props.sampleRate = sampleRate;
    props.numChannels = channelCount;

    return wav.createWriter (name, props);
}

}

#endif
