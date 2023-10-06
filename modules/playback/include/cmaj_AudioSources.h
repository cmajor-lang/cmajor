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

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "cmajor/helpers/cmaj_Patch.h"
#include "cmaj_AudioFileUtils.h"


namespace cmaj::audio_utils
{

//==============================================================================
struct NamedAudioSource : public Patch::CustomAudioSource
{
    virtual std::string getName() const = 0;
};

//==============================================================================
struct MuteAudioSource : public NamedAudioSource
{
    MuteAudioSource() = default;

    std::string getName() const override  { return "mute"; }
    void prepare (double) override {}
    void read (choc::buffer::InterleavedView<float>  dest) override { dest.clear(); }
    void read (choc::buffer::InterleavedView<double> dest) override { dest.clear(); }
};

//==============================================================================
struct LoopingFilePlayerSource : public NamedAudioSource
{
    LoopingFilePlayerSource (std::shared_ptr<std::istream> s)  : stream (std::move (s))
    {
    }

    ~LoopingFilePlayerSource() override
    {
        prepareThread.stop();
    }

    std::string getName() const override  { return "file"; }

    static constexpr uint64_t maxNumFrames = 48000 * 60 * 5;

    static Patch::CustomAudioSourcePtr createFromData (std::string&& data)
    {
        try
        {
            auto stream = std::make_shared<std::istringstream> (std::move (data), std::ios::binary | std::ios::in);

            if (cmaj::audio_utils::getAudioFormatListForReading().createReader (stream) != nullptr)
                return std::make_shared<LoopingFilePlayerSource> (std::move (stream));
        }
        catch (const std::exception&) {}

        return {};
    }

    static Patch::CustomAudioSourcePtr createFromData (const choc::value::ValueView& data)
    {
        if (! data.isString())
            return {};

        auto encodedData = data.get<std::string_view>();

        if (choc::text::startsWith (encodedData, "data:"))
        {
            auto base64Index = encodedData.find ("base64,");

            if (base64Index != std::string::npos)
            {
                auto base64 = choc::text::trim (encodedData.substr (base64Index + 7));

                std::string decoded;
                decoded.reserve ((base64.length() / 4u) * 3u + 4u);

                if (choc::base64::decode (base64, [&] (uint8_t b) { decoded += static_cast<char> (b); }))
                    return createFromData (std::move (decoded));
            }
        }

        return {};
    }

    template <typename FileCache>
    static Patch::CustomAudioSourcePtr createFromCachedFile (FileCache& fileCache, const std::filesystem::path& filename)
    {
        if (auto fileSize = fileCache.getFileSize (filename))
        {
            auto source = std::make_shared<LoopingFilePlayerSource> (nullptr);

            fileCache.requestRead (filename, { 0, fileSize }, [source] (const void* buffer, size_t size)
            {
                source->stream = std::make_shared<std::istringstream> (std::string (static_cast<const char*> (buffer), size), std::ios::binary | std::ios::in);
                source->startThreadIfPossible();
            });

            return source;
        }

        return {};
    }

    void prepare (double sampleRate) override
    {
        if (preparedSampleRate == sampleRate)
            return;

        dataReady = false;
        preparedSampleRate = sampleRate;
        prepareThread.stop();
        startThreadIfPossible();
    }

    void startThreadIfPossible()
    {
        if (stream != nullptr)
        {
            prepareThread.start (0, [this]
            {
                try
                {
                    stream->seekg (0);
                    data = cmaj::audio_utils::getAudioFormatListForReading().loadFileContent (stream, preparedSampleRate, maxNumFrames);
                    dataReady = true;
                }
                catch (const std::exception&) {}
            });

            prepareThread.trigger();
        }
    }

    void read (choc::buffer::InterleavedView<float> dest) override   { readAnyType (dest); }
    void read (choc::buffer::InterleavedView<double> dest) override  { readAnyType (dest); }

    template <typename Type>
    void readAnyType (choc::buffer::InterleavedView<Type> dest)
    {
        if (! dataReady)
        {
            nextFrameIndex = 0;
            dest.clear();
            return;
        }

        while (nextFrameIndex + dest.getNumFrames() > data.frames.getNumFrames())
        {
            auto numAvailable = data.frames.getNumFrames() - nextFrameIndex;
            copyRemappingChannels (dest.getStart (numAvailable), data.frames.fromFrame (nextFrameIndex));
            nextFrameIndex = 0;

            if (numAvailable == dest.getNumFrames())
                return;

            dest = dest.fromFrame (numAvailable);
        }

        copyRemappingChannels (dest, data.frames.getFrameRange ({ nextFrameIndex, nextFrameIndex + dest.getNumFrames()}));
        nextFrameIndex += dest.getNumFrames();
    }

    std::shared_ptr<std::istream> stream;
    std::atomic<bool> dataReady { false };
    choc::audio::AudioFileData data;
    double preparedSampleRate = 0;
    uint32_t nextFrameIndex = 0;
    choc::threading::TaskThread prepareThread;
};

} // namespace cmaj::audio_utils
