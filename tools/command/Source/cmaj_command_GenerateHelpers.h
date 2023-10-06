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

//==============================================================================
struct GeneratedFiles
{
    void addFile (std::string name, std::string content)
    {
        files.push_back ({ std::move (name), std::move (content) });
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

        if (! juce::File (outputFile).createDirectory())
            throw std::runtime_error ("Cannot create the target folder: " + outputFile);

        for (auto& f : files)
            f.write (outputFile);
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
    auto result = patch.generateCode (loadParams, targetType, extraOptions);

    if (result.messages.hasErrors())
        throw std::runtime_error (result.messages.toString());

    if (result.messages.hasWarnings())
        std::cerr << result.messages.toString();

    return result;
}

static inline void writeToFolderOrConsole (const std::string& outputFile, const std::string& content)
{
    if (outputFile.empty())
        std::cout << content;
    else
        choc::file::replaceFileWithContent (outputFile, content);
}
