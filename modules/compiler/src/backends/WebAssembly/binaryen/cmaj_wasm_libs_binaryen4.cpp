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

#include "binaryen/src/passes/PostEmscripten.cpp"
#include "binaryen/src/passes/RemoveUnusedBrs.cpp"
#include "binaryen/src/passes/AvoidReinterprets.cpp"
#include "binaryen/src/passes/Asyncify.cpp"
#include "binaryen/src/passes/SetGlobals.cpp"
#include "binaryen/src/passes/CodePushing.cpp"
#include "binaryen/src/passes/DeadCodeElimination.cpp"
#include "binaryen/src/passes/SafeHeap.cpp"
#include "binaryen/src/passes/OptimizeForJS.cpp"
#include "binaryen/src/passes/AlignmentLowering.cpp"
#include "binaryen/src/passes/ReorderFunctions.cpp"
#include "binaryen/src/passes/OptimizeAddedConstants.cpp"
#include "binaryen/src/passes/StripTargetFeatures.cpp"
#include "binaryen/src/passes/LocalSubtyping.cpp"
#include "binaryen/src/passes/NameTypes.cpp"
#include "binaryen/src/passes/ExtractFunction.cpp"
#include "binaryen/src/passes/OptimizeInstructions.cpp"
#include "binaryen/src/passes/MergeBlocks.cpp"
#include "binaryen/src/passes/RedundantSetElimination.cpp"
#include "binaryen/src/passes/ReorderLocals.cpp"
#include "binaryen/src/passes/Untee.cpp"
#include "binaryen/src/passes/NameList.cpp"
#include "binaryen/src/passes/Memory64Lowering.cpp"
#include "binaryen/src/passes/SimplifyLocals.cpp"
#include "binaryen/src/passes/DeAlign.cpp"
#include "binaryen/src/passes/InstrumentLocals.cpp"
#include "binaryen/src/passes/Intrinsics.cpp"
#include "binaryen/src/passes/DWARF.cpp"
#define Scanner Scanner2
#include "binaryen/src/passes/LocalCSE.cpp"
#undef Scanner
#undef DEBUG_TYPE
#include "binaryen/src/passes/StackCheck.cpp"
#include "binaryen/src/passes/DataFlowOpts.cpp"
#include "binaryen/src/passes/CodeFolding.cpp"
#include "binaryen/src/passes/FuncCastEmulation.cpp"
#include "binaryen/src/passes/InstrumentMemory.cpp"
#include "binaryen/src/cfg/Relooper.cpp"
#include "binaryen/src/support/safe_integer.cpp"
#include "binaryen/src/support/utilities.cpp"
#undef DEBUG_TYPE
#include "binaryen/src/support/file.cpp"
#include "binaryen/src/support/debug.cpp"
#include "binaryen/src/support/path.cpp"
#include "binaryen/src/support/bits.cpp"
#include "binaryen/src/support/threads.cpp"
#include "binaryen/src/support/archive.cpp"
#include "binaryen/src/passes/CoalesceLocals.cpp"
#include "binaryen/src/passes/DeNaN.cpp"

#endif
