//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     (C)2024 Cmajor Software Ltd
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     https://cmajor.dev
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88
//                                           ,88
//                                        888P"
//
//  This code may be used under either a GPLv3 or commercial
//  license: see LICENSE.md for more details.

#include <JuceHeader.h>
#include <assert.h>

#define CHOC_ASSERT(x) assert(x)
#include "../../../include/cmajor/helpers/cmaj_JUCEPluginFormat.h"
#include "../../../include/choc/javascript/choc_javascript_QuickJS.h"


// Looks for a .cmajorpatch in the same folder as the plugin, with the same name (up to the extension)
static std::optional<std::filesystem::path> findSiblingPatch (std::filesystem::path pluginFile)
{
    auto f = pluginFile;
    f.replace_extension (".cmajorpatch");

    if (exists (f))
        return f;

    return {};
}

// Looks for a sibling folder, in the same folder as the plugin, and with the same name as the
// plugin (but without an extension). If found, checks whether this folder contains a .cmajorpatch file.
static std::optional<std::filesystem::path> findSiblingPatchFolder (std::filesystem::path pluginFile)
{
    auto folder = pluginFile;
    folder.replace_extension ({});

    if (is_directory (folder))
    {
        std::error_code errorCode = {};

        for (auto& file : std::filesystem::directory_iterator (folder, std::filesystem::directory_options::skip_permission_denied, errorCode))
            if (file.path().extension() == ".cmajorpatch")
                return file;
    }

    return {};
}

// Looks for a .json file next to the plugin, with the same name as the plugin (apart from the
// extension). If found, tries to open it as JSON and read a `location` property - this
// is taken to be a file path leading to a .cmajorpatch file to load.
static std::optional<std::filesystem::path> findSiblingJSONFile (std::filesystem::path pluginFile)
{
    auto f = pluginFile;
    f.replace_extension (".json");

    if (exists (f))
    {
        try
        {
            auto json = choc::json::parse (choc::file::loadFileAsString (f.string()));

            if (! json.isObject())
                throw std::runtime_error ("Expected a JSON object");

            auto manifest = std::filesystem::path (json["location"].toString());

            if (manifest.extension() != ".cmajorpatch")
                throw std::runtime_error ("Expected the path of a .cmajorpatch file");

            if (manifest.is_relative())
                manifest = pluginFile.parent_path() / manifest;

            if (! exists (manifest))
                throw std::runtime_error ("No such file: " + manifest.string());

            return manifest;
        }
        catch (const std::exception& e)
        {
            std::cerr << "Error parsing " << f << ": " << e.what() << std::endl;
        }
    }

    return {};
}

static std::optional<std::filesystem::path> findAssociatedPatch (std::filesystem::path pluginFile)
{
    try
    {
        if (auto p = findSiblingJSONFile (pluginFile))
            return p;

        if (auto p = findSiblingPatch (pluginFile))
            return p;

        if (auto p = findSiblingPatchFolder (pluginFile))
            return p;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    return {};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    auto patch = std::make_shared<cmaj::Patch>();
    patch->setAutoRebuildOnFileChange (true);
    patch->createEngine = +[] { return cmaj::Engine::create(); };

    if (auto manifest = findAssociatedPatch (juce::File::getSpecialLocation (juce::File::currentApplicationFile)
                                               .getFullPathName().toStdString()))
    {
       #if CMAJ_USE_QUICKJS_WORKER
        enableQuickJSPatchWorker (*patch);
       #else
        enableWebViewPatchWorker (*patch);
       #endif

        return new cmaj::plugin::SinglePatchJITPlugin (std::move (patch), *manifest);
    }

    return new cmaj::plugin::JITLoaderPlugin (std::move (patch));
}
