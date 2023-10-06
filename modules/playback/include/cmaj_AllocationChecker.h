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

#include "cmajor/API/cmaj_Performer.h"

namespace cmaj
{

#if CMAJ_ENABLE_ALLOCATION_CHECKER

// ==============================================================================
// Implementation supporting counting allocations and frees used to spot problems
// in realtime safe code paths
struct ScopedAllocationTracker
{
    ScopedAllocationTracker();
    ~ScopedAllocationTracker();
};

struct ScopedDisableAllocationTracking
{
    ScopedDisableAllocationTracking();
    ~ScopedDisableAllocationTracking();
};

#else

// ==============================================================================
// Dummy implementation when allocation checker is not enabled
struct ScopedAllocationTracker
{
    ScopedAllocationTracker() {}
};

struct ScopedDisableAllocationTracking
{
    ScopedDisableAllocationTracking() {}
};

#endif

/// If allocation checking is enabled, this will return a wrapper, otherwise
/// will return the one passed in.
cmaj::PerformerPtr createAllocationCheckingPerformerWrapper (cmaj::PerformerPtr source);

} // namespace cmaj
