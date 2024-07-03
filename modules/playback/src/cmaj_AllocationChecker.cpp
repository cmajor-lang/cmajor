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

#include "../../compiler/include/cmaj_ErrorHandling.h"
#include "../include/cmaj_AllocationChecker.h"
#include "cmajor/helpers/cmaj_PerformerProxy.h"

#ifdef CMAJ_ENABLE_ALLOCATION_CHECKER

#include <new>
#include <cstddef>
#include <string>

namespace cmaj
{
    struct AllocationCount
    {
        size_t allocations = 0;
        size_t frees = 0;
    };

    static thread_local int disableAllocationTracker = 0;
    static thread_local int throwOnAllocation = 0;

    static thread_local cmaj::AllocationCount allocationCount;

    ScopedAllocationTracker::ScopedAllocationTracker()  { throwOnAllocation++; }
    ScopedAllocationTracker::~ScopedAllocationTracker() { throwOnAllocation--; }

    ScopedDisableAllocationTracking::ScopedDisableAllocationTracking()  { disableAllocationTracker++; }
    ScopedDisableAllocationTracking::~ScopedDisableAllocationTracking() { disableAllocationTracker--; }

    static void checkAllocationAllowed()
    {
        if (cmaj::disableAllocationTracker == 0)
        {
            if (cmaj::throwOnAllocation != 0)
            {
                cmaj::disableAllocationTracker++;
                CMAJ_ASSERT (cmaj::throwOnAllocation == 0);
            }

            cmaj::allocationCount.allocations++;
        }
    }

    void* performNew (std::size_t size)
    {
        checkAllocationAllowed();
        return std::malloc (size);
    }

    void performDelete (void* p) noexcept
    {
        checkAllocationAllowed();
        std::free (p);
    }
}

void* operator new (std::size_t size)                                           { return cmaj::performNew (size); }
void* operator new (std::size_t size, const std::nothrow_t& )  noexcept         { return cmaj::performNew (size); }

void operator delete (void* ptr) noexcept                                       { cmaj::performDelete (ptr); }
void operator delete (void* ptr, const std::nothrow_t&) noexcept                { cmaj::performDelete (ptr); }
void operator delete (void* ptr, std::size_t) noexcept                          { cmaj::performDelete (ptr); }
void operator delete (void* ptr, std::size_t, const std::nothrow_t&) noexcept   { cmaj::performDelete (ptr); }

//==============================================================================
namespace cmaj
{

struct PerformerAllocationCheckWrapper  : public choc::com::ObjectWithAtomicRefCount<cmaj::PerformerProxy, PerformerAllocationCheckWrapper>
{
    PerformerAllocationCheckWrapper (PerformerPtr p)
    {
        target = std::move (p);
    }

    Result reset() override
    {
        ScopedAllocationTracker allocationTracker;
        return target->reset();
    }

    Result setBlockSize (uint32_t numFramesForNextBlock) override
    {
        ScopedAllocationTracker allocationTracker;
        return target->setBlockSize (numFramesForNextBlock);
    }

    Result setInputFrames (EndpointHandle endpoint, const void* frameData, uint32_t numFrames) override
    {
        ScopedAllocationTracker allocationTracker;
        return target->setInputFrames (endpoint, frameData, numFrames);
    }

    Result setInputValue (EndpointHandle endpoint, const void* valueData, uint32_t rampFrames) override
    {
        ScopedAllocationTracker allocationTracker;
        return target->setInputValue (endpoint, valueData, rampFrames);
    }

    Result addInputEvent (EndpointHandle endpoint, uint32_t typeIndex, const void* eventData) override
    {
        ScopedAllocationTracker allocationTracker;
        return target->addInputEvent (endpoint, typeIndex, eventData);
    }

    Result copyOutputValue (EndpointHandle h, void* dest) override
    {
        ScopedAllocationTracker allocationTracker;
        return target->copyOutputValue (h, dest);
    }

    Result iterateOutputEvents (EndpointHandle h, void* context, HandleOutputEventCallback fn) override
    {
        ScopedAllocationTracker allocationTracker;

        struct CallbackInfo
        {
            void* context;
            HandleOutputEventCallback fn;

            static bool handleEvent (void* context, EndpointHandle handle, uint32_t dataTypeIndex,
                                        uint32_t frameOffset, const void* valueData, uint32_t valueDataSize)
            {
                cmaj::ScopedDisableAllocationTracking disableTracking;
                auto call = static_cast<CallbackInfo*> (context);
                return call->fn (call->context, handle, dataTypeIndex, frameOffset, valueData, valueDataSize);
            }
        };

        auto cb = CallbackInfo { context, fn };
        return target->iterateOutputEvents (h, &cb, CallbackInfo::handleEvent);
    }

    Result advance() override
    {
        ScopedAllocationTracker allocationTracker;
        return target->advance();
    }
};

cmaj::PerformerPtr createAllocationCheckingPerformerWrapper (cmaj::PerformerPtr source)
{
    return choc::com::create<PerformerAllocationCheckWrapper> (std::move (source));
}

} // namespace cmaj

#else

cmaj::PerformerPtr cmaj::createAllocationCheckingPerformerWrapper (cmaj::PerformerPtr source)
{
    return source;
}

#endif
