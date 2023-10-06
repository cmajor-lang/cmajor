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

#if defined (DEBUG) || defined (_DEBUG) || ! (defined (NDEBUG) || defined (_NDEBUG))
 #define CMAJ_DEBUG 1
#endif

#ifdef __clang__
 #define CMAJ_TODO  _Pragma("message(\"TODO\")")
#else
 #define CMAJ_TODO // ignore for now
#endif


//==============================================================================
// These flags must all be set up by the project, but are defined here to help
// intellisense not assume that they're all disabled

#ifndef CMAJ_ENABLE_PERFORMER_LLVM
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_PERFORMER_LLVM 1
#endif

#ifndef CMAJ_ENABLE_PERFORMER_WEBVIEW
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_PERFORMER_WEBVIEW 1
#endif

#ifndef CMAJ_ENABLE_PERFORMER_CPP
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_PERFORMER_CPP 1
#endif

#ifndef CMAJ_ENABLE_CODEGEN_CPP
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_CODEGEN_CPP 1
#endif

#ifndef CMAJ_ENABLE_CODEGEN_LLVM_WASM
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_CODEGEN_LLVM_WASM 1
#endif

#ifndef CMAJ_ENABLE_CODEGEN_BINARYEN
 #error // this should be defined globally by the project
 #define CMAJ_ENABLE_CODEGEN_BINARYEN 1
#endif

//==============================================================================
#if ! (CMAJ_ENABLE_PERFORMER_CPP || CMAJ_ENABLE_PERFORMER_WEBVIEW || CMAJ_ENABLE_PERFORMER_LLVM \
        || CMAJ_ENABLE_CODEGEN_BINARYEN || CMAJ_ENABLE_CODEGEN_LLVM_WASM)
 #error "Need to enable at least one back-end!"
#endif
