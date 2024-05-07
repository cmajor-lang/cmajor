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

#include "choc/audio/choc_MIDIFile.h"
#include "choc/gui/choc_WebView.h"
#include "../../../modules/playback/include/cmaj_PatchPlayer.h"
#include "../../../modules/playback/include/cmaj_AudioFileUtils.h"

//==============================================================================
struct RenderOptions
{
    void parseArguments (choc::ArgumentList& args)
    {
        if (args.size() == 0)
            throw std::runtime_error ("Expected a filename to play");

        if (auto input = args.removeExistingFileIfPresent ("--input"))
            inputAudioFile = input->string();

        if (auto midi = args.removeExistingFileIfPresent ("--midi"))
            inputMIDIFile  = midi->string();

        if (auto length = args.removeIntValue<int64_t> ("--length"))
        {
            if (*length < 0)
                throw std::runtime_error ("Illegal length");

            framesToRender = static_cast<uint64_t> (*length);
        }

        if (auto rate = args.removeIntValue<uint32_t> ("--rate"))
            audioOptions.sampleRate = *rate;

        if (auto chans = args.removeIntValue<uint32_t> ("--channels"))
            audioOptions.outputChannelCount = *chans;

        if (auto blockSize = args.removeIntValue<uint32_t> ("--blockSize"))
            audioOptions.blockSize = *blockSize;
        else
            audioOptions.blockSize = 512;

        outputAudioFile = args.removeExistingFile ("--output").string();

        auto files = args.getAllAsExistingFiles();

        if (files.size() != 1 || files[0].extension() != ".cmajorpatch")
            throw std::runtime_error ("Expected a .cmajorpatch file");

        patchFile = files[0].string();
    }

    std::string patchFile, inputAudioFile, inputMIDIFile, outputAudioFile;
    cmaj::audio_utils::AudioDeviceOptions audioOptions;
    uint64_t framesToRender = 0;
};


//==============================================================================
struct RenderState
{
    RenderState (const RenderOptions& options,
                 const choc::value::Value& engineOptions,
                 cmaj::BuildSettings& buildSettings)
      : patchPlayer (engineOptions, buildSettings, false)
    {
        auto audioOptions = options.audioOptions;
        audioOptions.createPlayer = cmaj::audio_utils::createRenderingPlayer;
        framesToRender = options.framesToRender;

        if (! options.inputAudioFile.empty())
        {
            reader = cmaj::audio_utils::createFileReader (options.inputAudioFile);

            if (reader == nullptr)
                throw std::runtime_error ("Couldn't open input file");

            auto numFrames = reader->getProperties().numFrames;
            auto numChannels = reader->getProperties().numChannels;

            audioOptions.sampleRate = static_cast<uint32_t> (reader->getProperties().sampleRate);
            audioOptions.inputChannelCount = numChannels;

            if (framesToRender == 0)
                framesToRender = numFrames;
        }

        if (! options.inputMIDIFile.empty())
        {
            try
            {
                auto content = choc::file::loadFileAsString (options.inputMIDIFile);

                choc::midi::File midi;
                midi.load (content.data(), content.size());

                inputMIDI = midi.toSequence();

                if (framesToRender == 0 && ! inputMIDI.events.empty())
                {
                    auto midiLength = inputMIDI.events.back().timeStamp;
                    framesToRender = std::max (framesToRender, static_cast<uint64_t> (midiLength * sampleRate));
                }
            }
            catch (const std::exception& e)
            {
                throw std::runtime_error (e.what());
            }
        }

        if (framesToRender <= 0)
            throw std::runtime_error ("If no input file is provided, use --length=<numFrames> to specify the number of frames to render");

        if (audioOptions.sampleRate == 0)
            throw std::runtime_error ("If no input file is provided, use --rate=<rate> to specify the sample-rate");

        sampleRate = static_cast<double> (audioOptions.sampleRate);

        writer = cmaj::audio_utils::createFileWriter (options.outputAudioFile, sampleRate,
                                                      audioOptions.outputChannelCount);

        if (writer == nullptr)
            throw std::runtime_error ("Couldn't open output file");

        audioOptions.provideInput = [this] (choc::buffer::ChannelArrayView<float> audioInput,
                                            std::vector<choc::midi::ShortMessage>& midiMessages,
                                            std::vector<uint32_t>& midiMessageTimes) -> bool
        {
            return this->provideInput (audioInput, midiMessages, midiMessageTimes);
        };

        audioOptions.handleOutput = [this] (const choc::buffer::ChannelArrayView<const float>& audioOutput) -> bool
        {
            return this->handleOutput (audioOutput);
        };

        std::cout << "Rendering: " << options.patchFile << std::endl;

        auto audioMIDIPlayer = std::make_shared<cmaj::audio_utils::MultiClientAudioMIDIPlayer> (audioOptions);
        patchPlayer.setAudioMIDIPlayer (audioMIDIPlayer);

        patchPlayer.onStatusChange = [] (const cmaj::Patch::Status& s)
        {
            if (s.messageList.hasErrors())
                throw std::runtime_error (s.messageList.toString());
        };

        if (! patchPlayer.loadPatch (options.patchFile, true))
            throw std::runtime_error ("Could not load patch");

        stopped = false;
        patchPlayer.startPlayback();
    }

    bool provideInput (choc::buffer::ChannelArrayView<float> audioInput,
                       std::vector<choc::midi::ShortMessage>& midiMessages,
                       std::vector<uint32_t>& midiMessageTimes)
    {
        if (framesRendered >= framesToRender)
        {
            stopped = true;
            return false;
        }

        if (reader != nullptr)
        {
            if (! reader->readFrames (framesRendered, audioInput))
            {
                std::cerr << "Failed to read from audio input" << std::endl;
                stopped = true;
                return false;
            }
        }
        else
        {
            audioInput.clear();
        }

        for (auto& midiEvent : inputMIDIIterator.readNextEvents (audioInput.getNumFrames() / sampleRate))
        {
            if (midiEvent.message.isShortMessage())
            {
                midiMessages.push_back (midiEvent.message.getShortMessage());
                midiMessageTimes.push_back (static_cast<uint32_t> (midiEvent.timeStamp * sampleRate - framesRendered));
            }
        }

        return true;
    }

    bool handleOutput (const choc::buffer::ChannelArrayView<const float>& audioOutput)
    {
        auto numFrames = audioOutput.getNumFrames();

        if (framesRendered + numFrames > framesToRender)
            return handleOutput (audioOutput.getStart (static_cast<choc::buffer::FrameCount> (framesToRender - framesRendered)));

        if (! writer->appendFrames (audioOutput))
        {
            std::cerr << "Failed to write to audio output" << std::endl;
            stopped = true;
            return false;
        }

        framesRendered += audioOutput.getNumFrames();
        return true;
    }

    void waitTillComplete()
    {
        while (! stopped)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    std::atomic<bool> stopped { true };
    uint64_t framesToRender = 0, framesRendered = 0;
    double sampleRate = 0;

    cmaj::PatchPlayer patchPlayer;

    std::unique_ptr<choc::audio::AudioFileReader> reader;
    std::unique_ptr<choc::audio::AudioFileWriter> writer;

    choc::midi::Sequence inputMIDI;
    choc::midi::Sequence::Iterator inputMIDIIterator { inputMIDI };
};


//==============================================================================
void render (choc::ArgumentList& args, const choc::value::Value& engineOptions, cmaj::BuildSettings& buildSettings)
{
    RenderOptions options;
    options.parseArguments (args);

    choc::messageloop::initialise();

    std::optional<std::exception> exceptionThrown;

    auto t = std::thread ([&]
    {
        try
        {
            RenderState renderState (options, engineOptions, buildSettings);
            renderState.waitTillComplete();
        }
        catch (const std::exception& e)
        {
            exceptionThrown = e;
        }
        catch (...)
        {
            exceptionThrown = std::runtime_error ("unknown exception");
        }

        choc::messageloop::stop();
    });

    choc::messageloop::run();
    t.join();

    if (exceptionThrown)
        throw *exceptionThrown;
}
