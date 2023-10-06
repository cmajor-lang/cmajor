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

#include "choc/text/choc_CodePrinter.h"
#include "choc/platform/choc_HighResolutionSteadyClock.h"

namespace cmaj
{

//==============================================================================
struct SourceCodeFormattingHelper
{
    SourceCodeFormattingHelper (AST::PrintOptionFlags flags)
    {
        if ((flags & AST::PrintOptionFlags::useFullyQualifiedNames) != 0)
            getQualifiedNameForObject = [] (const AST::Object& o) { return o.getFullyQualifiedReadableName(); };
        else
            getQualifiedNameForObject = [] (const AST::Object& o) { return std::string (o.getOriginalName()); };
    }

    enum class ParenStatus
    {
        notNeeded,
        needed,
        present
    };

    struct ExpressionToken
    {
        enum class Type { keyword, primitive, identifier, literal, punctuation, text };

        std::string text;
        Type type;
        ptr<const AST::Object> referencedObject;
    };

    struct ExpressionTokenList
    {
        ExpressionTokenList() = default;

        ExpressionTokenList (ExpressionTokenList&& other)
        {
            if (this != std::addressof (other))
            {
                tokens = std::move (other.tokens);
                parenStatus = other.parenStatus;
            }
        }

        ExpressionTokenList& operator= (ExpressionTokenList&& other)
        {
            if (this != std::addressof (other))
            {
                tokens = std::move (other.tokens);
                parenStatus = other.parenStatus;
            }

            return *this;
        }

        bool empty() const      { return tokens.empty(); }

        ExpressionTokenList&& add (ExpressionToken::Type type, std::string text) &&
        {
            tokens.push_back ({ std::move (text), type, nullptr });
            parenStatus = ParenStatus::notNeeded;
            return std::move (*this);
        }

        ExpressionTokenList&& add (ExpressionToken::Type type, std::string text, ptr<const AST::Object> reference) &&
        {
            tokens.push_back ({ std::move (text), type, reference });
            parenStatus = ParenStatus::notNeeded;
            return std::move (*this);
        }

        ExpressionTokenList&& addKeyword     (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::keyword,     std::move (text)); }
        ExpressionTokenList&& addText        (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::text,        std::move (text)); }
        ExpressionTokenList&& addLiteral     (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::literal,     std::move (text)); }
        ExpressionTokenList&& addPrimitive   (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::primitive,   std::move (text)); }
        ExpressionTokenList&& addPunctuation (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::punctuation, std::move (text)); }
        ExpressionTokenList&& addIdentifier  (std::string text) &&      { return std::move (*this).add (ExpressionToken::Type::identifier,  std::move (text)); }
        ExpressionTokenList&& addIdentifier  (std::string text, const AST::Object& ref) &&      { return std::move (*this).add (ExpressionToken::Type::identifier, std::move (text), ref); }

        ExpressionTokenList&& add (ExpressionTokenList&& other) &&
        {
            tokens.reserve (tokens.size() + other.tokens.size());

            for (auto& t : other.tokens)
                tokens.push_back (std::move (t));

            parenStatus = ParenStatus::notNeeded;
            return std::move (*this);
        }

        ExpressionTokenList&& setParensNeeded() &&
        {
            parenStatus = ParenStatus::needed;
            return std::move (*this);
        }

        std::string getWithoutParens() const
        {
            std::string result;
            result.reserve (length());

            for (auto& t : tokens)
                result += t.text;

            return result;
        }

        std::string getWithParensAlways() const
        {
            if (parenStatus == ParenStatus::present)
                return getWithoutParens();

            std::string result;
            result.reserve (length() + 2);
            result += '(';

            for (auto& t : tokens)
                result += t.text;

            result += ')';
            return result;
        }

        std::string getWithParensIfNeeded() const
        {
            return parenStatus == ParenStatus::needed ? getWithParensAlways()
                                                      : getWithoutParens();
        }

        ExpressionTokenList&& addParensIfNeeded() &&
        {
            if (parenStatus == ParenStatus::needed)
                addParens();

            return std::move (*this);
        }

        ExpressionTokenList&& addParensAlways() &&
        {
            if (parenStatus != ParenStatus::present)
                addParens();

            return std::move (*this);
        }

        size_t length() const
        {
            size_t len = 0;

            for (auto& t : tokens)
                len += t.text.length();

            return len;
        }

        choc::SmallVector<ExpressionToken, 4> tokens;
        ParenStatus parenStatus = ParenStatus::notNeeded;

    private:
        void addParens()
        {
            tokens.insert (tokens.begin(), { "(", ExpressionToken::Type::punctuation, nullptr });
            tokens.push_back ({ ")", ExpressionToken::Type::punctuation, nullptr });
            parenStatus = ParenStatus::present;
        }
    };

    void reset()    { out.reset(); }

    choc::text::CodePrinter out;

    static constexpr choc::text::CodePrinter::NewLine newLine = {};
    static constexpr choc::text::CodePrinter::BlankLine blankLine = {};

    std::function<std::string(const AST::Object&)> getQualifiedNameForObject;
};

//==============================================================================
template <typename MappedType, bool useMemberNamesInSignature>
struct DuckTypedStructMappings
{
    template <typename CreateNewObjectFn>
    MappedType getOrCreate (const AST::StructType& s, CreateNewObjectFn&& createNewObject)
    {
        {
            auto found = mappingsByPointer.find (std::addressof (s));

            if (found != mappingsByPointer.end())
                return found->second;
        }

        auto sig = getSignature (s);

        {
            auto found = mappingsBySignature.find (sig);

            if (found != mappingsBySignature.end())
            {
                mappingsByPointer[std::addressof (s)] = found->second;
                return found->second;
            }
        }

        auto newObject = createNewObject (s);

        mappingsBySignature[sig] = newObject;
        mappingsByPointer[std::addressof (s)] = newObject;
        return newObject;
    }

    static std::string getSignature (const AST::TypeBase& t)
    {
        if constexpr (useMemberNamesInSignature)
        {
            if (auto s = t.getAsStructType())
            {
                auto numMembers = s->memberNames.size();
                auto sig = "struct" + std::string (t.getName()) + std::to_string (numMembers);

                for (size_t i = 0; i < numMembers; ++i)
                    sig += "_" + getSignature (s->getMemberType (i)) + "/" + std::string (s->getMemberName (i));

                return sig;
            }
        }

        return t.getLayoutSignature();
    }

    std::unordered_map<const AST::StructType*, MappedType> mappingsByPointer;
    std::unordered_map<std::string, MappedType> mappingsBySignature;
};

//==============================================================================
struct CompilePerformanceTimes
{
    using Clock = choc::HighResolutionSteadyClock;
    using TimePoint = Clock::time_point;
    using Seconds = std::chrono::duration<double>;

    struct Category
    {
        std::string_view name;
        Seconds result;
    };

    std::vector<Category> categories;

    std::string getResults()
    {
        if (categories.empty())
            return {};

        std::vector<std::string> results;
        Seconds total {};

        for (auto& c : categories)
        {
            results.push_back (std::string (c.name) + ": " + choc::text::getDurationDescription (c.result));
            total += c.result;
        }

        return "Total build time: " + choc::text::getDurationDescription (total) + "\n"
                + choc::text::joinStrings (results, ", ");
    }

    struct PerformanceCounter
    {
        PerformanceCounter (Category& c) : category (c), startTime (Clock::now())
        {}

        ~PerformanceCounter()
        {
            Seconds elapsed = Clock::now() - startTime;
            category.result = elapsed;
        }

        Category& category;
        TimePoint startTime;
    };

    PerformanceCounter getCounter (std::string_view category)
    {
        categories.push_back ({ category, {} });
        return { categories.back() };
    }
};


} // namespace cmaj
