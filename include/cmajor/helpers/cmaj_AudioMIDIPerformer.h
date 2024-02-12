//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include <iostream>

#include "../../choc/memory/choc_Endianness.h"
#include "../../choc/containers/choc_VariableSizeFIFO.h"
#include "../../choc/containers/choc_Value.h"
#include "../../choc/containers/choc_NonAllocatingStableSort.h"
#include "../../choc/audio/choc_SampleBuffers.h"
#include "../../choc/audio/choc_MIDI.h"
#include "../../choc/audio/choc_AudioMIDIBlockDispatcher.h"

#include "cmaj_EndpointTypeCoercion.h"


namespace cmaj
{

//==============================================================================
/// A wrapper class that allows an Engine to be wrapped up in a form
/// that works as an audio/MIDI callback with asynchronous i/o for
/// event parameters.
///
struct AudioMIDIPerformer
{
    ~AudioMIDIPerformer();

    /// This is a base class for snooping on the data that's flowing through endpoints
    /// as they're being processed.
    struct AudioDataListener
    {
        virtual ~AudioDataListener() = default;
        virtual void process (const choc::buffer::InterleavedView<float>&) = 0;
        virtual void process (const choc::buffer::InterleavedView<double>&) = 0;
    };

    using OutputEventsReadyFn = std::function<void()>;
    using OutputEventHandlerFn = std::function<void(uint64_t frame, std::string_view endpointID, const choc::value::ValueView&)>;

    //==============================================================================
    // To create an AudioMIDIPerformer, create a Builder object, set up its connections,
    // and then call Builder::createPerformer() to get the performer object.
    struct Builder
    {
        Builder (cmaj::Engine, uint32_t eventFIFOSize);

        bool connectAudioInputTo (const std::vector<uint32_t>& inputChannels,
                                  const cmaj::EndpointDetails& endpoint,
                                  const std::vector<uint32_t>& endpointChannels,
                                  std::shared_ptr<AudioDataListener> listener);

        bool connectAudioOutputTo (const cmaj::EndpointDetails&,
                                   const std::vector<uint32_t>& endpointChannels,
                                   const std::vector<uint32_t>& outputChannels,
                                   std::shared_ptr<AudioDataListener> listener);

        bool connectMIDIInputTo (const cmaj::EndpointDetails&);
        bool connectMIDIOutputTo (const cmaj::EndpointDetails&);

        /// Sets a callback function which will be called (on the realtime audio thread)
        /// whenever some new output events have been queued. The function should trigger
        /// a call to handlePendingOutputEvents() either synchronously or asynchronously.
        bool setEventOutputHandler (OutputEventsReadyFn);

        /// Note that after creating the performer, this builder object can no longer
        /// be used - to create more performers, use new instances of the Builder
        std::unique_ptr<AudioMIDIPerformer> createPerformer();

    private:
        //==============================================================================
        std::unique_ptr<AudioMIDIPerformer> result;
        std::vector<bool> audioOutputChannelsUsed;

        template <typename SampleType>
        void addOutputCopyFunction (EndpointHandle, uint32_t numChannelsInEndpoint,
                                    const std::vector<uint32_t>& endpointChannels,
                                    const std::vector<uint32_t>& outputChannels,
                                    std::shared_ptr<AudioDataListener> listener);
        void createOutputChannelClearAction();
        void ensureInputScratchBufferChannelCount (uint32_t);
    };

    //==============================================================================
    // These can be called from any thread - it adds these incoming events and value changes
    // to a FIFO that will be read during the next call to process()
    bool postEvent (const cmaj::EndpointID& endpointID, const choc::value::ValueView& value);
    bool postEvent (cmaj::EndpointHandle endpointHandle, const choc::value::ValueView& value);
    bool postValue (const cmaj::EndpointID& endpointID, const choc::value::ValueView& value, uint32_t framesToReachValue);
    bool postValue (cmaj::EndpointHandle endpointHandle, const choc::value::ValueView& value, uint32_t framesToReachValue);

    //==============================================================================
    /// This should be called after calling the connect functions to set up the routing,
    /// and before beginning calls to process()
    bool prepareToStart();

    /// If 'replace' is true, it overwrites the output buffer and clears any channels that
    /// aren't in use. If false, it will add the output to whatever is already in the buffer.
    bool process (const choc::audio::AudioMIDIBlockDispatcher::Block&, bool replaceOutput);

    /// This version of process will automatically chop up a set of MIDI events with frame
    /// times into sub-blocks, and process each chunk separately
    bool processWithTimeStampedMIDI (const choc::buffer::ChannelArrayView<const float> audioInput,
                                     const choc::buffer::ChannelArrayView<float> audioOutput,
                                     const choc::midi::ShortMessage* midiInMessages,
                                     const int* midiInMessageTimes,
                                     uint32_t totalNumMIDIMessages,
                                     const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn& sendMidiOut,
                                     bool replaceOutput);

    /// Call this after processing ends, to clean up and release resources
    void playbackStopped();

    /// Synchronously calls the given function with any pending output events.
    /// It's safe to call this from any thread.
    void handlePendingOutputEvents (OutputEventHandlerFn&&);

    /// Detects infinite loops inside the current process callback by checking whether the process
    /// thread has been inside the same callback for more than the given amount of time. For this to
    /// work, it needs to be called regularly (at least a few times per second) by another thread.
    bool isStuckInInfiniteLoop (uint32_t thresholdMilliseconds = 500);

    cmaj::Engine engine;
    cmaj::Performer performer;

private:
    //==============================================================================
    EndpointTypeCoercionHelperList endpointTypeCoercionHelpers;

    std::vector<std::function<void(const choc::audio::AudioMIDIBlockDispatcher::Block&)>> preRenderFunctions,
                                                                                          postRenderReplaceFunctions,
                                                                                          postRenderAddFunctions;
    std::vector<cmaj::EndpointHandle> midiInputEndpoints, midiOutputEndpoints;
    std::vector<std::pair<cmaj::EndpointHandle, std::string>> eventOutputHandles;
    std::unordered_map<std::string, EndpointHandle> inputEndpointHandles;
    choc::fifo::VariableSizeFIFO eventQueue, valueQueue, outputEventQueue;
    OutputEventsReadyFn outputEventsReadyHandler;
    std::vector<std::pair<choc::midi::ShortMessage, uint32_t>> midiOutputMessages;
    choc::buffer::InterleavingScratchBuffer<float> audioInputScratchBuffer;
    std::vector<uint8_t> audioOutputScratchSpace;

    uint64_t numFramesProcessed = 0;
    static constexpr uint32_t maxFramesPerBlock = 512;
    uint32_t currentMaxBlockSize = 0;

    std::atomic<uint32_t> processCallCount { 0 };
    uint32_t lastCheckedProcessCallCount = 0;
    choc::HighResolutionSteadyClock::time_point lastCheckedProcessCallTime = {};

    //==============================================================================
    // To create an AudioMIDIPerformer, use a Builder object
    AudioMIDIPerformer (cmaj::Engine, uint32_t eventFIFOSize);

    void allocateScratch();
    void dispatchMIDIOutputEvents (const choc::audio::AudioMIDIBlockDispatcher::Block&);
    void moveOutputEventsToQueue();
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

static bool isFloat32 (const choc::value::Type& type)
{
    if (type.isVector())
        return isFloat32 (type.getElementType());

    if (type.isFloat32())
        return true;

    CMAJ_ASSERT (type.isFloat64());
    return false;
}

static uint32_t getNumFloatChannelsInStream (const cmaj::EndpointDetails& details)
{
    if (details.endpointType == cmaj::EndpointType::stream)
    {
        auto& type = details.dataTypes.front();

        if (details.dataTypes.front().isFloat())
            return 1;

        if (type.isVector() && type.getElementType().isFloat())
            return type.getNumElements();
    }

    return 0;
}

static uint32_t countTotalAudioChannels (const cmaj::EndpointDetailsList& endpoints)
{
    uint32_t total = 0;

    for (auto& e : endpoints)
        total += getNumFloatChannelsInStream (e);

    return total;
}

inline AudioMIDIPerformer::Builder::Builder (cmaj::Engine e, uint32_t eventFIFOSize)
    : result (new AudioMIDIPerformer (std::move (e), eventFIFOSize))
{
    CMAJ_ASSERT (e.isLoaded()); // The engine must be loaded before trying to build a performer for it

    audioOutputChannelsUsed.resize (countTotalAudioChannels (e.getOutputEndpoints()));
}

inline void AudioMIDIPerformer::Builder::ensureInputScratchBufferChannelCount (uint32_t channelsNeeded)
{
    [[maybe_unused]] auto resized = result->audioInputScratchBuffer.getInterleavedBuffer ({ channelsNeeded, maxFramesPerBlock });
}

inline bool AudioMIDIPerformer::Builder::connectAudioInputTo (const std::vector<uint32_t>& inputChannels,
                                                              const cmaj::EndpointDetails& endpoint,
                                                              const std::vector<uint32_t>& endpointChannels,
                                                              std::shared_ptr<AudioDataListener> listener)
{
    CMAJ_ASSERT (inputChannels.size() == endpointChannels.size());

    if (auto numChannelsInEndpoint = getNumFloatChannelsInStream (endpoint))
    {
        ensureInputScratchBufferChannelCount (numChannelsInEndpoint);
        auto endpointHandle = result->engine.getEndpointHandle (endpoint.endpointID);

        result->preRenderFunctions.push_back ([amp = result.get(), endpointHandle, numChannelsInEndpoint,
                                               endpointChannels, inputChannels, listener]
                                              (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
        {
            auto numFrames = block.audioInput.getNumFrames();
            auto interleavedBuffer = amp->audioInputScratchBuffer.getInterleavedBuffer ({ numChannelsInEndpoint, numFrames });

            for (uint32_t i = 0; i < inputChannels.size(); i++)
                copy (interleavedBuffer.getChannel (endpointChannels[i]),
                        block.audioInput.getChannel (inputChannels[i]));

            if (listener)
                listener->process (interleavedBuffer);

            amp->performer.setInputFrames (endpointHandle, interleavedBuffer.data.data, numFrames);
        });

        return true;
    }

    return false;
}

inline void AudioMIDIPerformer::Builder::createOutputChannelClearAction()
{
    uint32_t highestUsedChannel = 0;

    for (uint32_t i = 0; i < audioOutputChannelsUsed.size(); ++i)
        if (audioOutputChannelsUsed[i])
            highestUsedChannel = i + 1;

    if (highestUsedChannel == 0)
    {
        result->postRenderReplaceFunctions.push_back ([] (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
        {
            block.audioOutput.clear();
        });
    }
    else
    {
        std::vector<uint32_t> channelsToClear;

        for (uint32_t i = 0; i < highestUsedChannel; ++i)
            if (! audioOutputChannelsUsed[i])
                channelsToClear.push_back (i);

        if (channelsToClear.empty())
        {
            result->postRenderReplaceFunctions.push_back ([highestUsedChannel]
                                                          (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                auto totalChans = block.audioOutput.getNumChannels();

                if (totalChans > highestUsedChannel)
                    block.audioOutput.getChannelRange ({ highestUsedChannel, totalChans }).clear();
            });
        }
        else
        {
            result->postRenderReplaceFunctions.push_back ([channelsToClear, highestUsedChannel]
                                                          (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                for (auto chan : channelsToClear)
                    block.audioOutput.getChannel (chan).clear();

                auto totalChans = block.audioOutput.getNumChannels();

                if (totalChans > highestUsedChannel)
                    block.audioOutput.getChannelRange ({ highestUsedChannel, totalChans }).clear();
            });
        }
    }
}

template <typename SampleType>
void AudioMIDIPerformer::Builder::addOutputCopyFunction (EndpointHandle endpointHandle,
                                                         uint32_t numChannelsInEndpoint,
                                                         const std::vector<uint32_t>& endpointChannels,
                                                         const std::vector<uint32_t>& outputChannels,
                                                         std::shared_ptr<AudioDataListener> listener)
{
    CMAJ_ASSERT (endpointChannels.size() == outputChannels.size());

    auto scratch = choc::buffer::createInterleavedView (reinterpret_cast<SampleType*> (result->audioOutputScratchSpace.data()),
                                                        numChannelsInEndpoint, maxFramesPerBlock);

    if (endpointChannels.empty())
    {
        if (listener)
        {
            result->postRenderAddFunctions.push_back ([amp = result.get(), endpointHandle, scratch, listener]
                                                      (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                auto destSize = block.audioOutput.getSize();
                auto source = scratch.getStart (destSize.numFrames);

                amp->performer.copyOutputFrames (endpointHandle, source);
                listener->process (source);
            });

            result->postRenderReplaceFunctions.push_back ([amp = result.get(), endpointHandle, scratch, listener]
                                                          (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                auto destSize = block.audioOutput.getSize();
                auto source = scratch.getStart (destSize.numFrames);

                amp->performer.copyOutputFrames (endpointHandle, source);
                listener->process (source);
            });
        }

        return;
    }

    struct ChannelMap
    {
        uint32_t source, dest;
    };

    std::vector<ChannelMap> channelsToOverwrite, channelsToAddTo, allMappings;

    for (uint32_t i = 0; i < endpointChannels.size(); ++i)
    {
        auto src = endpointChannels[i];
        auto dest = outputChannels[i];

        if (audioOutputChannelsUsed.size() <= dest)
            audioOutputChannelsUsed.resize (dest + 1);

        if (audioOutputChannelsUsed[dest])
        {
            channelsToAddTo.push_back ({ src, dest });
        }
        else
        {
            channelsToOverwrite.push_back ({ src, dest });
            audioOutputChannelsUsed[dest] = true;
        }

        allMappings.push_back ({ src, dest });
    }

    result->postRenderAddFunctions.push_back ([amp = result.get(), endpointHandle, scratch, allMappings, listener]
                                              (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
    {
        auto destSize = block.audioOutput.getSize();
        auto source = scratch.getStart (destSize.numFrames);

        amp->performer.copyOutputFrames (endpointHandle, source);

        if (listener)
            listener->process (source);

        auto dest = block.audioOutput.getStart (destSize.numFrames);

        for (auto c : allMappings)
            add (dest.getChannel (c.dest), source.getChannel (c.source));
    });

    if (numChannelsInEndpoint == 1 && channelsToAddTo.empty())
    {
        if (channelsToOverwrite.size() == 1)
        {
            result->postRenderReplaceFunctions.push_back ([amp = result.get(), endpointHandle, listener, destChannel = channelsToOverwrite.front().dest]
                                                          (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                auto dest = block.audioOutput.getChannel (destChannel);
                amp->performer.copyOutputFrames (endpointHandle, dest.data.data, dest.getNumFrames());

                if (listener)
                    listener->process (choc::buffer::createInterleavedView (reinterpret_cast<SampleType*> (dest.data.data), 1u, dest.getNumFrames()));
            });
        }
        else
        {
            result->postRenderReplaceFunctions.push_back ([amp = result.get(), endpointHandle, listener, channelsToOverwrite]
                                                          (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
            {
                auto firstIndex = channelsToOverwrite.front().dest;
                auto numOutChans = block.audioOutput.getNumChannels();

                if (firstIndex < numOutChans)
                {
                    auto firstChan = block.audioOutput.getChannel (firstIndex);
                    amp->performer.copyOutputFrames (endpointHandle, firstChan.data.data, firstChan.getNumFrames());

                    if (listener)
                        listener->process (choc::buffer::createInterleavedView (reinterpret_cast<SampleType*> (firstChan.data.data), 1u, firstChan.getNumFrames()));

                    for (size_t i = 1; i < channelsToOverwrite.size(); ++i)
                    {
                        auto index = channelsToOverwrite[i].dest;

                        if (index < numOutChans)
                            copy (block.audioOutput.getChannel (index), firstChan);
                    }
                }
            });
        }
    }
    else
    {
        result->postRenderReplaceFunctions.push_back ([amp = result.get(), endpointHandle, scratch, channelsToOverwrite, channelsToAddTo, listener]
                                                      (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
        {
            auto destSize = block.audioOutput.getSize();
            auto source = scratch.getStart (destSize.numFrames);

            amp->performer.copyOutputFrames (endpointHandle, source);

            if (listener)
                listener->process (source);

            auto dest = block.audioOutput.getStart (destSize.numFrames);

            for (auto c : channelsToOverwrite)
                copy (dest.getChannel (c.dest), source.getChannel (c.source));

            for (auto c : channelsToAddTo)
                add (dest.getChannel (c.dest), source.getChannel (c.source));
        });
    }
}

inline bool AudioMIDIPerformer::Builder::connectAudioOutputTo (const cmaj::EndpointDetails& endpoint,
                                                               const std::vector<uint32_t>& endpointChannels,
                                                               const std::vector<uint32_t>& outputChannels,
                                                               std::shared_ptr<AudioDataListener> listener)
{
    CMAJ_ASSERT (outputChannels.size() == endpointChannels.size());

    if (auto numChannelsInEndpoint = getNumFloatChannelsInStream (endpoint))
    {
        auto endpointHandle = result->engine.getEndpointHandle (endpoint.endpointID);

        if (isFloat32 (endpoint.dataTypes.front()))
            addOutputCopyFunction<float> (endpointHandle, numChannelsInEndpoint, endpointChannels, outputChannels, listener);
        else
            addOutputCopyFunction<double> (endpointHandle, numChannelsInEndpoint, endpointChannels, outputChannels, listener);

        return true;
    }

    return false;
}

inline bool AudioMIDIPerformer::Builder::connectMIDIInputTo (const cmaj::EndpointDetails& endpoint)
{
    if (endpoint.isMIDI())
    {
        result->midiInputEndpoints.push_back (result->engine.getEndpointHandle (endpoint.endpointID));
        return true;
    }

    return false;
}

inline bool AudioMIDIPerformer::Builder::connectMIDIOutputTo (const cmaj::EndpointDetails& endpoint)
{
    if (endpoint.isMIDI())
    {
        result->midiOutputEndpoints.push_back (result->engine.getEndpointHandle (endpoint.endpointID));
        return true;
    }

    return false;
}

inline bool AudioMIDIPerformer::Builder::setEventOutputHandler (OutputEventsReadyFn h)
{
    CMAJ_ASSERT (result->eventOutputHandles.empty()); // can only add a handler once!
    result->outputEventsReadyHandler = std::move (h);

    if (result->outputEventsReadyHandler == nullptr)
        return false;

    for (const auto& endpointDetails : result->engine.getOutputEndpoints())
        if (endpointDetails.isEvent())
            if (auto endpointHandle = result->engine.getEndpointHandle (endpointDetails.endpointID))
                result->eventOutputHandles.emplace_back (endpointHandle, endpointDetails.endpointID.toString());

    return ! result->eventOutputHandles.empty();
}

inline std::unique_ptr<AudioMIDIPerformer> AudioMIDIPerformer::Builder::createPerformer()
{
    createOutputChannelClearAction();
    return std::move (result);
}

//==============================================================================
inline AudioMIDIPerformer::AudioMIDIPerformer (cmaj::Engine e, uint32_t eventFIFOSize)
    : engine (std::move (e))
{
    eventQueue.reset (eventFIFOSize);
    valueQueue.reset (eventFIFOSize);
    outputEventQueue.reset (eventFIFOSize);

    endpointTypeCoercionHelpers.initialise (engine, maxFramesPerBlock, true, true);

    for (auto& endpoint : engine.getInputEndpoints())
        inputEndpointHandles[endpoint.endpointID.toString()] = engine.getEndpointHandle (endpoint.endpointID);

    allocateScratch();
}

inline AudioMIDIPerformer::~AudioMIDIPerformer()
{
    performer = {};
    engine = {};
}

inline void AudioMIDIPerformer::allocateScratch()
{
    size_t scratchNeeded = 0;

    for (const auto& e : engine.getOutputEndpoints())
        if (auto numChans = getNumFloatChannelsInStream (e))
            scratchNeeded = std::max (scratchNeeded,
                                      numChans * maxFramesPerBlock
                                         * (isFloat32 (e.dataTypes.front()) ? sizeof (float)
                                                                            : sizeof (double)));

    if (scratchNeeded != 0)
        audioOutputScratchSpace.resize (scratchNeeded);
}

inline bool AudioMIDIPerformer::postEvent (cmaj::EndpointHandle handle, const choc::value::ValueView& value)
{
    if (auto coercedData = endpointTypeCoercionHelpers.coerceValueToMatchingType (handle, value, EndpointType::event))
    {
        auto typeIndex = static_cast<uint32_t> (coercedData.typeIndex);
        auto totalSize = static_cast<uint32_t> (sizeof (handle) + sizeof (typeIndex) + coercedData.data.size);

        return eventQueue.push (totalSize, [&] (void* dest)
        {
            auto d = static_cast<uint8_t*> (dest);
            choc::memory::writeNativeEndian (d, handle);
            d += sizeof (handle);
            choc::memory::writeNativeEndian (d, typeIndex);
            d += sizeof (typeIndex);
            std::memcpy (d, coercedData.data.data, coercedData.data.size);
        });
    }

    return false;
}

inline bool AudioMIDIPerformer::postEvent (const cmaj::EndpointID& endpointID, const choc::value::ValueView& value)
{
    auto activeHandle = inputEndpointHandles.find (endpointID.toString());

    if (activeHandle != inputEndpointHandles.end())
        return postEvent (activeHandle->second, value);

    return false;
}

inline bool AudioMIDIPerformer::postValue (const EndpointHandle handle, const choc::value::ValueView& value, uint32_t framesToReachValue)
{
    if (auto coercedData = endpointTypeCoercionHelpers.coerceValue (handle, value))
    {
        auto totalSize = static_cast<uint32_t> (sizeof (handle) + sizeof (framesToReachValue) + coercedData.size);

        return valueQueue.push (totalSize, [&] (void* dest)
        {
            auto d = static_cast<uint8_t*> (dest);
            choc::memory::writeNativeEndian (d, handle);
            d += sizeof (handle);
            choc::memory::writeNativeEndian (d, framesToReachValue);
            d += sizeof (framesToReachValue);
            std::memcpy (d, coercedData.data, coercedData.size);
        });
    }

    return false;
}

inline bool AudioMIDIPerformer::postValue (const cmaj::EndpointID& endpointID, const choc::value::ValueView& value, uint32_t framesToReachValue)
{
    auto activeHandle = inputEndpointHandles.find (endpointID.toString());

    if (activeHandle != inputEndpointHandles.end())
        return postValue (activeHandle->second, value, framesToReachValue);

    return false;
}

//==============================================================================
inline bool AudioMIDIPerformer::prepareToStart()
{
    performer = engine.createPerformer();

    if (performer == nullptr)
        return false;

    currentMaxBlockSize = std::min (maxFramesPerBlock, performer.getMaximumBlockSize());
    midiOutputMessages.reserve (midiOutputEndpoints.size() * performer.getEventBufferSize());
    endpointTypeCoercionHelpers.initialiseDictionary (performer);
    return true;
}

inline void AudioMIDIPerformer::playbackStopped()
{
    performer = {};
}

//==============================================================================
inline bool AudioMIDIPerformer::process (const choc::audio::AudioMIDIBlockDispatcher::Block& block, bool replaceOutput)
{
    try
    {
        if (performer == nullptr)
            return false;

        auto numFrames = block.audioOutput.getNumFrames();

        if (numFrames > currentMaxBlockSize)
        {
            for (uint32_t start = 0; start < numFrames;)
            {
                auto numToDo = std::min (currentMaxBlockSize, numFrames - start);

                if (! process ({ block.audioInput.getFrameRange ({ start, start + numToDo }),
                                 block.audioOutput.getFrameRange ({ start, start + numToDo }),
                                 start == 0 ? block.midiMessages : choc::span<choc::midi::ShortMessage>(),
                                 block.onMidiOutputMessage }, replaceOutput))
                    return false;

                start += numToDo;
            }

            return true;
        }

        ++processCallCount;
        performer.setBlockSize (numFrames);

        for (auto& f : preRenderFunctions)
            f (block);

        eventQueue.popAllAvailable ([&] (const void* data, [[maybe_unused]] uint32_t size)
        {
            CMAJ_ASSERT (size > 4);
            auto d = static_cast<const char*> (data);
            auto handle = choc::memory::readNativeEndian<cmaj::EndpointHandle> (d);
            d += sizeof (handle);
            auto typeIndex = choc::memory::readNativeEndian<uint32_t> (d);
            d += sizeof (typeIndex);

            performer.addInputEvent (handle, typeIndex, d);
        });

        valueQueue.popAllAvailable ([&] (const void* data, [[maybe_unused]] uint32_t size)
        {
            CMAJ_ASSERT (size > 4);
            auto d = static_cast<const char*> (data);
            auto handle = choc::memory::readNativeEndian<cmaj::EndpointHandle> (d);
            d += sizeof (handle);
            auto frameCount = choc::memory::readNativeEndian<uint32_t> (d);
            d += sizeof (frameCount);

            performer.setInputValue (handle, d, frameCount);
        });

        if (! midiInputEndpoints.empty())
        {
            for (auto midiEvent : block.midiMessages)
            {
                auto bytes = midiEvent.data;
                auto packedMIDI = static_cast<int32_t> ((bytes[0] << 16) | (bytes[1] << 8) | bytes[2]);

                for (auto& midiEndpoint : midiInputEndpoints)
                    performer.addInputEvent (midiEndpoint, 0, packedMIDI);
            }
        }

        performer.advance();
        dispatchMIDIOutputEvents (block);

        if (replaceOutput)
        {
            for (auto& f : postRenderReplaceFunctions)
                f (block);
        }
        else
        {
            for (auto& f : postRenderAddFunctions)
                f (block);
        }

        moveOutputEventsToQueue();
        numFramesProcessed += numFrames;
        ++processCallCount;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception thrown in audio process callback: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "Unknown exception thrown in audio process callback" << std::endl;
    }

    return false;
}

inline bool AudioMIDIPerformer::processWithTimeStampedMIDI (const choc::buffer::ChannelArrayView<const float> audioInput,
                                                            const choc::buffer::ChannelArrayView<float> audioOutput,
                                                            const choc::midi::ShortMessage* midiInMessages,
                                                            const int* midiInMessageTimes,
                                                            uint32_t totalNumMIDIMessages,
                                                            const choc::audio::AudioMIDIBlockDispatcher::HandleMIDIMessageFn& sendMidiOut,
                                                            bool replaceOutput)
{
    if (totalNumMIDIMessages == 0)
        return process (choc::audio::AudioMIDIBlockDispatcher::Block { audioInput, audioOutput, {}, sendMidiOut }, replaceOutput);

    auto remainingChunk = audioOutput.getFrameRange();
    uint32_t midiStartIndex = 0;

    while (remainingChunk.start < remainingChunk.end)
    {
        auto chunkToDo = remainingChunk;
        auto endOfMIDI = midiStartIndex;

        while (endOfMIDI < totalNumMIDIMessages)
        {
            auto eventTime = midiInMessageTimes[endOfMIDI];

            if (eventTime > (int) chunkToDo.start)
            {
                chunkToDo.end = static_cast<choc::buffer::FrameCount> (eventTime);
                break;
            }

            ++endOfMIDI;
        }

        if (! process (choc::audio::AudioMIDIBlockDispatcher::Block {
                           audioInput.getFrameRange (chunkToDo),
                           audioOutput.getFrameRange (chunkToDo),
                           choc::span<const choc::midi::ShortMessage> (midiInMessages + midiStartIndex,
                                                                       midiInMessages + endOfMIDI),
                           [&] (uint32_t frame, choc::midi::ShortMessage m)
                           {
                               sendMidiOut (chunkToDo.start + frame, m);
                           }
                       }, replaceOutput))
            return false;

        remainingChunk.start = chunkToDo.end;
        midiStartIndex = endOfMIDI;
    }

    return false;
}

inline void AudioMIDIPerformer::dispatchMIDIOutputEvents (const choc::audio::AudioMIDIBlockDispatcher::Block& block)
{
    if (! block.onMidiOutputMessage)
        return;

    for (const auto& endpointHandle : midiOutputEndpoints)
    {
        performer.iterateOutputEvents (endpointHandle,
                                        [this] (EndpointHandle, uint32_t,
                                        uint32_t frameOffset, const void* data, uint32_t size) -> bool
        {
            (void) size;
            CMAJ_ASSERT (size >= (3 * sizeof (uint8_t)));
            auto packed = *static_cast<const int32_t*> (data);
            midiOutputMessages.push_back ({ MIDIEvents::packedMIDIDataToMessage (packed), frameOffset });
            return true;
        });
    }

    if (midiOutputMessages.empty())
        return;

    // Sort the messages in case they come from multiple endpoints
    choc::sorting::stable_sort (midiOutputMessages.begin(), midiOutputMessages.end(),
                                [] (const auto& m1, const auto& m2) { return m1.second < m2.second; });

    for (const auto& m : midiOutputMessages)
        block.onMidiOutputMessage (m.second, m.first);

    midiOutputMessages.clear();
}

inline void AudioMIDIPerformer::handlePendingOutputEvents (OutputEventHandlerFn&& handler)
{
    outputEventQueue.popAllAvailable ([&] (const void* data, uint32_t size)
    {
        CMAJ_ASSERT (size > 12);
        auto d = static_cast<const uint8_t*> (data);

        auto handle = choc::memory::readNativeEndian<cmaj::EndpointHandle> (d);
        d += sizeof (handle);
        auto typeIndex = choc::memory::readNativeEndian<uint32_t> (d);
        d += sizeof (typeIndex);
        auto frame = choc::memory::readNativeEndian<uint64_t> (d);
        d += sizeof (frame);
        auto end = static_cast<const uint8_t*> (data) + size;
        auto& value = endpointTypeCoercionHelpers.getViewForOutputData (handle, typeIndex, { d, end });

        for (const auto& [eventOutputHandle, endpointID] : eventOutputHandles)
            if (eventOutputHandle == handle)
                return handler (frame, endpointID, value);
    });
}

inline void AudioMIDIPerformer::moveOutputEventsToQueue()
{
    if (outputEventsReadyHandler)
    {
        bool anyEvents = false;

        for (const auto& handle : eventOutputHandles)
        {
            performer.iterateOutputEvents (handle.first,
                                           [this, &anyEvents] (EndpointHandle h, uint32_t dataTypeIndex, uint32_t frameOffset,
                                                               const void* valueData, uint32_t valueDataSize) -> bool
            {
                auto frame = numFramesProcessed + frameOffset;
                auto totalSize = static_cast<uint32_t> (sizeof (h) + sizeof (dataTypeIndex) + sizeof (frame) + valueDataSize);

                bool ok = outputEventQueue.push (totalSize, [=] (void* dest)
                {
                    auto d = static_cast<uint8_t*> (dest);
                    choc::memory::writeNativeEndian (d, h);
                    d += sizeof (h);
                    choc::memory::writeNativeEndian (d, dataTypeIndex);
                    d += sizeof (dataTypeIndex);
                    choc::memory::writeNativeEndian (d, frame);
                    d += sizeof (frame);
                    std::memcpy (d, valueData, valueDataSize);
                });

                anyEvents = true;
                return ok;
            });
        }

        if (anyEvents)
            outputEventsReadyHandler();
    }
}

inline bool AudioMIDIPerformer::isStuckInInfiniteLoop (uint32_t thresholdMilliseconds)
{
    auto currentCount = processCallCount.load();

    if ((currentCount & 1) == 0)
        return false; // we're not inside a call, so no problem

    auto now = choc::HighResolutionSteadyClock::now();

    if (lastCheckedProcessCallCount != currentCount)
    {
        // inside a different call to last time
        lastCheckedProcessCallCount = currentCount;
        lastCheckedProcessCallTime = now;
        return false;
    }

    return (now - lastCheckedProcessCallTime) > std::chrono::milliseconds (thresholdMilliseconds);
}


} // namespace cmaj
