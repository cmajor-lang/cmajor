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

#include "../../choc/text/choc_UTF8.h"
#include "../../choc/containers/choc_Value.h"

#ifndef CMAJ_ASSERT
 #define CMAJ_ASSERT(x) CHOC_ASSERT(x)
 #define CMAJ_ASSERT_FALSE CMAJ_ASSERT(false)
#endif

namespace cmaj
{

struct SourceFileList;

//==============================================================================
/// A compact, cheap pointer to a code location, from which you can find a
/// FullCodeLocation via the SourceFileList class.
struct CodeLocation
{
    CodeLocation() = default;
    CodeLocation (choc::text::UTF8Pointer t) : text (t) {}

    bool operator== (CodeLocation other) const      { return text.data() == other.text.data(); }
    bool operator!= (CodeLocation other) const      { return text.data() != other.text.data(); }
    bool operator== (decltype (nullptr)) const      { return text.data() == nullptr; }
    bool operator!= (decltype (nullptr)) const      { return text.data() != nullptr; }

    bool empty() const                              { return text.empty(); }

    choc::text::UTF8Pointer text;
};

/// A pair of CodeLocation objects that define a substring within some code.
struct CodeLocationRange
{
    CodeLocation start, end;

    size_t length() const               { return start.empty() ? 0 : static_cast<size_t> (end.text.data() - start.text.data()); }
    bool empty() const                  { return length() == 0; }
    std::string_view toString() const   { return empty() ? std::string_view() : std::string_view (start.text.data(), length()); }
};

//==============================================================================
/// Holds the name and contents of a Cmajor code file.
struct SourceFile
{
    SourceFile (SourceFileList& o) : ownerList (o) {}

    /// Returns the location of the start of this file
    CodeLocation getCodeLocation() const         { return { getUTF8() }; }

    /// Returns the content of this file as UTF8
    choc::text::UTF8Pointer getUTF8() const      { return choc::text::UTF8Pointer (content.c_str()); }

    /// Returns true if this file actually contains the given CodeLocation
    bool contains (CodeLocation l) const
    {
        return l.text.data() >= content.data()
            && l.text.data() <= content.data() + content.length();
    }

    /// For a location within this file, this calculates its line and columm indexes
    choc::text::LineAndColumn getLineAndColumn (CodeLocation l) const
    {
        CMAJ_ASSERT (contains (l));
        return choc::text::findLineAndColumn (getUTF8(), l.text);
    }

    /// For a location within this file, this returns the contents of the line
    /// that it occurs within.
    std::string getSourceLine (CodeLocation l) const
    {
        CMAJ_ASSERT (contains (l));

        if (l.text != nullptr)
            if (auto start = l.text.findStartOfLine (getUTF8()))
                if (auto end = start.findEndOfLine())
                    return { start.data(), end.data() };

        return {};
    }

    SourceFileList& ownerList;
    std::string filename, content;
    bool isSystem = false;
};

//==============================================================================
/// Manages a list of SourceFile objects
struct SourceFileList
{
    /// Adds a file to the list
    SourceFile& add (std::string filename,
                     std::string content,
                     bool isSystem)
    {
        sourceFiles.push_back (std::make_unique<SourceFile> (*this));
        auto& f = *sourceFiles.back();
        f.filename = std::move (filename);
        f.content = std::move (content);
        f.isSystem = isSystem;
        return f;
    }

    /// Adds a file to the list from a JSON container
    void add (const choc::value::ValueView& json)
    {
        if (json.hasObjectMember ("name") && json.hasObjectMember ("content"))
        {
            auto name    = json["name"];
            auto content = json["content"];

            if (name.isString() && content.isString())
                add (std::string (name.getString()), std::string (content.getString()), false);
        }
    }

    /// Reloads this list from the given JSON data
    void setFromJSON (const choc::value::ValueView& json)
    {
        sourceFiles.clear();

        if (json.isObject())
        {
            add (json);
        }
        else if (json.isArray())
        {
            for (auto i : json)
                 add (i);
        }
    }

    //==============================================================================
    /// Attempts to find the file that contains this code location
    SourceFile* findSourceFileContaining (CodeLocation l) const
    {
        for (auto& s : sourceFiles)
            if (s->contains (l))
                return s.get();

        return {};
    }

    /// Finds the file that contains this code location, asserting if it isn't found
    SourceFile& getSourceFileContaining (CodeLocation l) const
    {
        auto f = findSourceFileContaining (l);
        CMAJ_ASSERT (f != nullptr);
        return *f;
    }

    /// Finds the file that contains this code location, and returns its line and column
    choc::text::LineAndColumn getLineAndColumn (CodeLocation l) const
    {
        if (auto s = findSourceFileContaining (l))
            return s->getLineAndColumn (l);

        return {};
    }

    //==============================================================================
    std::vector<std::unique_ptr<SourceFile>> sourceFiles;
};


} // namespace cmaj
