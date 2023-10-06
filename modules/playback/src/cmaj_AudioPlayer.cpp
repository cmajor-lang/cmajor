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

#include <thread>

#include "../include/cmaj_AudioPlayer.h"


namespace cmaj::audio_utils
{

//==============================================================================
struct RenderingPlayer  : public AudioMIDIPlayer
{
    RenderingPlayer (const AudioDeviceOptions& o) : AudioMIDIPlayer (o)
    {
    }

    ~RenderingPlayer() override
    {
        stop();
    }

    AvailableAudioDevices getAvailableDevices() override    { return {}; }

    void start (AudioMIDICallback& c) override
    {
        const std::lock_guard<decltype(startLock)> lock (startLock);

        if (callback == nullptr)
        {
            callback = std::addressof (c);
            renderThread = std::thread ([this] { render(); });
        }
    }

    void stop() override
    {
        {
            const std::lock_guard<decltype(startLock)> lock (startLock);
            callback = nullptr;
        }

        if (renderThread.joinable())
            renderThread.join();
    }

    void render()
    {
        CMAJ_ASSERT (options.blockSize != 0);
        choc::buffer::ChannelArrayBuffer<float> audioInput  (options.inputChannelCount,  options.blockSize);
        choc::buffer::ChannelArrayBuffer<float> audioOutput (options.outputChannelCount, options.blockSize);
        std::vector<choc::midi::ShortMessage> midiMessages;
        std::vector<uint32_t> midiMessageTimes;
        midiMessages.reserve (512);
        midiMessageTimes.reserve (512);

        for (;;)
        {
            audioInput.clear();
            audioOutput.clear();
            midiMessages.clear();
            midiMessageTimes.clear();

            const std::lock_guard<decltype(startLock)> lock (startLock);

            if (callback == nullptr)
                return;

            if (! options.provideInput (audioInput, midiMessages, midiMessageTimes))
            {
                callback = nullptr;
                return;
            }

            callback->prepareToStart (options.sampleRate, [] (uint32_t, choc::midi::ShortMessage) {});

            CMAJ_ASSERT (midiMessages.size() == midiMessageTimes.size());

            if (auto totalNumMIDIMessages = static_cast<uint32_t> (midiMessages.size()))
            {
                auto frameRange = audioOutput.getFrameRange();
                uint32_t midiStart = 0;

                while (frameRange.start < frameRange.end)
                {
                    auto chunkToDo = frameRange;
                    auto endOfMIDI = midiStart;

                    while (endOfMIDI < totalNumMIDIMessages)
                    {
                        auto eventTime = midiMessageTimes[endOfMIDI];

                        if (eventTime > chunkToDo.start)
                        {
                            chunkToDo.end = eventTime;
                            break;
                        }

                        ++endOfMIDI;
                    }

                    for (uint32_t i = midiStart; i < endOfMIDI; ++i)
                        callback->addIncomingMIDIEvent (midiMessages[i].data, midiMessages[i].size());

                    callback->process (audioInput.getFrameRange (chunkToDo),
                                       audioOutput.getFrameRange (chunkToDo),
                                       true);

                    frameRange.start = chunkToDo.end;
                    midiStart = endOfMIDI;
                }
            }
            else
            {
                callback->process (audioInput, audioOutput, true);
            }

            if (! options.handleOutput (audioOutput))
            {
                callback = nullptr;
                return;
            }
        }
    }

    std::mutex startLock;
    AudioMIDICallback* callback = nullptr;
    std::thread renderThread;
};

std::unique_ptr<AudioMIDIPlayer> createRenderingPlayer (const AudioDeviceOptions& options)
{
    /// Must provide both functions
    CMAJ_ASSERT (options.provideInput != nullptr && options.handleOutput != nullptr);

    return std::make_unique<RenderingPlayer> (options);
}

//==============================================================================
std::unique_ptr<AudioMIDIPlayer> createDummyPlayer (const AudioDeviceOptions& options)
{
    struct DummyPlayer : public AudioMIDIPlayer
    {
        DummyPlayer (const AudioDeviceOptions& o) : AudioMIDIPlayer (o) {}

        AvailableAudioDevices getAvailableDevices() override    { return {}; }
        void start (AudioMIDICallback&) override {}
        void stop() override {}
    };

    return std::make_unique<DummyPlayer> (options);
}

//==============================================================================
MultiClientAudioMIDIPlayer::MultiClientAudioMIDIPlayer (const AudioDeviceOptions& options)
    : player (options.createPlayer (options))
{
    CMAJ_ASSERT (player != nullptr);
}

MultiClientAudioMIDIPlayer::~MultiClientAudioMIDIPlayer() = default;

void MultiClientAudioMIDIPlayer::addCallback (AudioMIDICallback& c)
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

void MultiClientAudioMIDIPlayer::removeCallback (AudioMIDICallback& c)
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

void MultiClientAudioMIDIPlayer::prepareToStart (double sampleRate, choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn handleOutgoingMIDI)
{
    currentRate = sampleRate;
    currentMIDIFn = handleOutgoingMIDI;

    for (auto c : clients)
        c->prepareToStart (sampleRate, handleOutgoingMIDI);
}

void MultiClientAudioMIDIPlayer::addIncomingMIDIEvent (const void* data, uint32_t size)
{
    for (auto c : clients)
        c->addIncomingMIDIEvent (data, size);
}

void MultiClientAudioMIDIPlayer::process (choc::buffer::ChannelArrayView<const float> input,
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
