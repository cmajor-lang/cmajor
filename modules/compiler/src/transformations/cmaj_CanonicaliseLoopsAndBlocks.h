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
// There are a few objects that reference statements directly rather than inside a
// ScopeBlock, which makes it tricky for transformations to insert siblings, so
// this re-jigs things to make sure all statements can have others safely inserted
// before and after them.
//
// It also takes all the initialisers, counters and condition statements out of
// LoopStatements, leaving only their iterator statement for the back-end to deal with.
//
inline void canonicaliseLoopsAndBlocks (AST::Program& program)
{
    struct Canonicalise  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        Canonicalise (AST::Program& p) : super (p.allocator), program (p) {}

        void visit (AST::LoopStatement& loop) override
        {
            super::visit (loop);

            AST::ensureStatementIsBlock (loop.iterator);

            auto& body = AST::castToRef<AST::ScopeBlock> (loop.body);

            if (loop.condition != nullptr)
            {
                insertLoopBreakIfNotStatement (loop, body, AST::castToRef<AST::Expression> (loop.condition));
                loop.condition.reset();
            }

            if (loopNeedsOuterBlock (loop))
            {
                auto& outerBlock = createOuterBlockForLoop (loop);

                if (auto numIterations = loop.numIterations.getObject())
                {
                    if (auto var = numIterations->getAsVariableDeclaration())
                        convertCountVariable (outerBlock, body, loop, *var);
                    else
                        createCounterForLoop (outerBlock, body, loop, AST::castToValueRef (numIterations));

                    loop.numIterations.reset();
                }
            }
        }

        void visit (AST::IfStatement& i) override
        {
            super::visit (i);

            AST::ensureStatementIsBlock (i.trueBranch);
            AST::ensureStatementIsBlock (i.falseBranch);
        }

        static bool loopNeedsOuterBlock (AST::LoopStatement& loop)
        {
            return loop.numIterations != nullptr
                    || ! loop.initialisers.empty();
        }

        static AST::ScopeBlock& createOuterBlockForLoop (AST::LoopStatement& loop)
        {
            auto& block = loop.context.allocate<AST::ScopeBlock>();

            for (auto& initialiser : loop.initialisers)
                block.addStatement (AST::castToRef<AST::Statement> (initialiser));

            loop.initialisers.reset();

            AST::ParentBlockInsertionPoint (loop).replace (block);
            block.addStatement (loop);
            return block;
        }

        static void insertLoopBreakIfStatement (AST::LoopStatement& loop, AST::ScopeBlock& body, AST::Expression& breakIfCondition)
        {
            auto& b = body.context.allocate<AST::BreakStatement>();
            b.targetBlock.referTo (loop);
            auto& breakIf = AST::createIfStatement (body.context, breakIfCondition, b);
            body.addStatement (breakIf, 0);
        }

        static void insertLoopBreakIfNotStatement (AST::LoopStatement& loop, AST::ScopeBlock& body, AST::Expression& breakIfNotCondition)
        {
            auto& b = body.context.allocate<AST::BreakStatement>();
            b.targetBlock.referTo (loop);
            auto& breakIf = AST::createIfStatement (body.context, breakIfNotCondition, body.context.allocate<AST::ScopeBlock>(), b);
            body.addStatement (breakIf, 0);
        }

        void convertCountVariable (AST::ScopeBlock& outerBlock, AST::ScopeBlock& body, AST::LoopStatement& loop, AST::VariableDeclaration& countVariable)
        {
            outerBlock.addStatement (countVariable, 0);
            auto& countRef = AST::createVariableReference (outerBlock.context, countVariable);

            auto numIterations = countVariable.getType()->getAsBoundedType()->getBoundedIntLimit();
            auto& numIterationsConst = outerBlock.context.allocator.createConstantInt32 (static_cast<int32_t> (numIterations));

            addWrapFunctions (program, loop);
            countVariable.declaredType.referTo (loop.context.allocator.createInt32Type());
            countVariable.knownRange = { 0, numIterations };

            insertLoopBreakIfStatement (loop, body, AST::createBinaryOp (body, AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual,
                                                                         countRef, numIterationsConst));

            loop.iterator.referTo (AST::createPreInc (loop.context, countRef));
            AST::ensureStatementIsBlock (loop.iterator);
        }

        static void createCounterForLoop (AST::ScopeBlock& outerBlock, AST::ScopeBlock& body, AST::LoopStatement& loop, AST::ValueBase& numIterations)
        {
            auto& allocator = outerBlock.context.allocator;
            auto& loopCount = AST::createLocalVariableRef (outerBlock, "_count", numIterations, 0);
            auto& preDecrementedCount = AST::createPreDec (body.context, loopCount);
            auto& countIsLessThanZero = AST::createBinaryOp (body, AST::BinaryOpTypeEnum::Enum::lessThan,
                                                             preDecrementedCount, allocator.createConstantInt32 (0));
            insertLoopBreakIfStatement (loop, body, countIsLessThanZero);
        }

        AST::Program& program;
    };

    Canonicalise (program).visitObject (program.rootNamespace);
}

}
