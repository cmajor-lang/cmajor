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

#include <string>

namespace cmaj::cpp_utils
{
    template <typename Output>
    void escapeString (Output& out, std::string_view utf8)
    {
        bool avoidHexDigit = false;
        char lastChar = 0;

        for (auto c : utf8)
        {
            switch (c)
            {
                case '\t':   out << "\\t";  break;
                case '\n':   out << "\\n";  break;
                case '\r':   out << "\\r";  break;
                case '\"':   out << "\\\""; break;
                case '\\':   out << "\\\\"; break;

                // NB: two consecutive unescaped question-marks would form a trigraph
                case '?':    out << (lastChar == '?' ? "\\?" : "?"); break;

                default:
                    if (c >= 32 && c < 127)
                    {
                        if (avoidHexDigit && choc::text::hexDigitToInt (static_cast<uint32_t> (c)) >= 0)
                            out << "\"\"";

                        out << c;
                        break;
                    }

                    out << (c < 16 ? "\\x0" : "\\x") << choc::text::createHexString (c);
                    avoidHexDigit = true;
                    continue;
            }

            avoidHexDigit = false;
            lastChar = c;
        }
    }

    static constexpr size_t maxStringLiteralSize = 2040;

    inline std::string_view::size_type getSplitPoint (std::string_view text)
    {
        auto split = text.substr (0, maxStringLiteralSize).rfind ('\n');
        return split != std::string_view::npos && split > maxStringLiteralSize / 2 ? split : maxStringLiteralSize;
    }

    inline std::string createStringLiteral (std::string_view text)
    {
        if (text.length() > maxStringLiteralSize)
        {
            auto split = getSplitPoint (text);
            return createStringLiteral (text.substr (0, split)) + "\n" + cpp_utils::createStringLiteral (text.substr (split));
        }

        std::ostringstream s;
        s << '\"';
        escapeString (s, text);
        s << '\"';
        return s.str();
    }

    inline std::string createRawStringLiteral (std::string_view text, std::string guard = {})
    {
        if (text.length() > maxStringLiteralSize)
        {
            auto split = getSplitPoint (text);
            return createRawStringLiteral (text.substr (0, split), guard) + "\n" + cpp_utils::createRawStringLiteral (text.substr (split), guard);
        }

        if (choc::text::contains (text, ")" + guard + "\""))
        {
            if (guard.empty())
                guard = "text";

            for (int i = 0;; ++i)
            {
                if (! choc::text::contains (text, guard + std::to_string (i)))
                {
                    guard += std::to_string (i);
                    break;
                }
            }
        }

        std::ostringstream s;
        s << "R\"" << guard << "(" << text << ")" << guard << "\"";
        return s.str();
    }

    inline std::string createDataLiteral (std::string_view data)
    {
        std::ostringstream result;
        std::string currentLine;
        bool firstInLine = true;

        for (auto c : data)
        {
            if (firstInLine)
            {
                firstInLine = false;
            }
            else if (currentLine.length() > 200)
            {
                result << currentLine << ",\n";
                currentLine = "        ";
            }
            else
            {
                currentLine += ",";
            }

            if (static_cast<signed char> (c) < 0)
                currentLine += "(char)";

            currentLine += std::to_string (static_cast<signed char> (c));
        }

        result << currentLine;
        return result.str();
    }

    inline std::string makeSafeIdentifier (std::string_view name)
    {
        constexpr static std::string_view cppKeywords[] =
        {
            "alignas", "alignof", "and", "and_eq", "asm", "__asm", "atomic_cancel", "atomic_commit", "atomic_noexcept",
            "auto", "bitand", "bitor", "bool", "break", "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class",
            "compl", "concept", "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
            "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
            "extern", "false", "final", "float", "for", "friend", "goto", "if", "import", "inline", "int", "long", "module",
            "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "override",
            "private", "protected", "public", "reflexpr", "register", "reinterpret_cast", "requires", "return", "short",
            "signed", "sizeof", "static", "static_assert", "static_cast", "std", "struct", "switch", "synchronized", "template",
            "this", "thread_local", "throw", "transaction_safe", "transaction_safe_dynamic", "true", "try", "typedef", "typeid",
            "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq",

            // these are used in the generated boilerplate
            "name", "numInputEndpoints", "numOutputEndpoints", "numAudioInputChannels", "numAudioOutputChannels",
            "inputEndpoints", "outputEndpoints", "outputAudioStreams", "outputEvents", "outputMIDIEvents", "inputAudioStreams",
            "inputEvents", "inputMIDIEvents", "inputParameters",
            "StringHandle", "Null", "SizeType", "IndexType", "EndpointType", "EndpointInfo",
            "Array", "Vector", "Slice", "intrinsics",
        };

        auto result = makeSafeIdentifierName (std::string (name));

        for (auto keyword : cppKeywords)
            if (result == keyword)
                return result + "_";

        return result;
    }
}
