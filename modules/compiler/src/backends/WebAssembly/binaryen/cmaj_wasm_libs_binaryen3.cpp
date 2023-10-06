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

#undef DEBUG_TYPE
#include "binaryen/src/wasm/wasm-binary.cpp"
#include "binaryen/src/passes/Flatten.cpp"
#include "binaryen/src/passes/Poppify.cpp"
#include "binaryen/src/passes/StackIR.cpp"
#include "binaryen/src/passes/Precompute.cpp"
#include "binaryen/src/passes/Directize.cpp"
#include "binaryen/src/passes/Strip.cpp"
#include "binaryen/src/passes/LoopInvariantCodeMotion.cpp"
#include "binaryen/src/passes/RemoveNonJSOps.cpp"
#include "binaryen/src/passes/SimplifyGlobals.cpp"
#include "binaryen/src/passes/DuplicateImportElimination.cpp"
#include "binaryen/src/passes/RemoveUnusedNames.cpp"
#undef DEBUG_TYPE
#include "binaryen/src/passes/GenerateDynCalls.cpp"
#include "binaryen/src/passes/PrintCallGraph.cpp"
#include "binaryen/src/passes/RemoveUnusedModuleElements.cpp"
#include "binaryen/src/passes/pass.cpp"
#include "binaryen/src/passes/TrapMode.cpp"
#include "binaryen/src/passes/Heap2Local.cpp"
#include "binaryen/src/passes/Vacuum.cpp"
#include "binaryen/src/passes/LimitSegments.cpp"
#include "binaryen/src/passes/LegalizeJSInterface.cpp"
#include "binaryen/src/passes/Inlining.cpp"
#include "binaryen/src/passes/MergeLocals.cpp"
#include "binaryen/src/passes/LogExecution.cpp"
#include "binaryen/src/passes/PickLoadSigns.cpp"
#include "binaryen/src/passes/ConstHoisting.cpp"
#include "binaryen/src/passes/MinifyImportsAndExports.cpp"
#include "binaryen/src/passes/ConstantFieldPropagation.cpp"
#include "binaryen/src/passes/RemoveMemory.cpp"
#include "binaryen/src/passes/PrintFeatures.cpp"
#include "binaryen/src/passes/Print.cpp"
#include "binaryen/src/passes/DeadArgumentElimination.cpp"
#include "binaryen/src/passes/MemoryPacking.cpp"
#include "binaryen/src/passes/RemoveImports.cpp"
#include "binaryen/src/passes/RoundTrip.cpp"
#include "binaryen/src/passes/I64ToI32Lowering.cpp"
#include "binaryen/src/passes/Metrics.cpp"
#include "binaryen/src/passes/DuplicateFunctionElimination.cpp"
#include "binaryen/src/passes/SSAify.cpp"
#include "binaryen/src/passes/PrintFunctionMap.cpp"
#include "binaryen/src/passes/TypeRefining.cpp"
#include "binaryen/src/passes/GlobalRefining.cpp"
#include "binaryen/src/passes/SignatureRefining.cpp"
#include "binaryen/src/passes/test_passes.cpp"
#include "binaryen/src/passes/GUFA.cpp"
#include "binaryen/src/passes/SpillPointers.cpp"
#include "binaryen/src/passes/GlobalStructInference.cpp"

#endif
