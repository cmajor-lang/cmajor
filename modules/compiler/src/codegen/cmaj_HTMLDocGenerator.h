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

#include <map>
#include "choc/text/choc_HTML.h"
#include "choc/containers/choc_Span.h"

#include "../transformations/cmaj_Transformations.h"

namespace cmaj
{

//==============================================================================
static inline std::string createParagraph (choc::span<std::string> lines, size_t leadingSpaceToTrim)
{
    if (lines.empty())
        return {};

    auto getTrimmableSpace = [] (std::string_view text) -> int
    {
        for (size_t i = 0; i < text.length(); ++i)
            if (! choc::text::isWhitespace (text[i]))
                return static_cast<int> (i);

        return -1;
    };

    int maxTrimmable = -1;

    for (auto& line : lines)
    {
        auto trimmable = getTrimmableSpace (line);

        if (trimmable > 0 && (maxTrimmable < 0 || trimmable < maxTrimmable))
            maxTrimmable = trimmable;
    }

    auto amountToTrim = std::min (maxTrimmable, static_cast<int> (leadingSpaceToTrim));

    std::string result;
    bool isFirst = true;

    for (auto& line : lines)
    {
        if (isFirst)
            isFirst = false;
        else
            result += " \n";

        if (amountToTrim > 0 && getTrimmableSpace (line) >= amountToTrim)
            result += line.substr (static_cast<size_t> (amountToTrim));
        else if (! choc::text::trimStart (line).empty())
            result += line;
    }

    return result;
}

//==============================================================================
struct MarkdownToHTML
{
    MarkdownToHTML() = default;

    void convert (choc::html::HTMLElement& parent, const std::string& markdownText)
    {
        lines = choc::text::splitIntoLines (markdownText, false);

        while (nextLine < lines.size())
            parseNextLine (parent);
    }

private:
    std::vector<std::string> lines;
    size_t nextLine = 0;

    struct Line
    {
        bool isEOF = false;
        int leadingSpaces = 0;
        std::string_view line;
    };

    Line getNextLine()
    {
        if (nextLine >= lines.size())
            return { true, 0, {} };

        std::string_view line = lines[nextLine++];
        auto trimmedLine = choc::text::trimStart (line);
        return { false, static_cast<int> (line.length() - trimmedLine.length()), trimmedLine };
    }

    static bool isListItem (std::string_view text)
    {
        if (text.length() <= 1 || text[1] != ' ')
            return false;

        auto c = text[0];
        return c == '-' || c == '*' || c == '+';
    }

    static bool isHorizontalRule (std::string_view text)
    {
        if (text.length() < 3)
            return false;

        auto first = text[0];

        if (first != '-' && first != '=')
            return false;

        for (auto c : text)
            if (c != first)
                return false;

        return true;
    }

    static uint32_t getHeaderDepth (std::string_view text)
    {
        if (text.length() < 3 || text[0] != '#')
            return 0;

        uint32_t depth = 0;

        for (auto c : text)
            if (c == '#')
                ++depth;
            else
                break;

        if (depth >= text.length() - 2 || text[depth] != ' ')
            return 0;

        return depth;
    }

    void parseNextLine (choc::html::HTMLElement& parent)
    {
        auto line = getNextLine();

        if (line.isEOF)
            return;

        if (line.line.empty())
        {
            parent.addLineBreak();
            return;
        }

        if (parseUnorderedList (parent, line, -1))
            return;

        if (parseCodeBlock (parent, line))
            return;

        if (auto headerDepth = getHeaderDepth (line.line))
            return addContentSpans (parent.addChild ("h" + std::to_string (headerDepth)),
                                    choc::text::trimStart (choc::text::trimCharacterAtStart (line.line, '#')));

        if (isHorizontalRule (line.line))
        {
            parent.addChild ("hr");
            return;
        }

        addContentSpans (parent, line.line);
    }

    bool parseCodeBlock (choc::html::HTMLElement& parent, const Line& firstLine)
    {
        if (! choc::text::startsWith (firstLine.line, "```"))
            return false;

        auto& block = parent.addInlineChild ("pre").addInlineChild ("code");
        auto language = choc::text::trim (choc::text::trimCharacterAtStart (firstLine.line, '`'));
        std::vector<std::string> contentLines;

        for (;;)
        {
            auto line = getNextLine();

            if (line.isEOF)
            {
                addCodeBlockContent (block, contentLines, firstLine.leadingSpaces, language);
                return true;
            }

            if (auto end = line.line.find ("```"); end != std::string_view::npos)
            {
                if (end != 0)
                    contentLines.emplace_back (line.line.substr (0, end));

                addCodeBlockContent (block, contentLines, firstLine.leadingSpaces, language);
                return true;
            }

            contentLines.push_back (std::string (static_cast<std::string_view::size_type> (line.leadingSpaces), ' ')
                                      + std::string (line.line));
        }
    }

    void addCodeBlockContent (choc::html::HTMLElement& codeElement,
                              const std::vector<std::string>& contentLines,
                              int indent, std::string_view language)
    {
        (void) language; // TODO

        auto content = createParagraph (contentLines, static_cast<size_t> (indent));

        if (! content.empty())
            codeElement.addContent (content);
    }

    bool parseUnorderedList (choc::html::HTMLElement& parent, const Line& firstLine, int lastIndent)
    {
        if (! isListItem (firstLine.line) || firstLine.leadingSpaces <= lastIndent)
            return false;

        auto& ul = parent.addChild ("ul");
        choc::html::HTMLElement* li = nullptr;
        --nextLine;

        for (;;)
        {
            auto line = getNextLine();

            if (line.isEOF)
                return true;

            if (line.line.empty() || line.leadingSpaces < firstLine.leadingSpaces)
            {
                if (lastIndent >= 0)
                    --nextLine;

                return true;
            }

            if (isListItem (line.line))
            {
                if (parseUnorderedList (ul, line, firstLine.leadingSpaces))
                    continue;

                li = std::addressof (ul.addChild ("li"));
                line.line = choc::text::trimStart (line.line.substr (2));
            }

            if (parseCodeBlock (*li, line))
                continue;

            addContentSpans (*li, line.line);
        }
    }

    static void makeBold       (choc::html::HTMLElement& parent, std::string_view content)  { addContentSpans (parent.addInlineChild ("strong"), content); }
    static void makeItalic     (choc::html::HTMLElement& parent, std::string_view content)  { addContentSpans (parent.addInlineChild ("em"), content); }
    static void makeInlineCode (choc::html::HTMLElement& parent, std::string_view content)  { parent.addInlineChild ("code").addContent (content); }
    static void makeLink       (choc::html::HTMLElement& parent, std::string_view content)  { parent.addLink (content).addContent (content); }

    static void addContentSpans (choc::html::HTMLElement& parent, std::string_view text)
    {
        ElementRange firstElement;
        std::function<void(choc::html::HTMLElement&, std::string_view)> applyFirstElement;

        auto setFirstElement = [&] (ElementRange r, std::function<void(choc::html::HTMLElement&, std::string_view)> apply)
        {
            if (r && (! firstElement || r.start < firstElement.start))
            {
                firstElement = r;
                applyFirstElement = std::move (apply);
            }
        };

        if (auto r = findElementRange (text, "`"))  setFirstElement (r, makeInlineCode);

        if (auto r = findElementRange (text, "**")) setFirstElement (r, makeBold);
        if (auto r = findElementRange (text, "__")) setFirstElement (r, makeBold);

        if (auto r = findElementRange (text, "*"))  setFirstElement (r, makeItalic);
        if (auto r = findElementRange (text, "_"))  setFirstElement (r, makeItalic);

        for (auto proto : { "https:", "http:", "file:" })
        {
            if (auto start = text.find (proto); start != std::string::npos)
            {
                auto end = text.find (' ', start);

                if (end == std::string_view::npos)
                    end = text.length();

                setFirstElement ({ start, end, 0 }, makeLink);
            }
        }

        if (applyFirstElement)
        {
            addContentSpans (parent, text.substr (0, firstElement.start));
            applyFirstElement (parent, text.substr (firstElement.start + firstElement.delimiterLength,
                                                   firstElement.end - firstElement.start - firstElement.delimiterLength * 2));
            addContentSpans (parent, text.substr (firstElement.end));
        }
        else if (! text.empty())
        {
            parent.addContent (text);
        }
    }

    static auto findElement (std::string_view text, std::string_view delimiter, size_t startIndex)
    {
        auto index = text.find (delimiter, startIndex);

        if (index != std::string_view::npos)
            if (index == 0 || text[index - 1] != delimiter.front())
                if (index + delimiter.length() >= text.length() || text[index + delimiter.length()] != delimiter.back())
                    return index;

        return std::string_view::npos;
    }

    struct ElementRange
    {
        std::string_view::size_type start = 0, end = 0, delimiterLength = 0;

        operator bool() const    { return end > start; }
    };

    static ElementRange findElementRange (std::string_view text, std::string_view delimiter)
    {
        if (auto start = findElement (text, delimiter, 0); start != std::string_view::npos)
        {
            auto delimiterLen = delimiter.length();

            if (auto end = findElement (text, delimiter, start + delimiterLen); end != std::string_view::npos)
            {
                auto isWhitespace = [&] (size_t index)  { return index >= text.length() || std::iswspace (static_cast<wint_t> (text[index])); };
                auto isAlphaNum   = [&] (size_t index)  { return index <  text.length() && std::isalnum (text[index]); };

                if (! isAlphaNum (start - 1)
                     && ! isAlphaNum (end + delimiterLen)
                     && ! isWhitespace (start + delimiterLen)
                     && ! isWhitespace (end - 1))
                    return { start, end + delimiterLen, delimiterLen };
            }
        }

        return {};
    }
};

//==============================================================================
struct HTMLDocGenerator
{
    HTMLDocGenerator() = default;

    std::string generate (const cmaj::AST::Program& program,
                          const std::string& title,
                          const std::string& styleSheetURL)
    {
        choc::html::HTMLElement root ("html");
        root.setProperty ("lang", "en-US");

        auto& head = root.addChild ("head");
        head.addChild ("title").addContent (title);
        head.addChild ("link").setProperty ("rel", "stylesheet")
                              .setProperty ("href", styleSheetURL);

        auto& body = root.addChild ("body");

        for (auto& m : program.rootNamespace.getSubModules())
            addModule (rootIndex, body, body, m);

        return root.toDocument (true);
    }

    struct Index
    {
        std::string description, anchor;
        std::vector<std::unique_ptr<Index>> childIndexes;
        Index* parent = nullptr;

        Index& addChildIndex (std::string_view desc, std::string_view anch)
        {
            auto i = std::make_unique<Index>();
            i->description = desc;
            i->anchor = anch;
            i->parent = this;
            auto& result = *i;
            childIndexes.push_back (std::move (i));
            return result;
        }

        bool isChildOf (Index& p) const
        {
            if (parent == nullptr)
                return false;

            if (parent == &p)
                return true;

            return parent->isChildOf (p);
        }
    };

    Index rootIndex;
    Index* currentIndex = &rootIndex;

    struct ModuleDetails
    {
        Index& index;
        choc::html::HTMLElement& element;
    };

    std::map<std::string, ModuleDetails> moduleElementIndexes;

private:
    //==============================================================================
    cmaj::ProgramPrinter getPrinter() { return cmaj::ProgramPrinter (AST::PrintOptionFlags::useShortNames); }

    void addObject (choc::html::HTMLElement& parent, const AST::Property& p)
    {
        addObject (parent, p.getObjectRef());
    }

    void addObject (choc::html::HTMLElement& parent, const AST::Object& o)
    {
        addObject (parent, getPrinter().formatExpression (o));
    }

    void addObject (choc::html::HTMLElement& parent, const cmaj::SourceCodeFormattingHelper::ExpressionTokenList& tokens)
    {
        if (tokens.empty())
            return;

        for (auto& t : tokens.tokens)
        {
            auto getStyle = [] (cmaj::SourceCodeFormattingHelper::ExpressionToken::Type type) -> const char*
            {
                switch (type)
                {
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::keyword:      return "code_keyword";
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::primitive:    return "code_prim";
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::identifier:   return "code_ident";
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::literal:      return "code_literal";
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::punctuation:
                    case cmaj::SourceCodeFormattingHelper::ExpressionToken::Type::text:         return "code";
                    default: CMAJ_ASSERT_FALSE;
                }
            };

            auto& span = parent.addSpan (getStyle (t.type));

            if (t.referencedObject != nullptr && doesObjectHaveAnchor (*t.referencedObject))
                span.addLink ("#" + getAnchorName (*t.referencedObject)).addContent (t.text);
            else
                span.addContent (t.text);
        }
    }

    std::string getQualifiedNameForObject (const AST::Object& o)    { return std::string (o.getOriginalName()); }

    //==============================================================================
    std::unordered_map<const AST::Object*, std::string> anchors;
    std::unordered_set<std::string> usedAnchorNames;

    std::string getUniqueAnchorName (const std::string root)
    {
        auto tryName = [this] (const std::string& name)
        {
            if (usedAnchorNames.find (name) == usedAnchorNames.end())
            {
                usedAnchorNames.insert (name);
                return true;
            }

            return false;
        };

        if (tryName (root))
            return root;

        for (size_t suffix = 2;; ++suffix)
        {
            auto name = root + std::to_string (suffix);

            if (tryName (name))
                return name;
        }
    }

    std::string getAnchorName (const AST::Object& o)
    {
        if (auto found = anchors.find (std::addressof (o)); found != anchors.end())
            return found->second;

        auto name = getUniqueAnchorName (cmaj::makeSafeIdentifierName (std::string (o.getName())));
        anchors[std::addressof (o)] = name;
        return name;
    }

    static bool doesObjectHaveAnchor (const AST::Object& o)
    {
        return o.isModuleBase() || o.isFunction()
                || o.isStructType() || o.isAlias() || o.isEnumType()
                || o.isVariableDeclaration();
    }

    //==============================================================================
    std::string cleanComment (const AST::Comment& c)
    {
        auto content = c.getContent();

        if (choc::text::trim (content).empty())
            return {};

        auto lines = choc::text::splitIntoLines (content, false);

        for (auto& line : lines)
        {
            auto trimmedStartLine = choc::text::trimStart (line);

            if (trimmedStartLine.empty() || choc::text::startsWith (trimmedStartLine, "==="))
                line = {};
        }

        return createParagraph (lines, 100);
    }

    std::string getComment (const AST::Property& comment)
    {
        if (auto c = AST::castTo<AST::Comment> (comment))
            return cleanComment (*c);

        return {};
    }

    std::string getComment (const AST::Object& objectWithComment)
    {
        if (auto c = objectWithComment.getComment())
            return cleanComment (*c);

        return {};
    }

    std::string getComment (const std::string& rawComment)
    {
        return rawComment;
    }

    template <typename CommentOrObject>
    void addComment (choc::html::HTMLElement& parent, const CommentOrObject& commentOrObject)
    {
        if (auto text = getComment (commentOrObject); ! text.empty())
        {
            text = choc::text::trimCharacterAtStart (text, '\n');

            if (! text.empty())
                MarkdownToHTML().convert (parent.addDiv ("comment"), text);
        }
    }

    template <typename CommentOrObject>
    void addTableComment (choc::html::HTMLElement& parent, const CommentOrObject& commentOrObject)
    {
        if (auto text = getComment (commentOrObject); ! text.empty())
        {
            text = choc::text::trimCharacterAtStart (text, '\n');

            if (!text.empty())
                MarkdownToHTML().convert (parent.addDiv ("comment").addChild ("td"), text);
        }
    }

    bool isInternal (const AST::Object& o)
    {
        return hasAttribute (o, "@internal");
    }

    bool hasAttribute (const AST::Object& o, const std::string& attribute)
    {
        if (auto c = o.getComment())
            return choc::text::contains (c->getText(), attribute);

        return false;
    }

    // Tries to find the module-level comment from the file containing
    // this object
    struct FileComment
    {
        std::string moduleName, comment;
    };

    static FileComment getFileComment (const AST::Object& o)
    {
        if (auto sourceFile = o.context.allocator.sourceFileList.findSourceFileContaining (o.context.location))
        {
            auto fileContent = sourceFile->content;
            CMAJ_ASSERT (! fileContent.empty());
            auto t = choc::text::UTF8Pointer (fileContent.c_str());

            for (;;)
            {
                t = t.findEndOfWhitespace();
                auto comment = t;

                if (t.skipIfStartsWith ('/'))
                {
                    if (t.skipIfStartsWith ('/'))
                    {
                        t.skipIfStartsWith ('/');
                        auto start = t;
                        t = comment.find ("\n");
                        auto text = std::string (start.data(), (size_t) (t.data() - start.data()));

                        if (auto c = parseFileComment (text); ! c.moduleName.empty())
                            return c;

                        continue;
                    }

                    if (t.skipIfStartsWith ('*'))
                    {
                        t.skipIfStartsWith ('*');
                        auto start = t;
                        t = t.find ("*/");
                        if (t.empty()) break;
                        auto text = std::string (start.data(), (size_t) (t.data() - start.data()));

                        if (auto c = parseFileComment (text); ! c.moduleName.empty())
                            return c;

                        continue;
                    }
                }

                if (t.empty())
                    break;

                ++t;
            }
        }

        return {};
    }

    static FileComment parseFileComment (const std::string& comment)
    {
        FileComment fc;

        auto lines = choc::text::splitIntoLines (choc::text::trim (comment), false);

        if (! lines.empty())
        {
            auto name = choc::text::trim (lines.front());

            if (choc::text::startsWith (name, "std."))
            {
                fc.moduleName = name;
                lines.erase (lines.begin());
                fc.comment = choc::text::trim (choc::text::joinStrings (lines, "\n"));
            }
        }

        return fc;
    }

    void addTitle (choc::html::HTMLElement& parent, const std::string& text)
    {
        parent.addChild ("h2").addContent (text);
    }

    void addTitle (choc::html::HTMLElement& parent, const std::string& keyword,
                   const std::string& text, const std::string& url = {})
    {
        auto& title = parent.addChild ("h2");
        title.addSpan ("keyword").addContent (keyword);
        title.addContent (text);

        if (! url.empty())
            title.addLink (url).setProperty ("target", "_blank").addContent ("code");
    }

    //==============================================================================
    void addModule (Index& parentIndex, choc::html::HTMLElement& bodyElement,
                    choc::html::HTMLElement& parent, const AST::ModuleBase& m)
    {
        if (auto ns = AST::castTo<AST::Namespace> (m))
            return addNamespace (parentIndex, bodyElement, *ns);

        if (auto p = AST::castTo<AST::Processor> (m))
            return addProcessor (parentIndex, parent, *p);

        if (auto g = AST::castTo<AST::Graph> (m))
            return addGraph (parentIndex, parent, *g);
    }

    choc::html::HTMLElement& getModuleElement (choc::html::HTMLElement& bodyElement,
                                               const AST::Namespace& n)
    {
        auto fileComment = getFileComment (n);

        if (fileComment.moduleName.empty())
            return bodyElement;

        auto found = moduleElementIndexes.find (fileComment.moduleName);

        if (found != moduleElementIndexes.end())
            return found->second.element;

        auto uniqueAnchorName = getUniqueAnchorName (cmaj::makeSafeIdentifierName (std::string (fileComment.moduleName)));

        auto& section = bodyElement.addDiv ("module")
                                   .setID (uniqueAnchorName)
                                   .setProperty ("href", '#' + uniqueAnchorName);

        auto& moduleTitle = section.addDiv ("moduleTitle");

        addTitle (moduleTitle, fileComment.moduleName);
        addComment (section, fileComment.comment);

        ModuleDetails moduleDetails { rootIndex.addChildIndex (fileComment.moduleName, uniqueAnchorName), section };
        moduleElementIndexes.emplace (fileComment.moduleName, moduleDetails);

        return section;
    }

    ptr<Index> findModuleIndex (const AST::Namespace& n)
    {
        auto fileComment = getFileComment (n);

        if (fileComment.moduleName.empty())
            return {};

        auto found = moduleElementIndexes.find (fileComment.moduleName);

        if (found != moduleElementIndexes.end())
            return found->second.index;

        return {};
    }

    void addNamespace (Index& parentIndex, choc::html::HTMLElement& bodyElement, const AST::Namespace& n)
    {
        if (isInternal (n) || n.isSpecialised())
            return;

        auto& moduleElement = getModuleElement (bodyElement, n);

        auto moduleIndex = findModuleIndex (n);

        if (! moduleIndex || parentIndex.isChildOf (*moduleIndex))
            moduleIndex = parentIndex;

        auto& index = n.getName() == "std" ? parentIndex
                                           : moduleIndex->addChildIndex (n.getName(), getAnchorName (n));

        auto& section = moduleElement.addDiv ("namespace")
                                     .setID (getAnchorName (n))
                                     .setProperty ("href", '#' + getAnchorName (n));

        auto& namespaceTitle = section.addDiv ("namespaceTitle");

        if (getAnchorName (n.getParentNamespace()) == "std")
            addTitle (namespaceTitle, "namespace ",
                      n.getFullyQualifiedReadableName(),
                      "https://github.com/cmajor-lang/cmajor/blob/main/standard_library/std_library_"
                        + moduleIndex->anchor.substr(4) + ".cmajor");
        else
            addTitle (namespaceTitle, "namespace ", n.getFullyQualifiedReadableName());

        addSpecialisations (section, n);
        addComment (section, n);

        addStructs (section, n.structures);
        addFunctions (section, n.functions);
        addVariables (section, "Constants", n.constants);

        for (auto& m : n.getSubModules())
            if (AST::castTo<AST::Namespace> (m) == nullptr)
                addModule (index, bodyElement, section, m);

        for (auto& m : n.getSubModules())
            if (auto ns = AST::castTo<AST::Namespace> (m))
                addNamespace (index, bodyElement, *ns);
    }

    void addProcessor (Index& parentIndex, choc::html::HTMLElement& parent, const AST::Processor& p)
    {
        if (isInternal (p) || p.isSpecialised())
            return;

        parentIndex.addChildIndex (p.getName(), getAnchorName (p));

        auto& section = parent.addDiv ("processor").setID (getAnchorName (p));
        auto& processorTitle = section.addDiv("processorTitle");

        addTitle (processorTitle, "processor ", p.getFullyQualifiedReadableName());

        addSpecialisations (section, p);
        addComment (section, p);
        addEndpoints (section, p);
        addStructs (section, p.structures);
        addVariables (section, "Variables", p.stateVariables);
        addFunctions (section, p.functions);
    }

    void addGraph (Index& parentIndex, choc::html::HTMLElement& parent, const AST::Graph& g)
    {
        if (isInternal (g) || g.isSpecialised())
            return;

        parentIndex.addChildIndex (g.getName(), getAnchorName (g));

        auto& section = parent.addDiv ("graph").setID (getAnchorName (g));
        auto& graphTitle = section.addDiv ("graphTitle");
        addTitle (graphTitle, "graph ", g.getFullyQualifiedReadableName());

        addSpecialisations (section, g);
        addComment (section, g);
        addEndpoints (section, g);
        addNodes (section, g);
        addConnections (section, g);
        addStructs (section, g.structures);
        addVariables (section, "Constants", g.stateVariables);
        addFunctions (section, g.functions);
    }

    void addSpecialisations (choc::html::HTMLElement& parent, const AST::ModuleBase& m)
    {
        auto list = m.specialisationParams.iterateAs<AST::Object>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("specialisations");

        {
            auto firstLine = std::string (m.getModuleType()) + " " + std::string (m.getFullyQualifiedName()) + " (";

            auto& code = section.addSpan("specialisation").addChild ("code");

            code.addSpan ("keyword").addContent (m.getModuleType());
            code.addContent (" ");
            code.addSpan ("code_ident").addContent (std::string (m.getFullyQualifiedName()));
            code.addContent (" (");

            size_t item = 0;
            size_t items = list.size();

            auto indentString = std::string (firstLine.length(), ' ');

            for (auto& param : list)
            {
                if (auto alias = param.getAsAlias())
                {
                    switch (alias->aliasType.get())
                    {
                        case AST::AliasTypeEnum::Enum::typeAlias:           code.addSpan ("keyword").addContent ("using "); break;
                        case AST::AliasTypeEnum::Enum::processorAlias:      code.addSpan ("keyword").addContent ("processor "); break;
                        case AST::AliasTypeEnum::Enum::namespaceAlias:      code.addSpan ("keyword").addContent ("namespace "); break;
                    }

                    code.addSpan ("code_ident").addContent (param.getName());

                    if (alias->target != nullptr)
                    {
                        code.addContent (" = ");
                        addObject (code, param);
                    }
                }
                else if (param.isVariableDeclaration())
                {
                    addObject (code, param);
                }

                if (item == items - 1)
                {
                    code.addContent (")");
                }
                else
                {
                    code.addContent (",");
                    code.addLineBreak();
                    code.addContent (indentString);
                }

                item++;
            }
        }
    }

    void addEndpoints (choc::html::HTMLElement& parent, const AST::ProcessorBase& p)
    {
        auto inputs = p.getInputEndpoints (false);
        auto outputs = p.getOutputEndpoints (false);

        if (outputs.empty() && inputs.empty())
            return;

        auto& section = parent.addDiv ("endpoints");
        addTitle (section, "Endpoints");

        for (auto& e : outputs)
            addEndpoint (section, "output ", e);

        for (auto& e : inputs)
            addEndpoint (section, "input ", e);
    }

    void addEndpoint (choc::html::HTMLElement& parent, const std::string& direction, const AST::EndpointDeclaration& e)
    {
        auto& list = parent.addChild("ul");
        auto& item = list.addChild("li").addDiv ("endpoint");

        item.addSpan ("endpoint_dir").addContent (direction);
        item.addContent (" ");
        item.addSpan ("endpoint_kind").addContent (getEndpointTypeName (e.getEndpointType()));
        item.addContent (" ");
        item.addSpan ("endpoint_name").addContent (e.getName());
        item.addContent (" ");

        if (e.childPath != nullptr)
        {
            addObject (item.addSpan ("endpoint_source"), e.childPath);
        }
        else
        {
            CMAJ_ASSERT (! e.dataTypes.empty());
            item.addContent ("(");
            bool first = true;

            for (auto& t : e.dataTypes)
            {
                if (first)
                    first = false;
                else
                    item.addContent (", ");

                addObject (item, t);
            }

            item.addContent (")");
        }

        addComment (item, e);
    }

    void addNodes (choc::html::HTMLElement& parent, const AST::Graph& g)
    {
        auto list = g.nodes.iterateAs<AST::GraphNode>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("nodes");
        addTitle (section, "Nodes");

        for (auto& n : list)
        {
            auto& item = section.addDiv ("node");

            item.addSpan ("node_name").addContent (n.getName());
            item.addSpan ("code").addContent (" = ");
            addObject (item, n.processorType);

            if (auto arraySize = n.arraySize.getObject())
                addObject (item, getPrinter().formatBracketedValue (*arraySize));

            if (auto multiplier = n.clockMultiplierRatio.getObject())
            {
                item.addContent (" * ");
                addObject (item, *multiplier);
            }

            if (auto divider = n.clockDividerRatio.getObject())
            {
                item.addContent (" / ");
                addObject (item, *divider);
            }
        }
    }

    void addConnections (choc::html::HTMLElement& parent, const AST::Graph& g)
    {
        auto list = g.connections.iterateAs<AST::Connection>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("connections");
        addTitle (section, "Connections");

        for (auto& c : list)
        {
            auto& item = section.addDiv ("connection");

            // TODO
            addObject (item, c);
        }
    }

    void addFunctions (choc::html::HTMLElement& parent, const AST::ListProperty& functions)
    {
        auto list = functions.iterateAs<AST::Function>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("functions");
        addTitle (section, "Functions");

        for (auto& f : list)
        {
            if (! (isInternal (f) || f.isSpecialisedGeneric()))
            {
                auto& item = section.addDiv ("function").setID (getAnchorName (f));

                item.addSpan ("function_name").addContent (f.getName());

                addComment (item, f);
                addObject (item.addDiv ("function_decl").addChild ("code"), getPrinter().formatFunctionDeclaration (f));
            }
        }
    }

    void addStructs (choc::html::HTMLElement& parent, const AST::ListProperty& structs)
    {
        auto list = structs.iterateAs<AST::StructType>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("structs");
        addTitle (section, "Structs");

        for (auto& s: list)
        {
            auto& item = section.addDiv ("struct").setID (getAnchorName (s));

            item.addSpan ("code").addContent ("struct ");
            item.addSpan ("struct_name").addContent (s.getName());

            addComment (item, s);
            auto& membersList = item.addDiv ("struct_members")
                                    .addChild ("table")
                                    .setClass ("struct_table");

            membersList.addChild ("th").addContent ("Type");
            membersList.addChild ("th").addContent ("Member");

            if (! s.memberComments.empty())
                membersList.addChild ("th").addContent ("Comment");

            for (size_t i = 0; i < s.memberNames.size(); ++i)
            {
                auto& member = membersList.addChild ("tr").addChild ("td");

                member.addDiv("type");
                addObject (member, s.memberTypes[i]);

                auto& memberName = member.addChild("td");
                memberName.addDiv ("struct_member_name").addContent (s.getMemberName (i));
                memberName.addContent (" ");

                if (! s.memberComments.empty())
                {
                    if (s.memberComments.size() > i)
                        addTableComment (member, s.memberComments[i]);
                    else
                        member.addChild ("td");
                }
            }
        }
    }

    void addEnums (choc::html::HTMLElement& parent, const AST::ListProperty& enums)
    {
        auto list = enums.iterateAs<AST::EnumType>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("enums");
        addTitle (section, "Enums");

        for (auto& e : list)
        {
            auto& item = section.addDiv ("enum").setID (getAnchorName (e));

            item.addSpan ("code").addContent ("enum ");
            item.addSpan ("enum_name").addContent (e.getName());

            addComment (item, e);
            auto& membersList = item.addDiv ("enum_members");

            for (size_t i = 0; i < e.items.size(); ++i)
            {
                auto& member = membersList.addDiv ("enum_member");

                member.addDiv ("enum_member_name").addContent (e.items[i].toString());

                if (e.itemComments.size() > i)
                    addComment (member, e.itemComments[i]);
            }
        }
    }

    void addAliases (choc::html::HTMLElement& parent, const AST::ListProperty& aliases)
    {
        auto list = aliases.iterateAs<AST::Alias>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("aliases");
        addTitle (section, "Aliases");

        for (auto& a : list)
        {
            auto& item = section.addDiv ("alias").setID (getAnchorName (a));

            item.addContent ("alias ");
            item.addDiv ("alias_name").addContent (a.getName());
            item.addContent (" = ");
            addObject (item, a.target);

            addComment (item, a);
        }
    }

    void addVariables (choc::html::HTMLElement& parent, const std::string& description, const AST::ListProperty& variables)
    {
        auto list = variables.iterateAs<AST::VariableDeclaration>();

        if (list.size() == 0)
            return;

        auto& section = parent.addDiv ("variables");
        addTitle (section, description);
        auto& table = section.addChild ("table").setClass ("variable_table");

        table.addChild ("th").addContent ("Type");
        table.addChild ("th").addContent ("Variable");

        auto hasAnyConstants = [&]
        {
            for (auto& v : list)
                if (v.initialValue.getObject() != nullptr)
                    return true;

            return false;
        };

        auto hasAnyComments = [&]
        {
            for (auto& v : list)
                if (! getComment (v).empty())
                    return true;

            return false;
        };

        if (hasAnyConstants())
            table.addChild ("th").addContent ("Constant");

        if (hasAnyComments())
            table.addChild ("th").addContent ("Comment");

        for (auto& v : list)
        {
            auto& item = table.addDiv ("variable").setID (getAnchorName (v));
            auto& variable = item.addChild ("tr").addChild ("td");

            ptr<const AST::Object> type = v.declaredType.getPointer();

            if (type == nullptr)
                if (auto init = AST::castToValue (v.initialValue))
                    type = init->getResultType();

            if (type != nullptr)
            {
                addObject (variable.addDiv ("variable_type"), *type);
            }

            auto& variableName = variable.addChild ("td");
            variableName.addDiv ("variable_name").addContent (v.getName());

            if (auto constantValue = AST::castToValue (v.initialValue))
            {
                auto& constant = variable.addChild ("td");
                addObject (constant.addDiv("Constant"), *constantValue);
            }

            if (hasAnyComments())
            {
                auto& variableComment = variable.addChild ("td").addChild ("tr");
                addComment (variableComment, v);
            }
        }
    }
};

}
