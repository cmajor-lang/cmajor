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

#include "../../../../include/cmaj_DefaultFlags.h"

#if CMAJ_ENABLE_CODEGEN_BINARYEN

#include "choc/platform/choc_DisableAllWarnings.h"

#include "binaryen/src/wasm.h"

#include "binaryen/src/asmjs/shared-constants.cpp"
#include "binaryen/src/asmjs/asm_v_wasm.cpp"
#include "binaryen/src/asmjs/asmangle.cpp"
#include "binaryen/src/emscripten-optimizer/optimizer-shared.cpp"
#include "binaryen/src/emscripten-optimizer/parser.cpp"
#include "binaryen/src/emscripten-optimizer/simple_ast.cpp"
#include "binaryen/src/ir/LocalGraph.cpp"
#include "binaryen/src/ir/table-utils.cpp"
#include "binaryen/src/ir/names.cpp"
#include "binaryen/src/ir/properties.cpp"
#include "binaryen/src/ir/module-splitting.cpp"
#include "binaryen/src/ir/ExpressionManipulator.cpp"
#include "binaryen/src/ir/ExpressionAnalyzer.cpp"
#include "binaryen/src/ir/type-updating.cpp"
#include "binaryen/src/ir/stack-utils.cpp"
#include "binaryen/src/ir/ReFinalize.cpp"
#include "binaryen/src/ir/intrinsics.cpp"
#include "binaryen/src/ir/eh-utils.cpp"
#include "binaryen/src/ir/possible-contents.cpp"
#include "binaryen/src/ir/drop.cpp"
#include "binaryen/src/wasm/wasm-emscripten.cpp"
#include "binaryen/src/wasm/wasm-debug.cpp"
#include "binaryen/src/wasm/wasm.cpp"
#include "binaryen/src/wasm/parsing.cpp"
#include "binaryen/src/wasm/wasm-interpreter.cpp"
#include "binaryen/src/wasm/wasm-io.cpp"
#include "binaryen/src/wasm/literal.cpp"
#include "binaryen/src/wasm/wasm-validator.cpp"
#include "binaryen/src/wasm/wasm-stack.cpp"
#include "binaryen/src/passes/GlobalTypeOptimization.cpp"
#undef err
#include "binaryen/src/wasm/wat-parser.cpp"
#include "binaryen/src/wasm/wat-lexer.cpp"


// instead of including binaryen/src/support/colors.cpp, just going to
// stub these out to avoid the daft decision of enabling colour by default
#ifdef WIN32
void Colors::outputColorCode (std::ostream& stream, const unsigned short& colorCode) {}
#elif ! defined (__EMSCRIPTEN__)
void Colors::outputColorCode (std::ostream&, const char*) {}
#endif

void Colors::setEnabled(bool) {}
bool Colors::isEnabled() { return false; }

#endif
