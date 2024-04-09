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

#include "cmaj_JUCEPlugin.h"


namespace cmaj::plugin
{

//==============================================================================
///
/// This implementation of juce::AudioPluginFormat can be used to search for
/// and instantiate Cmajor patches as juce::AudioPluginInstance objects.
///
class JUCEPluginFormat   : public juce::AudioPluginFormat
{
public:
    using PluginInstance = JUCEPluginBase<JUCEPluginType_SinglePatchJIT>;

    JUCEPluginFormat (CacheDatabaseInterface::Ptr compileCache,
                      std::function<void(PluginInstance&)> patchChangeCallbackFn,
                      std::string hostDescriptionToUse)
       : cache (std::move (compileCache)),
         patchChangeCallback (std::move (patchChangeCallbackFn)),
         hostDescription (std::move (hostDescriptionToUse))
    {
    }

    static constexpr auto pluginFormatName    = "Cmajor";
    static constexpr auto patchFileExtension  = ".cmajorpatch";
    static constexpr auto patchFileWildcard   = "*.cmajorpatch";

    //==============================================================================
    juce::String getName() const override           { return pluginFormatName; }

    static bool fillInDescription (juce::PluginDescription& desc, const juce::String& fileOrIdentifier)
    {
        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (fileOrIdentifier));

            desc.name                = manifest.name;
            desc.descriptiveName     = manifest.description;
            desc.category            = manifest.category;
            desc.manufacturerName    = manifest.manufacturer;
            desc.version             = manifest.version;
            desc.fileOrIdentifier    = PluginInstance::createPatchID (manifest);
            desc.lastFileModTime     = juce::File (fileOrIdentifier).getLastModificationTime();
            desc.isInstrument        = manifest.isInstrument;
            desc.uniqueId            = static_cast<int> (std::hash<std::string>{} (manifest.ID));
            desc.pluginFormatName    = pluginFormatName;
            desc.lastInfoUpdateTime  = juce::Time::getCurrentTime();
            desc.deprecatedUid       = desc.uniqueId;

            return true;
        }
        catch (...) {}

        return false;
    }

    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& results,
                              const juce::String& fileOrIdentifier) override
    {
        auto d = std::make_unique<juce::PluginDescription>();

        if (fillInDescription (*d, fileOrIdentifier))
            results.add (std::move (d));
    }

    bool fileMightContainThisPluginType (const juce::String& fileOrIdentifier) override
    {
        return PluginInstance::isCmajorIdentifier (fileOrIdentifier)
                || juce::File::createFileWithoutCheckingPath (fileOrIdentifier).hasFileExtension (patchFileExtension);
    }

    juce::String getNameOfPluginFromIdentifier (const juce::String& fileOrIdentifier) override
    {
        if (auto name = PluginInstance::getNameFromPluginID (fileOrIdentifier); ! name.empty())
            return name;

        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (fileOrIdentifier));
            return manifest.name;
        }
        catch (...) {}

        return juce::File::createFileWithoutCheckingPath (fileOrIdentifier).getFileNameWithoutExtension();
    }

    bool pluginNeedsRescanning (const juce::PluginDescription& desc) override
    {
        juce::PluginDescription d;

        if (fillInDescription (d, desc.fileOrIdentifier))
            return d.lastFileModTime != desc.lastFileModTime;

        return false;
    }

    bool doesPluginStillExist (const juce::PluginDescription& desc) override
    {
        return fileMightContainThisPluginType (desc.fileOrIdentifier)
                && juce::File::createFileWithoutCheckingPath (desc.fileOrIdentifier).existsAsFile();
    }

    bool canScanForPlugins() const override             { return true; }
    bool isTrivialToScan() const override               { return true; }

    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath& directoriesToSearch, bool recursive, bool) override
    {
        juce::StringArray results;

        for (int j = 0; j < directoriesToSearch.getNumPaths(); ++j)
            searchForPatches (results, directoriesToSearch[j], recursive);

        return results;
    }

    static void searchForPatches (juce::StringArray& results, const juce::File& dir, bool recursive)
    {
        for (const auto& i : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto f = i.getFile();

            if (f.isDirectory())
            {
                if (recursive)
                    searchForPatches (results, f, true);
            }
            else if (f.hasFileExtension (patchFileExtension))
            {
                results.add (f.getFullPathName());
            }
        }
    }

    juce::FileSearchPath getDefaultLocationsToSearch() override
    {
        juce::FileSearchPath path;

       #if JUCE_WINDOWS
        path.add (juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                    .getChildFile ("Common Files\\Cmajor"));
       #elif JUCE_MAC || JUCE_IOS
        path.add (juce::File ("/Library/Audio/Plug-Ins/Cmajor"));
        path.add (juce::File ("~/Library/Audio/Plug-Ins/Cmajor"));
       #endif

        return path;
    }

    void createPluginInstance (const juce::PluginDescription& desc, double initialSampleRate,
                               int initialBufferSize, PluginCreationCallback callback) override
    {
        try
        {
            PatchManifest manifest;
            manifest.initialiseWithFile (PluginInstance::getFileFromPluginID (desc.fileOrIdentifier));

            auto patch = std::make_unique<Patch>();

            patch->setHostDescription (hostDescription);
            patch->setAutoRebuildOnFileChange (true);
            patch->createEngine = +[] { return cmaj::Engine::create(); };

           #if CMAJ_USE_QUICKJS_WORKER
            enableQuickJSPatchWorker (*patch);
           #else
            enableWebViewPatchWorker (*patch);
           #endif

            patch->cache = cache;
            patch->preload (manifest);

            auto plugin = std::make_unique<PluginInstance> (std::move (patch));
            plugin->patchChangeCallback = patchChangeCallback;
            plugin->setRateAndBufferSizeDetails (initialSampleRate, initialBufferSize);
            plugin->patch->setPlaybackParams (cmaj::Patch::PlaybackParams (initialSampleRate,
                                                                           static_cast<uint32_t> (initialBufferSize),
                                                                           2, 2));

            plugin->loadPatch (manifest);

            if (! plugin->isStatusMessageError)
                callback (std::move (plugin), {});
            else
                callback ({}, plugin->statusMessage);
        }
        catch (...)
        {
            callback ({}, "Failed to load manifest");
            return;
        }
    }

    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override   { return false; }

    CacheDatabaseInterface::Ptr cache;
    std::function<void(PluginInstance&)> patchChangeCallback;
    std::string hostDescription;
};


} // namespace cmaj::plugin
