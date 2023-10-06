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


/// Base class for objects that are only used when parsing, and which are later
/// replaced by semantic objects
struct SyntacticExpression  : public Expression
{
    SyntacticExpression (const ObjectContext& c)  : Expression (c) {}

    void addSideEffects (SideEffects&) const override   {}
    bool isSyntacticObject() const override             { return true; }
};

//==============================================================================
struct Identifier  : public SyntacticExpression
{
    Identifier (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Identifier, 1)

    bool hasName (PooledString nametoMatch) const override      { return name.hasName (nametoMatch); }
    PooledString getName() const override                       { return name; }
    void writeSignature (SignatureBuilder& sig) const override  { sig << name; }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, name) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ExpressionList  : public SyntacticExpression
{
    ExpressionList (const ObjectContext& c)  : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ExpressionList, 2)

    size_t size() const                     { return items.size(); }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "list";

        for (auto& item : items)
            sig << item;
    }

    choc::SmallVector<ref<Expression>, 8> getExpressions() const
    {
        choc::SmallVector<ref<Expression>, 8> results;

        for (auto& item : items)
            results.push_back (castToExpressionRef (item));

        return results;
    }

    void addSideEffects (SideEffects& effects) const override
    {
        for (auto& item : items)
            effects.add (item);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (items, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, items) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct BracketedSuffixTerm  : public SyntacticExpression
{
    BracketedSuffixTerm (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(BracketedSuffixTerm, 67)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "dim" << startIndex << endIndex;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (startIndex, visit);
        visitObjectIfPossible (endIndex, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  startIndex) \
        X (2, ChildObject,  endIndex) \
        X (3, BoolProperty, isRange) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct BracketedSuffix  : public SyntacticExpression
{
    BracketedSuffix (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(BracketedSuffix, 68)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "bs" << parent << terms;
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return parent->getLocationOfStartOfExpression();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (parent, visit);
        visitObjectIfPossible (terms, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  parent) \
        X (2, ListProperty, terms) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ChevronedSuffix  : public SyntacticExpression
{
    ChevronedSuffix (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ChevronedSuffix, 71)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "vec" << parent << terms;
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return parent->getLocationOfStartOfExpression();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (parent, visit);
        visitObjectIfPossible (terms, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  parent) \
        X (2, ListProperty, terms) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct NamespaceSeparator  : public SyntacticExpression
{
    NamespaceSeparator (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(NamespaceSeparator, 5)

    const ObjectContext& getLocationOfStartOfExpression() const override    { return lhs->getLocationOfStartOfExpression(); }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << lhs << rhs;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (lhs, visit);
        visitObjectIfPossible (rhs, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  lhs) \
        X (2, ChildObject,  rhs) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct CallOrCast  : public SyntacticExpression
{
    CallOrCast (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(CallOrCast, 6)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << functionOrType << arguments;
    }

    size_t getNumArguments() const
    {
        if (arguments == nullptr)
            return 0;

        if (auto list = castTo<ExpressionList> (arguments))
            return list->size();

        return 1;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (functionOrType, visit);
        visitObjectIfPossible (arguments, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || functionOrType.containsStatement (other)
                || arguments.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, functionOrType) \
        X (2, ChildObject, arguments) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct DotOperator  : public SyntacticExpression
{
    DotOperator (const ObjectContext& c) : SyntacticExpression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(DotOperator, 7)

    const ObjectContext& getLocationOfStartOfExpression() const override    { return lhs->getLocationOfStartOfExpression(); }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "dot" << lhs << rhs;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (lhs, visit);
        visitObjectIfPossible (rhs, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || lhs.containsStatement (other)
                || rhs.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, lhs) \
        X (2, ChildObject, rhs) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Comment  : public SyntacticExpression
{
    Comment (const ObjectContext& c) : SyntacticExpression (c) {}

    Comment (const ObjectContext& c, CodeLocationRange r) : SyntacticExpression (c)
    {
        text.set (getStringPool().get (r.toString()));
        CMAJ_ASSERT (isSingleLine() || isMultiLine());
    }

    CMAJ_AST_DECLARE_STANDARD_METHODS(Comment, 70)

    /// Returns the complete text of the comment, including all its slashes and stars
    std::string_view getText() const    { return text.get(); }

    /// Returns the text of the comment without the slashes or stars
    std::string getContent() const
    {
        if (isSingleLine())     return removeLeadingSlashes (getText());
        if (isMultiLine())      return removeTrailingStarSlashes (removeLeadingChars (getText().substr (1), '*'));
        return {};
    }

    bool isSingleLine() const           { return startsWith ("//"); }
    bool isMultiLine() const            { return startsWith ("/*"); }
    bool isDoxygenStyle() const         { return startsWith ("///") || startsWith ("/**"); }
    bool isReferringBackwards() const   { return startsWith ("///<") || startsWith ("/**<"); }

    static bool isReferringBackwards (CodeLocationRange r)
    {
        return choc::text::startsWith (r.toString(), "///<")
            || choc::text::startsWith (r.toString(), "/**<");
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, text) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

private:
    bool startsWith (std::string_view s) const    { return choc::text::startsWith (getText(), s); }

    static std::string_view removeLeadingChars (std::string_view s, char c)
    {
        while (! s.empty() && s.front() == c)
            s = s.substr (1);

        return s;
    }

    static std::string removeLeadingSlashes (std::string_view s)
    {
        if (s.find ("\n") == std::string_view::npos)
            return std::string (removeLeadingChars (s, '/'));

        auto lines = choc::text::splitIntoLines (s, false);

        for (auto& line : lines)
            line = std::string (removeLeadingChars (choc::text::trimStart (line), '/'));

        return choc::text::joinStrings (lines, "\n");
    }

    static std::string removeTrailingStarSlashes (std::string_view s)
    {
        s = choc::text::trimEnd (s);

        if (! s.empty() && s.back() == '/')
        {
            s = s.substr (0, s.length() - 1);

            while (! s.empty() && s.back() == '*')
                s = s.substr (0, s.length() - 1);
        }

        return std::string (s);
    }
};
