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

#include <filesystem>
#include <iostream>

#include "choc/memory/choc_ObjectPointer.h"
#include "choc/text/choc_Files.h"
#include "choc/text/choc_Wildcard.h"

#include "cmaj_javascript_ObjectHandle.h"
#include "cmaj_javascript_Helpers.h"

namespace cmaj::javascript
{

//==============================================================================
struct FileSystem  : public std::enable_shared_from_this<FileSystem>
{
    FileSystem() = default;
    virtual ~FileSystem() = default;

    virtual bool fileExists (const std::filesystem::path&) = 0;
    virtual bool isFolder (const std::filesystem::path&) = 0;
    virtual std::string readFile (const std::filesystem::path&) = 0;
    virtual void overwriteFile (const std::filesystem::path&, std::string_view newContent) = 0;
    virtual int64_t getModificationTime (const std::filesystem::path&) = 0;
    virtual std::vector<std::filesystem::path> findChildren (const std::filesystem::path&, bool findFolders, bool recursive, std::string_view wildcard) = 0;

    //==============================================================================
    void bind (choc::javascript::Context& context)
    {
        auto fileSystem = shared_from_this();

        context.registerFunction ("_fileExists", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            return choc::value::createBool (fileSystem->fileExists (getFile (args)));
        });

        context.registerFunction ("_fileIsFolder", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            return choc::value::createBool (fileSystem->isFolder (getFile (args)));
        });

        context.registerFunction ("_fileRead", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            try
            {
                auto data = fileSystem->readFile (getFile (args));

                if (data.find ('\0') != std::string::npos
                     || choc::text::findInvalidUTF8Data (data.data(), data.size()) != nullptr)
                    return choc::value::createArray (static_cast<uint32_t> (data.size()),
                                                     [&] (uint32_t i) { return static_cast<int32_t> (data[i]); });

                return choc::value::createString (data);
            }
            catch (const std::exception& e)
            {
                return createErrorObject (e.what());
            }

            return {};
        });

        context.registerFunction ("_fileReadAudioData", [] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            choc::value::Value result;
            choc::value::ValueView annotation;

            if (auto a = args[1])
                annotation = *a;

            auto file = getFile (args);

            auto error = cmaj::readAudioFileAsValue (result,
                                                     audio_utils::getAudioFormatListForReading(),
                                                     std::make_shared<std::ifstream> (file, std::ios::binary | std::ios::in),
                                                     annotation, 16, 48000 * 100);

            if (! error.empty())
                result = createErrorObject ("Failed to read audio file '" + file.string() + "': " + error);

            return result;
        });

        context.registerFunction ("_fileOverwrite", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            try
            {
                fileSystem->overwriteFile (getFile (args), args.get<std::string> (1));
            }
            catch (const std::exception& e)
            {
                return createErrorObject (e.what());
            }

            return {};
        });

        context.registerFunction ("_fileParent", [] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            return getPathValue (getFile (args).parent_path());
        });

        context.registerFunction ("_fileGetChild", [] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            auto childPath = args.get<std::string> (1);
            return getPathValue (getFile (args) / childPath);
        });

        context.registerFunction ("_fileGetSibling", [] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            auto sibling = args.get<std::string> (1);
            return getPathValue (getFile (args).parent_path() / sibling);
        });

        context.registerFunction ("_fileGetModificationTime", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            return choc::value::createInt64 (fileSystem->getModificationTime (getFile (args)));
        });

        context.registerFunction ("_fileFindChildren", [fileSystem] (choc::javascript::ArgumentList args) -> choc::value::Value
        {
            auto result = choc::value::createEmptyArray();

            bool findFolders = args.get<bool> (1, false);
            bool recurse = args.get<bool> (2, false);
            auto wildcard = args.get<std::string> (3);

            for (auto& file : fileSystem->findChildren (getFile (args), findFolders, recurse, wildcard))
                result.addArrayElement (getPathValue (file));

            return result;
        });

        context.run (getWrapperCode());
    }

private:
    //==============================================================================
    static std::filesystem::path getFile (choc::javascript::ArgumentList args)
    {
        auto path = args.get<std::string> (0);

        if (path.empty())
            return {};

        return std::filesystem::path (choc::text::replace (path, "\\", "/"));
    }

    static choc::value::Value getPathValue (const std::filesystem::path& f)
    {
        return choc::value::createString (f.string());
    }

    static std::string getWrapperCode()
    {
        return R"(
class File
{
    constructor (path)                  { this.path = path; }

    exists()                            { return _fileExists (this.path); }
    isFolder()                          { return _fileIsFolder (this.path); }
    read()                              { return _fileRead (this.path); }
    readAudioData (annotations)         { return _fileReadAudioData (this.path, annotations); }
    overwrite (newContent)              { return _fileOverwrite (this.path, newContent); }
    getModificationTime()               { return _fileGetModificationTime (this.path); }
    parent()                            { return new File (_fileParent (this.path)); }
    getChild (relativePath)             { return new File (_fileGetChild (this.path, relativePath)); }
    getSibling (relativePath)           { return new File (_fileGetSibling (this.path, relativePath)); }

    findChildren (shouldFindFolders, recursive, wildcard)
    {
        var result = [];
        const paths = _fileFindChildren (this.path, shouldFindFolders, recursive, wildcard);

        for (var i = 0; i < paths.length; ++i)
            result.push (new File (paths[i]));

        return result;
    }
}
)";
    }
};

//==============================================================================
struct StandardFileSystem : public FileSystem
{
    bool fileExists (const std::filesystem::path& file) override        { return exists (file); }
    bool isFolder (const std::filesystem::path& file) override          { return is_directory (file); }
    std::string readFile (const std::filesystem::path& file) override   { return choc::file::loadFileAsString (file.string()); }

    void overwriteFile (const std::filesystem::path& file, std::string_view newContent) override
    {
        choc::file::replaceFileWithContent (file, newContent);
    }

    int64_t getModificationTime (const std::filesystem::path& file) override
    {
        auto time = last_write_time (file).time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds> (time).count();
    }

    std::vector<std::filesystem::path> findChildren (const std::filesystem::path& file, bool findFolders, bool recurse, std::string_view wildcard) override
    {
        std::vector<std::filesystem::path> result;
        choc::text::WildcardPattern pattern (wildcard, true);

        if (recurse)
            findFiles<std::filesystem::recursive_directory_iterator> (result, file, findFolders, pattern);
        else
            findFiles<std::filesystem::directory_iterator> (result, file, findFolders, pattern);

        return result;
    }

private:
    template <typename Iterator>
    void findFiles (std::vector<std::filesystem::path>& result, const std::filesystem::path& path,
                    bool findFolders, const choc::text::WildcardPattern& wildcard)
    {
        for (auto& file : Iterator (path))
            if (findFolders == isFolder (file) && wildcard.matches (file.path().filename().string()))
                result.push_back (file);
    }
};

}
