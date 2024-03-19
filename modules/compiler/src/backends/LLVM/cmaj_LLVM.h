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

#include "../../../include/cmaj_DefaultFlags.h"
#include "../../../include/cmajor/API/cmaj_BuildSettings.h"
#include "../../../include/cmajor/COM/cmaj_EngineFactoryInterface.h"
#include "../../AST/cmaj_AST.h"
#include "../../codegen/cmaj_NativeTypeLayout.h"
#include "../WebAssembly/cmaj_WebAssembly.h"

#if CMAJ_ENABLE_PERFORMER_LLVM || CMAJ_ENABLE_CODEGEN_LLVM_WASM

namespace cmaj::llvm
{
   #if CMAJ_ENABLE_PERFORMER_LLVM
    static constexpr const char* backendName = "llvm";
    EngineFactoryPtr createEngineFactory();

    std::string generateAssembler (const cmaj::ProgramInterface& program,
                                   const cmaj::BuildSettings& buildSettings,
                                   const choc::value::Value& options);

    std::vector<std::string> getAssemberTargets();
   #endif

   #if CMAJ_ENABLE_CODEGEN_LLVM_WASM
    webassembly::WebAssemblyModule generateWebAssembly (const ProgramInterface&, const BuildSettings&, bool useSimd, bool createWAST = false);
   #endif
}

#endif
