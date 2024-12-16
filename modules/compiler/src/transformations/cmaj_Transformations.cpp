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

#include <iostream>
#include <map>
#include <set>

#include "../../include/cmaj_ErrorHandling.h"
#include "choc/text/choc_Wildcard.h"

#include "../passes/cmaj_Passes.h"
#include "../validation/cmaj_Validator.h"

#include "cmaj_Transformations.h"

#include "cmaj_BlockTransformation.h"
#include "cmaj_CreateSystemInitFunction.h"
#include "cmaj_MoveStateVariablesToStruct.h"
#include "cmaj_MoveVariablesToState.h"
#include "cmaj_RemoveAdvanceCalls.h"
#include "cmaj_RemoveResetCalls.h"
#include "cmaj_ReplaceWrapTypes.h"
#include "cmaj_ProcessorPropertiesToState.h"
#include "cmaj_CanonicaliseLoopsAndBlocks.h"
#include "cmaj_OversamplingTransformation.h"
#include "cmaj_TransformGraph.h"
#include "cmaj_HoistedEndpointConnector.h"
#include "cmaj_SimplifyGraphConnections.h"
#include "cmaj_ConvertComplexTypes.h"
#include "cmaj_RemoveGenericObjects.h"
#include "cmaj_ReplaceProcessorProperties.h"
#include "cmaj_FunctionInliner.h"
#include "cmaj_RemoveUnusedEndpoints.h"
#include "cmaj_RemoveUnusedNodes.h"
#include "cmaj_AddSparseStreamSupport.h"
#include "cmaj_CloneGraphNodes.h"
#include "cmaj_BinaryModuleFormat.h"
#include "cmaj_MergeDuplicateNamespaces.h"
#include "cmaj_ObfuscateNames.h"
#include "cmaj_DetermineFunctionAliasStatus.h"
#include "cmaj_MakeUnwrittenVariablesConst.h"
#include "cmaj_AddFallbackIntrinsics.h"
#include "cmaj_ReplaceMultidimensionalArrays.h"
#include "cmaj_ConvertLargeConstants.h"
#include "cmaj_TransformSlices.h"

namespace cmaj::transformations
{

static void runResolutionPasses (AST::Program& program, bool throwOnErrors)
{
    for (;;)
    {
        passes::PassResult result;

        result += passes::runPass<passes::TypeResolver>             (program, throwOnErrors);
        result += passes::runPass<passes::FunctionResolver>         (program, throwOnErrors);
        result += passes::runPass<passes::NameResolver>             (program, throwOnErrors);
        result += passes::runPass<passes::ModuleSpecialiser>        (program, throwOnErrors);
        result += passes::runPass<passes::ProcessorResolver>        (program, throwOnErrors);
        result += passes::runPass<passes::EndpointResolver>         (program, throwOnErrors);
        result += passes::runPass<passes::ConstantFolder>           (program, throwOnErrors);
        result += passes::runPass<passes::StrengthReduction>        (program, throwOnErrors);
        result += passes::runPass<passes::ExternalResolver>         (program, throwOnErrors);

        if (result.numChanges == 0)
            return;
    }
}

static void runFullResolutionAndChecks (AST::Program& program, uint64_t stackSizeLimit, bool allowTopLevelSlices, bool allowExternalFunctions)
{
    runResolutionPasses (program, false);
    passes::DuplicateNameCheckPass::check (program);
    runResolutionPasses (program, true);

    validation::PostLink::check (program, stackSizeLimit, allowTopLevelSlices, allowExternalFunctions);
}

void runBasicResolutionPasses (AST::Program& program)
{
    runResolutionPasses (program, false);
}

void prepareForResolution (AST::Program& program, uint64_t stackSizeLimit)
{
    runResolutionPasses (program, false);

    if (! validation::PostLoad::check (program))
        runFullResolutionAndChecks (program, stackSizeLimit, false, true);

    createHoistedEndpointConnections (program);
}

void prepareForCodeGen (AST::Program& program,
                        const BuildSettings& buildSettings,
                        bool useForwardBranchesForAdvance,
                        bool useDynamicSampleRate,
                        bool allowTopLevelSlices,
                        bool allowExternalFunctions,
                        const std::function<bool(AST::Intrinsic::Type)>& engineSupportsIntrinsic,
                        double& resultLatency,
                        const std::function<bool(const EndpointID&)>& isEndpointActive)
{
    CMAJ_ASSERT (buildSettings.getMaxBlockSize() != 0 && buildSettings.getEventBufferSize() != 0);

    cloneGraphNodes (program);

    auto processorReplacementState = replaceProcessorProperties (program, buildSettings.getMaxFrequency(), buildSettings.getFrequency(), useDynamicSampleRate);

    while (processorReplacementState.propertiesReplaced != 0)
    {
        runFullResolutionAndChecks (program, buildSettings.getMaxStackSize(), allowTopLevelSlices, allowExternalFunctions);
        processorReplacementState = replaceProcessorProperties (program, buildSettings.getMaxFrequency(), buildSettings.getFrequency(), useDynamicSampleRate);
    }

    runFullResolutionAndChecks (program, buildSettings.getMaxStackSize(), allowTopLevelSlices, allowExternalFunctions);
    simplifyGraphConnections (program);
    runResolutionPasses (program, allowTopLevelSlices);

    resultLatency = program.getMainProcessor().getLatency();

    determineFunctionAliasStatus (program);
    removeUnusedNodes (program);
    removeGenericAndParameterisedObjects (program);
    removeUnusedEndpoints (program, isEndpointActive);
    runResolutionPasses (program, allowTopLevelSlices);
    convertComplexTypes (program);
    addFallbackIntrinsics (program, engineSupportsIntrinsic);
    canonicaliseLoopsAndBlocks (program);
    replaceWrapTypes (program);
    transformSlices (program);
    replaceMultidimensionalArrays (program);
    convertUnwrittenVariablesToConst (program);
    inlineAllCallsWhichAdvance (program);
    createSystemInitFunctions (program, processorReplacementState.sessionIDVariable, processorReplacementState.frequencyVariable);
    convertLargeConstantsToGlobals (program);
    flattenGraph (program, buildSettings.getMaxBlockSize(), buildSettings.getEventBufferSize(), useForwardBranchesForAdvance);
}

void prepareForGraphGen (AST::Program& program,
                         double frequency,
                         uint64_t stackSizeLimit)
{
    cloneGraphNodes (program);
    replaceProcessorProperties (program, frequency, frequency, false);
    runFullResolutionAndChecks (program, stackSizeLimit, true, true);
    simplifyGraphConnections (program);
    runResolutionPasses (program, true);
}

}
