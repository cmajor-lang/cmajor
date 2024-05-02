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
#include <array>
#include <string_view>
#include <iostream>
#include "choc/text/choc_Files.h"
#include "cmajor/helpers/cmaj_EmbeddedWebAssets.h"
#include "cmajor/COM/cmaj_Library.h"

namespace cmaj
{
    struct EmbeddedAssets
    {
        EmbeddedAssets() = default;

        static EmbeddedAssets& getInstance()
        {
            static EmbeddedAssets i;
            return i;
        }

        /// Globally overrides the embedded assets with a local file path
        void setLocalAssetFolder (std::filesystem::path localFolder)
        {
            // This needs to be pointed at the root of the cmajor repo
            CMAJ_ASSERT (exists (localFolder / "include"));
            localAssetFolder = std::move (localFolder);
            std::cout << "Using local asset folder: " << localAssetFolder << std::endl;
        }

        /// Returns an asset's content by name, or an empty string if not found
        std::string findContent (std::string_view path)
        {
            if (localAssetFolder != std::filesystem::path())
            {
                if (auto local = localAssetFolder / "modules/embedded_assets/files" / path; exists (local))
                {
                    try
                    {
                        return choc::file::loadFileAsString (local.string());
                    }
                    catch (...) {}
                }

                if (choc::text::startsWith (path, "cmaj_api/"))
                    if (auto local = localAssetFolder / "javascript" / path; exists (local))
                        return choc::file::loadFileAsString (local.string());
            }

            if (choc::text::startsWith (path, "cmaj_api/"))
            {
                auto subPath = path.substr (std::string_view("cmaj_api/").length());

                if (subPath == "cmaj-version.js")
                    return getVersionModule();

                return std::string (cmaj::EmbeddedWebAssets::findResource (subPath));
            }

            for (auto& file : Files::files)
                if (path == file.name)
                    return std::string (file.content);

            return {};
        }

        /// This is the same as findContent() but will assert if the asset isn't found
        std::string getContent (std::string_view path)
        {
            auto content = findContent (path);
            CMAJ_ASSERT (! content.empty());
            return content;
        }

        static std::string getVersionModule()
        {
            return std::string ("export function getCmajorVersion() { return \"")
                      + cmaj::Library::getVersion() + "\"; }";
        }

        std::filesystem::path localAssetFolder;

    private:
        #include "cmaj_EmbeddedAssets_data.h"
    };
}
