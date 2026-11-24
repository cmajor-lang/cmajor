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

#include <iostream>

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "choc/audio/io/choc_AudioMIDIPlayer.h"
#include "choc/text/choc_JSON.h"

#include "cmajor/helpers/cmaj_Patch.h"
#include "cmajor/helpers/cmaj_PatchWorker_QuickJS.h"
#include "cmajor/helpers/cmaj_PatchWorker_WebView.h"
#include "cmaj_AllocationChecker.h"

namespace cmaj
{

//==============================================================================
struct PatchPlayer  : public choc::audio::io::AudioMIDICallback
{
    PatchPlayer (const choc::value::Value& engineOptions,
                 const cmaj::BuildSettings& buildSettings,
                 bool checkFilesForChanges)
    {
        initPatchCallbacks (engineOptions, buildSettings);
        patch.setAutoRebuildOnFileChange (checkFilesForChanges);
    }

    ~PatchPlayer() override
    {
        setAudioMIDIPlayer ({});

        patch.stopPlayback      = [] {};
        patch.startPlayback     = [] {};
        patch.patchChanged      = [] {};
        patch.statusChanged     = [] (const cmaj::Patch::Status&) {};
        patch.handleOutputEvent = [] (uint64_t, std::string_view, const choc::value::ValueView&) {};

        patch.unload();
    }

    //==============================================================================
    void updatePlaybackParams (bool synchronousRebuild)
    {
        cmaj::Patch::PlaybackParams params;

        if (audioPlayer != nullptr)
        {
            auto& options = audioPlayer->options;

            params.blockSize          = options.blockSize;
            params.sampleRate         = options.sampleRate;
            params.numInputChannels   = options.inputChannelCount;
            params.numOutputChannels  = options.outputChannelCount;

            if (options.sampleRate > 0)
                reportedSampleRate = static_cast<double> (options.sampleRate);
        }
        else
        {
            // If we don't have a device yet, use some dummy values to allow
            // us to still load patches
            params.blockSize          = 256;
            params.sampleRate         = 44100;
            params.numInputChannels   = 2;
            params.numOutputChannels  = 2;
        }

        patch.setPlaybackParams (params, synchronousRebuild);
    }

    void setAudioMIDIPlayer (std::shared_ptr<choc::audio::io::AudioMIDIPlayer> audioPlayerToUse)
    {
        if (audioPlayer != nullptr)
            audioPlayer->removeCallback (*this);

        audioPlayer = std::move (audioPlayerToUse);

        updatePlaybackParams (false);
        updatePlaybackState();
    }

    //==============================================================================
    bool loadPatch (const std::string& patchFile, bool synchronous = false)
    {
        return patch.loadPatchFromFile (patchFile, synchronous);
    }

    bool isPlaying() const
    {
        return playing;
    }

    void startPlayback()
    {
        playing = true;
        updatePlaybackState();
    }

    void stopPlayback()
    {
        playing = false;
        updatePlaybackState();
    }

    //==============================================================================
    // The timeline info that's sent to a patch's timecode inputs. All of these setters
    // can be called from any thread except the audio thread: the audio thread picks the
    // changes up at the start of its next block.

    void setTempo (float bpm)
    {
        newBPM = bpm;
    }

    void setTimeSig (uint32_t newNumerator, uint32_t newDenominator)
    {
        newTimeSig = (newNumerator << 16) | (newDenominator & 0xffffu);
    }

    /// Sets the playing and recording flags, which are independent: recording doesn't
    /// imply playing, or vice versa. The looping flag is left as it was.
    void setTransportState (bool isPlaying, bool isRecording)
    {
        setTransportFlag (transportFlag_playing, isPlaying);
        setTransportFlag (transportFlag_recording, isRecording);
    }

    void setTransportState (bool isPlaying, bool isRecording, bool isLooping)
    {
        setTransportFlags ((isPlaying ? transportFlag_playing : 0u)
                             | (isRecording ? transportFlag_recording : 0u)
                             | (isLooping ? transportFlag_looping : 0u));
    }

    void setTransportPlaying (bool isPlaying)       { setTransportFlag (transportFlag_playing, isPlaying); }
    void setTransportRecording (bool isRecording)   { setTransportFlag (transportFlag_recording, isRecording); }

    /// The looping flag is only passed on to the patch: there are no loop points, so the
    /// timeline position never jumps back by itself.
    void setTransportLooping (bool isLooping)       { setTransportFlag (transportFlag_looping, isLooping); }

    /// Moves the timeline to the given frame. The timeline only advances while the
    /// transport's playing flag is set (unlike totalFramesRendered, which always does).
    void setPosition (int64_t frame)
    {
        pendingPosition = std::max (static_cast<int64_t> (0), frame);
    }

    void rewind()
    {
        setPosition (0);
    }

    /// Makes the audio thread send the tempo, time signature, transport state and position
    /// to the patch again at the start of its next block, even if they haven't changed.
    void resendTimelineInfo()
    {
        needsTimelineInfoResend = true;
    }

    //==============================================================================
    /// A snapshot of the transport, as returned by getTransportInfo().
    struct TransportInfo
    {
        float tempo = 0;                    ///< 0 if no tempo has been set
        uint32_t numerator = 0,             ///< 0 if no time signature has been set
                 denominator = 0;
        bool playing = false, recording = false, looping = false;
        int64_t frame = 0;
        double quarterNote = 0, barStartQuarterNote = 0;
        double sampleRate = 0;              ///< 0 if it isn't known yet
    };

    /// Returns the current transport settings and timeline position. This is safe to call
    /// from any thread. A position set with setPosition() that the audio thread hasn't picked
    /// up yet is reported straight away.
    TransportInfo getTransportInfo() const
    {
        TransportInfo info;
        auto flags = transportFlags.load();
        auto timesig = newTimeSig.load();

        info.tempo       = newBPM.load();
        info.numerator   = timesig >> 16;
        info.denominator = timesig & 0xffffu;
        info.playing     = (flags & transportFlag_playing) != 0;
        info.recording   = (flags & transportFlag_recording) != 0;
        info.looping     = (flags & transportFlag_looping) != 0;
        info.sampleRate  = reportedSampleRate.load();

        auto pending = pendingPosition.load();
        info.frame = pending >= 0 ? pending : publishedTimelineFrame.load();

        calculateQuarterNotePosition (info.frame, info.sampleRate, info.tempo, info.numerator, info.denominator,
                                      info.quarterNote, info.barStartQuarterNote);
        return info;
    }

    /// Converts a frame position to quarter-notes, and the quarter-note at which its bar starts.
    /// Both are 0 unless the sample rate, tempo and time signature are all valid.
    static void calculateQuarterNotePosition (int64_t frame, double rate, float bpm,
                                              uint32_t timeSigNumerator, uint32_t timeSigDenominator,
                                              double& quarterNote, double& barStartQuarterNote)
    {
        quarterNote = 0;
        barStartQuarterNote = 0;

        if (rate > 0 && bpm > 0 && timeSigNumerator != 0 && timeSigDenominator != 0)
        {
            auto samplesPerQuarterNote = rate / (bpm / 60.0);
            auto quarterNotesPerBar = (4.0 * timeSigNumerator) / timeSigDenominator;
            quarterNote = static_cast<double> (frame) / samplesPerQuarterNote;
            auto barNumber = std::floor (quarterNote / quarterNotesPerBar);
            barStartQuarterNote = barNumber * quarterNotesPerBar;
        }
    }

    //==============================================================================
    void handleStatusChange (const cmaj::Patch::Status& s)
    {
        if (onStatusChange)
            onStatusChange (s);
    }

    bool handleClientMessage (PatchView& sourceView, const choc::value::ValueView& msg)
    {
        bool messageHandled = patch.handleClientMessage (sourceView, msg);

        if (messageHandled)
        {
            auto typeMember = msg["type"];

            // the reset patch has forgotten its timeline info, so it needs sending again
            if (typeMember.isString() && typeMember.getString() == "req_reset")
                resendTimelineInfo();
        }

        return messageHandled;
    }

    cmaj::Patch patch;

    std::function<void()> onPatchLoaded, onPatchUnloaded;
    std::function<void(const cmaj::Patch::Status&)> onStatusChange;

    uint64_t totalFramesRendered = 0;

private:
    //==============================================================================
    void initPatchCallbacks (const choc::value::Value& engineOptions, const cmaj::BuildSettings& buildSettings)
    {
        std::string engineType;

        if (engineOptions.isObject() && engineOptions.hasObjectMember("engine"))
            engineType = engineOptions["engine"].getString();

        patch.createEngine = [=]
        {
            auto engine = cmaj::Engine::create (engineType, &engineOptions);
            engine.setBuildSettings (buildSettings);
            return engine;
        };

        if (engineOptions.isObject() && engineOptions["worker"].toString() == "quickjs")
            enableQuickJSPatchWorker (patch);
        else
            enableWebViewPatchWorker (patch);

        patch.setHostDescription ("Cmajor Player");

        patch.stopPlayback      = [this] { setPatchCallbacksActive (false); };

        // (a new build has none of the timeline info that the previous one was sent)
        patch.startPlayback     = [this] { resendTimelineInfo(); setPatchCallbacksActive (true); };
        patch.patchChanged      = [this] { handlePatchChange(); };
        patch.statusChanged     = [this] (const cmaj::Patch::Status& s) { handleStatusChange (s); };

        patch.handleOutputEvent = [] (uint64_t frame, std::string_view endpointID, const choc::value::ValueView& v)
        {
            if (endpointID == getConsoleEndpointID())
                std::cout << "event out: " << endpointID << ": " << frame << " " << choc::json::toString (v, false) << std::endl;
        };
    }

    void handlePatchChange()
    {
        bool loaded = patch.isPlayable();

        if (patchLoaded != loaded)
        {
            patchLoaded = loaded;

            if (loaded)
            {
                if (onPatchLoaded)
                    onPatchLoaded();
            }
            else
            {
                if (onPatchUnloaded)
                    onPatchUnloaded();
            }
        }
    }

    //==============================================================================
    // (this can be called on any thread, so it mustn't touch the audio thread's state)
    void sampleRateChanged (double newRate) override
    {
        resendTimelineInfo();

        if (newRate > 0)
            reportedSampleRate = newRate;

        if (sampleRate != newRate)
        {
            sampleRate = newRate;
            updatePlaybackParams (false);
        }
    }

    std::optional<cmaj::ScopedAllocationTracker> allocationTracker;

    void startBlock() override
    {
        allocationTracker.emplace();

        patch.beginChunkedProcess();
        sendTimecodeEventsToPatch();
    }

    void processSubBlock (const choc::audio::AudioMIDIBlockDispatcher::Block& block, bool replaceOutput) override
    {
        auto numFrames = block.audioOutput.getNumFrames();
        patch.processChunk (block, replaceOutput);
        totalFramesRendered += numFrames;

        if (timelineIsPlaying)
            timelineFrame += static_cast<int64_t> (numFrames);
    }

    void endBlock() override
    {
        publishedTimelineFrame = timelineFrame;
        patch.endChunkedProcess();
        ++blockCounter;
        allocationTracker.reset();
    }

    // Runs on the audio thread at the start of each block. It reads the settings that the
    // other threads have changed (all atomics) and sends whatever has changed, or everything
    // if a resend was asked for, followed by the position.
    void sendTimecodeEventsToPatch()
    {
        if (auto newPosition = pendingPosition.exchange (-1); newPosition >= 0)
        {
            timelineFrame = newPosition;
            publishedTimelineFrame = newPosition;
        }

        auto flags = transportFlags.load();
        auto bpm = newBPM.load();
        auto timesig = newTimeSig.load();
        auto timeSigNumerator = timesig >> 16;
        auto timeSigDenominator = timesig & 0xffffu;

        timelineIsPlaying = (flags & transportFlag_playing) != 0;

        if (! patch.wantsTimecodeEvents())
            return;

        uint32_t timeout = 0;
        auto resendAll = needsTimelineInfoResend.exchange (false);

        // (until the flags have been set, the transport counts as playing, but no
        // transport state is sent, as was always the case)
        if ((flags & transportFlag_hasBeenSet) != 0 && (resendAll || flags != lastSentTransportFlags))
            patch.sendTransportState ((flags & transportFlag_recording) != 0,
                                      (flags & transportFlag_playing) != 0,
                                      (flags & transportFlag_looping) != 0,
                                      timeout);

        lastSentTransportFlags = flags;

        if (bpm > 0 && (resendAll || bpm != lastSentBPM))
            patch.sendBPM (bpm, timeout);

        lastSentBPM = bpm;

        if (timeSigNumerator != 0 && timeSigDenominator != 0 && (resendAll || timesig != lastSentTimeSig))
            patch.sendTimeSig (static_cast<int> (timeSigNumerator),
                               static_cast<int> (timeSigDenominator),
                               timeout);

        lastSentTimeSig = timesig;

        double quarterNote = 0, barStartQuarterNote = 0;
        calculateQuarterNotePosition (timelineFrame, sampleRate, bpm, timeSigNumerator, timeSigDenominator,
                                      quarterNote, barStartQuarterNote);

        patch.sendPosition (timelineFrame, quarterNote, barStartQuarterNote, timeout);
    }

    //==============================================================================
    static constexpr uint32_t transportFlag_playing     = 1,    // (the same bits as std::timeline::TransportState)
                              transportFlag_recording   = 2,
                              transportFlag_looping     = 4,
                              transportFlag_hasBeenSet  = 0x100;

    void setTransportFlag (uint32_t flag, bool shouldBeSet)
    {
        auto flags = transportFlags.load();

        while (! transportFlags.compare_exchange_weak (flags, transportFlag_hasBeenSet
                                                                | (shouldBeSet ? (flags | flag) : (flags & ~flag))))
        {}
    }

    void setTransportFlags (uint32_t newFlags)
    {
        transportFlags = newFlags | transportFlag_hasBeenSet;
    }

    //==============================================================================
    void updatePlaybackState()
    {
        if (audioPlayer)
        {
            if (playing && patchCallbackActive)
                audioPlayer->addCallback (*this);
            else
                audioPlayer->removeCallback (*this);
        }
    }

    void setPatchCallbacksActive (bool b)
    {
        if (patchCallbackActive != b)
        {
            patchCallbackActive = b;
            updatePlaybackState();
        }
    }

    bool patchLoaded = false,
         playing = false,
         patchCallbackActive = false;

    std::atomic<double> sampleRate { 0 };
    uint32_t blockCounter = 0;

    // The timeline settings, written by any thread and read by the audio thread
    std::atomic<float> newBPM { 0 };
    std::atomic<uint32_t> newTimeSig { 0 };
    std::atomic<uint32_t> transportFlags { transportFlag_playing };
    std::atomic<int64_t> pendingPosition { -1 };            // (-1 means no change)
    std::atomic<bool> needsTimelineInfoResend { true };
    std::atomic<int64_t> publishedTimelineFrame { 0 };     // (the audio thread's position, for getTransportInfo())
    std::atomic<double> reportedSampleRate { 0 };

    // Only used by the audio thread
    int64_t timelineFrame = 0;
    bool timelineIsPlaying = true;
    float lastSentBPM = 0;
    uint32_t lastSentTimeSig = 0, lastSentTransportFlags = 0;

    std::shared_ptr<choc::audio::io::AudioMIDIPlayer> audioPlayer;
};

} // namespace cmaj
