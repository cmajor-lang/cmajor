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

#include "cmaj_Patch.h"
#include "cmaj_GeneratedCppEngine.h"

#if CMAJ_USE_QUICKJS_WORKER
 #include "cmaj_PatchWorker_QuickJS.h"
#else
 #include "cmaj_PatchWorker_WebView.h"
#endif

#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <optional>

namespace cmaj::plugin
{

//==============================================================================
struct Environment
{
    enum class EngineType
    {
        AOT,
        JIT
    };

    struct VirtualFileSystem
    {
        std::function<std::unique_ptr<std::istream>(const std::filesystem::path&)> createFileReader;
        std::function<std::filesystem::path(const std::filesystem::path&)> getFullPathForFile;
        std::function<std::filesystem::file_time_type(const std::filesystem::path&)> getFileModificationTime;
        std::function<bool(const std::filesystem::path&)> fileExists;
    };

    EngineType engineType;
    std::function<cmaj::Engine()> createEngine;
    std::optional<VirtualFileSystem> vfs; // will default to OS filesystem

    PatchManifest makePatchManifest (const std::filesystem::path& path) const
    {
        try
        {
            PatchManifest manifest;
            manifest.needsToBuildSource = engineType == EngineType::JIT;

            if (vfs)
            {
                manifest.initialiseWithVirtualFile (path.generic_string(),
                                                    vfs->createFileReader,
                                                    [getFullPath = vfs->getFullPathForFile] (const auto& p) { return getFullPath (p).string(); },
                                                    vfs->getFileModificationTime,
                                                    vfs->fileExists);
            }
            else
            {
                manifest.initialiseWithFile (path);
            }

            return manifest;
        }
        catch (...) {}

        return {};
    }

    void initialisePatch (cmaj::Patch& patch) const
    {
        patch.createEngine = createEngine;
        patch.stopPlayback = [] {};
        patch.startPlayback = [] {};
        patch.patchChanged = [] {};
        patch.statusChanged = [] (auto&&...) {};
        patch.handleOutputEvent = [] (auto&&...) {};

       #if CMAJ_USE_QUICKJS_WORKER
        enableQuickJSPatchWorker (patch);
       #else
        enableWebViewPatchWorker (patch);
       #endif

        patch.setAutoRebuildOnFileChange (engineType == EngineType::JIT);
    }
};

//==============================================================================
struct FrequencyAndBlockSize
{
    double frequency;
    uint32_t maxBlockSize;
};

//==============================================================================
template <typename PatchClass>
Environment::VirtualFileSystem createVirtualFileSystem()
{
    return
    {
        [] (const auto& f) -> std::unique_ptr<std::istream>
        {
            for (auto& file : PatchClass::files)
                if (f == file.name)
                    return std::make_unique<std::istringstream> (std::string (file.content), std::ios::binary);

            return {};
        },
        [] (const auto& path) -> std::filesystem::path { return path; },
        [] (const auto&) -> std::filesystem::file_time_type { return {}; },
        [] (const auto& f)
        {
            for (auto& file : PatchClass::files)
                if (f == file.name)
                    return true;

            return false;
        }
    };
}

template <typename PatchClass>
Environment createGeneratedCppEnvironment()
{
    using PerformerClass = typename PatchClass::PerformerClass;

    return
    {
        Environment::EngineType::AOT,
        [] { return cmaj::createEngineForGeneratedCppProgram<PerformerClass>(); },
        createVirtualFileSystem<PatchClass>(),
    };
}

inline cmaj::PatchManifest::View findDefaultViewForPatch (const cmaj::Patch& patch)
{
    if (auto manifest = patch.getManifest())
        if (auto* maybeView = manifest->findDefaultView())
            return *maybeView;

    return {};
}


} // namespace cmaj::plugin
