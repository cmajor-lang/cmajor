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

#include "cmaj_AudioPlayer.h"

namespace cmaj::audio_utils
{

//==============================================================================
/**
 *   This object lets a single instance of an AudioMIDIPlayer share
 *   multiple callbacks, which can be dynamically added and removed
 *   while it's running.
 */
struct MultiClientAudioMIDIPlayer  : private AudioMIDICallback
{
    MultiClientAudioMIDIPlayer (std::shared_ptr<AudioMIDIPlayer>);
    ~MultiClientAudioMIDIPlayer() override;

    void addCallback (AudioMIDICallback&);
    void removeCallback (AudioMIDICallback&);

    /// This is the player that this client is controlling
    AudioMIDIPlayer& getAudioMIDIPlayer() const;


private:
    //==============================================================================
    std::shared_ptr<AudioMIDIPlayer> player;
    std::vector<AudioMIDICallback*> clients;
    double currentRate = 0;
    HandleMIDIOutEventFn currentMIDIFn;

    void prepareToStart (double, HandleMIDIOutEventFn) override;
    void addIncomingMIDIEvent (const void*, uint32_t) override;
    void process (choc::buffer::ChannelArrayView<const float>,
                  choc::buffer::ChannelArrayView<float>, bool) override;
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

inline MultiClientAudioMIDIPlayer::MultiClientAudioMIDIPlayer (std::shared_ptr<AudioMIDIPlayer> p)
    : player (std::move (p))
{
    CMAJ_ASSERT (player != nullptr);
}

inline MultiClientAudioMIDIPlayer::~MultiClientAudioMIDIPlayer() = default;

inline AudioMIDIPlayer& MultiClientAudioMIDIPlayer::getAudioMIDIPlayer() const
{
    return *player;
}

inline void MultiClientAudioMIDIPlayer::addCallback (AudioMIDICallback& c)
{
    bool needToStart = false;

    {
        const std::lock_guard<decltype(player->callbackLock)> lock (player->callbackLock);

        if (std::find (clients.begin(), clients.end(), std::addressof (c)) == clients.end())
        {
            needToStart = clients.empty();
            clients.push_back (std::addressof (c));

            if (! needToStart && currentRate != 0)
                c.prepareToStart (currentRate, currentMIDIFn);
        }
    }

    if (needToStart)
        player->start (*this);
}

inline void MultiClientAudioMIDIPlayer::removeCallback (AudioMIDICallback& c)
{
    bool needToStop = false;

    {
        const std::lock_guard<decltype(player->callbackLock)> lock (player->callbackLock);

        if (auto i = std::find (clients.begin(), clients.end(), std::addressof (c)); i != clients.end())
            clients.erase (i);

        needToStop = clients.empty();
    }

    if (needToStop)
    {
        player->stop();
        currentRate = 0;
        currentMIDIFn = {};
    }
}

inline void MultiClientAudioMIDIPlayer::prepareToStart (double sampleRate, choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn handleOutgoingMIDI)
{
    currentRate = sampleRate;
    currentMIDIFn = handleOutgoingMIDI;

    for (auto c : clients)
        c->prepareToStart (sampleRate, handleOutgoingMIDI);
}

inline void MultiClientAudioMIDIPlayer::addIncomingMIDIEvent (const void* data, uint32_t size)
{
    for (auto c : clients)
        c->addIncomingMIDIEvent (data, size);
}

inline void MultiClientAudioMIDIPlayer::process (choc::buffer::ChannelArrayView<const float> input,
                                                 choc::buffer::ChannelArrayView<float> output,
                                                 bool replaceOutput)
{
    if (clients.empty())
    {
        if (replaceOutput)
            output.clear();
    }
    else
    {
        for (size_t i = 0; i < clients.size(); ++i)
            clients[i]->process (input, output, replaceOutput && i == 0);
    }
}

} // namespace cmaj::audio_utils
