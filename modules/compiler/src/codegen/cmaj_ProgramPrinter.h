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

#include "cmaj_CodeGenHelpers.h"

namespace cmaj
{

//==============================================================================
struct ProgramPrinter  : public SourceCodeFormattingHelper
{
    ProgramPrinter (AST::PrintOptionFlags flags)
       : SourceCodeFormattingHelper (flags),
         skipAliases ((flags & AST::PrintOptionFlags::skipAliases) != 0)
    {}

    ~ProgramPrinter() = default;

    std::string print (const AST::Program& program)
    {
        reset();

        for (auto& m : program.getTopLevelModules())
            printModule (m);

        return out.toString();
    }

    std::string printObjectOrExpression (const AST::Object& o)
    {
        reset();

        if (auto e = o.getAsExpression())
            return formatExpression (*e).getWithParensIfNeeded();

        printObject (o);
        return out.toString();
    }

    std::string printFunctionCallDescription (const AST::FunctionCall& fc)    { return printFunctionCallWithTypes (fc); }
    std::string printFunctionCallDescription (const AST::CallOrCast& cc)      { return printFunctionCallWithTypes (cc); }

    std::string printFunctionDeclaration (const AST::Function& f)             { printFunction (f, false); return out.toString(); }

    static std::string formatBool (bool b)                                 { return b ? "true" : "false"; }
    static std::string formatInt32 (int32_t n)                             { return (n > 0xffff ? "0x" + choc::text::createHexString (n) : std::to_string (n)); }
    static std::string formatInt64 (int64_t n, std::string_view suffix)    { return (n > 0xffff ? "0x" + choc::text::createHexString (n) : std::to_string (n)) + std::string (suffix); }

    static std::string formatFloat (float n)
    {
        if (n == 0)             return "0.0f";
        if (std::isnan (n))     return "_nan32";
        if (std::isinf (n))     return n > 0 ? "_inf32" : "_ninf32";

        return choc::text::floatToString (n) + "f";
    }

    static std::string formatFloat (double n)
    {
        if (n == 0)             return "0.0";
        if (std::isnan (n))     return "_nan64";
        if (std::isinf (n))     return n > 0 ? "_inf64" : "_ninf64";

        return choc::text::floatToString (n);
    }

    template <typename StringList>
    static std::string createParenthesisedList (const StringList& args)
    {
        if (args.empty())
            return "()";

        std::string list (" (");
        bool first = true;

        for (auto& arg : args)
        {
            if (first)
                first = false;
            else
                list += ", ";

            list += arg;
        }

        return list + ")";
    }

    ExpressionTokenList formatExpression (const AST::Object& o)
    {
        if (auto r = o.getAsNamedReference())
        {
            if (! skipAliases)
            {
                auto name = getQualifiedNameForObject (*r);

                if (! name.empty())
                    return ExpressionTokenList().addIdentifier (name, r->getTarget());
            }

            return formatExpression (r->getTarget());
        }

        if (auto v = o.getAsValueBase())   return getValueExpression (*v);
        if (auto t = o.getAsTypeBase())    return getTypeExpression (*t);
        if (auto i = o.getAsIdentifier())  return ExpressionTokenList().addIdentifier (std::string (i->name.get()));

        if (auto d = o.getAsDotOperator())
            return formatExpression (d->lhs).addParensIfNeeded()
                    .addPunctuation (".")
                    .add (formatExpression (d->rhs).addParensIfNeeded());

        if (auto op = o.getAsInPlaceOperator())
            return formatExpression (op->target)
                    .addPunctuation (" " + std::string (op->getSymbol()) + "= ")
                    .add (formatExpression (op->source))
                    .setParensNeeded();

        if (auto a = o.getAsAssignment())
            return formatExpression (a->target).addParensIfNeeded()
                    .addPunctuation (" = ")
                    .add (formatExpression (a->source).addParensIfNeeded())
                    .setParensNeeded();

        if (auto b = o.getAsBracketedSuffix())          return formatExpression (b->parent).addParensIfNeeded().add (formatBracketedSuffixTerms (b->terms));
        if (auto c = o.getAsChevronedSuffix())          return formatExpression (c->parent).addParensIfNeeded().add (formatChevronedSuffixTerms (c->terms));

        if (auto l = o.getAsExpressionList())           return formatExpressionList (l->getExpressions()).setParensNeeded();
        if (auto n = o.getAsNamespaceSeparator())       return formatExpression (n->lhs).addPunctuation ("::").add (formatExpression (n->rhs));
        if (auto c = o.getAsCallOrCast())               return formatExpression (c->functionOrType).add (formatExpression (c->arguments).addParensAlways());
        if (auto i = o.getAsEndpointInstance())         return getEndpointInstance (*i);
        if (auto m = o.getAsModuleBase())               return ExpressionTokenList().addIdentifier (getQualifiedNameForObject (*m), *m);
        if (auto p = o.getAsGraphNode())                return ExpressionTokenList().addIdentifier (getQualifiedNameForObject (*p), *p);
        if (auto t = o.getAsAlias())                    return skipAliases ? formatExpression (t->target) : ExpressionTokenList().addIdentifier (getQualifiedNameForObject (*t), *t);
        if (auto v = o.getAsVariableDeclaration())      return getVariableDeclarationDescription (*v);
        if (auto w = o.getAsWriteToEndpoint())          return getWriteToEndpointDescription (*w);
        if (auto e = o.getAsEndpointDeclaration())      return ExpressionTokenList().addIdentifier (std::string (e->getName()), *e);
        if (auto h = o.getAsHoistedEndpointPath())      return getHoistedEndpointPath (*h);
        if (auto c = o.getAsConnection())               return getConnection (*c, false);
        if (o.isAdvance())                              return ExpressionTokenList().addKeyword ("advance").addPunctuation ("()");

        auto fn = o.getAsFunction();
        CMAJ_ASSERT (fn != nullptr);
        return ExpressionTokenList().addIdentifier (getQualifiedNameForObject (*fn), *fn);
    }

    ExpressionTokenList formatFunctionDeclaration (const AST::Function& f)
    {
        auto result = (f.isEventHandler ? ExpressionTokenList().addKeyword ("event")
                                        : formatExpression (f.returnType))
                        .addPunctuation (" ")
                        .addIdentifier (std::string (f.getName()));

        if (f.isGenericOrParameterised())
            result = std::move (result)
                       .addPunctuation ("<")
                       .add (formatExpressionList (f.genericWildcards.getAsObjectList()))
                       .addPunctuation (">");

        if (! f.parameters.empty())
            result = std::move (result).addPunctuation (" ");

        return std::move (result)
                .add (formatExpressionList (f.parameters.getAsObjectList()).addParensAlways())
                .add (formatAnnotation (f.annotation, true));
    }

    //==============================================================================
    template <typename Type>
    static ExpressionTokenList formatComplex (std::complex<Type> c)
    {
        auto r = ExpressionTokenList().addLiteral (formatFloat (c.real()));
        auto i = ExpressionTokenList().addLiteral (formatFloat (c.imag())).addIdentifier ("i");

        if (c.imag() == 0) return r;
        if (c.real() == 0) return i;

        return std::move (i).addPunctuation (" + ").add (std::move (r)).setParensNeeded();
    }

    template <typename List>
    ExpressionTokenList formatExpressionList (List&& list)
    {
        ExpressionTokenList result;
        bool isFirst = true;

        for (auto& item : list)
        {
            if (isFirst)
                isFirst = false;
            else
                result = std::move (result).addPunctuation (", ");

            result = std::move (result).add (formatExpression (item));
        }

        return result;
    }

    ExpressionTokenList formatBracketedValue (const AST::Object& value)
    {
        return ExpressionTokenList()
                .addPunctuation ("[")
                .add (formatExpression (value))
                .addPunctuation ("]");
    }

    ExpressionTokenList formatBracketedValue (const AST::ObjectProperty& value)
    {
        if (auto o = value.getObject())
            return formatBracketedValue (*o);

        return ExpressionTokenList().addPunctuation ("[]");
    }

    ExpressionTokenList formatBracketedSuffixTerms (const AST::ListProperty& terms)
    {
        if (terms.empty())
            return ExpressionTokenList().addPunctuation ("[]");

        auto result = ExpressionTokenList().addPunctuation ("[");
        bool isFirst = true;

        for (auto& t : terms)
        {
            auto& term = AST::castToRef<AST::BracketedSuffixTerm> (t);

            if (isFirst)
                isFirst = false;
            else
                result = std::move (result).addPunctuation (", ");

            if (term.isRange)
                result = std::move (result).add (formatExpression (term.startIndex))
                                           .addPunctuation (":")
                                           .add (formatExpression (term.endIndex));
            else
                result = std::move (result).add (formatExpression (term.startIndex));
        }

        return std::move (result).addPunctuation ("]");
    }

    ExpressionTokenList formatChevronedSuffixTerms (const AST::ListProperty& terms)
    {
        if (terms.empty())
            return ExpressionTokenList().addPunctuation ("<>");

        auto result = ExpressionTokenList().addPunctuation ("<");
        bool isFirst = true;

        for (auto& term : terms)
        {
            if (isFirst)
                isFirst = false;
            else
                result = std::move (result).addPunctuation (", ");

            result = std::move (result).add (formatExpression (term));
        }

        return std::move (result).addPunctuation (">");
    }

    ExpressionTokenList formatRangePair (const AST::ObjectProperty& start,
                                         const AST::ObjectProperty& end)
    {
        auto s = start.getObject();
        auto e = end.getObject();

        return formatRangePair (s != nullptr ? formatExpression (*s) : ExpressionTokenList(),
                                e != nullptr ? formatExpression (*e) : ExpressionTokenList());
    }

    ExpressionTokenList formatRangePair (ExpressionTokenList&& start,
                                         ExpressionTokenList&& end)
    {
        if (start.empty() && end.empty())
            return ExpressionTokenList().addPunctuation ("[]");

        return ExpressionTokenList()
                .addPunctuation ("[")
                .add (std::move (start))
                .addPunctuation (":")
                .add (std::move (end))
                .addPunctuation ("]");
    }

    ExpressionTokenList formatList (const AST::ListProperty& items, std::string_view separator)
    {
        ExpressionTokenList result;
        bool isFirst = true;

        for (auto& item : items)
        {
            if (isFirst)
                isFirst = false;
            else
                result = std::move (result).addPunctuation (std::string (separator));

            result = std::move (result).add (formatExpression (item));
        }

        return std::move (result).setParensNeeded();
    }

private:
    bool skipAliases;

    void printObject (const AST::Object& o)
    {
        if (auto e = o.getAsExpression())            { out << formatExpression (*e).getWithoutParens(); return; }
        if (auto s = o.getAsStatement())             return printStatement (*s);
        if (auto t = o.getAsAlias())                 return printAlias (*t, true);
        if (auto f = o.getAsFunction())              return printFunction (*f, true);
        if (auto m = o.getAsModuleBase())            return printModule (*m);
        if (auto e = o.getAsEndpointDeclaration())   return printEndpoint (*e);
        if (auto c = o.getAsConnection())            return printConnection (*c, true);
        if (auto p = o.getAsGraphNode())             return printGraphNode (*p);
        if (auto a = o.getAsAnnotation())            return printAnnotation (*a, false);
        if (auto h = o.getAsHoistedEndpointPath())   { out << getHoistedEndpointPath (*h).getWithoutParens(); return; }

        CMAJ_ASSERT_FALSE;
    }

    void printModule (const AST::ModuleBase& m)
    {
        out << choc::text::CodePrinter::SectionBreak()
            << m.getModuleType() << " " << m.getName().get();

        printSpecialisationParams (m.specialisationParams.getAsObjectList());
        printAnnotation (m.annotation, true);

        out << newLine;

        {
            auto indent = out.createIndentWithBraces();

            if (auto p = m.getAsProcessorBase())
            {
                for (auto& item : p->endpoints)
                    printEndpoint (AST::castToRef<AST::EndpointDeclaration> (item));

                for (auto& item : p->stateVariables)
                    printVariableDeclaration (AST::castToVariableDeclarationRef (item), true);

                out << blankLine;
            }

            if (auto g = m.getAsGraph())
            {
                for (auto node : g->nodes)
                    printGraphNode (AST::castToRef<AST::GraphNode> (node));

                out << blankLine;

                printConnections (g->connections);

                out << blankLine;
            }

            if (auto ns = m.getAsNamespace())
            {
                for (auto& item : ns->imports)
                    out << "import " << item->toStdString() << newLine;

                out << blankLine;

                for (auto item : ns->constants)
                    printVariableDeclaration (AST::castToVariableDeclarationRef (item), true);

                out << blankLine;
            }

            if (! m.aliases.empty())
            {
                for (auto& item : m.aliases)
                    printAlias (AST::castToRef<AST::Alias> (item), true);

                out << blankLine;
            }

            if (! m.staticAssertions.empty())
            {
                for (auto& item : m.staticAssertions)
                    printStatement (AST::castToRef<AST::Statement> (item));

                out << blankLine;
            }

            if (! m.structures.empty())
            {
                for (auto& item : m.structures)
                    printStructure (AST::castToRef<AST::StructType> (item));

                out << blankLine;
            }

            if (auto p = m.getAsProcessor())
            {
                out << blankLine;

                if (auto l = p->latency.getObject())
                    out << "processor.latency = " << formatExpression (*l).getWithoutParens() << blankLine;
            }

            if (! m.functions.empty())
            {
                for (auto& f : m.functions.iterateAs<AST::Function>())
                    printFunction (f, true);

                out << blankLine;
            }

            if (auto ns = m.getAsNamespace())
            {
                for (auto& sub : ns->subModules.iterateAs<AST::ModuleBase>())
                    printModule (sub);
            }
        }

        out << blankLine;
    }

    void printStatement (const AST::Statement& s)
    {
        if (auto b = s.getAsScopeBlock())
        {
            {
                printLabel (b->label);
                auto indent = out.createIndentWithBraces();

                for (auto& statement : b->statements)
                    printStatement (AST::castToRef<AST::Statement> (statement));
            }

            out << newLine;
            return;
        }

        if (auto i = s.getAsIfStatement())
        {
            out << blankLine
                << "if " << formatExpression (i->condition).getWithParensAlways() << newLine;

            printBlock (i->trueBranch);

            if (auto b = i->falseBranch.getObject())
            {
                out << "else" << newLine;
                printBlock (*b);
            }

            return;
        }

        if (auto r = s.getAsReturnStatement())
        {
            out << "return";

            if (auto value = r->value.getObject())
                out << " " << formatExpression (*value).getWithParensIfNeeded();

            out << ";" << newLine;
            return;
        }

        if (auto b = s.getAsBreakStatement())
            return printBreakOrContinue ("break", b->targetBlock);

        if (auto c = s.getAsContinueStatement())
            return printBreakOrContinue ("continue", c->targetBlock);

        if (auto loop = s.getAsLoopStatement())
        {
            out << blankLine;

            printLabel (loop->label);

            if (auto count = loop->numIterations.getObject())
            {
                out << ((count->isVariableDeclaration()) ? "for " : "loop ") << formatExpression (*count).getWithParensAlways() << newLine;
            }
            else
            {
                auto cond      = loop->condition.getObject();
                auto iterator  = loop->iterator.getObject();

                if (loop->initialisers.empty() && iterator == nullptr)
                {
                    if (cond != nullptr)
                        out << "while " << formatExpression (*cond).getWithParensAlways() << newLine;
                    else
                        out << "loop" << newLine;
                }
                else
                {
                    out << "for (";

                    if (! loop->initialisers.empty())
                        out << getMultipleVariableDeclarationDescription (loop->initialisers.getAsObjectTypeList<const AST::VariableDeclaration>());

                    out << ";";

                    if (cond != nullptr)
                        out << " " << formatExpression (*cond).getWithoutParens();

                    out << ";";

                    if (iterator != nullptr)
                    {
                        out << " ";
                        printStatementAsExpressionIfPossible (*iterator);
                    }

                    out << ")" << newLine;
                }
            }

            printBlock (loop->body);
            return;
        }

        if (s.getAsAdvance() != nullptr)
        {
            out << "advance();" << newLine;
            return;
        }

        if (s.getAsReset() != nullptr)
        {
            out << "reset();" << newLine;
            return;
        }

        if (auto v = s.getAsVariableDeclaration())
            return printVariableDeclaration (*v, true);

        if (auto w = s.getAsWriteToEndpoint())
        {
            out << getWriteToEndpointDescription (*w).getWithoutParens() << ";" << newLine;
            return;
        }

        if (auto sa = s.getAsStaticAssertion())
        {
            out << "static_assert(" << formatExpression (sa->condition).getWithoutParens();

            auto error = sa->error.get();

            if (! error.empty())
                out << ", " << choc::json::getEscapedQuotedString (error);

            out << ");" << newLine;
            return;
        }

        if (s.getAsNoopStatement() != nullptr)
            return;

        if (auto ex = s.getAsExpression())
        {
            out << formatExpression (*ex).getWithoutParens() << ";" << newLine;
            return;
        }

        if (auto fb = s.getAsForwardBranch())
        {
            out << "forward_branch " << formatExpression (fb->condition).getWithParensAlways() << " -> (";

            bool first = true;

            for (auto i : fb->targetBlocks)
            {
                if (! first)
                    out << ", ";

                out << i->getObject()->getName().get();
                first = false;
            }

            out << ");" << newLine;
            return;
        }

        if (auto a = s.getAsAlias())
        {
            printAlias (*a, true);
            return;
        }


        CMAJ_ASSERT_FALSE;
    }

    void printFunction (const AST::Function& f, bool printDefinition)
    {
        out << formatFunctionDeclaration (f).getWithoutParens();

        if (printDefinition)
        {
            if (auto mainBlock = f.mainBlock.getObject())
            {
                out << newLine;
                printObject (*mainBlock);
            }
            else
            {
                out << ";";
            }

            out << blankLine;
        }
    }

    void printStructure (const AST::StructType& s)
    {
        out << blankLine
            << "struct " << s.name.toStdString() << newLine;

        {
            auto indent = out.createIndentWithBraces();

            for (size_t i = 0; i < s.memberNames.size(); ++i)
                out << formatExpression (s.memberTypes[i]).getWithoutParens() << " " << s.getMemberName (i).get() << ";" << newLine;
        }

        out << blankLine;
    }

    void printBlock (const AST::Object& statement)
    {
        if (auto b = statement.getAsScopeBlock())
            return printStatement (*b);

        {
            auto indent = out.createIndentWithBraces();
            printStatement (AST::castToRef<AST::Statement> (statement));
        }

        out << newLine;
    }

    void printBreakOrContinue (std::string_view statement, const AST::Property& target)
    {
        out << statement;

        if (auto targetObject = target.getObject())
        {
            auto name = targetObject->getName().get();

            if (! name.empty())
                out << " " << name;
        }

        out << ";" << newLine;
    }

    void printLabel (const AST::StringProperty& prop)
    {
        auto name = prop.get().get();

        if (! name.empty())
            out << name << ": ";
    }

    void printAlias (const AST::Alias& t, bool addSemicolonAndNewLine)
    {
        if (skipAliases)
            return printObject (t.target.getObjectRef());

        switch (t.aliasType.get())
        {
            case AST::AliasTypeEnum::Enum::typeAlias:       out << "using "; break;
            case AST::AliasTypeEnum::Enum::namespaceAlias:  out << "namespace "; break;
            case AST::AliasTypeEnum::Enum::processorAlias:  out << "processor "; break;

            default:
                CMAJ_ASSERT_FALSE;
                break;
        }

        out << t.name.toStdString();

        if (t.target != nullptr)
            out << " = " << formatExpression (t.target).getWithoutParens();

        if (addSemicolonAndNewLine)
            out << ";" << newLine;
    }

    void printEndpoint (const AST::EndpointDeclaration& e)
    {
        out << (e.isInput ? "input " : "output ");

        if (auto childPath = e.childPath.getObject())
        {
            printObject (*childPath);
        }
        else
        {
            out << e.endpointType.getEnumString().get() << " ";

            if (e.dataTypes.size() > 1)
                printParenthesisedCommaSeparatedList (e.dataTypes.getAsObjectList());
            else
                printCommaSeparatedList (e.dataTypes);
        }

        out << " " << e.getName().get();

        if (auto arraySize = e.arraySize.getObject())
            out << formatBracketedValue (*arraySize).getWithoutParens();

        printAnnotation (e.annotation, true);
        out << ";" << newLine;
    }

    ExpressionTokenList getHoistedEndpointPath (const AST::HoistedEndpointPath& path)
    {
        ExpressionTokenList result;
        bool isFirst = true;

        for (auto& p : path.pathSections)
        {
            if (isFirst)
                isFirst = false;
            else
                result = std::move (result).addPunctuation (".");

            result = std::move (result).add (formatExpression (p).addParensIfNeeded());
        }

        return result;
    }

    ExpressionTokenList getConnection (const AST::Connection& c, bool addSemicolonAndNewLine)
    {
        ExpressionTokenList result;

        if (auto sourceChain = AST::castTo<AST::Connection> (c.sources[0]))
        {
            result = getConnection (*sourceChain, false);
        }
        else
        {
            result = ExpressionTokenList().addKeyword ("connection ");

            if (c.interpolation.get() != AST::InterpolationTypeEnum::Enum::none)
                result = std::move (result).addPunctuation ("[").addText (std::string (c.interpolation.getEnumString())).addPunctuation ("] ");

            result = std::move (result).add (formatExpressionList (c.sources).setParensNeeded());
        }

        result = std::move (result).addPunctuation (" -> ");

        if (auto delay = c.delayLength.getObject())
            result = std::move (result).add (formatBracketedValue (*delay)).addPunctuation (" -> ");

        result = std::move (result).add (formatExpressionList (c.dests).setParensNeeded());

        if (addSemicolonAndNewLine)
            result = std::move (result).addPunctuation (";\n");

        return result;
    }

    void printConnection (const AST::Connection& c, bool addSemicolonAndNewLine)
    {
        if (auto sourceChain = AST::castTo<AST::Connection> (c.sources[0]))
        {
            printConnection (*sourceChain, false);
        }
        else
        {
            if (c.interpolation.get() != AST::InterpolationTypeEnum::Enum::none)
                out << "[" << std::string (c.interpolation.getEnumString()) << "] ";

            out << formatExpressionList (c.sources).getWithoutParens();
        }

        out << " -> ";

        if (auto delay = c.delayLength.getObject())
          out << formatBracketedValue (*delay).getWithoutParens() << " -> ";

        out << formatExpressionList (c.dests).getWithoutParens();

        if (addSemicolonAndNewLine)
            out << ";" << newLine;
    }

    void printConnections (const AST::ListProperty& connections, bool topLevel = true)
    {
        if (topLevel)
        {
            out << "connection ";
            out << newLine;
        }

        auto indent = out.createIndentWithBraces();

        for (auto item : connections)
        {
            if (auto co = AST::castTo<AST::Connection> (item))
            {
                printConnection (*co, true);
            }
            else if (auto cl = AST::castTo<AST::ConnectionList> (item))
            {
                printConnections (cl->connections, false);
            }
            else if (auto ci = AST::castTo<AST::ConnectionIf> (item))
            {
                out << "if ";
                out << formatExpression (ci->condition).getWithParensAlways();
                out << newLine;
                printConnections (AST::castToRef<AST::ConnectionList> (ci->trueConnections).connections);
                out << newLine;

                if (ci->falseConnections != nullptr)
                {
                    out << "else" << newLine;
                    printConnections (AST::castToRef<AST::ConnectionList> (ci->falseConnections).connections);
                }
            }
        }

        out << newLine;
    }

    void printGraphNode (const AST::GraphNode& node)
    {
        out << "node " << node.getName().get() << " = " << formatExpression (node.processorType).getWithParensIfNeeded();

        if (auto arraySize = node.arraySize.getObject())
            out << formatBracketedValue (*arraySize).getWithoutParens();

        if (auto multiplier = node.clockMultiplierRatio.getObject())
            out << " * " << formatExpression (*multiplier).getWithParensIfNeeded();

        if (auto divider = node.clockDividerRatio.getObject())
            out << " / " << formatExpression (*divider).getWithParensIfNeeded();

        out << ";" << newLine;
    }

    void printStatementAsExpressionIfPossible (const AST::Object& o)
    {
        if (auto e = o.getAsExpression())
        {
            out << formatExpression (*e).getWithoutParens();
            return;
        }

        if (auto b = o.getAsScopeBlock())
            if (b->statements.size() == 1)
                return printStatementAsExpressionIfPossible (b->statements.front().getObjectRef());

        printObject (o);
    }

    ExpressionTokenList getVariableTypeDescription (const AST::VariableDeclaration& v)
    {
        if (v.declaredType != nullptr)
            return formatExpression (v.declaredType.getObjectRef());

        if (v.isConstant)
            return ExpressionTokenList().addKeyword ("let");

        return ExpressionTokenList().addKeyword ("var");
    }

    ExpressionTokenList getVariableDeclarationDescription (const AST::VariableDeclaration& v)
    {
        auto desc = getVariableTypeDescription (v)
                      .addPunctuation (" ")
                      .addIdentifier (std::string (v.getName()));

        if (auto init = v.initialValue.getObject())
            return std::move (desc).addPunctuation (" = ").add (formatExpression (*init));

        return desc;
    }

    std::string getMultipleVariableDeclarationDescription (choc::span<ref<const AST::VariableDeclaration>> variables)
    {
        auto desc = getVariableTypeDescription (variables.front()).getWithParensIfNeeded() + " ";
        bool first = true;

        for (auto& var : variables)
        {
            if (first)
                first = false;
            else
                desc += ", ";

            auto& v = var.get();
            desc += std::string (v.getName());

            if (auto init = v.initialValue.getObject())
                desc += " = " + formatExpression (*init).getWithParensIfNeeded();
        }

        return desc;
    }

    void printVariableDeclaration (const AST::VariableDeclaration& v, bool addSemicolonAndNewLine)
    {
        if (v.isExternal)
            out << "external ";

        out << getVariableDeclarationDescription (v).getWithoutParens();

        printAnnotation (v.annotation, true);

        if (addSemicolonAndNewLine)
            out << ";" << newLine;
    }

    void printAnnotation (const AST::Annotation& a, bool addSpaceBefore)
    {
        out << formatAnnotation (a, addSpaceBefore).getWithoutParens();
    }

    void printAnnotation (const AST::Property& a, bool addSpaceBefore)
    {
        if (auto annotation = AST::castTo<AST::Annotation> (a))
            printAnnotation (*annotation, addSpaceBefore);
    }

    ExpressionTokenList formatAnnotation (const AST::Property& a, bool addSpaceBefore)
    {
        if (auto annotation = AST::castTo<AST::Annotation> (a))
            return formatAnnotation (*annotation, addSpaceBefore);

        return {};
    }

    ExpressionTokenList formatAnnotation (const AST::Annotation& a, bool addSpaceBefore)
    {
        if (auto num = a.names.size())
        {
            if (addSpaceBefore)
                return ExpressionTokenList().addPunctuation ("  ").add (formatAnnotation (a, false));

            auto result = ExpressionTokenList().addPunctuation ( "[[ ");
            bool first = true;

            for (size_t i = 0; i < num; ++i)
            {
                if (first)
                    first = false;
                else
                    result = std::move (result).addPunctuation (", ");

                result = std::move (result)
                           .addIdentifier (std::string (a.names[i].toString()))
                           .addPunctuation (": ")
                           .add (formatExpression (a.values[i]));
            }

            return std::move (result).addPunctuation (" ]]");
        }

        return {};
    }

    void printSpecialisationParams (choc::span<ref<AST::Object>> params)
    {
        if (! params.empty())
        {
            out << " ";
            printParenthesisedCommaSeparatedList (params);
        }
    }

    ExpressionTokenList getWriteToEndpointDescription (const AST::WriteToEndpoint& w)
    {
        auto s = formatExpression (w.target).addParensIfNeeded();

        if (auto index = w.targetIndex.getObject())
            s = std::move (s).add (formatBracketedValue (*index));

        return std::move (s).addPunctuation (" <- ").add (formatExpression (w.value).addParensIfNeeded());
    }

    //==============================================================================
    ExpressionTokenList formatExpression (const AST::Property& p)
    {
        if (auto o = p.getObject())
            return formatExpression (*o);

        return {};
    }

    ExpressionTokenList getValueExpression (const AST::ValueBase& e)
    {
        if (auto v = e.getAsVariableReference())    return ExpressionTokenList().addIdentifier (getQualifiedNameForObject (v->getVariable()), v->getVariable());
        if (auto c = e.getAsConstantInt32())        return ExpressionTokenList().addLiteral (formatInt32 (static_cast<int32_t> (c->value)));
        if (auto c = e.getAsConstantInt64())        return ExpressionTokenList().addLiteral (formatInt64 (static_cast<int64_t> (c->value), "i64"));
        if (auto c = e.getAsConstantFloat32())      return ExpressionTokenList().addLiteral (formatFloat (static_cast<float> (c->value.get())));
        if (auto c = e.getAsConstantFloat64())      return ExpressionTokenList().addLiteral (formatFloat (static_cast<double> (c->value.get())));
        if (auto b = e.getAsConstantBool())         return ExpressionTokenList().addLiteral (formatBool (b->value));
        if (auto s = e.getAsConstantString())       return ExpressionTokenList().addLiteral (choc::json::getEscapedQuotedString (s->value.toString()));
        if (auto c = e.getAsConstantComplex32())    return formatComplex (*c->getAsComplex32());
        if (auto c = e.getAsConstantComplex64())    return formatComplex (*c->getAsComplex64());

        if (auto a = e.getAsConstantAggregate())
            return formatExpression (a->type)
                    .addPunctuation (" ")
                    .add (formatExpressionList (a->values.getAsObjectList()).addParensAlways());

        if (auto c = e.getAsCast())
            return formatExpression (c->targetType)
                    .add (formatExpressionList (c->arguments).addParensAlways());

        if (auto c = e.getAsStateUpcast())
            return ExpressionTokenList().addKeyword ("upcast ")
                    .add (formatExpression (c->targetType))
                    .add (formatExpression (c->argument).addParensAlways());

        if (auto f = e.getAsFunctionCall())
            return formatExpression (f->targetFunction)
                    .add (formatExpressionList (f->arguments.getAsObjectList()).addParensAlways());

        if (auto u = e.getAsUnaryOperator())
            return ExpressionTokenList().addPunctuation (std::string (u->getSymbol()))
                    .add (formatExpression (u->input).addParensIfNeeded());

        if (auto b = e.getAsBinaryOperator())
            return formatExpression (b->lhs).addParensIfNeeded()
                    .addPunctuation (" " + std::string (b->getSymbol())+ " ")
                    .add (formatExpression (b->rhs).addParensIfNeeded())
                    .setParensNeeded();

        if (auto t = e.getAsTernaryOperator())
            return formatExpression (t->condition).addParensIfNeeded()
                    .addPunctuation (" ? ")
                    .add (formatExpression (t->trueValue).addParensIfNeeded())
                    .addPunctuation (" : ")
                    .add (formatExpression (t->falseValue).addParensIfNeeded())
                    .setParensNeeded();

        if (auto p = e.getAsPreOrPostIncOrDec())
        {
            auto op = ExpressionTokenList().addPunctuation (p->isIncrement ? "++" : "--");
            auto target = formatExpression (p->target);

            return (p->isPost ? std::move (target).add (std::move (op))
                              : std::move (op).add (std::move (target))).setParensNeeded();
        }

        if (auto ge = e.getAsGetElement())
        {
            if (ge->isAtFunction)
                return formatExpression (ge->parent).addParensIfNeeded()
                        .addPunctuation (".")
                        .addKeyword ("at")
                        .add (formatExpressionList (ge->indexes).addParensAlways());

            return formatExpression (ge->parent).addParensIfNeeded()
                     .addPunctuation ("[")
                     .add (formatExpressionList (ge->indexes))
                     .addPunctuation ("]");
        }

        if (auto slice = e.getAsGetArrayOrVectorSlice())
            return formatExpression (slice->parent).addParensIfNeeded()
                     .add (formatRangePair (slice->start, slice->end));

        if (auto m = e.getAsGetStructMember())
            return formatExpression (m->object).addParensIfNeeded()
                     .addPunctuation (".")
                     .addIdentifier (std::string (m->member.toString()));

        if (auto en = e.getAsConstantEnum())
            return formatExpression (en->type).addParensIfNeeded()
                     .addPunctuation ("::")
                     .addIdentifier (std::string (en->getEnumItemName()));

        if (auto t = e.getAsValueMetaFunction())
            return ExpressionTokenList().addIdentifier (std::string (t->op.getEnumString()))
                     .add (formatExpressionList (t->arguments.getAsObjectList()).addParensAlways());
            // return formatExpression (t->source).addParensIfNeeded()
            //          .addPunctuation (".")
            //          .addIdentifier (std::string (t->op.getEnumString()));

        if (auto pp = e.getAsProcessorProperty())
            return ExpressionTokenList().addKeyword ("processor")
                    .addPunctuation (".")
                    .addIdentifier (std::string (pp->property.getEnumString()));

        if (auto r = e.getAsReadFromEndpoint())
            return formatExpression (r->endpointInstance);

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ExpressionTokenList getTypeExpression  (const AST::TypeBase& e)
    {
        if (auto p = e.getAsPrimitiveType())     return ExpressionTokenList().addPrimitive (std::string (p->getTypeName()));
        if (auto a = e.getAsArrayType())         return formatExpression (a->getInnermostElementTypeObject()).addParensIfNeeded().addPunctuation ("[").add (formatList (a->dimensionList, ", ")).addPunctuation ("]");
        if (auto v = e.getAsVectorType())        return formatExpression (v->elementType).addParensIfNeeded().addPunctuation ("<").add (formatExpression (v->numElements)).addPunctuation (">");
        if (auto s = e.getAsStructType())        return ExpressionTokenList().addIdentifier (getQualifiedNameForObject (*s), *s);
        if (auto b = e.getAsBoundedType())       return ExpressionTokenList().addKeyword (b->isClamp ? "clamp" : "wrap").addPunctuation ("<").add (formatExpression (b->limit)).addPunctuation (">");
        if (auto r = e.getAsMakeConstOrRef())    return (r->makeConst ? ExpressionTokenList().addKeyword ("const ") : ExpressionTokenList()).add (formatExpression (r->source)).add (r->makeRef ? ExpressionTokenList().addPunctuation ("&") : ExpressionTokenList());
        if (auto t = e.getAsTypeMetaFunction())  return formatExpression (t->source).addParensIfNeeded().addPunctuation (".").addIdentifier (std::string (t->op.getEnumString()));
        if (auto n = e.getAsEnumType())          return ExpressionTokenList().addKeyword ("enum ").addIdentifier (std::string (n->name.toString()), *n);
        CMAJ_ASSERT_FALSE;
        return {};
    }

    ExpressionTokenList getEndpointInstance (const AST::EndpointInstance& e)
    {
        ExpressionTokenList processor, endpoint;

        if (auto node = AST::castTo<AST::GraphNode> (e.node))
            processor = std::move (processor).addIdentifier (std::string (node->getOriginalName()), *node);
        else if (auto unresolvedNode = e.node.getObject())
            processor = formatExpression (*unresolvedNode);

        if (auto endpointRef = AST::castTo<AST::NamedReference> (e.endpoint))
            endpoint = formatExpression (endpointRef->getTarget());
        else if (e.endpoint != nullptr)
            endpoint = formatExpression (e.endpoint);

        if (processor.empty())
            return endpoint;

        if (endpoint.empty())
            return processor;

        return std::move (processor).addParensIfNeeded()
                .addPunctuation (".")
                .add (std::move (endpoint).addParensIfNeeded());
    }

    template <typename CallType>
    static AST::ObjectRefVector<const AST::TypeBase> getArgTypes (const CallType& args)
    {
        AST::ObjectRefVector<const AST::TypeBase> argTypes;

        for (auto& arg : args)
            argTypes.push_back (*AST::castToValueRef (arg).getResultType());

        return argTypes;
    }

    std::string printFunctionCallWithTypes (const AST::FunctionCall& fc)
    {
        std::string methodCall;
        auto argTypes = getArgTypes (fc.arguments);

        return methodCall
                + std::string (AST::castToRef<AST::Function> (fc.targetFunction).getOriginalName())
                + formatExpressionList (argTypes).getWithParensAlways();
    }

    std::string printFunctionCallWithTypes (const AST::CallOrCast& cc)
    {
        std::string name;

        if (auto fn = AST::castTo<AST::Function> (cc.functionOrType))
            name = fn->getOriginalName();
        else
            name = formatExpression (cc.functionOrType).getWithParensIfNeeded();

        return name + formatExpressionList (getArgTypes (cc.arguments.getAsObjectList())).getWithParensAlways();
    }

    //==============================================================================
    template <typename Array>
    void printCommaSeparatedList (Array&& items)
    {
        bool isFirst = true;

        for (auto& i : items)
        {
            if (isFirst)
                isFirst = false;
            else
                out << ", ";

            auto& item = i.get();

            if (auto v = item.getAsVariableDeclaration())        printVariableDeclaration (*v, false);
            else if (auto e = item.getAsExpression())            out << formatExpression (*e).getWithoutParens();
            else if (auto t = item.getAsAlias())                 printAlias (*t, false);
            else if (auto r = item.getAsNamedReference())        out << r->getName().get();
            else if (auto n = item.getAsGraphNode())             out << n->getName().get();
            else                                                 CMAJ_ASSERT_FALSE;
        }
    }

    void printCommaSeparatedList (const AST::ListProperty& items)
    {
        printCommaSeparatedList (items.getAsObjectList());
    }

    template <typename Array>
    void printParenthesisedCommaSeparatedList (Array&& items)
    {
        out << "(";
        printCommaSeparatedList (items);
        out << ")";
    }
};

inline std::string AST::print (const Object& o, PrintOptionFlags flags)   { ProgramPrinter printer (flags); return printer.printObjectOrExpression (o); }
inline std::string AST::print (const Program& p, PrintOptionFlags flags)  { ProgramPrinter printer (flags); return printer.print (p); }
inline std::string AST::print (const Object& o)                           { return print (o, PrintOptionFlags::defaultStyle); }
inline std::string AST::print (const Program& p)                          { return print (p, PrintOptionFlags::defaultStyle); }

inline std::string AST::printFunctionCallDescription (const Object& call, PrintOptionFlags flags)
{
    ProgramPrinter p (flags);

    if (auto fc = call.getAsFunctionCall())
        return p.printFunctionCallDescription (*fc);

    return p.printFunctionCallDescription (*call.getAsCallOrCast());
}

inline std::string AST::printFunctionDeclaration (const Function& f, PrintOptionFlags flags)
{
    ProgramPrinter p (flags);
    return p.printFunctionDeclaration (f);
}


}
