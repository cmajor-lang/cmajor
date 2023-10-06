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

#include "juce/cmaj_JUCEHeaders.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include "../../../modules/playback/include/cmaj_AudioPlayer.h"
#include "../../../modules/playback/include/cmaj_AllocationChecker.h"

namespace cmaj
{

//==============================================================================
struct JUCEAudioMIDIPlayer  : public cmaj::audio_utils::AudioMIDIPlayer,
                              public juce::AudioIODeviceCallback,
                              public juce::MidiInputCallback,
                              private juce::Timer,
                              private juce::ChangeListener
{
    JUCEAudioMIDIPlayer (const cmaj::audio_utils::AudioDeviceOptions& o)
        : AudioMIDIPlayer (o)
    {
        audioDeviceManager.addChangeListener (this);
    }

    ~JUCEAudioMIDIPlayer() override
    {
        audioDeviceManager.removeChangeListener (this);
        close();
    }

    //==============================================================================
    static std::unique_ptr<AudioMIDIPlayer> create (const cmaj::audio_utils::AudioDeviceOptions& o)
    {
        auto player = std::make_unique<JUCEAudioMIDIPlayer> (o);

        if (player->open())
            return player;

        return {};
    }

    //==============================================================================
    bool open()
    {
        close();

        audioDeviceManager.setCurrentAudioDeviceType (options.audioAPI, true);

        auto setup = audioDeviceManager.getAudioDeviceSetup();

        if (! options.outputDeviceName.empty())
            setup.outputDeviceName = options.outputDeviceName;

        if (! options.inputDeviceName.empty())
            setup.inputDeviceName = options.inputDeviceName;

        if (options.sampleRate > 0)
            setup.sampleRate = options.sampleRate;

       #if CHOC_OSX
        setup.bufferSize = 128;  // The JUCE default seems to be 512 which is a bit too pessimistic
       #endif

        if (options.blockSize > 0)
            setup.bufferSize = static_cast<int> (options.blockSize);

        audioDeviceManager.initialise (static_cast<int> (options.inputChannelCount),
                                       static_cast<int> (options.outputChannelCount),
                                       nullptr, true, {}, std::addressof (setup));

        audioDeviceManager.addMidiInputDeviceCallback ({}, this);
        audioDeviceManager.addAudioCallback (this);

        refreshMIDIDeviceList();
        startTimer (500);

        return refreshCurrentDeviceOptions();
    }

    void close()
    {
        stop();
        audioDeviceManager.removeAudioCallback (this);
        audioDeviceManager.removeMidiInputDeviceCallback ({}, this);
        audioDeviceManager.closeAudioDevice();
        stopTimer();
    }

    void start (cmaj::audio_utils::AudioMIDICallback& c) override
    {
        const std::lock_guard<decltype(callbackLock)> lock (callbackLock);
        callback = std::addressof (c);
        prepareCallback (c);
    }

    void stop() override
    {
        const std::lock_guard<decltype(callbackLock)> lock (callbackLock);
        callback = nullptr;
    }

    cmaj::audio_utils::AvailableAudioDevices getAvailableDevices() override
    {
        cmaj::audio_utils::AvailableAudioDevices result;

        for (auto& type : audioDeviceManager.getAvailableDeviceTypes())
            result.availableAudioAPIs.push_back (type->getTypeName().toStdString());

        if (auto deviceType = audioDeviceManager.getCurrentDeviceTypeObject())
        {
            for (auto type : deviceType->getDeviceNames (true))
                result.availableInputDevices.push_back (type.toStdString());

            for (auto type : deviceType->getDeviceNames (false))
                result.availableOutputDevices.push_back (type.toStdString());
        }

        if (auto device = audioDeviceManager.getCurrentAudioDevice())
        {
            for (auto rate : device->getAvailableSampleRates())
                result.sampleRates.push_back (static_cast<int32_t> (rate));

            for (auto blockSize : device->getAvailableBufferSizes())
                result.blockSizes.push_back (blockSize);
        }

        return result;
    }

private:
    //==============================================================================
    juce::AudioDeviceManager audioDeviceManager;
    double currentSampleRate = 0;

    juce::StringArray midiInputDeviceIDs, midiOutputDeviceIDs;
    std::vector<std::unique_ptr<juce::MidiInput>> activeMIDIInputDevices;
    std::vector<std::unique_ptr<juce::MidiOutput>> activeMIDIOutputDevices;

    cmaj::audio_utils::AudioMIDICallback* callback = nullptr;

    // Tracks the number of invalid MIDI messages we have received
    uint32_t invalidMIDIMessageCount = 0;

    //==============================================================================
    void prepareCallback (cmaj::audio_utils::AudioMIDICallback& c)
    {
        if (currentSampleRate != 0)
            c.prepareToStart (currentSampleRate,
                              [&] (uint32_t frame, choc::midi::ShortMessage message)
                              {
                                  handleOutgoingMidiMessage (frame, message);
                              });
    }

    static std::string getChannelListDescription (const juce::BigInteger& channels)
    {
        std::string s;

        for (int chan = 0; chan <= channels.getHighestBit(); ++chan)
        {
            if (channels[chan])
            {
                if (! s.empty())
                    s += ", ";

                s += std::to_string (chan);
            }
        }

        return s.empty() ? "(none)" : ("(" + s + ")");
    }

    static void printDeviceDescription (std::ostream& out, juce::AudioIODevice& device)
    {
        out << "Audio device: " << device.getName() << " (" << device.getTypeName() << ")" << std::endl
            << device.getCurrentSampleRate() << "Hz, "
            << device.getCurrentBufferSizeSamples() << " frames, latency: "
            << (int) ((device.getOutputLatencyInSamples() + device.getInputLatencyInSamples()) * 1000.0 / device.getCurrentSampleRate()) << "ms"
            << ", output channels: " << getChannelListDescription (device.getActiveOutputChannels())
            << ", input channels: " << getChannelListDescription (device.getActiveInputChannels())
            << std::endl;
    }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message) override
    {
        const std::lock_guard<decltype(callbackLock)> lock (callbackLock);

        if (callback)
            callback->addIncomingMIDIEvent (message.getRawData(), static_cast<uint32_t> (message.getRawDataSize()));
    }

    void handleOutgoingMidiMessage (uint32_t frame, choc::midi::ShortMessage message)
    {
        int numBytesUsed = 0;
        auto m = juce::MidiMessage (message.data, 3, numBytesUsed, 0, double (frame));

        if (numBytesUsed <= 0)
        {
            invalidMIDIMessageCount++;
            return;
        }

        for (auto& device : activeMIDIOutputDevices)
            device->sendMessageNow (m);
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        currentSampleRate = device->getCurrentSampleRate();

        const std::lock_guard<decltype(callbackLock)> lock (callbackLock);

        if (callback)
            prepareCallback (*callback);

        printDeviceDescription (std::cout, *device);
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* input, int numInputChans,
                                           float* const* output, int numOutputChans,
                                           int numFrames, const juce::AudioIODeviceCallbackContext&) override
    {
        cmaj::ScopedAllocationTracker allocationTracker;

        auto inputView = choc::buffer::createChannelArrayView (input,
                                                               static_cast<choc::buffer::ChannelCount> (numInputChans),
                                                               static_cast<choc::buffer::FrameCount> (numFrames));

        auto outputView = choc::buffer::createChannelArrayView (output,
                                                                static_cast<choc::buffer::ChannelCount> (numOutputChans),
                                                                static_cast<choc::buffer::FrameCount> (numFrames));

        const std::lock_guard<decltype(callbackLock)> lock (callbackLock);

        if (callback)
            callback->process (inputView, outputView, true);
        else
            outputView.clear();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        refreshCurrentDeviceOptions();

        if (deviceOptionsChanged)
            deviceOptionsChanged();
    }

    bool refreshCurrentDeviceOptions()
    {
        if (auto device = audioDeviceManager.getCurrentAudioDevice())
        {
            options.audioAPI = device->getTypeName().toStdString();

            auto deviceSetup = audioDeviceManager.getAudioDeviceSetup();
            options.outputDeviceName = deviceSetup.outputDeviceName.toStdString();
            options.inputDeviceName  = deviceSetup.inputDeviceName.toStdString();

            options.sampleRate = static_cast<uint32_t> (device->getCurrentSampleRate());
            options.blockSize = static_cast<uint32_t> (device->getCurrentBufferSizeSamples());
            options.inputChannelCount = static_cast<uint32_t> (device->getInputChannelNames().size());
            options.outputChannelCount = static_cast<uint32_t> (device->getOutputChannelNames().size());

            return true;
        }

        options.audioAPI = {};
        options.outputDeviceName = {};
        options.inputDeviceName  = {};
        options.sampleRate = {};
        options.blockSize = {};
        options.inputChannelCount = {};
        options.outputChannelCount = {};

        return false;
    }

    void refreshMIDIDeviceList()
    {
        // Input devices
        {
            juce::StringArray devicesNeeded;

            for (auto& device : juce::MidiInput::getAvailableDevices())
                devicesNeeded.add (device.identifier);

            if (midiInputDeviceIDs != devicesNeeded)
            {
                midiInputDeviceIDs = devicesNeeded;
                std::vector<std::unique_ptr<juce::MidiInput>> newDevices;

                for (auto& device : activeMIDIInputDevices)
                {
                    if (devicesNeeded.contains (device->getIdentifier()))
                    {
                        devicesNeeded.removeString (device->getIdentifier());
                        newDevices.emplace_back (std::move (device));
                    }
                    else
                    {
                        std::cout << "MIDI input closed: " << device->getName() << std::endl;
                    }
                }

                for (auto& deviceID : devicesNeeded)
                {
                    if (auto device = juce::MidiInput::openDevice (deviceID, this))
                    {
                        std::cout << "Opening MIDI input: " << device->getName() << std::endl;
                        device->start();
                        newDevices.emplace_back (std::move (device));
                    }
                }

                activeMIDIInputDevices = std::move (newDevices);
            }
        }

        // Output devices
        {
            juce::StringArray devicesNeeded;

            for (auto& device : juce::MidiOutput::getAvailableDevices())
                devicesNeeded.add (device.identifier);

            if (midiOutputDeviceIDs != devicesNeeded)
            {
                midiOutputDeviceIDs = devicesNeeded;
                std::vector<std::unique_ptr<juce::MidiOutput>> newDevices;

                for (auto& device : activeMIDIOutputDevices)
                {
                    if (devicesNeeded.contains (device->getIdentifier()))
                    {
                        devicesNeeded.removeString (device->getIdentifier());
                        newDevices.emplace_back (std::move (device));
                    }
                    else
                    {
                        std::cout << "MIDI output closed: " << device->getName() << std::endl;
                    }
                }

                // For now write to all output devices
                for (auto& deviceID : devicesNeeded)
                {
                    if (auto device = juce::MidiOutput::openDevice (deviceID))
                    {
                        std::cout << "Opening MIDI output: " << device->getName() << std::endl;
                        newDevices.emplace_back (std::move (device));
                    }
                }

                activeMIDIOutputDevices = std::move (newDevices);
            }

        }
    }

    void timerCallback() override
    {
        refreshMIDIDeviceList();
        startTimer (2500);
    }
};

}
