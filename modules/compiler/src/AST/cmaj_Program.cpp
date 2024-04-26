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

#include "../../include/cmaj_ErrorHandling.h"
#include "../../../../include/cmajor/COM/cmaj_Library.h"
#include "cmaj_AST.h"
#include "cmaj_Lexer.h"
#include "../transformations/cmaj_Transformations.h"
#include "cmaj_Parser.h"
#include "../standard_library/cmaj_StandardLibrary.h"
#include "../standard_library/cmaj_StandardLibraryBinary.h"

namespace cmaj
{
    const char* Library::getVersion()
    {
        return CMAJ_VERSION;
    }

    ProgramPtr Library::createProgram()
    {
        return choc::com::create<cmaj::AST::Program>();
    }

    void AST::Program::parse (const SourceFile& source, bool isSystemModule)
    {
        Parser::parseModuleDeclarations (allocator, source, isSystemModule, parsingComments, rootNamespace, {});
        resetMainProcessor();
    }

    void AST::Program::addStandardLibraryCode()
    {
        for (auto& m : transformations::parseBinaryModule (allocator, standardLibraryData, sizeof (standardLibraryData), false))
            rootNamespace.subModules.addChildObject (m);

        transformations::mergeDuplicateNamespaces (rootNamespace);
    }
}
