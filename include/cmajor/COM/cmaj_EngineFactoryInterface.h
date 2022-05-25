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

#include "cmaj_EngineInterface.h"


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
struct EngineFactoryInterface   : public COMObjectBase
{
    ~EngineFactoryInterface() override = default;

    /// Takes an optional JSON string specifying the options for the engine
    [[nodiscard]] virtual EngineInterface* createEngine (const char* engineCreationOptions) = 0;
    virtual const char* getName() = 0;
};

using EngineFactoryPtr = choc::com::Ptr<EngineFactoryInterface>;

} // namespace cmaj
