//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include "../../choc/audio/choc_AudioFileFormat.h"
#include "../../choc/audio/choc_SincInterpolator.h"
#include "../../choc/audio/choc_SampleBufferUtilities.h"

namespace cmaj
{

//==============================================================================
/// Describes an external variable
struct ExternalVariable
{
    std::string name;
    choc::value::Type type;
    choc::value::Value annotation;
};


//==============================================================================
/// A list of the external variables in a program
struct ExternalVariableList
{
    std::vector<ExternalVariable> externals;

    static ExternalVariableList fromJSON (const choc::value::ValueView& json)
    {
        try
        {
            ExternalVariableList list;

            for (uint32_t i = 0; i < json.size(); ++i)
            {
                list.externals.push_back ({});
                auto& e = list.externals.back();

                auto element = json[i];
                e.name = element["name"].getString();
                e.type = choc::value::Type::fromValue (element["type"]);
                e.annotation = element["annotation"];
            }

            return list;
        }
        catch (const std::exception&)
        {}

        return {};
    }

    choc::value::Value toJSON() const
    {
        auto list = choc::value::createEmptyArray();

        for (auto& e : externals)
            list.addArrayElement (choc::value::createObject ({},
                                                             "name", e.name,
                                                             "type", e.type.toValue(),
                                                             "annotation", e.annotation));
        return list;
    }
};

//==============================================================================
/// Creates an object that corresponds to a Cmajor std::audio_data object.
/// The frames parameter must be an array of frames
inline choc::value::Value createAudioFileObject (const choc::value::ValueView& frames, double sampleRate)
{
    return choc::value::createObject ("AudioFile",
                                      "frames", frames,
                                      "sampleRate", sampleRate);
}

/// Creates an object that can be used as a Cmajor std::audio object, from
/// a frame buffer and a sample rate
inline choc::value::Value convertAudioDataToObject (choc::buffer::ChannelArrayView<float> source, double sampleRate)
{
    choc::buffer::InterleavingScratchBuffer<float> scratchBuffer;
    return createAudioFileObject (choc::buffer::createValueViewFromBuffer (scratchBuffer.interleave (source)), sampleRate);
}

/// Attempts to load the contents of an audio file into a choc::value::Value,
/// so that it can be passed into an engine as an external variable.
/// On success, returns an empty string, or an error message on failure.
inline std::string readAudioFileAsValue (choc::value::Value& result,
                                         const choc::audio::AudioFileFormatList& fileFormatList,
                                         std::shared_ptr<std::istream> fileReader,
                                         const choc::value::ValueView& annotation,
                                         uint32_t maxNumChannels = 16,
                                         uint64_t maxNumFrames = 48000 * 100)
{
    auto reader = fileFormatList.createReader (fileReader);

    if (reader == nullptr)
        return "Cannot open file";

    auto rate = reader->getProperties().sampleRate;
    auto numFrames = reader->getProperties().numFrames;
    auto numChannels = reader->getProperties().numChannels;

    if (rate <= 0)                      return "Cannot open file";
    if (numFrames > maxNumFrames)       return "File too long";
    if (numChannels > maxNumChannels)   return "Too many channels";

    if (numFrames == 0)
        return {};

    choc::buffer::ChannelArrayBuffer<float> buffer (numChannels, static_cast<choc::buffer::FrameCount> (numFrames));

    bool ok = reader->readFrames (0, buffer.getView());

    if (! ok)
        return "Failed to read from file";

    if (annotation.isObject())
    {
        auto channelToUse = annotation["sourceChannel"];

        if (channelToUse.isInt())
        {
            auto channel = channelToUse.getWithDefault<int64_t> (-1);

            if (channel < 0 || channel >= numChannels)
                return "sourceChannel index is out-of-range";

            choc::buffer::ChannelArrayBuffer<float> extractedChannel (1u, static_cast<choc::buffer::FrameCount> (numFrames));
            copy (extractedChannel, buffer.getChannel (static_cast<choc::buffer::ChannelCount> (channel)));
            buffer = std::move (extractedChannel);
            numChannels = 1;
        }

        auto targetRate = annotation["resample"].getWithDefault<double> (0);

        if (targetRate != 0)
        {
            static constexpr double maxRatio = 64.0;

            if (targetRate > rate * maxRatio || targetRate < rate / maxRatio)
                return "Resampling ratio is out-of-range";

            auto ratio = targetRate / rate;
            auto newFrameCount = static_cast<uint64_t> (ratio * numFrames + 0.5);

            if (newFrameCount > maxNumFrames)
                return "File too long";

            if (newFrameCount != numFrames)
            {
                choc::buffer::ChannelArrayBuffer<float> resampledData (numChannels, static_cast<uint32_t> (newFrameCount));
                choc::interpolation::sincInterpolate (resampledData, buffer);
                buffer = std::move (resampledData);
                rate = targetRate;
            }
        }
    }

    result = convertAudioDataToObject (buffer, rate);

    if (result.isVoid())
        return "Failed to encode file";

    return {};
}

}
