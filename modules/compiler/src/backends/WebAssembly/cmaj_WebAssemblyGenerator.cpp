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

#include "../../../include/cmaj_DefaultFlags.h"

#if CMAJ_ENABLE_CODEGEN_LLVM_WASM

#include "cmaj_WebAssembly.h"
#include "cmaj_JavascriptClassGenerator.h"

#if CMAJ_ENABLE_CODEGEN_LLVM_WASM
 #include "../LLVM/cmaj_LLVM.h"
#endif

namespace cmaj::webassembly
{

JavascriptWrapper generateJavascriptWrapper (const ProgramInterface& p, std::string_view opts, const BuildSettings& buildSettings)
{
    auto options = choc::json::parse (opts);

    JavascriptClassGenerator gen (AST::getProgram (p), buildSettings, {}, SIMDMode (options));

    JavascriptWrapper w;
    w.code = gen.generate();
    w.mainClassName = gen.mainClassName;
    return w;
}

std::string generateWAST (const ProgramInterface& p, const BuildSettings& buildSettings)
{
   #if CMAJ_ENABLE_CODEGEN_LLVM_WASM
    return llvm::generateWebAssembly (p, buildSettings, true).binaryWASMData;
   #endif
}

} // namespace cmaj::webassembly

#endif
