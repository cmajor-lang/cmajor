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

#include "cmaj_ProgramInterface.h"

#ifdef __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#elif __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#endif

namespace cmaj
{

//==============================================================================
/// An endpoint handle is an ID provided by a performer to identify one of
/// its endpoints - see PerformerInterface::getEndpointHandle()
using EndpointHandle = uint32_t;


//==============================================================================
/** This is the basic COM API class for a performer.

    Note that the cmaj::Performer class provides a much nicer-to-use wrapper
    around this class, to avoid you needing to understand all the COM nastiness!

    PerformerInterface objects are created by an EngineInterface (or the cmaj::Engine
    helper class), and they are a fully linked, stateful, ready to render instance
    of a program.
*/
struct PerformerInterface   : public choc::com::Object
{
    PerformerInterface() = default;

    //==============================================================================
    /// Sets the number of frames which should be rendered during each subsequent call to advance().
    ///
    /// To use a performer, the caller must repeatedly:
    ///   - call setBlockSize() to specify the size of block to render (if the size hasn't changed
    ///     since the last call to setBlockSize() then there's no need to call it again)
    ///   - pass appropriately-sized chunks of data and event values to any input endpoints
    ///     that will need it to process the block
    ///   - call advance() to perform the rendering
    ///   - empty any outgoing events or stream data from any output endpoints
    ///
    virtual void setBlockSize (uint32_t numFramesForNextBlock) = 0;

    /// Provides a block of frames to an input stream endpoint.
    /// This function must only be called on the rendering thread, as part of the preparations for
    /// a call to advance().
    /// You should call this function for each input stream endpoint, to provide the chunk of data that
    /// it will use in the next advance() call. The number of frames provided must be the same as the
    /// size set by the last call to setBlockSize().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// It should only be called once before each advance() call.
    virtual void setInputFrames (EndpointHandle, const void* frameData, uint32_t numFrames) = 0;

    /// Sets the current value for a latching input value endpoint.
    /// Before calling advance(), this can optionally be called for a value input to change its value.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// It should only be called once for each stream within the same advance call.
    virtual void setInputValue (EndpointHandle, const void* valueData, uint32_t numFramesToReachValue) = 0;

    /// Adds an event to the queue for an input event endpoint.
    /// This function must only be called on the rendering thread, as part of the preparations for
    /// a call to advance().
    /// It can be called multiple times if needed to dispatch a sequence of event handler callbacks.
    /// Depending on the back-end implementation, these may either be invoked synchronously during this
    /// call, or they may be queued and invoked at the start of the next advance() call.
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// If the endpoint is an event that supports multiple types, the typeIndex selects the one to use
    /// (just set it to 0 for endpoints with only one type).
    virtual void addInputEvent (EndpointHandle, uint32_t typeIndex, const void* eventData) = 0;

    /// Fetches the data for the current value of an output stream or value endpoint.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the value or frame data for the given endpoint.
    /// The data pointer and size returned point to a chunk of choc::value::ValueView data, whose type
    /// the caller should know in advance by getting the endpoint's details.
    /// The pointer that is returned will become invalid as soon as another method is called on the performer.
    virtual void copyOutputValue (EndpointHandle, void* dest) = 0;

    /// Copies out the data from an output stream endpoint.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to retrieve the value or frame data for the given endpoint.
    /// The pointer provided will have a chunk of choc::value::ValueView data written to it, whose type
    /// the caller should know in advance by getting the endpoint's details.
    virtual void copyOutputFrames (EndpointHandle, void* dest, uint32_t numFramesToCopy) = 0;

    /// A user-callback function that is passed to iterateOutputEvents().
    /// The frameOffset is an index into the block that was last rendered during the advance() call.
    /// If this returns true, then iteration will continue. If false, then iteration will stop.
    using HandleOutputEventCallback = bool(*)(void* context, EndpointHandle, uint32_t dataTypeIndex,
                                              uint32_t frameOffset, const void* valueData, uint32_t valueDataSize);

    /// Iterates the events that were pushed into an output event stream during the last advance() call.
    /// This function must only be called on the rendering thread, after a call to advance().
    /// The handle must have been obtained by calling getEndpointHandle() before the program is linked.
    /// After calling advance(), this can be called to fetch events that were sent to the given endpoint.
    virtual void iterateOutputEvents (EndpointHandle, void* context, HandleOutputEventCallback) = 0;

    /// Renders the next block.
    /// The number of frames rendered will be the number that was last specified by a call to setBlockSize().
    virtual void advance() = 0;

    /// Retrieves the string from a handle used in the current program, or nullptr if not found.
    virtual const char* getStringForHandle (uint32_t handle, size_t& stringLength) = 0;

    /// Returns the total number of over- and under-runs that have happened since the program was linked.
    /// These occur when the caller fails to fully empty or fill the input and output endpoint streams
    /// between calls to advance().
    virtual uint32_t getXRuns() = 0;

    /// Returns the maximum number of frames that may be set as the block size in a call to setBlockSize().
    virtual uint32_t getMaximumBlockSize() = 0;

    /// Returns the maximum number of events that can be sent per block.
    virtual uint32_t getEventBufferSize() = 0;

    /// Returns the performer's internal latency in frames
    virtual double getLatency() = 0;

    /// If there has been a runtime error, this returns the message, or nullptr if there isn't one.
    virtual const char* getRuntimeError() = 0;
};

using PerformerPtr = choc::com::Ptr<PerformerInterface>;

} // namespace cmaj

#ifdef __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#endif
