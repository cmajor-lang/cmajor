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

#include <cassert>
#include <emscripten.h>
#include <iostream>
#include <sstream>

#undef CHOC_ASSERT
#define CHOC_ASSERT(x) assert(x)

#include "../../cmajor/include/cmajor/API/cmaj_Engine.h"
#include "../../cmajor/include/cmajor/helpers/cmaj_PatchManifest.h"

//==============================================================================
struct VirtualPatch
{
    VirtualPatch() = default;

    VirtualPatch (uint32_t maxBlockSize)
    {
        engine = cmaj::Engine::create();

        engine.setBuildSettings (engine.getBuildSettings()
                                    .setFrequency (44100)
                                    .setSessionID (1)
                                    .setMaxBlockSize (maxBlockSize));
    }

    void addFile (std::string_view path, std::string_view content)
    {
        files.push_back ({ std::string (path), std::string (content) });
    }

    bool generate (const std::string& targetType, const std::string& options)
    {
        std::string manifestFile = findManifestFile();

        if (manifestFile.empty())
            return false;

        cmaj::PatchManifest manifest;

        manifest.initialiseWithVirtualFile (manifestFile,
                                            [this] (const std::string& path) { return createFileReader (path); },
                                            [] (const std::string& path) { return path; },
                                            [] (const std::string) { return std::filesystem::file_time_type(); },
                                            [this] (const std::string& path) { return fileExists (path); });

        cmaj::Program program;

        if (! manifest.addSourceFilesToProgram (program,
                                                output.messages,
                                                [] (cmaj::DiagnosticMessageList&, const std::string&, const std::string& c) { return c; },
                                                [] {}))
            return false;

        engine.setBuildSettings (engine.getBuildSettings()
                                    .setMainProcessor (manifest.mainProcessor));

        if (! engine.load (output.messages, program, manifest.createExternalResolverFunction(), {}))
            return false;

        preventMainProcessorEndpointsBeingRemoved();

        output = engine.generateCode (targetType, options);

        return ! output.generatedCode.empty();
    }

    std::string findManifestFile()
    {
        std::string manifestFile;

        for (auto& file : files)
        {
            if (choc::text::endsWith (file.path, ".cmajorpatch"))
            {
                if (! manifestFile.empty())
                {
                    output.messages.add (cmaj::DiagnosticMessage::createError ("There must be only be one .cmajorpatch file in the bundle provided", {}));
                    return {};
                }

                manifestFile = file.path;
            }
        }

        if (manifestFile.empty())
            output.messages.add (cmaj::DiagnosticMessage::createError ("There must be a .cmajorpatch file in the bundle provided", {}));

        return manifestFile;
    }

    std::shared_ptr<std::istream> createFileReader (const std::string& path)
    {
        for (auto& f : files)
            if (f.path == path)
                return std::make_shared<std::istringstream> (f.content, std::ios::binary);

        unresolvedPathsRequested.push_back (path);
        return {};
    }

    bool fileExists (const std::string& path) const
    {
        for (auto& f : files)
            if (f.path == path)
                return true;

        return false;
    }

    const char* getMessages()
    {
        returnedStringCache = output.messages.toString();
        return returnedStringCache.c_str();
    }

    const char* getUnresolvedFiles()
    {
        std::sort (unresolvedPathsRequested.begin(), unresolvedPathsRequested.end());
        unresolvedPathsRequested.erase (std::unique (unresolvedPathsRequested.begin(), unresolvedPathsRequested.end()), unresolvedPathsRequested.end());

        auto v = choc::value::createEmptyArray();

        for (auto& p : unresolvedPathsRequested)
            v.addArrayElement (p);

        returnedStringCache = choc::json::toString (v);
        return returnedStringCache.c_str();
    }

    void preventMainProcessorEndpointsBeingRemoved()
    {
        for (const auto& endpoint : engine.getInputEndpoints())
            (void) engine.getEndpointHandle (endpoint.endpointID);

        for (const auto& endpoint : engine.getOutputEndpoints())
            (void) engine.getEndpointHandle (endpoint.endpointID);
    }

    struct File
    {
        std::string path, content;
    };

    cmaj::Engine engine;
    std::vector<File> files;
    std::vector<std::string> unresolvedPathsRequested;
    cmaj::Engine::CodeGenOutput output;
    std::string returnedStringCache;
};

//==============================================================================
static VirtualPatch patch;

extern "C" const char* EMSCRIPTEN_KEEPALIVE getCmajorVersion()
{
    return cmaj::Library::getVersion();
}

extern "C" void EMSCRIPTEN_KEEPALIVE initialise (int maxBlockSizeToUse)
{
    patch = VirtualPatch (static_cast<uint32_t> (maxBlockSizeToUse));
}

extern "C" void EMSCRIPTEN_KEEPALIVE addFile (const char* filename, const char* content, int contentSize)
{
    patch.addFile (filename, std::string_view (content, static_cast<size_t> (contentSize)));
}

extern "C" bool EMSCRIPTEN_KEEPALIVE generate()
{
    return patch.generate ("javascript", {});
}

extern "C" const char* EMSCRIPTEN_KEEPALIVE getUnresolvedResourceFiles()
{
    return patch.getUnresolvedFiles();
}

extern "C" const char* EMSCRIPTEN_KEEPALIVE getMessages()
{
    return patch.getMessages();
}

extern "C" const char* EMSCRIPTEN_KEEPALIVE getGeneratedCode()
{
    return patch.output.generatedCode.c_str();
}
