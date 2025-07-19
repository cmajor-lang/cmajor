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

#include "../../choc/choc/containers/choc_COM.h"

#ifdef __clang__
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#elif __GNUC__
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // COM objects can't have a virtual destructor
#endif

namespace cmaj
{

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
struct ProgramInterface  : public choc::com::Object
{
    ProgramInterface() = default;

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

#ifdef __clang__
 #pragma clang diagnostic pop
#elif __GNUC__
 #pragma GCC diagnostic pop
#endif
