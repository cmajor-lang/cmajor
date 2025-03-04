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

namespace cmaj
{

//==============================================================================
inline constexpr std::string_view getRootNamespaceName()            { return "_root"; }
inline constexpr std::string_view getStdLibraryNamespaceName()      { return "std"; }
inline constexpr std::string_view getIntrinsicsNamespaceName()      { return "intrinsics"; }
inline constexpr std::string_view getIntrinsicsNamespaceFullName()  { return "std::intrinsics"; }

static inline ptr<AST::Namespace> findIntrinsicsNamespace (AST::Namespace& ns)
{
    auto& rootNamespace = ns.getRootNamespace();

    if (rootNamespace.intrinsicsNamespace == nullptr)
    {
        if (auto stdNamespace = rootNamespace.findSystemChildNamespace (rootNamespace.getStrings().stdLibraryNamespaceName))
            rootNamespace.intrinsicsNamespace = stdNamespace->findSystemChildNamespace (rootNamespace.getStrings().intrinsicsNamespaceName);
    }

    return rootNamespace.intrinsicsNamespace;
}

}
