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

/// Migrates local variables that are used in an advance function to be state
/// variables instead to avoid problems when re-entering
inline void moveVariablesToState (AST::ProcessorBase& processor)
{
    struct MoveVariables  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        MoveVariables (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        std::vector<ref<AST::Advance>> advanceCalls;
        std::vector<AST::VariableDeclaration*> variablesToMove;

        void visit (AST::Advance& advance) override
        {
            advanceCalls.push_back (advance);
        }

        void visit (AST::Function& f) override
        {
            if (f.isMainFunction())
            {
                advanceCalls.clear();
                super::visit (f);

                if (! advanceCalls.empty())
                {
                    variablesToMove.clear();
                    auto& mainBlock = *f.getMainBlock();
                    findVariablesToMove (mainBlock);

                    auto& processor = *f.getParentScopeRef().getAsProcessorBase();

                    for (auto& v : variablesToMove)
                        moveVariableToState (processor, *v);
                }
            }
        }

        void findVariablesToMove (AST::Object& o)
        {
            if (auto block = o.getAsScopeBlock())
            {
                for (auto& s : block->statements)
                    findVariablesToMove (s->getObjectRef());
            }
            else if (auto v = o.getAsVariableDeclaration())
            {
                if (v->isLocal() && ! v->isParameter())
                    if (std::find (variablesToMove.begin(), variablesToMove.end(), v) == variablesToMove.end())
                        variablesToMove.push_back (v);
            }
            else if (auto loop = o.getAsLoopStatement())
            {
                findVariablesToMove (loop->body);
            }
            else if (auto ifStatement = o.getAsIfStatement())
            {
                findVariablesToMove (ifStatement->trueBranch);

                if (ifStatement->falseBranch != nullptr)
                    findVariablesToMove (ifStatement->falseBranch);
            }
        }

        void moveVariableToState (AST::ProcessorBase& processor, AST::VariableDeclaration& v)
        {
            auto& parentBlock = AST::castToRef<AST::ScopeBlock> (v.getParentScopeRef());

            if (! isVariableUsedAfterAdvance (parentBlock, v))
                return;

            auto statementIndex = parentBlock.findIndexOfStatementContaining (v);
            CMAJ_ASSERT (statementIndex >= 0);

            if (v.hasStaticConstantValue())
            {
                v.isConstant = true;
                parentBlock.statements.remove (static_cast<size_t> (statementIndex));
            }
            else
            {
                if (v.declaredType == nullptr)
                    v.declaredType.referTo (*v.getType());

                if (auto t = AST::castTo<AST::MakeConstOrRef> (v.declaredType))
                    t->makeConst = false;

                auto init = AST::castTo<AST::ValueBase> (v.initialValue);

                if (init == nullptr)
                {
                    CMAJ_ASSERT (v.initialValue == nullptr);
                    init = v.getType()->allocateConstantValue (v.context);
                }
                else
                {
                    statementIndex += convertSliceCastSourcesToVariables (processor, v, *init, parentBlock, statementIndex);
                }

                auto& assignment = AST::createAssignment (v.context,
                                                          AST::createVariableReference (v.context, v),
                                                          *init);
                v.initialValue.reset();
                v.isConstant = false;
                parentBlock.setStatement (static_cast<size_t> (statementIndex), assignment);
            }

            v.variableType = AST::VariableTypeEnum::Enum::state;
            processor.stateVariables.addChildObject (v);
            CMAJ_ASSERT (v.isStateVariable());
        }

        // when we move a local variable to the state, it may be initialised from a cast-to-slice whose
        // source is a stack-local expression, so we need to also move these to the state so they have
        // a long-lived address. Returns number of statements inserted.
        static int convertSliceCastSourcesToVariables (AST::ProcessorBase& processor, AST::VariableDeclaration& variable,
                                                       AST::ValueBase& initialValue, AST::ScopeBlock& parentBlock, const int statementIndex)
        {
            int numStatementsAdded = 0;

            initialValue.visitObjectsInScope ([&] (AST::Object& o)
            {
                if (auto c = o.getAsCast())
                {
                    if (c->arguments.size() == 1 && c->getResultType()->isSlice())
                    {
                        auto& source = AST::castToValueRef (c->arguments.front());

                        if (! source.isVariableDeclaration())
                        {
                            auto& context = variable.context;
                            auto& temp = AST::createStateVariable (processor, "_temp", source.getResultType(), {});
                            auto& assignment = AST::createAssignment (context, AST::createVariableReference (context, temp), source);
                            parentBlock.addStatement (assignment, statementIndex);
                            c->arguments.reset();
                            c->arguments.addChildObject (AST::createVariableReference (context, temp));
                            ++numStatementsAdded;
                        }
                    }
                }
            });

            return numStatementsAdded;
        }

        static bool isVariableUsedAfterAdvance (AST::ScopeBlock& variableParentBlock, AST::VariableDeclaration& variable)
        {
            bool hasPassedVariableDecl = false;
            bool hasPassedAdvance = false;
            bool isUsedAfterAdvance = false;

            std::map<ptr<AST::VariableDeclaration>, std::set<ptr<AST::VariableDeclaration>>> variableAliases;

            variableParentBlock.visitObjectsInScope ([&] (AST::Object& o)
            {
                if (! hasPassedVariableDecl)
                {
                    if (std::addressof (variable) == std::addressof (o))
                        hasPassedVariableDecl = true;

                    return;
                }

                if (! hasPassedAdvance)
                {
                    if (o.isAdvance())
                        hasPassedAdvance = true;

                    if (auto l = o.getAsLoopStatement())
                    {
                        // within a loop statement, even if an advance call happens after the
                        // variable is accessed, that that could happen before it next time round
                        l->visitObjectsInScope ([&] (AST::Object& ob)
                        {
                            hasPassedAdvance = hasPassedAdvance || ob.isAdvance();
                        });
                    }
                    else if (auto a = AST::castTo<AST::Assignment> (o))
                    {
                        if (auto lhs = AST::castToValue (a->target))
                        {
                            if (auto lhsVar = lhs->getSourceVariable())
                            {
                                if (lhsVar->getType()->containsSlice())
                                {
                                    a->source->visitObjectsInScope([&] (AST::Object& obj)
                                    {
                                        if (auto vr = AST::castTo<AST::VariableReference> (obj))
                                            variableAliases[*lhsVar].insert (vr->getVariable());
                                    });
                                }
                            }
                        }
                    }

                    return;
                }

                if (std::addressof (variable) == std::addressof (o))
                {
                    isUsedAfterAdvance = true;
                }
                else if (auto vr = AST::castTo<AST::VariableReference> (o))
                {
                    if (std::addressof (variable) == std::addressof (vr->getVariable()))
                        isUsedAfterAdvance = true;

                    auto& v = vr->getVariable();

                    // If the variable contains a slice, it's possible that it aliases the variable we are
                    // looking for. Check the initialValue and any variable assigned to it for our variable
                    if (v.getType()->containsSlice())
                    {
                        if (v.initialValue != nullptr)
                        {
                            v.initialValue->visitObjectsInScope([&] (AST::Object& obj)
                            {
                                if (auto r = AST::castTo<AST::VariableReference> (obj))
                                    if (std::addressof (variable) == std::addressof (r->getVariable()))
                                        isUsedAfterAdvance = true;
                            });
                        }

                        auto i = variableAliases.find (v);

                        if (i != variableAliases.end())
                        {
                            if (i->second.find (variable) != i->second.end())
                                isUsedAfterAdvance = true;
                        }
                    }
                }
            });

            return isUsedAfterAdvance;
        }
    };

    MoveVariables (processor.context.allocator).visitObject (processor);
}

}
