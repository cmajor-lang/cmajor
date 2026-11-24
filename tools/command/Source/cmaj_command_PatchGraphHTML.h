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

#include "choc/text/choc_JSON.h"
#include "choc/text/choc_StringUtilities.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedAssets.h"

/// Writes the self-contained interactive page that `cmaj generate --target=graph` produces
/// when its output file ends in .html
namespace cmaj::patch_graph_html
{
    /// The embedded panel_api/cmaj-patch-graph modules that the page inlines
    inline constexpr const char* moduleNames[] = { "layered-layout.js", "layout.js", "render-svg.js",
                                                   "activity.js", "patch-graph-view.js" };

    inline constexpr const char* moduleFolder = "panel_api/cmaj-patch-graph/";
    inline constexpr const char* templateName = "embedded_patch_graph_template.html";

    //==============================================================================
    /// Replaces each placeholder in a template with its value, in a single pass, so that
    /// nothing in an inserted value can be mistaken for another placeholder
    [[nodiscard]] inline std::string fillInPlaceholders (std::string_view text,
                                                         std::initializer_list<std::pair<std::string_view, std::string>> values)
    {
        std::string result;
        size_t position = 0;

        for (;;)
        {
            auto next = std::string_view::npos;
            const std::pair<std::string_view, std::string>* found = nullptr;

            for (auto& value : values)
            {
                if (auto index = text.find (value.first, position); index < next)
                {
                    next = index;
                    found = std::addressof (value);
                }
            }

            if (found == nullptr)
                break;

            result += text.substr (position, next - position);
            result += found->second;
            position = next + found->first.length();
        }

        result += text.substr (position);
        return result;
    }

    /// Builds a self-contained HTML page showing an interactive view of a patch graph, from the
    /// JSON produced by the "graph" code generation target
    [[nodiscard]] inline std::string createPatchGraphHTML (const std::string& graphJSON, const std::string& title)
    {
        auto& assets = cmaj::EmbeddedAssets::getInstance();
        auto modules = choc::value::createObject ({});

        for (auto name : moduleNames)
            modules.addMember (name, assets.getContent (std::string (moduleFolder) + name));

        // stops any "</script>" inside the inlined text from ending the script element
        auto escapeForScript = [] (const std::string& s) { return choc::text::replace (s, "</", "<\\/"); };

        return fillInPlaceholders (assets.getContent (templateName),
                                   {{ "PATCH_GRAPH_TITLE",   choc::text::replace (title, "&", "&amp;", "<", "&lt;", ">", "&gt;") },
                                    { "PATCH_GRAPH_MODULES", escapeForScript (choc::json::toString (modules, false)) },
                                    { "PATCH_GRAPH_DATA",    escapeForScript (graphJSON) }});
    }

} // namespace cmaj::patch_graph_html
