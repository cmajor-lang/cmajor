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
struct RemoveAdvanceCalls  : public AST::NonParameterisedObjectVisitor
{
    using super = AST::NonParameterisedObjectVisitor;
    using super::visit;

    RemoveAdvanceCalls (AST::Allocator& a) : super (a) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    bool useForwardBranch = false;

    std::vector<ref<AST::Advance>> advanceCalls;
    std::vector<ref<AST::ReturnStatement>> returnStatements;

    void visit (AST::Function& f) override
    {
        if (f.isMainFunction())
        {
            advanceCalls.clear();
            returnStatements.clear();
            super::visit (f);

            if (! advanceCalls.empty())
            {
                auto& processor = f.getParentProcessor();
                auto& resumeIndex = AST::createStateVariable (processor, "_resumeIndex", f.context.allocator.createInt32Type(), {});

                if (useForwardBranch)
                    replaceAdvanceCallsUsingForwardBranch (*f.getMainBlock(), resumeIndex);
                else
                    replaceAdvanceCallsUsingConditions (*f.getMainBlock(), resumeIndex);
            }
        }
        else
        {
            f.visitObjectsInScope ([&] (AST::Object& o)
            {
                if (auto advance = AST::castTo<AST::Advance> (o))
                    if (advance->node != nullptr)
                        replaceNodeAdvanceCall (*advance);
            });
        }
    }

    void visit (AST::Advance& advance) override
    {
        if (advance.node != nullptr)
        {
            replaceNodeAdvanceCall (advance);
            return;
        }

        advanceCalls.push_back (advance);
    }

    void visit (AST::ReturnStatement& r) override
    {
        returnStatements.push_back (r);
    }

    //==============================================================================
    void replaceNodeAdvanceCall (AST::Advance& advance)
    {
        auto node              = advance.getNode();
        auto processor         = node->getProcessorType();
        auto nodeName          = node->getName();
        auto mainFunction      = processor->findMainFunction();
        auto advancingFunction = advance.findParentFunction();
        auto& stateParam       = AST::createVariableReference (mainFunction->context, advancingFunction->getParameter (advance.getStrings()._state));
        auto& block            = mainFunction->allocateChild<AST::ScopeBlock>();
        auto ioMemberName      = advance.getStringPool().get (std::string (nodeName.get()) + "_io");
        auto& ioStructMember   = AST::createGetStructMember (block.context, stateParam, ioMemberName);

        ptr<AST::ValueBase> arrayIndex;

        if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (advance.node))
            arrayIndex = AST::castToValue (getElement->getSingleIndexProperty());

        for (auto e : processor->getOutputEndpoints (false))
            if (e->isStream())
                zeroStructElement (block, ioStructMember, e->getName(), node->getArraySize(), arrayIndex);

        addFunctionCall (block, *mainFunction, AST::createGetStructMember (block.context, stateParam, nodeName), ioStructMember, node->getArraySize(), arrayIndex);

        for (auto e : processor->getInputEndpoints (false))
            if (e->isStream())
                zeroStructElement (block, ioStructMember, e->getName(), node->getArraySize(), arrayIndex);

        advance.replaceWith (block);
    }

    void zeroStructElement (AST::ScopeBlock& block, AST::Expression& structMember, AST::PooledString elementName, std::optional<int> arraySize, ptr<AST::ValueBase> arrayIndex)
    {
        if (! arraySize)
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       AST::createGetStructMember (block.context, structMember, elementName),
                                                       block.context.allocator.createConstantInt32 (0)));
            return;
        }

        if (arrayIndex != nullptr)
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       AST::createGetStructMember (block.context,
                                                                                   AST::createGetElement (block.context, structMember, *arrayIndex),
                                                                                   elementName),
                                                       block.context.allocator.createConstantInt32 (0)));

            return;
        }

        for (int i = 0; i < arraySize.value(); i++)
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       AST::createGetStructMember (block.context,
                                                                                   AST::createGetElement (block.context, structMember, i),
                                                                                   elementName),
                                                       block.context.allocator.createConstantInt32 (0)));
        }
    }

    void addFunctionCall (AST::ScopeBlock& block, AST::Function& fn, AST::Expression& state, AST::Expression& io, std::optional<int> arraySize, ptr<AST::ValueBase> arrayIndex)
    {
        if (! arraySize)
        {
            block.addStatement (AST::createFunctionCall (block.context, fn, state, io));
            return;
        }

        if (arrayIndex != nullptr)
        {
            block.addStatement (AST::createFunctionCall (block.context,
                                                         fn,
                                                         AST::createGetElement (block.context, state, *arrayIndex),
                                                         AST::createGetElement (block.context, io, *arrayIndex)));
            return;
        }

        for (int i = 0; i < arraySize.value(); i++)
        {
            block.addStatement (AST::createFunctionCall (block.context,
                                                         fn,
                                                         AST::createGetElement (block.context, state, i),
                                                         AST::createGetElement (block.context, io, i)));
        }
    }

    void replaceAdvanceCallsUsingForwardBranch (AST::ScopeBlock& mainBlock, AST::VariableDeclaration& resumeIndex)
    {
        auto& forwardBranchStatement = mainBlock.allocateChild<AST::ForwardBranch>();

        auto& resumeIndexRef = AST::createVariableReference (mainBlock.context, resumeIndex);
        forwardBranchStatement.condition.referTo (resumeIndexRef);

        // checks whether we need a block at the end to trap calls to the function after a return
        // has been performed. (This must be checked before we replace the advances with returns)
        bool needsHandlerForFunctionReturn = (! returnStatements.empty()
                                                 || ! validation::StatementExitMethods (mainBlock).doesNotExit());

        int32_t advanceIndex = 1;

        for (auto advanceCall : advanceCalls)
        {
            auto& block = AST::castToRef<AST::ScopeBlock> (advanceCall->getParentScope());
            auto insertIndex = block.findIndexOfStatementContaining (advanceCall);

            // If the advance is the last statement in an infinite loop, and does nothing on iteration, make it resume
            // at the start of the loop, not the end..
            if (auto parentLoop = AST::castTo<AST::LoopStatement> (block.getParentScope()))
            {
                if (parentLoop->isInfinite() && parentLoop->iterator == nullptr)
                {
                    if (insertIndex == static_cast<int32_t> (block.statements.size() - 1))
                    {
                        // if we'd be adding a jump that goes straight to the start of the function, then skip it
                        if (advanceIndex == 1 && ! mainBlock.statements.empty() && parentLoop == mainBlock.statements.front().getObjectRef())
                        {
                            block.setStatement (static_cast<size_t> (insertIndex), block.allocateChild<AST::ReturnStatement>()); // replace advance call
                            continue;
                        }

                        setResumeIndexToValue (block, resumeIndexRef, advanceIndex, insertIndex++);
                        block.setStatement (static_cast<size_t> (insertIndex), block.allocateChild<AST::ReturnStatement>()); // replace advance call

                        auto& resumeBlock = createResumeBlock (AST::castToRef<AST::ScopeBlock> (parentLoop->getParentScopeRef()), advanceIndex++);
                        forwardBranchStatement.targetBlocks.addReference (resumeBlock);
                        parentLoop->replaceWith (resumeBlock);
                        resumeBlock.addStatement (*parentLoop);
                        continue;
                    }
                }
            }

            setResumeIndexToValue (block, resumeIndexRef, advanceIndex, insertIndex++);
            AST::addReturnStatement (block, insertIndex++);

            auto& resumeBlock = createResumeBlock (block, advanceIndex++);
            forwardBranchStatement.targetBlocks.addReference (resumeBlock);
            advanceCall->replaceWith (resumeBlock);
        }

        if (needsHandlerForFunctionReturn)
        {
            for (auto returnStatement : returnStatements)
            {
                auto& block = AST::castToRef<AST::ScopeBlock> (returnStatement->getParentScope());
                auto insertIndex = block.findIndexOfStatementContaining (returnStatement);

                setResumeIndexToValue (block, resumeIndexRef, advanceIndex, insertIndex++);
            }

            // Add a return block at the end of the function which always sets us to return here
            {
                auto& resumeBlock = createResumeBlock (mainBlock, advanceIndex);

                setResumeIndexToValue (resumeBlock, resumeIndexRef, advanceIndex);
                mainBlock.addStatement (resumeBlock);
                forwardBranchStatement.targetBlocks.addReference (resumeBlock);
            }
        }

        if (! forwardBranchStatement.targetBlocks.empty())
            mainBlock.addStatement (forwardBranchStatement, 0);
    }

    AST::ScopeBlock& createResumeBlock (AST::ScopeBlock& parent, int32_t resumeIndex)
    {
        auto& resumeBlock = parent.allocateChild<AST::ScopeBlock>();
        resumeBlock.label = resumeBlock.getStringPool().get ("_resume_" + std::to_string (resumeIndex));
        return resumeBlock;
    }

    void setResumeIndexToValue (AST::ScopeBlock& block, AST::VariableReference& resumeIndexRef, int32_t index, int32_t insertIndex = -1)
    {
        AST::addAssignment (block,
                            resumeIndexRef,
                            block.context.allocator.createConstantInt32 (index),
                            insertIndex);
    }

    //==============================================================================
    void replaceAdvanceCallsUsingConditions (AST::ScopeBlock& mainBlock, AST::VariableDeclaration& stateResumeIndex)
    {
        auto& localResumeIndex = AST::createLocalVariable (mainBlock, "_resume",
                                                           AST::createVariableReference (mainBlock.context, stateResumeIndex),
                                                           0);

        std::unordered_set<const AST::IfStatement*> addedIfStatements;
        size_t advanceIndex = 1;

        for (auto& advance : advanceCalls)
        {
            replaceAdvanceCall (advance, stateResumeIndex, localResumeIndex,
                                static_cast<int32_t> (advanceIndex), mainBlock, addedIfStatements,
                                advanceIndex == advanceCalls.size());
            ++advanceIndex;
        }

        // Ensure any return statements, or dropping off the end of the function have correct behaviour
        {
            auto& functionTerminatedIndexValue = mainBlock.context.allocator.createConstantInt32 (-1);
            auto& stateResumeIndexRef = AST::createVariableReference (mainBlock.context, stateResumeIndex);

            for (auto returnStatement : returnStatements)
            {
                auto& block = AST::castToRef<AST::ScopeBlock> (returnStatement->getParentScope());
                auto insertIndex = block.findIndexOfStatementContaining (returnStatement);

                AST::addAssignment (block, stateResumeIndexRef, functionTerminatedIndexValue, insertIndex);
            }

            AST::addAssignment (mainBlock, stateResumeIndexRef, functionTerminatedIndexValue);

            auto& checkComplete = AST::createIfStatement (mainBlock.context,
                                                          AST::createBinaryOp (mainBlock.context,
                                                                               AST::BinaryOpTypeEnum::Enum::equals,
                                                                               AST::createVariableReference (mainBlock.context, localResumeIndex),
                                                                               functionTerminatedIndexValue),
                                                          mainBlock.allocateChild<AST::ReturnStatement>());

            mainBlock.addStatement (checkComplete, 1);
        }
    }

    void replaceAdvanceCall (AST::Advance& advance,
                             AST::VariableDeclaration& stateResumeIndex,
                             AST::VariableDeclaration& localResumeIndex,
                             int32_t advanceNumber, AST::ScopeBlock& mainBlock,
                             std::unordered_set<const AST::IfStatement*>& addedIfStatements,
                             bool isFinalAdvance)
    {
        {
            auto& block = AST::castToRef<AST::ScopeBlock> (advance.getParentScope());
            auto advanceIndex = block.findIndexOfStatementContaining (advance);
            CMAJ_ASSERT (advanceIndex >= 0);

            recursivelyAddConditionToBlocks (localResumeIndex, advanceNumber, mainBlock,
                                             block, static_cast<size_t> (advanceIndex + 1),
                                             addedIfStatements, true, isFinalAdvance);
        }

        auto& block = AST::castToRef<AST::ScopeBlock> (advance.getParentScope());
        auto statementIndex = block.findIndexOfStatementContaining (advance);
        CMAJ_ASSERT (statementIndex >= 0);

        auto& assignNewResumeIndex = block.allocateChild<AST::Assignment>();
        assignNewResumeIndex.target.referTo (AST::createVariableReference (block.context, stateResumeIndex));
        assignNewResumeIndex.source.createConstant (advanceNumber);

        block.setStatement (static_cast<size_t> (statementIndex), assignNewResumeIndex);
        block.addStatement (block.allocateChild<AST::ReturnStatement>(), statementIndex + 1);
    }

    void recursivelyAddConditionToBlocks (AST::VariableDeclaration& resumeIndex,
                                          int32_t advanceNumber, AST::ScopeBlock& mainBlock,
                                          AST::ScopeBlock& block, size_t endIndex,
                                          std::unordered_set<const AST::IfStatement*>& addedIfStatements,
                                          bool blockEndsWithAdvance, bool isFinalAdvance)
    {
        CMAJ_ASSERT (endIndex <= block.statements.size());
        size_t startIndex = std::addressof (mainBlock) == std::addressof (block) ? 1 : 0;

        while (startIndex < endIndex)
        {
            if (auto firstIfStatement = AST::castTo<AST::IfStatement> (block.statements[startIndex]))
            {
                if (addedIfStatements.find (firstIfStatement.get()) != addedIfStatements.end())
                {
                    ++startIndex;
                    continue;
                }
            }

            break;
        }

        if (startIndex < endIndex)
        {
            auto& condition = block.allocateChild<AST::IfStatement>();
            addedIfStatements.insert (std::addressof (condition));

            condition.condition.referTo (AST::createBinaryOp (condition,
                                                              AST::BinaryOpTypeEnum::Enum::lessThan,
                                                              AST::createVariableReference (block.context, resumeIndex),
                                                              block.context.allocator.createConstantInt32 (advanceNumber)));

            auto& conditionBlock = condition.allocateChild<AST::ScopeBlock>();
            condition.trueBranch.referTo (conditionBlock);

            if (blockEndsWithAdvance)
            {
                auto& falseBranch = condition.allocateChild<AST::ScopeBlock>();
                condition.falseBranch.referTo (falseBranch);

                if (isFinalAdvance)
                {
                    falseBranch.addStatement (AST::createAssignment (falseBranch.context,
                                                                     AST::createVariableReference (falseBranch.context, resumeIndex),
                                                                     block.context.allocator.createConstantInt32 (0)));
                }
                else
                {
                    auto& resetIndexCheck = falseBranch.allocateChild<AST::IfStatement>();
                    resetIndexCheck.condition.referTo (AST::createBinaryOp (condition,
                                                                            AST::BinaryOpTypeEnum::Enum::equals,
                                                                            AST::createVariableReference (block.context, resumeIndex),
                                                                            block.context.allocator.createConstantInt32 (advanceNumber)));
                    auto& resetVariableScope = resetIndexCheck.allocateChild<AST::ScopeBlock>();
                    resetIndexCheck.trueBranch.referTo (resetVariableScope);
                    resetVariableScope.addStatement (AST::createAssignment (resetVariableScope.context,
                                                                            AST::createVariableReference (resetVariableScope.context, resumeIndex),
                                                                            block.context.allocator.createConstantInt32 (0)));
                    falseBranch.addStatement (resetIndexCheck);
                }
            }

            auto& firstStatement = AST::castToRef<AST::Statement> (block.statements[startIndex]);
            block.setStatement (startIndex, condition);

            conditionBlock.addStatement (firstStatement);

            for (size_t i = startIndex + 1; i < endIndex; ++i)
                conditionBlock.addStatement (AST::castToRef<AST::Statement> (block.statements[i]));

            block.statements.remove (startIndex + 1, endIndex);
        }

        auto parentScope = block.getParentScope();

        if (AST::castTo<AST::Function> (parentScope) != nullptr)
            return;

        if (auto loop = AST::castTo<AST::LoopStatement> (parentScope))
        {
            auto& loopParent = *loop->findParentOfType<AST::ScopeBlock>();
            auto loopIndex = loopParent.findIndexOfStatementContaining (*loop);

            recursivelyAddConditionToBlocks (resumeIndex, advanceNumber, mainBlock, loopParent,
                                             static_cast<size_t> (loopIndex), addedIfStatements,
                                             false, isFinalAdvance);
        }
        else if (auto ifStatement = AST::castTo<AST::IfStatement> (parentScope))
        {
            if (addedIfStatements.find (ifStatement.get()) == addedIfStatements.end())
            {
                AST::BinaryOpTypeEnum::Enum comparison, logicalOp;

                if (ifStatement->falseBranch.getObject() == block)
                {
                    comparison = AST::BinaryOpTypeEnum::Enum::lessThan;
                    logicalOp = AST::BinaryOpTypeEnum::Enum::logicalAnd;
                }
                else
                {
                    CMAJ_ASSERT (ifStatement->trueBranch.getObject() == block);
                    comparison = AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual;
                    logicalOp = AST::BinaryOpTypeEnum::Enum::logicalOr;
                }

                auto& checkAdvanceNumber = AST::createBinaryOp (ifStatement, comparison,
                                                                AST::createVariableReference (block.context, resumeIndex),
                                                                block.context.allocator.createConstantInt32 (advanceNumber));

                auto& newCondition = AST::createBinaryOp (ifStatement, logicalOp, checkAdvanceNumber,
                                                          AST::castToRef<AST::Expression> (ifStatement->condition));

                ifStatement->condition.referTo (newCondition);
            }

            auto& ifParent = *ifStatement->findParentOfType<AST::ScopeBlock>();
            auto ifIndex = ifParent.findIndexOfStatementContaining (*ifStatement);
            CMAJ_ASSERT (ifIndex >= 0);

            recursivelyAddConditionToBlocks (resumeIndex, advanceNumber, mainBlock, ifParent,
                                             static_cast<size_t> (ifIndex), addedIfStatements,
                                             false, isFinalAdvance);
        }
        else if (auto parentBlock = block.findParentOfType<AST::ScopeBlock>())
        {
            auto blockIndex = parentBlock->findIndexOfStatementContaining (block);
            CMAJ_ASSERT (blockIndex >= 0);

            recursivelyAddConditionToBlocks (resumeIndex, advanceNumber, mainBlock, *parentBlock,
                                             static_cast<size_t> (blockIndex), addedIfStatements,
                                             false, isFinalAdvance);
        }
        else
        {
            CMAJ_ASSERT_FALSE;
        }
    }
};

inline void removeAdvanceCalls (AST::ProcessorBase& processor, bool useForwardBranch)
{
    RemoveAdvanceCalls remover (processor.context.allocator);
    remover.useForwardBranch = useForwardBranch;
    remover.visitObject (processor);
}

}
