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

#include <algorithm>
#include "choc/text/choc_TextTable.h"
#include "choc/text/choc_CodePrinter.h"
#include "choc/text/choc_Wildcard.h"

//==============================================================================
struct GeneratedFiles
{
    void addFile (std::string name, std::string content)
    {
        files.push_back ({ std::move (name), std::move (content) });
    }

    static std::string trimLeadingSlash (std::string path)
    {
        if (choc::text::startsWith (path, "./"))
            return path.substr (2);

        if (choc::text::startsWith (path, "/"))
            return path.substr (1);

        return path;
    }

    void readAndAddFile (std::filesystem::path file, std::filesystem::path relativeTo)
    {
        addFile (relative (file, relativeTo).generic_string(),
                 choc::file::loadFileAsString (file.string()));
    }

    void findAndAddFiles (std::filesystem::path folder, std::filesystem::path relativeTo)
    {
        for (auto& file : std::filesystem::recursive_directory_iterator (folder))
            if (! is_directory (file) && file.path().filename() != ".DS_Store")
                readAndAddFile (file, relativeTo);
    }

    void findAndAddFiles (std::filesystem::path folder, std::filesystem::path relativeTo, choc::text::WildcardPattern wildcard)
    {
        for (auto& wc : wildcard)
            wc = trimLeadingSlash (wc);

        for (auto& file : std::filesystem::recursive_directory_iterator (folder))
            if (! is_directory (file) && file.path().filename() != ".DS_Store"
                 && wildcard.matches (relative (file.path(), relativeTo).generic_string()))
                readAndAddFile (file, relativeTo);
    }

    void sort()
    {
        std::sort (files.begin() + 1, files.end());
        auto last = std::unique (files.begin(), files.end());
        files.erase (last, files.end());
    }

    void writeToOutputFolder (const std::string& outputFile)
    {
        if (outputFile.empty())
            throw std::runtime_error ("Expected an argument --output=<target folder>");

        std::filesystem::create_directories (outputFile);

        for (auto& f : files)
            f.write (outputFile);
    }

    void addPatchResources (const cmaj::PatchManifest& manifest)
    {
        for (auto& f : cmaj::EmbeddedWebAssets::files)
            addFile ("cmaj_api/" + std::string (f.name), std::string (f.content));

        auto manifestFilePath = std::filesystem::path (manifest.manifestFile);
        auto manifestFilename = manifestFilePath.filename();
        auto fullPathToManifest = std::filesystem::path (manifest.getFullPathForFile (manifestFilename.string()));
        auto manifestFolder = fullPathToManifest.parent_path();

        for (auto& view : manifest.views)
        {
            auto viewFile = std::filesystem::path (manifest.getFullPathForFile (view.getSource()));
            auto viewFolder = viewFile.parent_path();

            if (viewFolder != manifestFolder)
                findAndAddFiles (viewFolder, manifestFolder);
            else
                readAndAddFile (viewFile, manifestFolder);
        }

        for (auto& resource : manifest.resources)
            findAndAddFiles (manifestFolder, manifestFolder, choc::text::WildcardPattern (resource, false));

        if (! manifest.patchWorker.empty())
            readAndAddFile (manifestFolder / trimLeadingSlash (manifest.patchWorker), manifestFolder);
    }

    void addWamResources()
    {
        for (auto& f : cmaj::EmbeddedWamAssets::files)
            addFile (std::string (f.name), std::string (f.content));
    }

    //==============================================================================
    struct File
    {
        std::string filename, content;

        void write (std::filesystem::path outputFile)
        {
            if (! filename.empty())
                outputFile = outputFile / filename;

            choc::file::replaceFileWithContent (outputFile, content);
        }

        bool operator== (const File& other) const
        {
            return getNameForSorting() == other.getNameForSorting();
        }

        bool operator< (const File& other) const
        {
            return getNameForSorting() < other.getNameForSorting();
        }

        std::string getNameForSorting() const
        {
            return choc::text::contains (filename, "/") ? filename : ("_" + filename);
        }
    };

    std::vector<File> files;
};

//==============================================================================
static inline cmaj::Engine::CodeGenOutput generateCodeAndCheckResult (cmaj::Patch& patch,
                                                                      const cmaj::Patch::LoadParams& loadParams,
                                                                      const std::string& targetType,
                                                                      const std::string& extraOptions)
{
    choc::messageloop::initialise();

    cmaj::Engine::CodeGenOutput result;

    auto t = std::thread ([&]
    {
        result = patch.generateCode (loadParams, targetType, extraOptions);
        choc::messageloop::stop();
    });

    choc::messageloop::run();
    t.join();

    if (result.messages.hasErrors())
        throw std::runtime_error (result.messages.toString());

    if (result.messages.hasWarnings())
        std::cout << result.messages.toString() << std::endl;

    return result;
}

static inline void writeToFolderOrConsole (const std::string& outputFile, const std::string& content)
{
    if (outputFile.empty())
        std::cout << content;
    else
        choc::file::replaceFileWithContent (outputFile, content);
}
