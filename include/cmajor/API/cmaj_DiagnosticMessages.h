//
//     ,ad888ba,                              88
//    d8"'    "8b
//   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
//   Y8,           88    88    88  88     88  88
//    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2022 Sound Stacks Ltd
//     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
//                                           ,88
//                                        888P"
//
//  Cmajor may be used under the terms of the ISC license:
//
//  Permission to use, copy, modify, and/or distribute this software for any purpose with or
//  without fee is hereby granted, provided that the above copyright notice and this permission
//  notice appear in all copies. THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//  WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
//  AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
//  CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
//  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
//  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#pragma once

#include "cmaj_SourceFiles.h"
#include "../../choc/text/choc_JSON.h"


namespace cmaj
{

//==============================================================================
/// A descriptive representation of a code location, suitable for generating error
/// messages and other logging.
struct FullCodeLocation
{
    std::string filename, sourceLine;
    choc::text::LineAndColumn lineAndColumn;

    /// Returns "[filename]:[line]:[col]: " in a format suitable for an error message
    std::string getLocationDescription() const;

    /// Returns the source line followed by another line with a '^' pointing to the column
    std::string getAnnotatedSourceLine() const;

    /// Returns a description in standard error message format including a severity and description
    std::string getFullDescription (const std::string& severity, const std::string& description) const;

    /// Returns a full report including severity, description and annotated source line
    std::string getAnnotatedDescription (const std::string& severity, const std::string& description) const;

    static FullCodeLocation from (const SourceFile&, CodeLocation);
    static FullCodeLocation from (const SourceFileList&, CodeLocation);
};

//==============================================================================
/// An error or warning message.
struct DiagnosticMessage  final
{
    enum class Type
    {
        error,
        warning,
        note,
        internalCompilerError
    };

    enum class Category
    {
        none,
        compile,
        runtime
    };

    bool isNote() const                     { return type == Type::note; }
    bool isWarning() const                  { return type == Type::warning; }
    bool isError() const                    { return type == Type::error || isInternalCompilerError(); }
    bool isInternalCompilerError() const    { return type == Type::internalCompilerError; }

    std::string getSeverity() const             { return isWarning() ? "warning" : (isNote() ? "note" : "error"); }
    std::string getCategory() const             { return category == Category::compile ? "compile" : (category == Category::runtime ? "runtime" : "none"); }
    std::string getFullDescription() const      { return location.getFullDescription (getSeverity(), description); }
    std::string getAnnotatedSourceLine() const  { return location.getAnnotatedSourceLine(); }

    DiagnosticMessage withLocation (FullCodeLocation l) const   { return create (description, std::move (l), type, category); }

    template <typename ObjectOrContext>
    DiagnosticMessage withContext (const ObjectOrContext&) const;

    void print (std::ostream&) const;

    choc::value::Value toJSON() const;
    bool loadFromJSON (const choc::value::ValueView&);

    static DiagnosticMessage create (std::string desc, FullCodeLocation l, Type t, Category c)
    {
        return DiagnosticMessage { std::move (l), std::move (desc), t, c };
    }

    static DiagnosticMessage createError (std::string desc, FullCodeLocation l)
    {
        return create (std::move (desc), std::move (l), Type::error, Category::compile);
    }

    FullCodeLocation location;
    std::string description;
    Type type = Type::error;
    Category category = Category::none;
};

//==============================================================================
/// Manages a list of errors or warnings
struct DiagnosticMessageList
{
    DiagnosticMessageList() = default;
    ~DiagnosticMessageList() = default;

    //==============================================================================
    void add (const DiagnosticMessage&);
    void add (const DiagnosticMessageList&);
    bool addFromJSON (const choc::value::ValueView&);
    void prepend (const DiagnosticMessage&);

    template <typename ObjectOrContext>
    void add (const ObjectOrContext& context, const DiagnosticMessage& message)
    {
        add (message.withContext (context));
    }

    bool empty() const                       { return messages.empty(); }
    size_t size() const                      { return messages.size(); }

    size_t countMessageType (DiagnosticMessage::Type) const;

    bool hasErrors() const                   { return countMessageType (DiagnosticMessage::Type::error) != 0 || hasInternalCompilerErrors(); }
    bool hasWarnings() const                 { return countMessageType (DiagnosticMessage::Type::warning) != 0; }
    bool hasNotes() const                    { return countMessageType (DiagnosticMessage::Type::note) != 0; }
    bool hasInternalCompilerErrors() const   { return countMessageType (DiagnosticMessage::Type::internalCompilerError) != 0; }

    void clear();

    /// Returns a list of all the messages in a printable format
    std::string toString() const;

    /// Returns a version of the list in JSON format
    choc::value::Value toJSON() const;

    std::string toJSONString (bool multiLine) const;

    /// Returns true if none of the added messages were errors
    bool addFromJSONString (std::string_view);

    // Creates a DiagnosticMessageList from a JSON string
    static DiagnosticMessageList fromJSONString (std::string_view);

    std::vector<DiagnosticMessage> messages;
};



//==============================================================================
//        _        _           _  _
//     __| |  ___ | |_   __ _ (_)| | ___
//    / _` | / _ \| __| / _` || || |/ __|
//   | (_| ||  __/| |_ | (_| || || |\__ \ _  _  _
//    \__,_| \___| \__| \__,_||_||_||___/(_)(_)(_)
//
//   Code beyond this point is implementation detail...
//
//==============================================================================

inline void DiagnosticMessage::print (std::ostream& out) const
{
    out << getFullDescription() << std::endl
        << getAnnotatedSourceLine() << std::endl;
}

inline choc::value::Value DiagnosticMessage::toJSON() const
{
    return choc::json::create ("severity",                 getSeverity(),
                               "message",                  description,
                               "fileName",                 location.filename,
                               "lineNumber",               static_cast<int32_t> (location.lineAndColumn.line),
                               "columnNumber",             static_cast<int32_t> (location.lineAndColumn.column),
                               "sourceLine",               location.sourceLine,
                               "annotatedLine",            getAnnotatedSourceLine(),
                               "fullDescription",          getFullDescription(),
                               "category",                 getCategory());
}

inline bool DiagnosticMessage::loadFromJSON (const choc::value::ValueView& v)
{
    if (! (v.isObject() && v.hasObjectMember ("message")))
        return false;

    description = v["message"].toString();
    location.filename = v["fileName"].toString();
    location.sourceLine = v["sourceLine"].toString();

    location.lineAndColumn.line = static_cast<size_t> (v["lineNumber"].getWithDefault<int64_t> (0));
    location.lineAndColumn.column = static_cast<size_t> (v["columnNumber"].getWithDefault<int64_t> (0));

    auto sev = v["severity"].toString();
    type = sev == "warning" ? Type::warning : (sev == "note" ? Type::note : Type::error);

    auto cat = v["category"].toString();
    category = cat == "compile" ? Category::compile : (cat == "runtime" ? Category::runtime : Category::none);

    return true;
}

//==============================================================================
inline std::string FullCodeLocation::getLocationDescription() const
{
    std::string position;

    if (lineAndColumn.isValid())
        return (filename.empty() ? lineAndColumn.toString()
                                 : (filename + ':' + lineAndColumn.toString())) + ": ";

    if (! filename.empty())
        return filename + ": ";

    return {};
}

inline std::string FullCodeLocation::getAnnotatedSourceLine() const
{
    choc::text::UTF8Pointer sourceUTF8 (sourceLine.c_str());

    if (sourceLine.empty() || sourceUTF8.length() < lineAndColumn.column)
        return {};

    auto result = choc::text::trimEnd (sourceLine) + "\n";

    // To make things line up correctly, we need to use tab characters wherever
    // they occur in the source line
    for (size_t i = 0; i < lineAndColumn.column - 1; ++i)
        result += sourceUTF8.popFirstChar() == '\t' ? '\t' : ' ';

    return result + "^";
}

inline std::string FullCodeLocation::getFullDescription (const std::string& severity, const std::string& description) const
{
    auto message = getLocationDescription();

    if (! severity.empty())
        message += severity + ": ";

    return message + description;
}

inline std::string FullCodeLocation::getAnnotatedDescription (const std::string& severity, const std::string& description) const
{
    auto message = getFullDescription (severity, description);
    auto annotatedLine = getAnnotatedSourceLine();

    if (! annotatedLine.empty())
        message += "\n" + annotatedLine;

    return message;
}

inline FullCodeLocation FullCodeLocation::from (const SourceFile& sourceFile, CodeLocation l)
{
    return { sourceFile.filename, sourceFile.getSourceLine (l), sourceFile.getLineAndColumn (l) };
}

inline FullCodeLocation FullCodeLocation::from (const SourceFileList& sourceFileList, CodeLocation l)
{
    if (auto s = sourceFileList.findSourceFileContaining (l))
        return from (*s, l);

    return {};
}

//==============================================================================
inline size_t DiagnosticMessageList::countMessageType (DiagnosticMessage::Type type) const
{
    size_t count = 0;

    for (auto& m : messages)
        if (m.type == type)
            ++count;

    return count;
}

inline void DiagnosticMessageList::clear()                                  { messages.clear(); }
inline void DiagnosticMessageList::add (const DiagnosticMessage& m)         { messages.push_back (m); }
inline void DiagnosticMessageList::prepend (const DiagnosticMessage& m)     { messages.insert (messages.begin(), m); }

inline void DiagnosticMessageList::add (const DiagnosticMessageList& other)
{
    for (auto& m : other.messages)
        add (m);
}

inline bool DiagnosticMessageList::addFromJSON (const choc::value::ValueView& json)
{
    DiagnosticMessage m;

    if (m.loadFromJSON (json))
    {
        messages.push_back (std::move (m));
        return true;
    }

    return false;
}

inline std::string DiagnosticMessageList::toString() const
{
    std::ostringstream output;

    for (auto& m : messages)
        output << m.getFullDescription() << std::endl
               << m.getAnnotatedSourceLine() << std::endl;

    return output.str();
}

inline choc::value::Value DiagnosticMessageList::toJSON() const
{
    if (messages.empty())
        return {};

    if (messages.size() == 1)
        return messages.front().toJSON();

    auto array = choc::value::createEmptyArray();

    for (auto& m : messages)
        array.addArrayElement (m.toJSON());

    return array;
}

inline std::string DiagnosticMessageList::toJSONString (bool multiLine) const
{
    return choc::json::toString (toJSON(), multiLine);
}

inline bool DiagnosticMessageList::addFromJSONString (std::string_view json)
{
    auto v = choc::json::parse (json);
    bool noErrors = true;

    if (v.isArray())
    {
        for (uint32_t i = 0; i < v.size(); ++i)
        {
            addFromJSON (v[i]);
            noErrors = noErrors && ! messages.back().isError();
        }
    }
    else if (v.isObject())
    {
        addFromJSON (v);
        noErrors = noErrors && ! messages.back().isError();
    }

    return noErrors;
}

inline DiagnosticMessageList DiagnosticMessageList::fromJSONString (std::string_view json)
{
    DiagnosticMessageList list;
    list.addFromJSONString (json);
    return list;
}


} // namespace cmaj
