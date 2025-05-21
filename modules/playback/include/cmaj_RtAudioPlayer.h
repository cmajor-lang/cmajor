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

#include "cmaj_AudioMIDIPlayer.h"

namespace cmaj::audio_utils
{

//==============================================================================
///
///  Creates an RtAudio based AudioMIDIPlayer object.
///
std::unique_ptr<cmaj::audio_utils::AudioMIDIPlayer> createRtAudioMIDIPlayer (const cmaj::audio_utils::AudioDeviceOptions&);

}

//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

#include <algorithm>
#include "choc/gui/choc_MessageLoop.h"
#include "choc/text/choc_OpenSourceLicenseList.h"

#if CHOC_OSX
 #define __MACOSX_CORE__ 1
#elif CHOC_WINDOWS
 #define __WINDOWS_WASAPI__ 1
 #define __WINDOWS_DS__ 1
 #define __WINDOWS_MM__ 1
#elif CHOC_LINUX
 #define __LINUX_ALSA__
 #define __UNIX_JACK__ 1
#endif

#include "choc/platform/choc_DisableAllWarnings.h"
#include "../../../3rdParty/rtaudio/RtAudio.cpp"
#include "../../../3rdParty/rtaudio/RtMidi.cpp"
#include "choc/platform/choc_ReenableAllWarnings.h"

namespace cmaj::audio_utils
{

//==============================================================================
struct RtAudioMIDIPlayer  : public AudioMIDIPlayer
{
    RtAudioMIDIPlayer (const AudioDeviceOptions& o)
        : AudioMIDIPlayer (o)
    {
        close();
    }

    ~RtAudioMIDIPlayer() override
    {
        close();
    }

    //==============================================================================
    bool open() override
    {
        close();

        if (openAudio())
        {
            ensureAllMIDIDevicesOpen();
            deviceListCheckTimer = choc::messageloop::Timer (4000u, [this] { return checkDeviceList(); });
            return true;
        }

        return false;
    }

    void close() override
    {
        deviceListCheckTimer = {};
        stop();
        rtMidiIns.clear();
        rtMidiOuts.clear();
        closeAudio();
    }

    std::vector<uint32_t> getAvailableSampleRates() override   { return availableSampleRates; }
    std::vector<uint32_t> getAvailableBlockSizes() override    { return { 16, 32, 48, 64, 96, 128, 196, 224, 256, 320, 480, 512, 768, 1024, 1536, 2048 }; }

    std::vector<std::string> getAvailableAudioAPIs() override
    {
        std::vector<std::string> result;
        std::vector<RtAudio::Api> apis;
        RtAudio::getCompiledApi (apis);

        for (auto& api : apis)
            result.push_back (RtAudio::getApiDisplayName (api));

        return result;
    }

    std::vector<std::string> getAvailableInputDevices() override
    {
        std::vector<std::string> result;

        for (auto& device : getAudioDeviceList())
            if (device.inputChannels != 0)
                result.push_back (device.name);

        return result;
    }

    std::vector<std::string> getAvailableOutputDevices() override
    {
        std::vector<std::string> result;

        for (auto& device : getAudioDeviceList())
            if (device.outputChannels != 0)
                result.push_back (device.name);

        return result;
    }

    std::string getLastError() override
    {
        return lastError;
    }

private:
    void start() override {}
    void stop() override {}

    //==============================================================================
    struct NamedMIDIIn
    {
        RtAudioMIDIPlayer* owner = {};
        std::unique_ptr<RtMidiIn> midiIn;
        std::string name;
    };

    struct NamedMIDIOut
    {
        std::unique_ptr<RtMidiOut> midiOut;
        std::string name;
    };

    std::unique_ptr<RtAudio> rtAudio;
    std::vector<std::unique_ptr<NamedMIDIIn>> rtMidiIns;
    std::vector<NamedMIDIOut> rtMidiOuts;

    choc::buffer::ChannelCount numInputChannels = {}, numOutputChannels = {};
    std::vector<const float*> inputChannelPointers;
    std::vector<float*> outputChannelPointers;
    uint32_t xruns = 0;
    std::vector<uint32_t> availableSampleRates;
    choc::messageloop::Timer deviceListCheckTimer;
    std::string lastError;

    void handleAudioError (RtAudioErrorType, const std::string& errorText)
    {
        lastError = errorText;
    }

    void handleStreamUpdate()
    {
        updateSampleRate (static_cast<uint32_t> (rtAudio->getStreamSampleRate()));
    }

    void handleMIDIError (NamedMIDIIn& m, RtMidiError::Type type, const std::string& errorText)
    {
        std::cout << "MIDI device error: Device: " << m.name << ": " << errorText << std::endl;
        (void) type;
    }

    uint32_t chooseBestSampleRate() const
    {
        auto preferredRate = options.sampleRate > 0 ? options.sampleRate : 44100u;

        if (options.sampleRate > 0)
            for (auto rate : availableSampleRates)
                if (rate == preferredRate)
                    return rate;

        for (auto rate : availableSampleRates)
            if (rate >= 44100u)
                return rate;

        if (! availableSampleRates.empty())
            return availableSampleRates.back();

        return 44100;
    }

    bool openAudio()
    {
        lastError = {};

        rtAudio = std::make_unique<RtAudio> (getAPIToUse(),
                                             [this] (RtAudioErrorType type, const std::string& errorText) { handleAudioError (type, errorText); },
                                             [this] () { handleStreamUpdate (); });

        auto devices = getAudioDeviceList();

        auto getDeviceForID = [&] (unsigned int deviceID) -> RtAudio::DeviceInfo*
        {
            for (auto& i : devices)
                if (i.ID == deviceID)
                    return std::addressof (i);

            return nullptr;
        };

        auto getDeviceForName = [&] (const std::string& name, bool isInput) -> RtAudio::DeviceInfo*
        {
            for (auto& i : devices)
                if (i.name == name && isInput == (i.inputChannels != 0))
                    return std::addressof (i);

            return nullptr;
        };

        auto inputDeviceInfo = options.inputDeviceName.empty() ? getDeviceForID (rtAudio->getDefaultInputDevice())
                                                               : getDeviceForName (options.inputDeviceName, true);

        auto outputDeviceInfo = options.outputDeviceName.empty() ? getDeviceForID (rtAudio->getDefaultOutputDevice())
                                                                 : getDeviceForName (options.outputDeviceName, false);

        updateAvailableSampleRateList (inputDeviceInfo, outputDeviceInfo);

        options.outputDeviceName = outputDeviceInfo ? outputDeviceInfo->name : std::string();
        options.inputDeviceName  = inputDeviceInfo ? inputDeviceInfo->name : std::string();

        RtAudio::StreamParameters inParams, outParams;

        if (options.inputChannelCount == 0)
        {
            inputDeviceInfo = nullptr;
        }
        else
        {
            if (inputDeviceInfo != nullptr)
            {
                numInputChannels = static_cast<choc::buffer::ChannelCount> (std::min (options.inputChannelCount,
                                                                                      inputDeviceInfo->inputChannels));
                inputChannelPointers.resize (numInputChannels);
                inParams.deviceId = inputDeviceInfo->ID;
                inParams.nChannels = static_cast<unsigned int> (numInputChannels);
                inParams.firstChannel = 0;
            }
        }

        if (options.outputChannelCount == 0)
        {
            outputDeviceInfo = nullptr;
        }
        else
        {
            CMAJ_ASSERT (outputDeviceInfo != nullptr);
            numOutputChannels = static_cast<choc::buffer::ChannelCount> (std::min (options.outputChannelCount,
                                                                                   outputDeviceInfo->outputChannels));
            outputChannelPointers.resize (numOutputChannels);
            outParams.deviceId = outputDeviceInfo->ID;
            outParams.nChannels = static_cast<unsigned int> (numOutputChannels);
            outParams.firstChannel = 0;
        }

        auto framesPerBuffer = static_cast<unsigned int> (options.blockSize);

        if (framesPerBuffer == 0)
            framesPerBuffer = 128;

        RtAudio::StreamOptions streamOptions;
        streamOptions.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_SCHEDULE_REALTIME | RTAUDIO_ALSA_USE_DEFAULT;

        auto error = rtAudio->openStream (outputDeviceInfo != nullptr ? std::addressof (outParams) : nullptr,
                                          inputDeviceInfo != nullptr ? std::addressof (inParams) : nullptr,
                                          RTAUDIO_FLOAT32,
                                          (unsigned int) chooseBestSampleRate(),
                                          std::addressof (framesPerBuffer),
                                          rtAudioCallback,
                                          this, std::addressof (streamOptions));

        if (error != RTAUDIO_NO_ERROR)
        {
            if (lastError.empty())
                lastError = rtAudio->getErrorText();

            options.audioAPI = {};
            options.outputDeviceName = {};
            options.inputDeviceName  = {};
            options.sampleRate = {};
            options.blockSize = {};
            options.inputChannelCount = {};
            options.outputChannelCount = {};

            return false;
        }

        options.audioAPI = RtAudio::getApiDisplayName (rtAudio->getCurrentApi());
        options.sampleRate = static_cast<uint32_t> (rtAudio->getStreamSampleRate());
        options.blockSize = static_cast<uint32_t> (framesPerBuffer);
        options.inputChannelCount = static_cast<uint32_t> (numInputChannels);
        options.outputChannelCount = static_cast<uint32_t> (numOutputChannels);

        ensureAllMIDIDevicesOpen();

        rtAudio->startStream();

        std::cout << "Audio API: " << options.audioAPI
                  << ", Output device: " << options.outputDeviceName
                  << ", Input device: " << options.inputDeviceName
                  << ", Rate: " << options.sampleRate
                  << "Hz, Block size: " << options.blockSize
                  << " frames, Latency: " << rtAudio->getStreamLatency()
                  << " frames, Output channels: " << options.outputChannelCount
                  << ", Input channels: " << options.inputChannelCount << std::endl;

        return true;
    }

    void closeAudio()
    {
        if (rtAudio != nullptr)
        {
            rtAudio->closeStream();
            rtAudio.reset();
        }

        lastError = {};
        xruns = 0;
        numInputChannels = {};
        numOutputChannels = {};
        updateAvailableSampleRateList (nullptr, nullptr);
    }

    RtAudio::Api getAPIToUse() const
    {
        if (! options.audioAPI.empty())
        {
            std::vector<RtAudio::Api> apis;
            RtAudio::getCompiledApi (apis);

            for (auto api : apis)
                if (RtAudio::getApiDisplayName (api) == options.audioAPI)
                    return api;
        }

        return RtAudio::Api::UNSPECIFIED;
    }

    //==============================================================================
    void handleOutgoingMidiMessage (const void* data, uint32_t length) override
    {
        for (auto& out : rtMidiOuts)
            out.midiOut->sendMessage (static_cast<const unsigned char*> (data), length);
    }

    static int rtAudioCallback (void* output, void* input, unsigned int numFrames,
                                double streamTime, RtAudioStreamStatus status, void* userData)
    {
        return static_cast<RtAudioMIDIPlayer*> (userData)
                ->audioCallback (static_cast<float*> (output), static_cast<const float*> (input),
                                 static_cast<choc::buffer::FrameCount> (numFrames), streamTime, status);
    }

    static void rtMidiCallback (double, std::vector<unsigned char>* message, void* userData)
    {
        auto& m = *static_cast<NamedMIDIIn*> (userData);
        m.owner->midiCallback (m, message->data(), static_cast<uint32_t> (message->size()));
    }

    static void rtMidiErrorCallback (RtMidiError::Type type, const std::string& errorText, void* userData)
    {
        auto& m = *static_cast<NamedMIDIIn*> (userData);
        m.owner->handleMIDIError (m, type, errorText);
    }

    int audioCallback (float* output, const float* input, choc::buffer::FrameCount numFrames,
                       double streamTime, RtAudioStreamStatus status)
    {
        (void) streamTime;

        if ((status & RTAUDIO_INPUT_OVERFLOW) || (status & RTAUDIO_OUTPUT_UNDERFLOW))
            ++xruns;

        for (uint32_t i = 0; i < numInputChannels; ++i)
            inputChannelPointers[i] = input + numFrames * i;

        for (uint32_t i = 0; i < numOutputChannels; ++i)
            outputChannelPointers[i] = output + numFrames * i;

        auto inputView = choc::buffer::createChannelArrayView (inputChannelPointers.data(), numInputChannels, numFrames);
        auto outputView = choc::buffer::createChannelArrayView (outputChannelPointers.data(), numOutputChannels, numFrames);

        process (inputView, outputView, true);

        return 0;
    }

    void midiCallback (NamedMIDIIn& m, const void* data, uint32_t size)
    {
        addIncomingMIDIEvent (m.name.c_str(), data, size);
    }

    std::vector<RtAudio::DeviceInfo> getAudioDeviceList() const
    {
        std::vector<RtAudio::DeviceInfo> list;

        for (auto i : rtAudio->getDeviceIds())
            list.push_back (rtAudio->getDeviceInfo (i));

        return list;
    }

    bool checkDeviceList()
    {
        ensureAllMIDIDevicesOpen();
        return true;
    }

    void updateAvailableSampleRateList (RtAudio::DeviceInfo* inputDeviceInfo, RtAudio::DeviceInfo* outputDeviceInfo)
    {
        std::vector<unsigned int> rates;

        if (inputDeviceInfo != nullptr && outputDeviceInfo != nullptr)
        {
            auto inRates = inputDeviceInfo->sampleRates;
            auto outRates = outputDeviceInfo->sampleRates;
            std::sort (inRates.begin(), inRates.end());
            std::sort (outRates.begin(), outRates.end());

            std::set_intersection (inRates.begin(), inRates.end(),
                                   outRates.begin(), outRates.end(),
                                   std::back_inserter (rates));
        }
        else if (inputDeviceInfo != nullptr)
        {
            rates = inputDeviceInfo->sampleRates;
        }
        else if (outputDeviceInfo != nullptr)
        {
            rates = outputDeviceInfo->sampleRates;
        }

        std::sort (rates.begin(), rates.end());
        rates.erase (std::unique (rates.begin(), rates.end()), rates.end());

        if (rates.empty())
            availableSampleRates = { 44100, 48000 };
        else
            availableSampleRates = rates;
    }

    std::unique_ptr<NamedMIDIIn> openMIDIIn (unsigned int portNum)
    {
        static constexpr unsigned int queueSize = 512;

        auto m = std::make_unique<NamedMIDIIn>();
        m->owner = this;
        m->midiIn = std::make_unique<RtMidiIn> (RtMidi::Api::UNSPECIFIED, "Cmajor", queueSize);
        m->midiIn->setCallback (rtMidiCallback, m.get());
        m->midiIn->setErrorCallback (rtMidiErrorCallback, m.get());
        m->midiIn->openPort (portNum, "Cmajor Input");
        m->name = m->midiIn->getPortName (portNum);
        return m;
    }

    bool isMIDIInOpen (const std::string& name) const
    {
        for (auto& m : rtMidiIns)
            if (m->name == name)
                return true;

        return false;
    }

    NamedMIDIOut openMIDIOut (unsigned int portNum)
    {
        NamedMIDIOut m;
        m.midiOut = std::make_unique<RtMidiOut> (RtMidi::Api::UNSPECIFIED, "Cmajor");
        m.midiOut->setErrorCallback (rtMidiErrorCallback, this);
        m.midiOut->openPort (portNum, "Cmajor Input");
        m.name = m.midiOut->getPortName (portNum);
        return m;
    }

    bool isMIDIOutOpen (const std::string& name) const
    {
        for (auto& m : rtMidiOuts)
            if (m.name == name)
                return true;

        return false;
    }

    void ensureAllMIDIDevicesOpen()
    {
        ensureAllMIDIInputsOpen();
        ensureAllMIDIOutputsOpen();
    }

    void ensureAllMIDIInputsOpen()
    {
        try
        {
            std::vector<std::string> newInputs;

            {
                RtMidiIn m;
                m.setErrorCallback (rtMidiErrorCallback, this);

                auto numPorts = m.getPortCount();

                for (unsigned int i = 0; i < numPorts; ++i)
                    newInputs.push_back (m.getPortName (i));
            }

            for (auto i = rtMidiIns.begin(); i != rtMidiIns.end();)
            {
                if (std::find (newInputs.begin(), newInputs.end(), (*i)->name) == newInputs.end())
                {
                    std::cout << "Closing MIDI input: " << (*i)->name << std::endl;
                    i = rtMidiIns.erase(i);
                }
                else
                {
                    ++i;
                }
            }

            for (unsigned int i = 0; i < newInputs.size(); ++i)
            {
                if (! isMIDIInOpen (newInputs[i]))
                {
                    std::cout << "Opening MIDI input: " << newInputs[i] << std::endl;
                    rtMidiIns.push_back (openMIDIIn (i));
                }
            }
        }
        catch (const RtMidiError& e)
        {
            e.printMessage();
        }
    }

    void ensureAllMIDIOutputsOpen()
    {
        try
        {
            std::vector<std::string> newOutputs;

            {
                RtMidiOut m;
                m.setErrorCallback (rtMidiErrorCallback, this);

                auto numPorts = m.getPortCount();

                for (unsigned int i = 0; i < numPorts; ++i)
                    newOutputs.push_back (m.getPortName (i));
            }

            for (auto i = rtMidiOuts.begin(); i != rtMidiOuts.end();)
            {
                if (std::find (newOutputs.begin(), newOutputs.end(), i->name) == newOutputs.end())
                {
                    std::cout << "Closing MIDI output: " << i->name << std::endl;
                    i = rtMidiOuts.erase(i);
                }
                else
                {
                    ++i;
                }
            }

            for (unsigned int i = 0; i < newOutputs.size(); ++i)
            {
                if (! isMIDIOutOpen (newOutputs[i]))
                {
                    std::cout << "Opening MIDI output: " << newOutputs[i] << std::endl;
                    rtMidiOuts.push_back (openMIDIOut (i));
                }
            }
        }
        catch (const RtMidiError& e)
        {
            e.printMessage();
        }
    }
};

//==============================================================================
inline std::unique_ptr<AudioMIDIPlayer> createRtAudioMIDIPlayer (const AudioDeviceOptions& o)
{
    auto player = std::make_unique<RtAudioMIDIPlayer> (o);

    if (player->open())
        return player;

    std::cout << "Failed to open audio device: " << player->getLastError() << std::endl;
    return {};
}


CHOC_REGISTER_OPEN_SOURCE_LICENCE(RtAudio, R"(
==============================================================================
RtAudio license:

RtAudio provides a common API (Application Programming Interface)
for realtime audio input/output across Linux (native ALSA, Jack,
and OSS), Macintosh OS X (CoreAudio and Jack), and Windows
(DirectSound, ASIO and WASAPI) operating systems.

RtAudio GitHub site: https://github.com/thestk/rtaudio
RtAudio WWW site: http://www.music.mcgill.ca/~gary/rtaudio/

RtAudio: realtime audio i/o C++ classes
Copyright (c) 2001-2023 Gary P. Scavone

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

Any person wishing to distribute modifications to the Software is
asked to send the modifications to the original developer so that
they can be incorporated into the canonical version.  This is,
however, not a binding provision of this license.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
)")

CHOC_REGISTER_OPEN_SOURCE_LICENCE(RtMidi, R"(
==============================================================================
RtMidi license:

This class implements some common functionality for the realtime
MIDI input/output subclasses RtMidiIn and RtMidiOut.

RtMidi GitHub site: https://github.com/thestk/rtmidi
RtMidi WWW site: http://www.music.mcgill.ca/~gary/rtmidi/

RtMidi: realtime MIDI i/o C++ classes
Copyright (c) 2003-2023 Gary P. Scavone

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

Any person wishing to distribute modifications to the Software is
asked to send the modifications to the original developer so that
they can be incorporated into the canonical version.  This is,
however, not a binding provision of this license.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
)")

} // namespace cmaj::audio_utils
