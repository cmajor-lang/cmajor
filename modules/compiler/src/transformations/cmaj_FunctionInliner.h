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

namespace cmaj::transformations
{

//==============================================================================
struct FunctionInliner
{
    using ShouldInlineCall = std::function<bool(const AST::FunctionCall&, const AST::Function&)>;

    static bool inlineMatchingCalls (AST::ScopeBlock& scopeToSearch, const ShouldInlineCall& shouldInline)
    {
        struct FunctionVisitor  : public AST::Visitor
        {
            using super = AST::Visitor;
            using super::visit;

            FunctionVisitor (AST::Allocator& a, const ShouldInlineCall& pred)
                : super (a), predicate (pred) {}

            void visit (AST::FunctionCall& call) override
            {
                // don't call super::visit on the whole thing, as we don't want to follow it to other functions
                super::visitProperty (call.arguments);

                auto& fn = *call.getTargetFunction();

                if (! fn.isIntrinsic() && predicate (call, fn))
                {
                    callsToInline.push_back (std::addressof (call));

                    for (auto& ternary : activeTernaries)
                    {
                        if (ternary != nullptr)
                        {
                            ternariesToConvert.push_back (ternary);
                            ternary = nullptr;
                        }
                    }
                }
            }

            void visit (AST::TernaryOperator& ternary) override
            {
                activeTernaries.push_back (std::addressof (ternary));
                super::visit (ternary);
                activeTernaries.pop_back();
            }

            CMAJ_DO_NOT_VISIT_CONSTANTS

            const ShouldInlineCall& predicate;
            std::vector<AST::TernaryOperator*> activeTernaries, ternariesToConvert;
            std::vector<AST::FunctionCall*> callsToInline;
        };

        FunctionVisitor visitor (scopeToSearch.context.allocator, shouldInline);
        visitor.visitObject (scopeToSearch);

        std::sort (visitor.ternariesToConvert.begin(), visitor.ternariesToConvert.end());
        visitor.ternariesToConvert.erase (std::unique (visitor.ternariesToConvert.begin(), visitor.ternariesToConvert.end()), visitor.ternariesToConvert.end());

        for (auto t : visitor.ternariesToConvert)
            convertTernaryToIfStatement (*t);

        std::vector<AST::PooledString> usedLabelNames;

        for (auto fn : visitor.callsToInline)
            inlineFunctionCall (*fn, usedLabelNames);

        return ! visitor.callsToInline.empty();
    }

private:
    //==============================================================================
    static void inlineFunctionCall (AST::FunctionCall& call, std::vector<AST::PooledString>& usedLabelNames)
    {
        auto& allocator = call.context.allocator;
        auto& function = allocator.createDeepClone (*call.getTargetFunction());
        CMAJ_ASSERT (! function.isGenericOrParameterised());

        ptr<AST::VariableDeclaration> resultVariable;
        auto returnType = AST::castToTypeBase (function.returnType);

        if (returnType != nullptr && ! returnType->isVoid())
        {
            resultVariable = call.allocateChild<AST::VariableDeclaration>();
            resultVariable->name = call.getStringPool().get ("_returnvalue_" + std::string (function.getName()));
            resultVariable->declaredType.referTo (*returnType);
        }

        auto& mainBlock = AST::castToRef<AST::ScopeBlock> (function.mainBlock);

        for (size_t i = 0; i < function.parameters.size(); ++i)
        {
            ptr<AST::ValueBase> arg;

            if (i < call.arguments.size())
                arg = AST::castToValueRef (call.arguments[i]);

            auto& param = function.getParameter(i);

            if (auto ref = param.getType()->getAsMakeConstOrRef())
            {
                if (ref->makeRef)
                {
                    if (auto sourceRef = AST::castToSkippingReferences<AST::VariableReference> (arg))
                    {
                        param.replaceWith (sourceRef->getVariable());
                        continue;
                    }

                    const_cast<AST::MakeConstOrRef*> (ref)->makeRef.set (false);
                }
            }

            mainBlock.addStatement (param, static_cast<int> (i));
            param.variableType = AST::VariableTypeEnum::Enum::local;

            if (arg != nullptr)
                param.initialValue.referTo (*arg);
        }

        std::vector<AST::ReturnStatement*> returns;
        findReturns (returns, mainBlock);

        auto lastStatementInFn = getLastRealStatement (mainBlock);
        bool needToBreakOut = returns.size() > 1 || (returns.size() == 1 && lastStatementInFn == *returns.front());
        replaceReturns (returns, resultVariable, needToBreakOut ? ptr<AST::Object> (mainBlock) : nullptr);

        auto label = mainBlock.getStringPool().get (AST::createUniqueName ("_inlined_" + std::string (function.getName()), usedLabelNames));
        usedLabelNames.push_back (label);
        mainBlock.label = label;

        if (resultVariable != nullptr)
        {
            AST::ParentBlockInsertionPoint insertionPoint (call);
            insertionPoint.insert (*resultVariable);
            insertionPoint.insert (mainBlock);

            call.replaceWith (AST::createVariableReference (call.context, *resultVariable));
        }
        else
        {
            call.replaceWith (mainBlock);
        }

        mainBlock.setParentScope (call.getParentScopeRef());
    }

    //==============================================================================
    static ptr<AST::Statement> getLastRealStatement (AST::ScopeBlock& block)
    {
        for (size_t i = block.statements.size(); i > 0; --i)
        {
            auto& last = block.statements[i - 1].getObjectRef();

            if (auto lastBlock = last.getAsScopeBlock())
            {
                if (auto lastReal = getLastRealStatement (*lastBlock))
                    return lastReal;

                continue; // ignore empty trailing blocks
            }

            if (! last.isDummyStatement())
                return AST::castTo<AST::Statement> (last);
        }

        return {};
    }

    static void findReturns (std::vector<AST::ReturnStatement*>& results, AST::Statement& statement)
    {
        if (auto ret = statement.getAsReturnStatement())
        {
            results.push_back (ret);
            return;
        }

        if (auto block = statement.getAsScopeBlock())
        {
            for (auto& s : block->statements)
                findReturns (results, AST::castToRef<AST::Statement> (s));
        }
        else if (auto loop = statement.getAsLoopStatement())
        {
            for (auto& init : loop->initialisers)
                if (auto s = AST::castTo<AST::Statement> (init))
                    findReturns (results, *s);

            findReturns (results, AST::castToRef<AST::Statement> (loop->body));
        }
        else if (auto ifStatement = statement.getAsIfStatement())
        {
            findReturns (results, AST::castToRef<AST::Statement> (ifStatement->trueBranch));

            if (ifStatement->falseBranch != nullptr)
                findReturns (results, AST::castToRef<AST::Statement> (ifStatement->falseBranch));
        }
    }

    static void replaceReturns (choc::span<AST::ReturnStatement*> returns,
                                ptr<AST::VariableDeclaration> resultVariable,
                                ptr<AST::Object> blockToReturnFrom)
    {
        for (auto ret : returns)
        {
            auto& context = ret->context;
            auto& allocator = context.allocator;

            if (ret->value != nullptr)
            {
                auto& assignResult = AST::createAssignment (context, AST::createVariableReference (context, *resultVariable),
                                                            AST::castToRef<AST::Expression> (ret->value));

                if (blockToReturnFrom != nullptr)
                {
                    AST::ParentBlockInsertionPoint insertionPoint (*ret);
                    insertionPoint.insertionIndex++;
                    insertionPoint.insert (allocator.allocate<AST::BreakStatement> (context, *blockToReturnFrom));
                }

                ret->replaceWith (assignResult);
            }
            else
            {
                if (blockToReturnFrom != nullptr)
                    ret->replaceWith (allocator.allocate<AST::BreakStatement> (context, *blockToReturnFrom));
                else
                    ret->replaceWith (context.allocate<AST::NoopStatement>());
            }
        }
    }

    static void convertTernaryToIfStatement (AST::TernaryOperator& ternary)
    {
        AST::ParentBlockInsertionPoint insertionPoint (ternary);

        auto& context = ternary.context;
        auto& allocator = context.allocator;

        auto& resultType = *ternary.getResultType();
        auto& resultVariable = allocator.allocate<AST::VariableDeclaration> (context);
        resultVariable.name = resultVariable.getStringPool().get ("_ternary");
        resultVariable.declaredType.referTo (resultType);

        insertionPoint.insert (resultVariable);

        auto& ifStatement = allocator.allocate<AST::IfStatement> (context);

        auto createIfBranch = [&] (AST::Object& value) -> AST::ScopeBlock&
        {
            auto& block = ifStatement.allocateChild<AST::ScopeBlock>();
            auto& a = block.allocateChild<AST::Assignment>();
            a.target.setChildObject (AST::createVariableReference (context, resultVariable));
            a.source.setChildObject (value);
            block.addStatement (a);
            return block;
        };

        ifStatement.condition.setChildObject (ternary.condition);
        ifStatement.trueBranch.setChildObject (createIfBranch (ternary.trueValue.getObjectRef()));
        ifStatement.falseBranch.setChildObject (createIfBranch (ternary.falseValue.getObjectRef()));

        insertionPoint.insert (ifStatement);

        ternary.replaceWith (AST::createVariableReference (context, resultVariable));
    }
};

//==============================================================================
static inline bool inlineAllCallsWhichAdvance (const AST::Program& program)
{
    bool anyDone = false;

    program.visitAllModules (true, [&] (AST::ModuleBase& module) -> void
    {
        if (auto processorBase = module.getAsProcessorBase())
        {
            if (auto mainFunction = processorBase->findMainFunction())
            {
                AST::FunctionInfoGenerator functionInfo;
                functionInfo.generate (program);

                while (FunctionInliner::inlineMatchingCalls (AST::castToRef<AST::ScopeBlock> (mainFunction->mainBlock),
                                                             [] (const AST::FunctionCall&, const AST::Function& fn)
                                                             {
                                                                 return AST::FunctionInfoGenerator::getInfo (fn).advanceCall != nullptr;
                                                             }))
                {
                    functionInfo.generate (program);
                    anyDone = true;
                }
            }
        }
    });

    return anyDone;
}


}
