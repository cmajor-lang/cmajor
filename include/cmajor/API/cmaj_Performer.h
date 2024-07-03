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
    ~Performer();

    Performer (const Performer&) = default;
    Performer (Performer&&) = default;
    Performer& operator= (const Performer&) = default;
    Performer& operator= (Performer&&) = default;

    Performer (PerformerPtr);

    /// Returns true if this is a valid performer.
    operator bool() const                           { return performer; }

    bool operator!= (decltype (nullptr)) const      { return performer; }
    bool operator== (decltype (nullptr)) const      { return ! performer; }

    //==============================================================================
    /// Resets the performer to the state it was before it processed any data
    Result reset();

    //==============================================================================
    /// Sets the number of frames which should be rendered during the next call to advance().
    Result setBlockSize (uint32_t numFramesForNextBlock);

    /// Provides a block of frames to an input stream endpoint.
    /// This function must only be called on the rendering thread, as part of the preparations for
    /// a call to advance().
    /// You should call this function for each input stream endpoint, to provide the chunk of data that
    /// it will use in the next advance() call. The number of frames provided must be the same as the
    /// size set by the last call to setBlockSize().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// It should only be called once before each advance() call.
    Result setInputFrames (EndpointHandle, const void* frameData, uint32_t numFrames);

    /// Provides a block of frames to an input stream endpoint.
    /// This is a helper function for calling the other setInputFrames() method with an audio buffer.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that the format of data provided matches the endpoint,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    Result setInputFrames (EndpointHandle, const choc::buffer::InterleavedView<SampleType>&);

    /// Sets the current value for a latching input value endpoint.
    /// This function must only be called on the rendering thread, as part of the preparations for
    /// a call to advance().
    /// For an input value endpoint, if you call this function before advance(), it changes the value of
    /// that endpoint (and it'll keep that value until you change it again).
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// The value provided can either be:
    ///   - A choc::value::ValueView (with a type that exactly matches the endpoint type)
    ///   - A raw void* that points to data chunk in a choc::value::ValueView format
    ///   - A primitive value such as int32_t, float, double etc, which will be converted to
    ///     a choc::value::ValueView internally, but whose type is still expected to be correct.
    /// Note that if the format of data passed in isn't correct, the result will be undefined behaviour.
    template <typename ValueType>
    Result setInputValue (EndpointHandle, const ValueType& newValue, uint32_t numFramesToReachValue);

    /// Adds an event to the queue for an input event endpoint.
    /// This function must only be called on the rendering thread, as part of the preparations for
    /// a call to advance().
    /// It can be called multiple times if needed to dispatch a sequence of event handler callbacks.
    /// Depending on the back-end implementation, these may either be invoked synchronously during this
    /// call, or they may be queued and invoked at the start of the next advance() call.
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
    Result addInputEvent (EndpointHandle, uint32_t typeIndex, const ValueType& eventValue);

    /// Copies-out the frame data from an output stream endpoint.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the frame data for the given endpoint.
    /// The pointer provided will have a chunk of choc::value::ValueView data written to it, whose type
    /// the caller should know in advance by getting the endpoint's details.
    Result copyOutputFrames (EndpointHandle, void* dest, uint32_t numFramesToCopy) const;

    /// Copies the last block of samples from a stream endpoint to a sample buffer.
    /// This is a helper function to make it easier to invoke copyOutputFrames() with an audio buffer.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that you're asking for the correct format of data,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    Result copyOutputFrames (EndpointHandle, const choc::buffer::InterleavedView<SampleType>& destBuffer) const;

    /// Copies the last block of samples from a stream endpoint to a sample buffer.
    /// This is a helper function to make it easier to invoke copyOutputFrames() with an audio buffer.
    /// NB: to avoid overhead in the realtime audio thread, this doesn't perform any kind of
    /// sanity-checking to make sure that you're asking for the correct format of data,
    /// so it's up to the caller to make sure you get it right!
    template <typename SampleType>
    Result copyOutputFrames (EndpointHandle, choc::buffer::InterleavedBuffer<SampleType>& destBuffer) const;

    /// Copies-out the data for the current value of an output value endpoint.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the value data for the given endpoint.
    /// The pointer provided will have a chunk of choc::value::ValueView data written to it, whose type
    /// the caller should know in advance by getting the endpoint's details.
    /// The pointer that is returned will become invalid as soon as another method is called on the performer.
    Result copyOutputValue (EndpointHandle, void* dest) const;

    /// Iterates the events that were pushed into an output event stream during the last advance() call.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to fetch events that were sent to the given endpoint.
    ///
    /// The functor provided must have the form:
    ///  (EndpointHandle, uint32_t dataTypeIndex, uint32_t frameOffset,
    ///   const void* valueData, uint32_t valueDataSize) -> bool
    template <typename HandlerFn>
    Result iterateOutputEvents (EndpointHandle, HandlerFn&&);

    /// Renders the next block.
    /// The number of frames rendered will be the number that was last specified by a call to setBlockSize().
    Result advance();

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

inline Performer::~Performer()
{
    performer = {};  // explicitly release the performer before the library
    library = {};
}

inline Result Performer::setBlockSize (uint32_t numFramesForNextBlock)
{
    return performer->setBlockSize (numFramesForNextBlock);
}

inline Result Performer::setInputFrames (EndpointHandle endpoint, const void* frameData, uint32_t numFrames)
{
    return performer->setInputFrames (endpoint, frameData, numFrames);
}

template <typename SampleType>
Result Performer::setInputFrames (EndpointHandle endpoint, const choc::buffer::InterleavedView<SampleType>& buffer)
{
    return performer->setInputFrames (endpoint, buffer.data.data, buffer.getNumFrames());
}

template <typename ValueType>
Result Performer::setInputValue (EndpointHandle e, const ValueType& newValue, uint32_t numFramesToReachValue)
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
        return performer->setInputValue (e, std::addressof (v), numFramesToReachValue);
    }
    else if constexpr (std::is_same<const ValueType, const void* const>::value
                        || std::is_same<const ValueType, const char* const>::value)
    {
        return performer->setInputValue (e, newValue, numFramesToReachValue);
    }
    else if constexpr (std::is_same<const ValueType, const bool>::value)
    {
        int32_t v = newValue ? 1 : 0;
        return performer->setInputValue (e, std::addressof (v), numFramesToReachValue);

    }
    else if constexpr (std::is_same<const ValueType, const choc::value::ValueView>::value
                        || std::is_same<const ValueType, const choc::value::Value>::value)
    {
        return performer->setInputValue (e, newValue.getRawData(), numFramesToReachValue);
    }
}

template <typename ValueType>
Result Performer::addInputEvent (EndpointHandle e, uint32_t type, const ValueType& value)
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
        return performer->addInputEvent (e, type, std::addressof (v));
    }
    else if constexpr (std::is_same<const ValueType, const void* const>::value
                        || std::is_same<const ValueType, const char* const>::value)
    {
        return performer->addInputEvent (e, type, value);
    }
    else if constexpr (std::is_same<const ValueType, const bool>::value)
    {
        int32_t v = value ? 1 : 0;
        return performer->addInputEvent (e, type, std::addressof (v));
    }
    else if constexpr (std::is_same<const ValueType, const choc::value::ValueView>::value
                        || std::is_same<const ValueType, const choc::value::Value>::value)
    {
        return performer->addInputEvent (e, type, value.getRawData());
    }
}

inline Result Performer::copyOutputValue (EndpointHandle endpoint, void* dest) const
{
    return performer->copyOutputValue (endpoint, dest);
}

inline Result Performer::copyOutputFrames (EndpointHandle endpoint, void* dest, uint32_t numFramesToCopy) const
{
    return performer->copyOutputFrames (endpoint, dest, numFramesToCopy);
}

template <typename SampleType>
Result Performer::copyOutputFrames (EndpointHandle endpoint, const choc::buffer::InterleavedView<SampleType>& destBuffer) const
{
    return performer->copyOutputFrames (endpoint, destBuffer.data.data, destBuffer.getNumFrames());
}

template <typename SampleType>
Result Performer::copyOutputFrames (EndpointHandle endpoint, choc::buffer::InterleavedBuffer<SampleType>& destBuffer) const
{
    return performer->copyOutputFrames (endpoint, destBuffer.getView().data.data, destBuffer.getNumFrames());
}

template <typename HandlerFn>
inline Result Performer::iterateOutputEvents (EndpointHandle endpoint, HandlerFn&& handler)
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

    return performer->iterateOutputEvents (endpoint, std::addressof (handler), Callback::handleEvent);
}

inline Result Performer::reset()
{
    return performer->reset();
}

inline Result Performer::advance()
{
    return performer->advance();
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
