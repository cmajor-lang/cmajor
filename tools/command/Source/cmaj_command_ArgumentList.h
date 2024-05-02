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
#include <vector>
#include <optional>
#include "../../../modules/compiler/include/cmaj_ErrorHandling.h"
#include "choc/platform/choc_Platform.h"
#include "choc/text/choc_StringUtilities.h"


struct ArgumentList
{
    ArgumentList (int argc, const char* const* argv)
    {
        if (argc > 0)
            executableName = argv[0];

        for (int i = 1; i < argc; ++i)
            args.emplace_back (argv[i]);

       #if CHOC_OSX
        if (auto index = indexOf ("-NSDocumentRevisionsDebugMode"); index >= 0)
        {
            removeIndex (index);
            removeIndex (index);
        }
       #endif
    }

    size_t size() const
    {
        return args.size();
    }

    bool empty() const
    {
        return args.empty();
    }

    std::string operator[] (size_t index) const
    {
        if (index < args.size())
            return args[index];

        return {};
    }

    bool contains (std::string_view arg) const
    {
        return indexOf (arg) >= 0;
    }

    bool removeIfFound (std::string_view arg)
    {
        if (auto i = indexOf (arg); i >= 0)
        {
            removeIndex (i);
            return true;
        }

        return false;
    }

    int indexOf (std::string_view arg) const
    {
        CHOC_ASSERT (! choc::text::trim (arg).empty());
        bool isDoubleDash = isDoubleDashOption (arg);

        for (size_t i = 0; i < args.size(); ++i)
            if (args[i] == arg || (isDoubleDash && getLongOptionType (args[i]) == arg))
                return static_cast<int> (i);

        return -1;
    }

    void removeIndex (int index)
    {
        if (index >= 0 && index < static_cast<int> (args.size()))
            args.erase (args.begin() + index);
    }

    void throwIfNotFound (std::string_view arg) const
    {
        if (! contains (arg))
            throw std::runtime_error ("Expected argument: '" + std::string (arg) + "'");
    }

    std::optional<std::string> getValueFor (std::string_view argToFind, bool remove)
    {
        bool isDoubleDash = isDoubleDashOption (argToFind);
        bool isSingleDash = isSingleDashOption (argToFind);

        CHOC_ASSERT (isDoubleDash || isSingleDash); // the arg you pass in needs to be a "--option" or "-option"

        if (auto i = indexOf (argToFind); i >= 0)
        {
            auto index = static_cast<size_t> (i);

            if (isSingleDash)
            {
                if (index + 1 < args.size() && ! isOption (args[index + 1]))
                {
                    auto value = args[index + 1];

                    if (remove)
                    {
                        removeIndex (i);
                        removeIndex (i);
                    }

                    return value;
                }

                if (remove)
                    removeIndex (i);

                return {};
            }

            if (isDoubleDash)
            {
                auto value = getLongOptionValue (args[index]);

                if (remove)
                    removeIndex (i);

                return value;
            }
        }

        return {};
    }

    std::optional<std::string> removeValueFor (std::string_view argToFind)
    {
        return getValueFor (argToFind, true);
    }

    std::string removeValueFor (std::string_view argToFind, std::string defaultValue)
    {
        if (auto v = getValueFor (argToFind, true))
            return *v;

        return defaultValue;
    }

    template <typename IntType>
    std::optional<IntType> removeIntValue (std::string_view argToFind)
    {
        if (auto v = removeValueFor (argToFind))
            return static_cast<IntType> (std::stoll (*v));

        return {};
    }

    template <typename IntType>
    IntType removeIntValue (std::string_view argToFind, IntType defaultValue)
    {
        if (auto v = removeValueFor (argToFind))
            return static_cast<IntType> (std::stoll (*v));

        return defaultValue;
    }

    std::filesystem::path getExistingFile (std::string_view arg, bool remove)
    {
        auto path = getValueFor (arg, remove);

        if (! path)
        {
            throwIfNotFound (arg);
            throw std::runtime_error ("Expected a filename after '" + std::string (arg) + "'");
        }

        return getAbsolutePath (*path);
    }

    std::filesystem::path removeExistingFile (std::string_view arg)
    {
        return getExistingFile (arg, true);
    }

    std::optional<std::filesystem::path> getExistingFileIfPresent (std::string_view arg, bool remove)
    {
        if (contains (arg))
            return getExistingFile (arg, remove);

        return {};
    }

    std::optional<std::filesystem::path> removeExistingFileIfPresent (std::string_view arg)
    {
        return getExistingFileIfPresent (arg, true);
    }

    std::filesystem::path getExistingFolder (std::string_view arg, bool remove)
    {
        return throwIfNotAFolder (getExistingFile (arg, remove));
    }

    std::optional<std::filesystem::path> removeExistingFolderIfPresent (std::string_view arg)
    {
        if (contains (arg))
            return getExistingFolder (arg, true);

        return {};
    }

    std::vector<std::filesystem::path> getAllAsFiles()
    {
        std::vector<std::filesystem::path> result;

        for (auto& arg : args)
            if (! isOption (arg))
                result.push_back (getAbsolutePath (arg));

        return result;
    }

    std::vector<std::filesystem::path> getAllAsExistingFiles()
    {
        auto files = getAllAsFiles();

        for (auto& f : files)
            throwIfNonexistent (f);

        return files;
    }

    static std::filesystem::path getAbsolutePath (const std::filesystem::path& path)
    {
        return path.is_absolute() ? path : std::filesystem::current_path() / path;
    }

    static std::filesystem::path throwIfNonexistent (const std::filesystem::path& path)
    {
        if (! exists (path))
            throw std::runtime_error ("File does not exist: " + path.string());

        return path;
    }

    static std::filesystem::path throwIfNotAFolder (const std::filesystem::path& path)
    {
        throwIfNonexistent (path);

        if (! is_directory (path))
            throw std::runtime_error (path.string() + " is not a folder");

        return path;
    }

    std::string executableName;
    std::vector<std::string> args;

private:
    //==============================================================================
    static bool isOption (std::string_view s)            { return isSingleDashOption (s) || isDoubleDashOption (s); }
    static bool isSingleDashOption (std::string_view s)  { return s.length() > 1 && s[0] == '-' && s[1] != '-'; }
    static bool isDoubleDashOption (std::string_view s)  { return s.length() > 2 && s[0] == '-' && s[1] == '-' && s[2] != '-'; }

    static std::string getLongOptionType (std::string_view s)
    {
        if (! isDoubleDashOption (s))
            return {};

        if (auto equals = s.find ("="); equals != std::string_view::npos)
            return std::string (choc::text::trim (s.substr (0, equals)));

        return std::string (s);
    }

    static std::string getLongOptionValue (std::string_view s)
    {
        if (! isDoubleDashOption (s))
            return {};

        if (auto equals = s.find ("="); equals != std::string_view::npos)
            return std::string (choc::text::trim (s.substr (equals + 1)));

        return std::string (s);
    }
};
