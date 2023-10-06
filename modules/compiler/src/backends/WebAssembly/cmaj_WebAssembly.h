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

#include "../../../include/cmaj_ErrorHandling.h"

#if CMAJ_ENABLE_CODEGEN_BINARYEN || CMAJ_ENABLE_CODEGEN_LLVM_WASM

#include "../../../../../../cmajor/include/cmajor/COM/cmaj_EngineFactoryInterface.h"
#include "../../codegen/cmaj_CodeGenerator.h"

namespace cmaj::webassembly
{
    struct WebAssemblyModule
    {
        std::string binaryWASMData;

        uint32_t initialNumMemPages = 0,
                 stackTop = 0,
                 stateStructAddress = 0,
                 ioStructAddress = 0,
                 scratchSpaceAddress = 0;

        ptr<const AST::TypeBase> stateStructType, ioStructType;
        NativeTypeLayoutCache nativeTypeLayouts;
        std::function<choc::value::Type(const AST::TypeBase&)> getChocType;
        choc::value::SimpleStringDictionary stringDictionary;
    };

    namespace binaryen
    {
        WebAssemblyModule generateWebAssembly (const ProgramInterface&, const BuildSettings&);
        std::string generateWAST (const ProgramInterface&, const BuildSettings&);
    }

   #if CMAJ_ENABLE_CODEGEN_LLVM_WASM || CMAJ_ENABLE_CODEGEN_BINARYEN
    struct JavascriptWrapper
    {
        std::string code, mainClassName;
    };

    JavascriptWrapper generateJavascriptWrapper (const ProgramInterface&, const BuildSettings&, bool useBinaryen);

    std::string generateWAST (const ProgramInterface&, const BuildSettings&);
   #endif

   #if CMAJ_ENABLE_PERFORMER_WEBVIEW
    EngineFactoryPtr createEngineFactory();
   #endif

   #if CMAJ_ENABLE_PERFORMER_WEBVIEW && CMAJ_ENABLE_CODEGEN_BINARYEN
    EngineFactoryPtr createEngineFactoryWithBinaryen();
   #endif
}

#endif
