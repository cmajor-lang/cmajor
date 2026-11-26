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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
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

//==============================================================================
/// The settings of a SignalGeneratorSource. These are clamped to the ranges below (and
/// while it renders, the generator also keeps frequencies below 0.49 * the sample rate).
struct SignalGeneratorSettings
{
    enum class SignalType : int32_t { sine, sweep, white, pink, impulse, silence };

    [[nodiscard]] static std::string_view getTypeName (SignalType t)
    {
        switch (t)
        {
            case SignalType::sine:      return "sine";
            case SignalType::sweep:     return "sweep";
            case SignalType::white:     return "white";
            case SignalType::pink:      return "pink";
            case SignalType::impulse:   return "impulse";
            case SignalType::silence:
            default:                    return "silence";
        }
    }

    [[nodiscard]] static std::optional<SignalType> getTypeForName (std::string_view name)
    {
        for (auto t : { SignalType::sine, SignalType::sweep, SignalType::white,
                        SignalType::pink, SignalType::impulse, SignalType::silence })
            if (getTypeName (t) == name)
                return t;

        return {};
    }

    SignalType type = SignalType::sine;
    double level = -12.0;               ///< peak level, in dBFS
    double frequency = 440.0;           ///< Hz, for the sine
    double startFrequency = 20.0;       ///< Hz, where each sweep starts
    double endFrequency = 20000.0;      ///< Hz, where each sweep ends
    double sweepSeconds = 5.0;          ///< the length of each sweep
    double impulseRate = 2.0;           ///< impulses per second

    static constexpr double minLevel = -120.0,      maxLevel = 0.0;
    static constexpr double minFrequency = 1.0,     maxFrequency = 48000.0;
    static constexpr double minSweepSeconds = 0.1,  maxSweepSeconds = 600.0;
    static constexpr double minImpulseRate = 0.1,   maxImpulseRate = 1000.0;

    /// Merges in whichever fields are present in an object like
    /// `{ type, level, frequency, startFrequency, endFrequency, sweepSeconds, impulseRate }`,
    /// ignoring any that are missing, non-numeric or an unknown type, and then clamps them.
    void applyChanges (const choc::value::ValueView& changes)
    {
        if (! changes.isObject())
            return;

        if (auto typeName = changes["type"]; typeName.isString())
            if (auto t = getTypeForName (typeName.getString()))
                type = *t;

        auto readNumber = [&] (std::string_view name, double& target)
        {
            if (auto v = changes[name]; v.isInt() || v.isFloat())
                if (auto d = v.getWithDefault<double> (target); std::isfinite (d))
                    target = d;
        };

        readNumber ("level", level);
        readNumber ("frequency", frequency);
        readNumber ("startFrequency", startFrequency);
        readNumber ("endFrequency", endFrequency);
        readNumber ("sweepSeconds", sweepSeconds);
        readNumber ("impulseRate", impulseRate);
        clamp();
    }

    void clamp()
    {
        level          = std::clamp (level, minLevel, maxLevel);
        frequency      = std::clamp (frequency, minFrequency, maxFrequency);
        startFrequency = std::clamp (startFrequency, minFrequency, maxFrequency);
        endFrequency   = std::clamp (endFrequency, minFrequency, maxFrequency);
        sweepSeconds   = std::clamp (sweepSeconds, minSweepSeconds, maxSweepSeconds);
        impulseRate    = std::clamp (impulseRate, minImpulseRate, maxImpulseRate);
    }

    [[nodiscard]] choc::value::Value toValue() const
    {
        return choc::json::create ("type", std::string (getTypeName (type)),
                                   "level", level,
                                   "frequency", frequency,
                                   "startFrequency", startFrequency,
                                   "endFrequency", endFrequency,
                                   "sweepSeconds", sweepSeconds,
                                   "impulseRate", impulseRate);
    }

    bool operator== (const SignalGeneratorSettings& other) const
    {
        return type == other.type && level == other.level && frequency == other.frequency
                && startFrequency == other.startFrequency && endFrequency == other.endFrequency
                && sweepSeconds == other.sweepSeconds && impulseRate == other.impulseRate;
    }

    bool operator!= (const SignalGeneratorSettings& other) const   { return ! operator== (other); }
};

//==============================================================================
/// Generates a test signal - a sine, a logarithmic sweep, white or pink noise, an
/// impulse train, or silence - and sends the same signal to every channel.
///
/// Its settings may be changed from any thread while it plays. Each one is passed to
/// the audio thread through an atomic, and the audio thread smooths the level and
/// frequency, and crossfades from the old signal to the new one when the type changes,
/// so that a dragged control doesn't click. read() never allocates or locks.
struct SignalGeneratorSource : public NamedAudioSource
{
    using Settings = SignalGeneratorSettings;
    using SignalType = SignalGeneratorSettings::SignalType;

    //==============================================================================
    SignalGeneratorSource (const Settings& initialSettings = {})
    {
        setSettings (initialSettings);
    }

    std::string getName() const override  { return "generator"; }

    /// If the existing source is a generator, this merges the changes into its settings in
    /// place, and returns it. Otherwise it returns a new generator, whose settings are the
    /// base settings with the changes merged in.
    static std::shared_ptr<SignalGeneratorSource> createOrUpdate (const Patch::CustomAudioSourcePtr& existingSource,
                                                                  const choc::value::ValueView& changes,
                                                                  Settings baseSettings = {})
    {
        if (auto generator = std::dynamic_pointer_cast<SignalGeneratorSource> (existingSource))
        {
            generator->applyChanges (changes);
            return generator;
        }

        baseSettings.applyChanges (changes);
        return std::make_shared<SignalGeneratorSource> (baseSettings);
    }

    /// May be called from any thread but the audio thread.
    void setSettings (Settings newSettings)
    {
        newSettings.clamp();
        std::scoped_lock lock (settingsLock);
        settings = newSettings;
        publishSettings();
    }

    /// Merges some changes into the settings (see Settings::applyChanges()).
    /// May be called from any thread but the audio thread.
    void applyChanges (const choc::value::ValueView& changes)
    {
        std::scoped_lock lock (settingsLock);
        settings.applyChanges (changes);
        publishSettings();
    }

    [[nodiscard]] Settings getSettings() const
    {
        std::scoped_lock lock (settingsLock);
        return settings;
    }

    void prepare (double newSampleRate) override
    {
        if (newSampleRate > 0)
            pendingSampleRate.store (static_cast<float> (newSampleRate));
    }

    void read (choc::buffer::InterleavedView<float> dest) override   { readAnyType (dest); }
    void read (choc::buffer::InterleavedView<double> dest) override  { readAnyType (dest); }

    /// The time constant of the one-pole smoothing applied to the level and (log) frequency
    static constexpr double smoothingSeconds = 0.01;
    /// The length of the crossfade when the signal type changes
    static constexpr double crossfadeSeconds = 0.025;
    /// Makes the RMS of the pink noise filter's output equal to that of its white noise input
    static constexpr double pinkNoiseGain = 0.3276;

private:
    //==============================================================================
    struct PinkNoiseFilter
    {
        // Paul Kellet's refined pink noise filter
        double process (double white)
        {
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            auto pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;
            return pink;
        }

        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    };

    struct Voice
    {
        SignalType type = SignalType::silence;
        double phase = 0, sweepTime = 0, impulsePhase = 1.0;
        PinkNoiseFilter pink;
    };

    struct BlockTargets
    {
        SignalType type;
        double gain, logFrequency, sweepStartLog, sweepEndLog, sweepSeconds, impulseRate;
    };

    void publishSettings()
    {
        targetType.store (static_cast<int32_t> (settings.type));
        targetLevel.store (static_cast<float> (settings.level));
        targetFrequency.store (static_cast<float> (settings.frequency));
        targetStartFrequency.store (static_cast<float> (settings.startFrequency));
        targetEndFrequency.store (static_cast<float> (settings.endFrequency));
        targetSweepSeconds.store (static_cast<float> (settings.sweepSeconds));
        targetImpulseRate.store (static_cast<float> (settings.impulseRate));
    }

    void updateSampleRate()
    {
        auto newRate = static_cast<double> (pendingSampleRate.load());

        if (newRate != sampleRate)
        {
            sampleRate = newRate;
            smoothingCoeff = 1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate));
            crossfadeLength = std::max (1u, static_cast<uint32_t> (crossfadeSeconds * sampleRate));
            fadeSamplesRemaining = std::min (fadeSamplesRemaining, crossfadeLength);
        }
    }

    BlockTargets getBlockTargets() const
    {
        auto maxLogFrequency = std::log (0.49 * sampleRate);
        auto getLogFrequency = [&] (const std::atomic<float>& f) { return std::min (std::log (static_cast<double> (f.load())), maxLogFrequency); };

        return { static_cast<SignalType> (targetType.load()),
                 std::pow (10.0, static_cast<double> (targetLevel.load()) / 20.0),
                 getLogFrequency (targetFrequency),
                 getLogFrequency (targetStartFrequency),
                 getLogFrequency (targetEndFrequency),
                 static_cast<double> (targetSweepSeconds.load()),
                 static_cast<double> (targetImpulseRate.load()) };
    }

    template <typename Type>
    void readAnyType (choc::buffer::InterleavedView<Type> dest)
    {
        updateSampleRate();
        auto targets = getBlockTargets();

        if (! hasStarted)
        {
            // it starts at the target frequency, but fades in from silence
            hasStarted = true;
            currentVoice = {};
            currentVoice.type = targets.type;
            currentLogFrequency = targets.logFrequency;
            currentGain = 0;
        }

        auto numChannels = dest.getNumChannels();
        auto numFrames = dest.getNumFrames();

        for (decltype (numFrames) frame = 0; frame < numFrames; ++frame)
        {
            auto sample = static_cast<Type> (renderNextSample (targets));

            for (decltype (numChannels) channel = 0; channel < numChannels; ++channel)
                dest.getSample (channel, frame) = sample;
        }
    }

    double renderNextSample (const BlockTargets& targets)
    {
        // A type change starts a crossfade, unless one is already in progress, in which
        // case it waits for that one to finish
        if (fadeSamplesRemaining == 0 && targets.type != currentVoice.type)
        {
            fadingVoice = currentVoice;
            currentVoice = {};
            currentVoice.type = targets.type;
            fadeSamplesRemaining = crossfadeLength;
        }

        currentGain += (targets.gain - currentGain) * smoothingCoeff;
        currentLogFrequency += (targets.logFrequency - currentLogFrequency) * smoothingCoeff;

        auto output = generate (currentVoice, targets);

        if (fadeSamplesRemaining != 0)
        {
            auto fadeIn = 1.0 - static_cast<double> (fadeSamplesRemaining) / static_cast<double> (crossfadeLength);
            output = output * fadeIn + generate (fadingVoice, targets) * (1.0 - fadeIn);
            --fadeSamplesRemaining;
        }

        // (pink noise has the same RMS as white, so its peaks can be about 3x the level: this
        // keeps a test signal within full scale whatever the settings)
        return std::clamp (output * currentGain, -1.0, 1.0);
    }

    double generate (Voice& voice, const BlockTargets& targets)
    {
        constexpr auto twoPi = 2.0 * 3.141592653589793238;

        auto advancePhase = [&] (double frequency)
        {
            voice.phase += frequency / sampleRate;

            if (voice.phase >= 1.0)
                voice.phase -= std::floor (voice.phase);
        };

        switch (voice.type)
        {
            case SignalType::sine:
            {
                auto s = std::sin (twoPi * voice.phase);
                advancePhase (std::exp (currentLogFrequency));
                return s;
            }

            case SignalType::sweep:
            {
                // the phase carries on across each restart, so the waveform stays continuous
                auto s = std::sin (twoPi * voice.phase);
                auto position = std::min (1.0, voice.sweepTime / targets.sweepSeconds);
                advancePhase (std::exp (targets.sweepStartLog + (targets.sweepEndLog - targets.sweepStartLog) * position));
                voice.sweepTime += 1.0 / sampleRate;

                if (voice.sweepTime >= targets.sweepSeconds)
                    voice.sweepTime = 0;

                return s;
            }

            case SignalType::white:     return getNextRandom();
            case SignalType::pink:      return voice.pink.process (getNextRandom()) * pinkNoiseGain;

            case SignalType::impulse:
            {
                auto s = 0.0;

                if (voice.impulsePhase >= 1.0)
                {
                    voice.impulsePhase -= std::floor (voice.impulsePhase);
                    s = 1.0;
                }

                voice.impulsePhase += targets.impulseRate / sampleRate;
                return s;
            }

            case SignalType::silence:
            default:
                return 0;
        }
    }

    /// Returns a uniformly distributed value in [-1, 1), from a xorshift64* generator
    double getNextRandom()
    {
        randomState ^= randomState >> 12;
        randomState ^= randomState << 25;
        randomState ^= randomState >> 27;
        auto r = randomState * 2685821657736338717ull;
        return static_cast<double> (r >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }

    //==============================================================================
    mutable std::mutex settingsLock;    // (the audio thread never takes this)
    Settings settings;

    std::atomic<int32_t> targetType { static_cast<int32_t> (SignalType::sine) };

    std::atomic<float> targetLevel { -12.0f }, targetFrequency { 440.0f },
                       targetStartFrequency { 20.0f }, targetEndFrequency { 20000.0f },
                       targetSweepSeconds { 5.0f }, targetImpulseRate { 2.0f };
    std::atomic<float> pendingSampleRate { 44100.0f };

    // Everything below here belongs to the audio thread
    double sampleRate = 0, smoothingCoeff = 0;
    uint32_t crossfadeLength = 1, fadeSamplesRemaining = 0;
    bool hasStarted = false;
    Voice currentVoice, fadingVoice;
    double currentGain = 0, currentLogFrequency = 0;
    uint64_t randomState = 0x9e3779b97f4a7c15ull;

    static_assert (std::atomic<float>::is_always_lock_free && std::atomic<int32_t>::is_always_lock_free);
};

} // namespace cmaj::audio_utils
