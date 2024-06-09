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

#include <mutex>
#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "choc/audio/choc_AudioMIDIBlockDispatcher.h"

namespace cmaj::audio_utils
{

struct AudioMIDICallback
{
    virtual ~AudioMIDICallback() = default;

    virtual void sampleRateChanged (double newRate) = 0;
    virtual void startBlock() = 0;
    virtual void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block&, bool replaceOutput) = 0;
    virtual void endBlock() = 0;
};

//==============================================================================
struct AudioDeviceOptions
{
    uint32_t sampleRate = 0;
    uint32_t blockSize = 0;
    uint32_t inputChannelCount = 2;
    uint32_t outputChannelCount = 2;
    std::string audioAPI, inputDeviceName, outputDeviceName;
};

//==============================================================================
struct AvailableAudioDevices
{
    std::vector<std::string> availableAudioAPIs,
                             availableInputDevices,
                             availableOutputDevices;

    std::vector<int32_t> sampleRates, blockSizes;
};

//==============================================================================
struct AudioMIDIPlayer
{
    AudioMIDIPlayer (const AudioDeviceOptions& o) : options (o) {}
    virtual ~AudioMIDIPlayer() = default;

    virtual void start (AudioMIDICallback&) = 0;
    virtual void stop() = 0;

    virtual AvailableAudioDevices getAvailableDevices() = 0;

    /// The options that this device was created with.
    AudioDeviceOptions options;

    /// The player will use this lock around any calls to the callback.
    std::mutex callbackLock;

    /// Provide this callback if you want to know when the options
    /// are changed (e.g. the sample rate). No guarantees about which
    /// thread may call it.
    std::function<void()> deviceOptionsChanged;


protected:
    //==============================================================================
    AudioMIDICallback* callback = nullptr;
    choc::audio::AudioMIDIBlockDispatcher dispatcher;

    void prepareToStart (cmaj::audio_utils::AudioMIDICallback& c, double newSampleRate, choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn handleOutgoingMIDI)
    {
        const std::scoped_lock lock (callbackLock);
        callback = std::addressof (c);

        if (newSampleRate != 0)
        {
            c.sampleRateChanged (newSampleRate);
            dispatcher.reset (newSampleRate);
            dispatcher.setMidiOutputCallback (std::move (handleOutgoingMIDI));
        }
    }

    void clearCallback()
    {
        const std::scoped_lock lock (callbackLock);
        callback = nullptr;
    }

    void addIncomingMIDIEvent (const void* data, uint32_t size)
    {
        const std::scoped_lock lock (callbackLock);
        dispatcher.addMIDIEvent (data, size);
    }

    void process (choc::buffer::ChannelArrayView<const float> input,
                  choc::buffer::ChannelArrayView<float> output,
                  bool replaceOutput)
    {
        const std::scoped_lock lock (callbackLock);

        if (callback)
        {
            callback->startBlock();
            dispatcher.setAudioBuffers (input, output);

            dispatcher.processInChunks ([this, replaceOutput]
                                        (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                callback->processSubBlock (block, replaceOutput);
            });

            callback->endBlock();
        }
        else
        {
            output.clear();
        }
    }
};

}
