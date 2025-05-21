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

//==============================================================================
/**
 *   Contains properties to control the choice and setup of the audio devices when
 *   creating an AudioMIDIPlayer object.
 */
struct AudioDeviceOptions
{
    /// Preferred sample rate, or 0 to use the default.
    uint32_t sampleRate = 0;

    /// Preferred block size, or 0 to use the default.
    uint32_t blockSize = 0;

    /// Number of input channels required.
    uint32_t inputChannelCount = 0;

    /// Number of output channels required.
    uint32_t outputChannelCount = 2;

    /// Optional API to use (e.g. "CoreAudio", "WASAPI").
    /// Leave empty to use the default.
    /// @seeAudioMIDIPlayer::getAvailableAudioAPIs()
    std::string audioAPI;

    /// Optional input device name - leave empty for a default.
    /// You can get these names from AudioMIDIPlayer::getAvailableInputDevices()
    std::string inputDeviceName;

    /// Optional output device name - leave empty for a default.
    /// You can get these names from AudioMIDIPlayer::getAvailableOutputDevices()
    std::string outputDeviceName;
};


//==============================================================================
/**
 *  A callback which can be attached to an AudioMIDIPlayer, to receive callbacks
 *  which process chunks of synchronised audio and MIDI data.
 */
struct AudioMIDICallback
{
    virtual ~AudioMIDICallback() = default;

    /// This will be invoked (on an unspecified thread) if the sample rate
    /// of the device changes while this callback is attached.
    virtual void sampleRateChanged (double newRate) = 0;

    /// This will be called once before a set of calls to processSubBlock() are
    /// made, to allow the client to do any setup work that's needed.
    virtual void startBlock() = 0;

    /// After a call to startBlock(), one or more calls to processSubBlock() will be
    /// made for chunks of the main block, providing any MIDI messages that should be
    /// handled at the start of that particular subsection of the block.
    /// If `replaceOutput` is true, the caller must overwrite any data in the audio
    /// output buffer. If it is false, the caller must add its output to any existing
    /// data in that buffer.
    virtual void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block&,
                                  bool replaceOutput) = 0;

    /// After enough calls to processSubBlock() have been made to process the whole
    /// block, a call to endBlock() allows the client to do any clean-up work necessary.
    virtual void endBlock() = 0;
};

//==============================================================================
/**
 *  A multi-client device abstraction that provides unified callbacks for processing
 *  blocks of audio and MIDI input/output.
 *
*/
struct AudioMIDIPlayer
{
    virtual ~AudioMIDIPlayer() = default;

    //==============================================================================
    /// Attaches a callback to this device.
    void addCallback (AudioMIDICallback&);
    /// Removes a previously-attached callback to this device.
    void removeCallback (AudioMIDICallback&);

    //==============================================================================
    /// The options that this device was created with.
    AudioDeviceOptions options;

    /// Provide this callback if you want to know when the options
    /// are changed (e.g. the sample rate). No guarantees about which
    /// thread may call it.
    std::function<void()> deviceOptionsChanged;

    /// Returns a list of values that AudioDeviceOptions::audioAPI
    /// could be given.
    virtual std::vector<std::string> getAvailableAudioAPIs() = 0;

    /// Returns a list of sample rates that this device could be opened with.
    virtual std::vector<uint32_t> getAvailableSampleRates() = 0;

    /// Returns a list of block sizes that could be used to open this device.
    virtual std::vector<uint32_t> getAvailableBlockSizes() = 0;

    /// Returns a list of devices that could be used for
    /// AudioDeviceOptions::inputDeviceName
    virtual std::vector<std::string> getAvailableInputDevices() = 0;

    /// Returns a list of devices that could be used for
    /// AudioDeviceOptions::outputDeviceName
    virtual std::vector<std::string> getAvailableOutputDevices() = 0;

    /// Attempts to open the device for use, returning true if successful.
    /// On failure, use getLastError() to find out more.
    virtual bool open() = 0;

    /// Closes the device.
    virtual void close() = 0;

    /// If something failed when opening the device, this will return a string
    virtual std::string getLastError() = 0;


protected:
    //==============================================================================
    /// This is an abstract base class, so you don't construct one of them directly.
    /// To get one,
    AudioMIDIPlayer (const AudioDeviceOptions&);

    std::vector<AudioMIDICallback*> callbacks;
    std::mutex callbackLock;
    choc::audio::AudioMIDIBlockDispatcher dispatcher;
    uint32_t prerollFrames = 0;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void handleOutgoingMidiMessage (const void* data, uint32_t length) = 0;

    void updateSampleRate (uint32_t);
    void addIncomingMIDIEvent (choc::audio::AudioMIDIBlockDispatcher::MIDIDeviceID, const void*, uint32_t);
    void process (choc::buffer::ChannelArrayView<const float> input,
                  choc::buffer::ChannelArrayView<float> output, bool replaceOutput);
};




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

inline AudioMIDIPlayer::AudioMIDIPlayer (const AudioDeviceOptions& o) : options (o)
{
    // There seem to be a lot of devices which glitch as they start up, so this silent
    // preroll time gets us past that before the first block of real audio is sent.
    // Would be nice to know when it's actually needed, but hey..
    prerollFrames = 20000;

    dispatcher.setMidiOutputCallback ([this] (uint32_t, choc::midi::MessageView m)
    {
        if (auto len = m.length())
            handleOutgoingMidiMessage (m.data(), len);
    });

    dispatcher.reset (options.sampleRate);
    callbacks.reserve (16);
}

inline void AudioMIDIPlayer::addCallback (AudioMIDICallback& c)
{
    bool needToStart = false;

    if (options.sampleRate != 0)
        c.sampleRateChanged (options.sampleRate);

    {
        const std::scoped_lock lock (callbackLock);

        if (std::find (callbacks.begin(), callbacks.end(), std::addressof (c)) != callbacks.end())
            return;

        needToStart = callbacks.empty();
        callbacks.push_back (std::addressof (c));
    }

    if (needToStart)
        start();
}

inline void AudioMIDIPlayer::removeCallback (AudioMIDICallback& c)
{
    bool needToStop = false;

    {
        const std::scoped_lock lock (callbackLock);

        if (auto i = std::find (callbacks.begin(), callbacks.end(), std::addressof (c)); i != callbacks.end())
            callbacks.erase (i);

        needToStop = callbacks.empty();
    }

    if (needToStop)
        stop();
}

inline void AudioMIDIPlayer::updateSampleRate (uint32_t newRate)
{
    if (options.sampleRate != newRate)
    {
        options.sampleRate = newRate;

        if (newRate != 0)
        {
            const std::scoped_lock lock (callbackLock);

            for (auto c : callbacks)
                c->sampleRateChanged (newRate);

            dispatcher.reset (options.sampleRate);
        }

        if (deviceOptionsChanged)
            deviceOptionsChanged();
    }
}

inline void AudioMIDIPlayer::addIncomingMIDIEvent (choc::audio::AudioMIDIBlockDispatcher::MIDIDeviceID deviceID, const void* data, uint32_t size)
{
    const std::scoped_lock lock (callbackLock);
    dispatcher.addMIDIEvent (deviceID, data, size);
}

inline void AudioMIDIPlayer::process (choc::buffer::ChannelArrayView<const float> input,
                                      choc::buffer::ChannelArrayView<float> output,
                                      bool replaceOutput)
{
    const std::scoped_lock lock (callbackLock);

    if (prerollFrames != 0)
    {
        prerollFrames -= std::min (prerollFrames, input.getNumFrames());

        if (replaceOutput)
            output.clear();

        return;
    }

    if (callbacks.empty())
    {
        if (replaceOutput)
            output.clear();

        return;
    }

    for (auto c : callbacks)
        c->startBlock();

    dispatcher.setAudioBuffers (input, output);

    dispatcher.processInChunks ([this, replaceOutput]
                                (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
    {
        bool replace = replaceOutput;

        for (auto c : callbacks)
        {
            c->processSubBlock (block, replace);
            replace = false;
        }
    });

    for (auto c : callbacks)
        c->endBlock();
}

}
