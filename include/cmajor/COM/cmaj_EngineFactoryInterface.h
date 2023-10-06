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

#include "cmaj_EngineInterface.h"

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
/**
    This is the basic COM API for a factory that can create engines for the
    different back-ends (e.g. LLVM, WASM, etc).

    To get the list of available engine types and to create one, see the
    cmaj::Library class. Or you can use the cmaj::Engine helper class to
    skip all the COM and factory pattern nastiness and just get an engine
    using an idiomatic C++ style.
*/
struct EngineFactoryInterface   : public choc::com::Object
{
    /// Takes an optional JSON string specifying the options for the engine
    [[nodiscard]] virtual EngineInterface* createEngine (const char* engineCreationOptions) = 0;
    virtual const char* getName() = 0;
};

using EngineFactoryPtr = choc::com::Ptr<EngineFactoryInterface>;

} // namespace cmaj

#ifdef __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#endif
