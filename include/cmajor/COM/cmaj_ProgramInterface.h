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

#include "../../choc/containers/choc_COM.h"

namespace cmaj
{

using COMObjectBase = choc::com::ObjectWithAtomicRefCount<choc::com::Object>;

/// Options used by Program::getSyntaxTree()
struct SyntaxTreeOptions
{
    /// Pass nullptr to get everything in the root namespace.
    const char* namespaceOrModule  = nullptr;

    bool includeSourceLocations    = false;
    bool includeComments           = false;
    bool includeFunctionContents   = false;
};

//==============================================================================
/**
    The basic COM API class for a program.

    Note that the cmaj::Program class provides a much nicer-to-use wrapper
    around this class, to avoid you needing to understand all the COM nastiness!

    To create a ProgramInterface, either create a cmaj::Program, or to create
    just the underlying COM object without the wrapper class, call
    cmaj::Library::createProgram().
*/
struct ProgramInterface  : public COMObjectBase
{
    ProgramInterface() = default;
    virtual ~ProgramInterface() = default;

    /// Parses some content, and returns either a nullptr or a JSON-encoded error
    /// that can be parsed with DiagnosticMessageList::fromJSONString()
    [[nodiscard]] virtual choc::com::String* parse (const char* filename,
                                                    const char* fileContent,
                                                    size_t fileContentSize) = 0;

    /// Returns a JSON version of the current syntax tree.
    [[nodiscard]] virtual choc::com::String* getSyntaxTree (const SyntaxTreeOptions&) = 0;
};

using ProgramPtr = choc::com::Ptr<ProgramInterface>;

} // namespace cmaj
