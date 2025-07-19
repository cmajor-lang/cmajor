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

#include "../../choc/choc/audio/choc_AudioFileFormat.h"
#include "../../choc/choc/audio/choc_SincInterpolator.h"
#include "../../choc/choc/audio/choc_SampleBufferUtilities.h"

namespace cmaj
{

//==============================================================================
/// Describes an external variable
struct ExternalVariable
{
    std::string name;
    choc::value::Type type;
    choc::value::Value annotation;

    static ExternalVariable fromJSON (const choc::value::ValueView& json)
    {
        ExternalVariable e;

        e.name = json["name"].getString();
        e.type = choc::value::Type::fromValue (json["type"]);
        e.annotation = json["annotation"];

        return e;
    }

    choc::value::Value toJSON() const
    {
        return choc::json::create ("name", name,
                                   "type", type.toValue(),
                                   "annotation", annotation);
    }
};


//==============================================================================
/// A list of the external variables in a program
struct ExternalVariableList
{
    std::vector<ExternalVariable> externals;

    static ExternalVariableList fromJSON (const choc::value::ValueView& json)
    {
        if (json.isArray())
        {
            try
            {
                ExternalVariableList list;

                for (uint32_t i = 0; i < json.size(); ++i)
                    list.externals.push_back (ExternalVariable::fromJSON (json[i]));

                return list;
            }
            catch (const std::exception&)
            {}
        }

        return {};
    }

    choc::value::Value toJSON() const
    {
        auto list = choc::value::createEmptyArray();

        for (auto& e : externals)
            list.addArrayElement (e.toJSON());

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
    try
    {
        double targetSampleRate = 0;
        int32_t channelToExtract = -1;

        if (annotation.isObject())
        {
            targetSampleRate = annotation["resample"].getWithDefault<double> (0);
            auto channelToUse = annotation["sourceChannel"];

            if (channelToUse.isInt())
            {
                channelToExtract = channelToUse.getWithDefault<int32_t> (-1);

                if (channelToExtract < 0)
                    return "sourceChannel index is out-of-range";
            }
        }

        auto data = fileFormatList.loadFileContent (fileReader, targetSampleRate, maxNumFrames, maxNumChannels);

        if (channelToExtract >= 0)
        {
            if (channelToExtract >= static_cast<int32_t> (data.frames.getNumChannels()))
                return "sourceChannel index is out-of-range";

            choc::buffer::ChannelArrayBuffer<float> extractedChannel (1u, data.frames.getNumFrames(), false);
            copy (extractedChannel, data.frames.getChannel (static_cast<choc::buffer::ChannelCount> (channelToExtract)));
            data.frames = std::move (extractedChannel);
        }

        result = convertAudioDataToObject (data.frames, data.sampleRate);

        if (result.isVoid())
            return "Failed to decode file";
    }
    catch (const std::exception& e)
    {
        return e.what();
    }

    return {};
}

}
