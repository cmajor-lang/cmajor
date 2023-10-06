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

    using HandleMIDIOutEventFn = choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn;

    virtual void prepareToStart (double sampleRate, HandleMIDIOutEventFn) = 0;

    virtual void addIncomingMIDIEvent (const void* data, uint32_t size) = 0;

    virtual void process (choc::buffer::ChannelArrayView<const float> input,
                          choc::buffer::ChannelArrayView<float> output,
                          bool replaceOutput) = 0;
};

struct AudioMIDIPlayer;

//==============================================================================
struct AudioDeviceOptions
{
    uint32_t sampleRate = 0;
    uint32_t blockSize = 0;
    uint32_t inputChannelCount = 2;
    uint32_t outputChannelCount = 2;
    std::string audioAPI, inputDeviceName, outputDeviceName;

    /// If these lambdas are provided, then a fake device is created and these
    /// functions are called on a thread to do the rendering. They can return false to
    /// stop the device
    std::function<bool(choc::buffer::ChannelArrayView<float> audioInput,
                       std::vector<choc::midi::ShortMessage>& midiMessages,
                       std::vector<uint32_t>& midiMessageTimes)> provideInput;
    std::function<bool(choc::buffer::ChannelArrayView<const float> audioOutput)> handleOutput;

    using CreateAudioPlayerFn = std::function<std::shared_ptr<AudioMIDIPlayer>(const AudioDeviceOptions&)>;
    CreateAudioPlayerFn createPlayer;
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

    AudioDeviceOptions options;
    std::mutex callbackLock;
    std::function<void()> deviceOptionsChanged;
};

std::unique_ptr<AudioMIDIPlayer> createDummyPlayer (const AudioDeviceOptions&);
std::unique_ptr<AudioMIDIPlayer> createRenderingPlayer (const AudioDeviceOptions&);


//==============================================================================
struct MultiClientAudioMIDIPlayer  : private AudioMIDICallback
{
    MultiClientAudioMIDIPlayer (const AudioDeviceOptions&);
    ~MultiClientAudioMIDIPlayer() override;

    void addCallback (AudioMIDICallback&);
    void removeCallback (AudioMIDICallback&);

    std::shared_ptr<AudioMIDIPlayer> player;

private:
    std::vector<AudioMIDICallback*> clients;
    double currentRate = 0;
    HandleMIDIOutEventFn currentMIDIFn;

    void prepareToStart (double sampleRate, HandleMIDIOutEventFn) override;
    void addIncomingMIDIEvent (const void* data, uint32_t size) override;
    void process (choc::buffer::ChannelArrayView<const float> input,
                  choc::buffer::ChannelArrayView<float> output,
                  bool replaceOutput) override;
};

}
