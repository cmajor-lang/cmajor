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

#include "choc/containers/choc_SmallVector.h"

namespace cmaj
{

//==============================================================================
struct IdentifierPath
{
    IdentifierPath() = default;

    explicit IdentifierPath (std::string path) : fullPath (std::move (path))
    {
        auto length = fullPath.length();
        size_t currentSectionStart = 0;

        while (currentSectionStart < length)
        {
            auto nextBreak = fullPath.find ("::", currentSectionStart);
            CMAJ_ASSERT (nextBreak != 0);

            if (nextBreak == std::string::npos)
            {
                sections.push_back ({ static_cast<uint32_t> (currentSectionStart), static_cast<uint32_t> (length) });
                break;
            }

            sections.push_back ({ static_cast<uint32_t> (currentSectionStart), static_cast<uint32_t> (nextBreak) });
            currentSectionStart = nextBreak + 2;
        }
    }

    explicit IdentifierPath (std::string_view path) : IdentifierPath (std::string (path)) {}

    bool empty() const                                  { return fullPath.empty(); }
    bool isUnqualified() const                          { return sections.size() == 1; }
    std::string_view getSection (size_t index) const    { return getSection (sections[index]); }
    std::string_view getLastPart() const                { return sections.empty() ? std::string_view() : getSection (sections.back()); }
    std::string_view getParentPath() const              { return sections.size() <= 1 ? std::string_view() : std::string_view (fullPath.data(), sections[sections.size() - 2].end); }
    bool operator== (std::string_view path) const       { return fullPath == path; }
    bool operator!= (std::string_view path) const       { return fullPath != path; }
    bool operator== (const IdentifierPath& path) const  { return fullPath == path.fullPath; }
    bool operator!= (const IdentifierPath& path) const  { return fullPath != path.fullPath; }
    operator const std::string&() const                 { return fullPath; }

    IdentifierPath getChildPath (std::string_view child) const    { return IdentifierPath (join (fullPath, child)); }

    IdentifierPath operator+ (const IdentifierPath& child) const
    {
        return IdentifierPath (join (fullPath, child.fullPath));
    }

    IdentifierPath withoutTopLevelName() const
    {
        if (sections.empty())
            return *this;

        if (sections.size() == 1)
            return {};

        return IdentifierPath (fullPath.substr (sections[1].start));
    }

    IdentifierPath withoutTopLevelNameIfPresent (std::string_view nameToRemove) const
    {
        if (empty() || ! choc::text::startsWith (fullPath, nameToRemove))
            return *this;

        auto remainder = fullPath.substr (nameToRemove.length());

        if (choc::text::startsWith (remainder, "::"))   return IdentifierPath (remainder.substr (2));
        if (choc::text::endsWith (nameToRemove, "::"))  return IdentifierPath (remainder);

        return *this;
    }

    choc::SmallVector<std::string_view, 8> getSectionNames() const
    {
        choc::SmallVector<std::string_view, 8> names;
        names.reserve (sections.size());

        for (auto s : sections)
            names.push_back (getSection (s));

        return names;
    }

    static std::string join (std::string_view parent, std::string_view child)
    {
        return std::string (parent) + "::" + std::string (child);
    }

    std::string fullPath;

    struct Section { uint32_t start, end; };
    choc::SmallVector<Section, 8> sections;

    std::string_view getSection (Section s) const        { return std::string_view (fullPath.data() + s.start, s.end - s.start); }
};


} // namespace cmaj
