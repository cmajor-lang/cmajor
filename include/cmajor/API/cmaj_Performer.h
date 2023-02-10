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

#include <cassert>

#include "cmaj_Program.h"
#include "cmaj_Endpoints.h"
#include "cmaj_ExternalVariables.h"
#include "../../choc/audio/choc_SampleBufferUtilities.h"

namespace cmaj
{

/** A class that acts as a wrapper around a PerformerInterface object, replacing
    its clunky COM API with nicer, idiomatic C++ methods.

    This is basically a smart-pointer to a PerformerInterface object, so bear in mind
    that copying a Performer is just copying a (ref-counted) pointer - it won't make a
    copy of the underlying performer itself.
*/
struct Performer
{
    /// This creates an empty performer, which is basically a null pointer.
    /// To get a usable Performer, get yourself an Engine object, use it to
    /// build a program, and call its Engine::createPerformer() method.
    Performer() = default;
    ~Performer() = default;

    Performer (PerformerPtr);

    /// Returns true if this is a valid performer.
    operator bool() const                           { return performer; }

    bool operator!= (decltype (nullptr)) const      { return performer; }
    bool operator== (decltype (nullptr)) const      { return ! performer; }

    //==============================================================================
    /// Sets the number of frames which should be rendered during the next call to advance().
    void setBlockSize (uint32_t numFramesForNextBlock);

    /// Provides a block of frames to an input stream endpoint.
    /// Before a call to advance(), this function has to be called for each stream input, to provide
    /// it with a chunk of data to use in advance(). The number of frames provided is expected to be
    /// the same as the size set by the last call to setBlockSize().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// It should only be called once before each advance() call.
    void setInputFrames (EndpointHandle, const void* frameData, uint32_t numFrames);

    /// Provides a block of frames to an input stream endpoint.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that the format of data provided matches the endpoint,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    void setInputFrames (EndpointHandle, const choc::buffer::InterleavedView<SampleType>&);

    /// Sets the current value for a latching input value endpoint.
    /// Before calling advance(), this can optionally be called for a value input to change its value.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// It should only be called once for each stream within the same advance call.
    /// The value provided can either be:
    ///   - A choc::value::ValueView (with a type that exactly matches the endpoint type)
    ///   - A raw void* that points to data chunk in a choc::value::ValueView format
    ///   - A primitive value such as int32_t, float, double etc, which will be converted to
    ///     a choc::value::ValueView internally, but whose type is still expected to be correct.
    /// Note that if the format of data passed in isn't correct, the result will be undefined behaviour.
    template <typename ValueType>
    void setInputValue (EndpointHandle, const ValueType& newValue, uint32_t numFramesToReachValue);

    /// Adds an event to the queue for an input event endpoint.
    /// Before calling advance(), this can be called (multiple times if needed) to queue-up a sequence of
    /// events which will all be invoked (in the order they were added) on the first frame of the block
    /// when advance() is called.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// If the endpoint is an event that supports multiple types, the typeIndex selects the one to use
    /// (just set it to 0 for endpoints with only one type).
    /// The value provided can either be:
    ///   - A choc::value::ValueView (with a type that exactly matches the endpoint type)
    ///   - A raw void* that points to data chunk in a choc::value::ValueView format
    ///   - A primitive value such as int32_t, float, double etc, which will be converted to
    ///     a choc::value::ValueView internally, but whose type is still expected to be correct.
    /// Note that if the format of data passed in isn't correct, the result will be undefined behaviour.
    template <typename ValueType>
    void addInputEvent (EndpointHandle, uint32_t typeIndex, const ValueType& eventValue);

    /// Copies-out the frame data from an output stream endpoint.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the frame data for the given endpoint.
    /// The pointer provided will have a chunk of choc::value::ValueView data written to it, whose type
    /// the caller should know in advance by getting the endpoint's details.
    void copyOutputFrames (EndpointHandle, void* dest, uint32_t numFramesToCopy) const;

    /// Copies the last block of samples from a stream endpoint to a sample buffer.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that you're asking for the correct format of data,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    void copyOutputFrames (EndpointHandle, const choc::buffer::InterleavedView<SampleType>& destBuffer) const;

    /// Copies the last block of samples from a stream endpoint to a sample buffer.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that you're asking for the correct format of data,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    void copyOutputFrames (EndpointHandle, choc::buffer::InterleavedBuffer<SampleType>& destBuffer) const;

    /// Copies-out the data for the current value of an output value endpoint.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the value data for the given endpoint.
    /// The pointer provided will have a chunk of choc::value::ValueView data written to it, whose type
    /// the caller should know in advance by getting the endpoint's details.
    /// The pointer that is returned will become invalid as soon as another method is called on the performer.
    void copyOutputValue (EndpointHandle, void* dest) const;

    /// Iterates the events that were pushed into an output event stream during the last advance() call.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to fetch events that were sent to the given endpoint.
    ///
    /// The functor provided must have the form:
    ///  (EndpointHandle, uint32_t dataTypeIndex, uint32_t frameOffset,
    ///   const void* valueData, uint32_t valueDataSize) -> bool
    template <typename HandlerFn>
    void iterateOutputEvents (EndpointHandle, HandlerFn&&);

    /// Renders the next block.
    /// The number of frames rendered will be the number that was last specified by a call to setBlockSize().
    void advance();

    /// Retrieves the string from a handle used in the current program, or an empty string if not found.
    std::string_view getStringForHandle (uint32_t handle) const;

    /// Returns the total number of over- and under-runs that have happened since the program was linked.
    /// These occur when the caller fails to fully empty or fill the input and output endpoint streams
    /// between calls to advance().
    uint32_t getXRuns() const;

    /// Returns the maximum number of frames that may be set as the block size in a call to setBlockSize().
    uint32_t getMaximumBlockSize() const;

    /// Returns the maximum number of events that can be sent per block.
    uint32_t getEventBufferSize() const;

    /// Returns the performer's internal latency in frames
    double getLatency() const;

    /// If there has been a runtime error, this returns the message, or nullptr if there isn't one.
    const char* getRuntimeError() const;

    //==============================================================================
    /// The underlying performer that this helper object is wrapping.
    PerformerPtr performer;

private:
    Library::SharedLibraryPtr library;
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

inline Performer::Performer (PerformerPtr p) : performer (p), library (Library::getSharedLibraryPtr()) {}

inline void Performer::setBlockSize (uint32_t numFramesForNextBlock)
{
    performer->setBlockSize (numFramesForNextBlock);
}

inline void Performer::setInputFrames (EndpointHandle endpoint, const void* frameData, uint32_t numFrames)
{
    performer->setInputFrames (endpoint, frameData, numFrames);
}

template <typename SampleType>
void Performer::setInputFrames (EndpointHandle endpoint, const choc::buffer::InterleavedView<SampleType>& buffer)
{
    performer->setInputFrames (endpoint, buffer.data.data, buffer.getNumFrames());
}

template <typename ValueType>
void Performer::setInputValue (EndpointHandle e, const ValueType& newValue, uint32_t numFramesToReachValue)
{
    static_assert (std::is_same<const ValueType, const int32_t>::value
                   || std::is_same<const ValueType, const int64_t>::value
                   || std::is_same<const ValueType, const float>::value
                   || std::is_same<const ValueType, const double>::value
                   || std::is_same<const ValueType, const bool>::value
                   || std::is_same<const ValueType, const choc::value::ValueView>::value
                   || std::is_same<const ValueType, const choc::value::Value>::value
                   || std::is_same<const ValueType, const void* const>::value
                   || std::is_same<const ValueType, const char* const>::value,
                   "The argument to setInputValue must be one of the supported primitive types, or a choc::value::ValueView");

    if constexpr (std::is_same<const ValueType, const int32_t>::value
                   || std::is_same<const ValueType, const int64_t>::value
                   || std::is_same<const ValueType, const float>::value
                   || std::is_same<const ValueType, const double>::value)
    {
        ValueType v = newValue;
        performer->setInputValue (e, std::addressof (v), numFramesToReachValue);
    }
    else if constexpr (std::is_same<const ValueType, const void* const>::value
                        || std::is_same<const ValueType, const char* const>::value)
    {
        performer->setInputValue (e, newValue, numFramesToReachValue);
    }
    else if constexpr (std::is_same<const ValueType, const bool>::value)
    {
        int32_t v = newValue ? 1 : 0;
        performer->setInputValue (e, std::addressof (v), numFramesToReachValue);

    }
    else if constexpr (std::is_same<const ValueType, const choc::value::ValueView>::value
                        || std::is_same<const ValueType, const choc::value::Value>::value)
    {
        performer->setInputValue (e, newValue.getRawData(), numFramesToReachValue);
    }
}

template <typename ValueType>
void Performer::addInputEvent (EndpointHandle e, uint32_t type, const ValueType& value)
{
    static_assert (std::is_same<const ValueType, const int32_t>::value
                   || std::is_same<const ValueType, const int64_t>::value
                   || std::is_same<const ValueType, const float>::value
                   || std::is_same<const ValueType, const double>::value
                   || std::is_same<const ValueType, const bool>::value
                   || std::is_same<const ValueType, const choc::value::ValueView>::value
                   || std::is_same<const ValueType, const choc::value::Value>::value
                   || std::is_same<const ValueType, const void* const>::value
                   || std::is_same<const ValueType, const char* const>::value,
                   "The argument to addInputEvent must be one of the supported primitive types, or a choc::value::ValueView");

    if constexpr (std::is_same<const ValueType, const int32_t>::value
                   || std::is_same<const ValueType, const int64_t>::value
                   || std::is_same<const ValueType, const float>::value
                   || std::is_same<const ValueType, const double>::value)
    {
        ValueType v = value;
        performer->addInputEvent (e, type, std::addressof (v));
    }
    else if constexpr (std::is_same<const ValueType, const void* const>::value
                        || std::is_same<const ValueType, const char* const>::value)
    {
        performer->addInputEvent (e, type, value);
    }
    else if constexpr (std::is_same<const ValueType, const bool>::value)
    {
        int32_t v = value ? 1 : 0;
        performer->addInputEvent (e, type, std::addressof (v));
    }
    else if constexpr (std::is_same<const ValueType, const choc::value::ValueView>::value
                        || std::is_same<const ValueType, const choc::value::Value>::value)
    {
        performer->addInputEvent (e, type, value.getRawData());
    }
}

inline void Performer::copyOutputValue (EndpointHandle endpoint, void* dest) const
{
    performer->copyOutputValue (endpoint, dest);
}

inline void Performer::copyOutputFrames (EndpointHandle endpoint, void* dest, uint32_t numFramesToCopy) const
{
    performer->copyOutputFrames (endpoint, dest, numFramesToCopy);
}

template <typename SampleType>
void Performer::copyOutputFrames (EndpointHandle endpoint, const choc::buffer::InterleavedView<SampleType>& destBuffer) const
{
    performer->copyOutputFrames (endpoint, destBuffer.data.data, destBuffer.getNumFrames());
}

template <typename SampleType>
void Performer::copyOutputFrames (EndpointHandle endpoint, choc::buffer::InterleavedBuffer<SampleType>& destBuffer) const
{
    performer->copyOutputFrames (endpoint, destBuffer.getView().data.data, destBuffer.getNumFrames());
}

template <typename HandlerFn>
inline void Performer::iterateOutputEvents (EndpointHandle endpoint, HandlerFn&& handler)
{
    struct Callback
    {
        static bool handleEvent (void* context, EndpointHandle handle, uint32_t dataTypeIndex,
                                 uint32_t frameOffset, const void* valueData, uint32_t valueDataSize)
        {
            auto h = static_cast<HandlerFn*> (context);
            return (*h) (handle, dataTypeIndex, frameOffset, valueData, valueDataSize);
        }
    };

    performer->iterateOutputEvents (endpoint, std::addressof (handler), Callback::handleEvent);
}

inline void Performer::advance()
{
    performer->advance();
}

inline std::string_view Performer::getStringForHandle (uint32_t handle) const
{
    size_t length;

    if (auto s = performer->getStringForHandle (handle, length))
        return std::string_view (s, length);

    return {};
}

inline uint32_t Performer::getXRuns() const             { return performer != nullptr ? performer->getXRuns() : 0; }
inline uint32_t Performer::getMaximumBlockSize() const  { return performer->getMaximumBlockSize(); }
inline double Performer::getLatency() const             { return performer->getLatency(); }
inline uint32_t Performer::getEventBufferSize() const   { return performer->getEventBufferSize(); }
inline const char* Performer::getRuntimeError() const   { return performer != nullptr ? performer->getRuntimeError() : nullptr; }


} // namespace cmaj
