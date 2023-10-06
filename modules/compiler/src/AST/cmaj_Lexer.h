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

namespace cmaj
{

//==============================================================================
struct LexerTokenType
{
    constexpr LexerTokenType() = default;
    explicit constexpr LexerTokenType (const char* t) : token (t) {}

    constexpr operator bool() const                                    { return ! token.empty(); }

    constexpr bool operator== (const LexerTokenType& other) const      { return token == other.token; }
    constexpr bool operator!= (const LexerTokenType& other) const      { return token != other.token; }
    constexpr bool operator== (const char* other) const                { return token == other; }
    constexpr bool operator!= (const char* other) const                { return token != other; }

    operator std::string() const
    {
        CMAJ_ASSERT (! token.empty());
        return token.front() == '\\' ? std::string (token.substr (1))
                                     : choc::text::addDoubleQuotes (std::string (token));
    }

    std::string_view token;
};

//==============================================================================
namespace LexerToken
{
    static constexpr LexerTokenType   eof             { "\\end-of-file" },
                                      identifier      { "\\identifier" },
                                      literalString   { "\\string literal" },
                                      literalFloat32  { "\\float32 literal" },
                                      literalFloat64  { "\\float64 literal" },
                                      literalInt32    { "\\int32 literal" },
                                      literalInt64    { "\\int64 literal" },
                                      literalImag32   { "\\imag32 imaginary literal" },
                                      literalImag64   { "\\imag64 imaginary literal" };

    #define CMAJ_OPERATOR_LIST(X) \
        X(";",      semicolon) \
        X(".",      dot) \
        X(",",      comma) \
        X("(",      openParen) \
        X(")",      closeParen) \
        X("{",      openBrace) \
        X("}",      closeBrace) \
        X("[[",     openDoubleBracket) \
        X("]]",     closeDoubleBracket) \
        X("[",      openBracket) \
        X("]",      closeBracket) \
        X("::",     doubleColon) \
        X(":",      colon) \
        X("?",      question) \
        X("==",     equals) \
        X("=",      assign) \
        X("!=",     notEquals) \
        X("!",      logicalNot) \
        X("+=",     plusEquals) \
        X("++",     plusplus) \
        X("+",      plus) \
        X("-=",     minusEquals) \
        X("--",     minusminus) \
        X("->",     rightArrow) \
        X("-",      minus) \
        X("*=",     timesEquals) \
        X("**",     exponent) \
        X("*",      times) \
        X("/=",     divideEquals) \
        X("/",      divide) \
        X("%=",     moduloEquals) \
        X("%",      modulo) \
        X("^=",     xorEquals) \
        X("^",      bitwiseXor) \
        X("&=",     bitwiseAndEquals) \
        X("&&=",    logicalAndEquals) \
        X("&&",     logicalAnd) \
        X("&",      bitwiseAnd) \
        X("|=",     bitwiseOrEquals) \
        X("||=",    logicalOrEquals) \
        X("||",     logicalOr) \
        X("|",      bitwiseOr) \
        X("<<=",    leftShiftEquals) \
        X("<<",     leftShift) \
        X("<=",     lessThanOrEqual) \
        X("<-",     leftArrow) \
        X("<",      lessThan) \
        X(">>>=",   rightShiftUnsignedEquals) \
        X(">>=",    rightShiftEquals) \
        X(">>>",    rightShiftUnsigned) \
        X(">>",     rightShift) \
        X(">=",     greaterThanOrEqual) \
        X(">",      greaterThan) \
        X("~",      bitwiseNot)

    #define CMAJ_DECLARE_OPERATOR(text, name)  static constexpr LexerTokenType operator_ ## name { text };
    CMAJ_OPERATOR_LIST (CMAJ_DECLARE_OPERATOR)
    #undef CMAJ_DECLARE_OPERATOR

    #define CMAJ_KEYWORD_LIST(X) \
        X(if)         X(do)         X(for)        X(let)       X(var)        X(int)        X(try)        X(else) \
        X(bool)       X(true)       X(case)       X(enum)      X(loop)       X(void)       X(node)       X(while) \
        X(break)      X(const)      X(int32)      X(int64)      X(float)     X(false)      X(using)      X(fixed) \
        X(graph)      X(input)      X(event)      X(class)      X(catch)     X(throw)      X(output)     X(return) \
        X(string)     X(struct)     X(import)     X(switch)     X(public)    X(double)     X(private)    X(float32) \
        X(float64)    X(default)    X(complex)    X(continue)   X(external)  X(operator)   X(processor)  X(namespace) \
        X(complex32)  X(complex64)  X(connection) X(forward_branch)

    #define CMAJ_DECLARE_KEYWORD(name)  static constexpr LexerTokenType keyword_ ## name { #name };
    CMAJ_KEYWORD_LIST (CMAJ_DECLARE_KEYWORD)
    #undef CMAJ_DECLARE_KEYWORD
}

//==============================================================================
struct Lexer
{
    Lexer() = default;
    virtual ~Lexer() = default;

    using Char = choc::text::UnicodeChar;
    using CharPointer = choc::text::UTF8Pointer;

    void setLexerInput (const CodeLocation& input)
    {
        location = input;
        setLexerPosition (location.text);
    }

    bool matches (LexerTokenType t) const                       { return currentToken == t; }
    bool matches (std::string_view name) const                  { return matches (LexerToken::identifier) && currentTokenText == name; }

    template <typename Type>
    bool matchesAny (Type t) const                              { return matches (t); }

    template <typename Type1, typename... Args>
    bool matchesAny (Type1 t1, Args... others) const            { return matches (t1) || matchesAny (others...); }

    bool matchesAnyKeyword() const
    {
        #define CMAJ_CHECK_KEYWORD(name) if (matches (LexerToken::keyword_ ## name)) return true;
        CMAJ_KEYWORD_LIST (CMAJ_CHECK_KEYWORD)
        #undef CMAJ_CHECK_KEYWORD
        return false;
    }

    void expect (LexerTokenType expected)
    {
        if (! skipIf (expected))
            throwError (Errors::foundWhenExpecting (currentToken, expected));
    }

    LexerTokenType skip()
    {
        previousCommentStart = {};
        skipWhitespaceAndComments();
        location.text = nextCharacter;
        auto last = currentToken;
        currentToken = matchNextToken();
        return last;
    }

    CodeLocation getLocationAndSkip()
    {
        auto l = location;
        skip();
        return l;
    }

    bool skipIf (LexerTokenType expected)                      { if (matches (expected)) { skip(); return true; } return false; }

    bool skipIfKeywordOrIdentifier (const char* text)          { if (matches (text) || currentToken == text) { skip(); return true; } return false; }

    void replaceCurrentToken (LexerTokenType replaceWith)      { currentToken = replaceWith; }

    std::string_view readIdentifier()
    {
        auto name = currentTokenText;
        expect (LexerToken::identifier);
        return name;
    }

    CharPointer getLexerPosition() const           { return location.text; }

    void setLexerPosition (CharPointer newPos)
    {
        if (nextCharacter != newPos || location.text != newPos)
        {
            nextCharacter = newPos;
            skip();
        }
    }

    virtual AST::ObjectContext getContext() const = 0;

    /// Returns a comment if one was parsed between the last token
    /// and the current one.
    CodeLocationRange getCurrentComment() const
    {
        if (previousCommentStart != nullptr)
        {
            CMAJ_ASSERT (previousCommentStart < previousCommentEnd);
            return { previousCommentStart, previousCommentEnd };
        }

        return {};
    }

    template <typename ObjectOrContext>
    [[noreturn]] void throwError (const ObjectOrContext& errorContext, DiagnosticMessage message) const
    {
        DiagnosticMessageList messages;
        messages.add (errorContext, message);
        cmaj::throwError (messages);
    }

    [[noreturn]] void throwError (const DiagnosticMessage& message) const
    {
        throwError (getContext(), message);
    }

    //==============================================================================
    CodeLocation location;
    LexerTokenType currentToken;

    std::string_view currentTokenText;
    std::string escapedStringLiteral;
    int64_t currentIntLiteral = 0;
    double currentDoubleLiteral = 0;

private:
    //==============================================================================
    CharPointer nextCharacter;
    LexerTokenType currentLiteralType = {};
    CharPointer previousCommentStart, previousCommentEnd;

    static bool isDigit (CharPointer p) noexcept       { if (auto t = p.data()) return choc::text::isDigit (*t); return false; }

    static bool skipIfStartsWith (CharPointer& p, const char* text)                      { return p.skipIfStartsWith (text); }

    template <typename... Args>
    static bool skipIfStartsWith (CharPointer& p, const char* text, Args... others)      { return p.skipIfStartsWith (text) || skipIfStartsWith (p, others...); }

    //==============================================================================
    LexerTokenType matchNextToken()
    {
        for (;;)
        {
            if (isDigit (nextCharacter))
                return readNumericLiteral (false);

            auto firstChar = *(nextCharacter.data());

            if (isIdentifierStart (firstChar))
            {
                uint32_t tokenLength = 1;
                auto tokenEnd = nextCharacter.data();

                while (isIdentifierBody (*++tokenEnd))
                    if (++tokenLength > AST::maxIdentifierLength)
                        throwError (Errors::identifierTooLong());

                if (auto keyword = matchKeyword (tokenLength, nextCharacter))
                {
                    nextCharacter += tokenLength;
                    return keyword;
                }

                currentTokenText = std::string_view (nextCharacter.data(), tokenLength);
                nextCharacter = CharPointer (tokenEnd);
                return LexerToken::identifier;
            }

            if (firstChar == '-' && isDigit (nextCharacter + 1))
            {
                ++nextCharacter;
                auto literalType = readNumericLiteral (true);

                if (literalType == LexerToken::literalInt32 || literalType == LexerToken::literalInt64)
                    currentIntLiteral = -currentIntLiteral;
                else
                    currentDoubleLiteral = -currentDoubleLiteral;

                return literalType;
            }

            if (firstChar == '.')
                if (readFloat())
                    return currentLiteralType;

            if (readStringLiteral (firstChar))
                return LexerToken::literalString;

            if (auto op = matchOperator (nextCharacter))
                return op;

            if (firstChar == '_' && isIdentifierBody (*(nextCharacter.data() + 1)))
                throwError (Errors::noLeadingUnderscoreAllowed());

            if (! nextCharacter.empty())
                throwError (Errors::illegalCharacter (std::string (nextCharacter.data(), (nextCharacter + 1).data())));

            return LexerToken::eof;
        }
    }

    static LexerTokenType matchKeyword (size_t len, CharPointer p) noexcept
    {
        #define CMAJ_COMPARE_KEYWORD(name) if (len == sizeof (#name) - 1 && p.startsWith (#name)) return LexerToken::keyword_ ## name;
        CMAJ_KEYWORD_LIST (CMAJ_COMPARE_KEYWORD)
        #undef CMAJ_COMPARE_KEYWORD
        return {};
    }

    static LexerTokenType matchOperator (CharPointer& text) noexcept
    {
        auto p = text;
        #define CMAJ_COMPARE_OPERATOR(str, name) if (p.startsWith (str)) { text = p + (sizeof (str) - 1); return LexerToken::operator_ ## name; }
        CMAJ_OPERATOR_LIST (CMAJ_COMPARE_OPERATOR)
        #undef CMAJ_COMPARE_OPERATOR
        return {};
    }

    static constexpr bool isIdentifierStart (char c) noexcept  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
    static constexpr bool isIdentifierBody  (char c) noexcept  { return isIdentifierStart (c) || (c >= '0' && c <= '9') || c == '_'; }

    void skipWhitespaceAndComments()
    {
        for (auto t = nextCharacter;;)
        {
            t = t.findEndOfWhitespace();
            auto comment = t;

            if (comment.skipIfStartsWith ('/'))
            {
                auto start = t;

                if (comment.skipIfStartsWith ('/'))
                {
                    t = comment.find ("\n");

                    if (! isPreviousCommentConnected (start))
                        previousCommentStart = start;

                    previousCommentEnd = t;
                    continue;
                }

                if (comment.skipIfStartsWith ('*'))
                {
                    location.text = t;
                    comment = comment.find ("*/");
                    if (comment.empty()) throwError (Errors::unterminatedComment());
                    t = comment + 2;
                    previousCommentStart = start;
                    previousCommentEnd = t;
                    continue;
                }
            }

            nextCharacter = t;
            return;
        }
    }

    bool isPreviousCommentConnected (CharPointer nextCommentStart) const
    {
        if (previousCommentStart == nullptr || previousCommentStart >= nextCommentStart)
            return false;

        return choc::text::startsWith (getCurrentComment().toString(), "//")
                && choc::text::trim (CodeLocationRange { previousCommentEnd, nextCommentStart }.toString()).empty();
    }

    LexerTokenType readNumericLiteral (bool isNegative)
    {
        auto t = nextCharacter;

        if (skipIfStartsWith (t, "0x", "0X"))
        {
            readInteger<16> (t);
        }
        else if (readFloat())
        {
            return currentLiteralType;
        }
        else if (*t == '0' && isDigit (t + 1))
        {
            throwError (Errors::noOctalLiterals());
        }
        else if (skipIfStartsWith (t, "0b", "0B"))
        {
            readInteger<2> (t);
        }
        else
        {
            readInteger<10> (t);
        }

        return checkIntLiteralRange (isNegative);
    }

    LexerTokenType checkIntLiteralRange (bool isNegative)
    {
        if (currentLiteralType == LexerToken::literalInt32)
        {
            if (isNegative && currentIntLiteral > -static_cast<int64_t> (std::numeric_limits<int32_t>::min()))
                throwError (Errors::integerLiteralNeedsSuffix());

            if (! isNegative && currentIntLiteral > static_cast<int64_t> (std::numeric_limits<int32_t>::max()))
                throwError (Errors::integerLiteralNeedsSuffix());
        }

        return currentLiteralType;
    }

    template <int base>
    void readInteger (CharPointer t)
    {
        uint64_t value = 0;
        bool anyDigits = false;

        for (;;)
        {
            int d;

            if constexpr (base == 16)
            {
                d = choc::text::hexDigitToInt (*t);
            }
            else
            {
                d = (int) (*t - '0');

                if (d >= base)
                    break;
            }

            if (d < 0)
                break;

            auto digitValue = (uint64_t) d;

            if (value <= std::numeric_limits<uint64_t>::max() / base)
            {
                value *= base;

                if (value <= std::numeric_limits<uint64_t>::max() - digitValue)
                {
                    value += digitValue;
                    anyDigits = true;
                    ++t;
                    continue;
                }
            }

            throwError (Errors::integerLiteralTooLarge());
        }

        if (! anyDigits)
            throwError (Errors::errorInNumericLiteral());

        nextCharacter = t;
        currentIntLiteral = (int64_t) value;
        currentLiteralType = readIntLiteralSuffix();
        throwErrorIfInvalidLiteralSuffix (false);
    }

    LexerTokenType readIntLiteralSuffix()
    {
        if (skipIfStartsWith (nextCharacter, "i32", "_i32"))              return LexerToken::literalInt32;
        if (skipIfStartsWith (nextCharacter, "i64", "_i64", "L", "_L"))   return LexerToken::literalInt64;

        return LexerToken::literalInt32;
    }

    static bool skipDigits (CharPointer& t)
    {
        if (! isDigit (t))
            return false;

        while (isDigit (++t)) {}
        return true;
    }

    bool readFloat()
    {
        auto t = nextCharacter;
        bool anyDigits = skipDigits (t);
        bool hasDecimalPoint = t.skipIfStartsWith ('.');

        if (hasDecimalPoint)
            anyDigits = skipDigits (t) || anyDigits;

        if (! anyDigits)
            return false;

        if (t.skipIfStartsWith ('e') || t.skipIfStartsWith ('E'))
        {
            if (! t.skipIfStartsWith ('-'))
                t.skipIfStartsWith ('+');

            if (! skipDigits (t))
                return false;
        }
        else if (! hasDecimalPoint)
        {
            return false;
        }

        currentDoubleLiteral = std::stod (std::string (nextCharacter.data(), t.data()));
        nextCharacter = t;
        currentLiteralType = readFloatLiteralSuffix();
        throwErrorIfInvalidLiteralSuffix (true);
        return true;
    }

    LexerTokenType readFloatLiteralSuffix()
    {
        if (skipIfStartsWith (nextCharacter, "f32i", "_f32i", "fi"))    return LexerToken::literalImag32;
        if (skipIfStartsWith (nextCharacter, "f64i", "_f64i", "i"))     return LexerToken::literalImag64;
        if (skipIfStartsWith (nextCharacter, "f64", "_f64"))            return LexerToken::literalFloat64;
        if (skipIfStartsWith (nextCharacter, "f32", "_f32", "f", "_f")) return LexerToken::literalFloat32;

        return LexerToken::literalFloat64;
    }

    void throwErrorIfInvalidLiteralSuffix (bool wasFloatLiteral)
    {
        if (isIdentifierBody (*nextCharacter.data()) || isDigit (nextCharacter))
        {
            location.text = nextCharacter;

            if (! wasFloatLiteral && (nextCharacter.startsWith ("i") || nextCharacter.startsWith ("I")))
                throwError (Errors::complexNumberSuffixNeedsFloat());

            throwError (Errors::unrecognisedLiteralSuffix());
        }
    }

    bool readStringLiteral (const char quoteType)
    {
        if (quoteType != '"' && quoteType != '\'')
            return false;

        ++nextCharacter;
        escapedStringLiteral = {};

        for (;;)
        {
            auto nextChar = nextCharacter.popFirstChar();

            if (nextChar == static_cast<Char> (quoteType))
            {
                throwErrorIfInvalidLiteralSuffix (false);
                return true;
            }

            if (nextChar == 0)
                throwError (Errors::endOfInputInStringConstant());

            nextChar = readNextStringLiteralChar (nextChar);
            choc::text::appendUTF8 (escapedStringLiteral, nextChar);
        }
    }

    Char readNextStringLiteralChar (Char c)
    {
        if (c == '\\')
        {
            c = nextCharacter.popFirstChar();

            switch (c)
            {
                case 'a':  return '\a';
                case 'b':  return '\b';
                case 'f':  return '\f';
                case 'n':  return '\n';
                case 'r':  return '\r';
                case 't':  return '\t';

                case 'u':
                {
                    uint32_t codePoint = 0;

                    for (uint32_t i = 0; i < 4; ++i)
                        codePoint = (codePoint << 4) + getNextHexDigit();

                    if (codePoint == 0)
                        break;

                    return static_cast<Char> (codePoint);
                }

                default:
                    break;
            }
        }

        return c;
    }

    uint32_t getNextHexDigit()
    {
        uint32_t c = nextCharacter.popFirstChar();
        auto d1 = c -  static_cast<uint32_t> ('0');         if (d1 < 10u)  return d1;
        auto d2 = d1 + static_cast<uint32_t> ('0' - 'a');   if (d2 < 6u)   return d2 + 10;
        auto d3 = d2 + static_cast<uint32_t> ('a' - 'A');   if (d3 < 6u)   return d3 + 10;

        location.text = --nextCharacter;
        throwError (Errors::errorInEscapeCode());
    }
};

} // namespace cmaj
