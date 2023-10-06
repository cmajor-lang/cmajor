#pragma once

#include "cmajor/API/cmaj_Engine.h"
#include "cmajor/helpers/cmaj_GeneratedCppEngine.h"

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
                    return std::make_unique<std::istringstream> (std::string (file.content));

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
        []
        {
            return cmaj::createEngineForGeneratedCppProgram<PerformerClass>();
        },
        createVirtualFileSystem<PatchClass>(),
    };
}

} // namespace cmaj::plugin
