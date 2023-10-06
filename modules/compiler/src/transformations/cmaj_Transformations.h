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

#include "cmaj_EventHandlerUtilities.h"
#include "cmaj_ValueStreamUtilities.h"

namespace cmaj::transformations
{
    /// Gets a program to the point where it's passed basic validity checks and is ready
    /// to have its sample rate and other processor properties set, and for its endpoints
    /// and externals to be queried and their values resolved.
    void prepareForResolution (AST::Program&,
                               uint64_t stackSizeLimit);

    /// After resolving the program, this does a full validity check, flattens any graphs and
    /// runs transformations to lower its structure to a simpler subset of the AST that's
    /// suitable for the code generator to use
    void prepareForCodeGen (AST::Program&,
                            const BuildSettings&,
                            bool useForwardBranchesForAdvance,
                            bool useDynamicSampleRate,
                            bool allowTopLevelSlices,
                            bool allowExternalFunctions,
                            const std::function<bool(AST::Intrinsic::Type)>& engineSupportsIntrinsic,
                            double& resultLatency,
                            const std::function<bool(const EndpointID&)>& isEndpointActive);

    // Run passes for graph generation
    void prepareForGraphGen (AST::Program&,
                             double frequency,
                             uint64_t stackSizeLimit);

    /// Runs a set of basic simplification and resolution passes, ignoring errors
    /// and stopping when it runs out of things to change.
    void runBasicResolutionPasses (AST::Program&);

    /// Recursively finds child namespaces with the same name and merges them
    void mergeDuplicateNamespaces (AST::Namespace& parentNamespace);

    /// Blanks-out the names of any internal symbols in this program
    void obfuscateNames (AST::Program&);

    /// Store a set of top-level AST objects as a binary module
    std::vector<uint8_t> createBinaryModule (const AST::ObjectRefVector<AST::ModuleBase>& objects);

    /// Reloads a set of objects from a binary module that was created with createBinaryModule()
    AST::ObjectRefVector<AST::ModuleBase> parseBinaryModule (AST::Allocator&, const void*, size_t,
                                                             bool checkHashValidity = true);

    /// Checks whether this seems to be a valid chunk of module data
    bool isValidBinaryModuleData (const void*, size_t);
}
