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

#include "../AST/cmaj_AST.h"
#include "cmaj_NativeTypeLayout.h"

namespace cmaj
{

//==============================================================================
/// Takes a Program and translates the higher-level AST structure into a sequence of
/// calls to a target builder such as an interpreter, JIT engine or transpiler.
template <typename Builder>
struct CodeGenerator
{
    CodeGenerator (Builder& b, AST::ProcessorBase& p) : builder (b), mainProcessor (p)
    {
        dependencies.addDependencies (mainProcessor);
        dependencies.addDependencies (*mainProcessor.findStruct (mainProcessor.getStrings().stateStructName));
        dependencies.addDependencies (*mainProcessor.findStruct (mainProcessor.getStrings().ioStructName));
    }

    //==============================================================================
    void emitTypes()
    {
        std::unordered_set<const AST::Object*> done;

        for (auto* s : dependencies.structs)
            emitType (*s, done);

        for (auto* s : dependencies.aliases)
            emitType (*s, done);
    }

    void emitGlobals()
    {
        for (auto* v : dependencies.stateVariables)
        {
            ValueReader initialValue = {};

            if (! v->isInitialisedInInit && v->isCompileTimeConstant() && v->initialValue != nullptr)
                initialValue = createValueReader (*AST::getAsFoldedConstant (v->initialValue));

            builder.addGlobalVariable (*v, *v->getType(), globalVariableNames.getName (*v), std::move (initialValue));
        }
    }

    void emitFunctions()
    {
        for (auto* f : dependencies.functions)
        {
            CMAJ_ASSERT (! f->isGenericOrParameterised());
            emitFunction (*f);
        }
    }

    //==============================================================================
    using ValueReader      = typename Builder::ValueReader;
    using ValueReference   = typename Builder::ValueReference;
    using BreakInstruction = typename Builder::BreakInstruction;

    struct FunctionCallArgValue
    {
        const AST::TypeBase& paramType;
        ValueReader valueReader;
        ValueReference valueReference;
    };

    std::string getVariableName (const AST::VariableDeclaration& v)
    {
        if (v.isLocal() || v.isParameter())
        {
            CMAJ_ASSERT (isInsideFunction);
            return localVariableNames.getName (v);
        }

        return globalVariableNames.getName (v);
    }

    std::string getFunctionName (const AST::Function& f)
    {
        if (f.isEventHandler && f.findParentModule() == mainProcessor)
            return AST::getEventHandlerFunctionName (f);

        return functionNames.getName (f);
    }

    bool variableMustHaveAddress (const AST::VariableDeclaration& v)
    {
        if (v.isGlobal())
            return true;

        auto found = variableNeedsAddress.find (std::addressof (v));

        if (found != variableNeedsAddress.end())
            return found->second;

        bool addressNeeded = false;

        v.findParentFunction()->getMainBlock()->visitObjectsInScope ([&] (AST::Object& o)
        {
            if (addressNeeded)
                return;

            if (auto call = o.getAsFunctionCall())
                addressNeeded = callUsesVariableRef (*call, v);
        });

        if (addressNeeded)
            setVariableNeedsAddress (v);

        return addressNeeded;
    }

    static bool callUsesVariableRef (const AST::FunctionCall& call, const AST::VariableDeclaration& v)
    {
        auto& targetFunction = *call.getTargetFunction();
        size_t index = 0;

        for (auto& paramType : targetFunction.getParameterTypes())
        {
            if (paramType->isReference())
            {
                auto& arg = AST::castToValueRef (call.arguments[index]);

                if (arg.getSourceVariable() == v)
                    return true;
            }

            ++index;
        }

        return false;
    }

    ValueReader createConstant (const AST::ConstantValueBase& value)    { return createConstantReader (value); }
    ValueReader createNullConstant (const AST::TypeBase& type)          { return createNullConstantReader (type); }


private:
    //==============================================================================
    struct VariableNameList  : public AST::UniqueNameList<AST::VariableDeclaration, VariableNameList>
    {
        std::string getRootName (const AST::VariableDeclaration& v)
        {
            return Builder::makeSafeIdentifier (v.name.get());
        }
    };

    struct FunctionNameList  : public AST::UniqueNameList<AST::Function, FunctionNameList>
    {
        std::string getRootName (const AST::Function& f)
        {
            if (f.isExported)
                return std::string (f.getName());

            return Builder::makeSafeIdentifier (f.getFullyQualifiedReadableName());
        }
    };

    //==============================================================================
    Builder& builder;
    AST::Dependencies dependencies;
    AST::ProcessorBase& mainProcessor;

    VariableNameList globalVariableNames, localVariableNames;
    FunctionNameList functionNames;

    std::vector<std::function<void()>> postStatementOperationStack;
    bool isInsideFunction = false;
    ptr<const AST::ScopeBlock> currentBlock;
    ptr<const AST::LoopStatement> currentLoop;
    std::vector<ref<const AST::ContinueStatement>> continueStatements;
    std::unordered_map<const AST::VariableDeclaration*, bool> variableNeedsAddress;

    static constexpr int32_t maxNumUnrolledIterations = 4;

    struct UnresolvedBreak
    {
        ref<const AST::Statement> loopOrBlockToBreakFrom;
        BreakInstruction breakToResolve;
    };

    std::vector<UnresolvedBreak> unresolvedBreaks;

    //==============================================================================
    void setVariableNeedsAddress (const AST::VariableDeclaration& v)
    {
        variableNeedsAddress[std::addressof (v)] = true;
    }

    void findContinueStatements (const AST::Function& f)
    {
        continueStatements.clear();

        const_cast<AST::Function&> (f).visitObjectsInScope ([this] (const AST::Object& o)
        {
            if (auto c = o.getAsContinueStatement())
                continueStatements.push_back (*c);
        });
    }

    bool containsContinueStatement (const AST::LoopStatement& loop) const
    {
        for (auto& c : continueStatements)
            if (c->targetBlock.getObject() == loop)
                return true;

        return false;
    }

    //==============================================================================
    void emitType (const AST::Object& t, std::unordered_set<const AST::Object*>& done)
    {
        if (done.find (std::addressof (t)) != done.end())
            return;

        if (auto s = t.getAsStructType())
        {
            for (auto& memberType : s->memberTypes)
                emitReferencedTypes (memberType->getObjectRef(), done);

            if (auto tuple = AST::castTo<AST::StructType> (s->tupleType))
                emitReferencedTypes (*tuple, done);

            builder.addStruct (*s);
        }
        else
        {
            auto& alias = AST::castToRef<AST::Alias> (t);
            auto& targetType = AST::castToTypeBaseRef (alias.getTargetSkippingReferences());

            emitReferencedTypes (targetType, done);
            builder.addAlias (alias, targetType);
        }

        done.insert (std::addressof (t));
    }

    void emitReferencedTypes (const AST::Object& type, std::unordered_set<const AST::Object*>& done)
    {
        if (auto a = AST::castToSkippingReferences<AST::Alias> (type))
            return emitType (*a, done);

        if (auto t = AST::castToTypeBase (type))
        {
            if (auto s = t->getAsStructType())      return emitType (*s, done);
            if (auto m = t->getAsMakeConstOrRef())  return emitReferencedTypes (*m->getSource(), done);
            if (auto a = t->getAsArrayType())       return emitReferencedTypes (a->getInnermostElementTypeRef(), done);
        }
    }

    void emitFunction (const AST::Function& f)
    {
        if (builder.program.externalFunctionManager.findResolvedFunction (f) != nullptr)
            return;

        isInsideFunction = true;
        currentLoop = {};
        localVariableNames.clear();
        findContinueStatements (f);

        builder.beginFunction (f, getFunctionName (f), AST::castToTypeBaseRef (f.returnType));
        emitBlock (*f.getMainBlock());
        builder.endFunction();

        isInsideFunction = false;
        CMAJ_ASSERT (unresolvedBreaks.empty());
    }

    //==============================================================================
    void emitStatement (const AST::Object& statement)
    {
        CMAJ_ASSERT (postStatementOperationStack.empty());
        emitStatementInstructions (statement);
        flushPostStatementOps();
    }

    void emitStatement (const AST::Property& p)      { emitStatement (p.getObjectRef()); }

    void emitStatementInstructions (const AST::Object& statement)
    {
        if (auto b = statement.getAsScopeBlock())           return emitBlock (*b);
        if (auto l = statement.getAsLoopStatement())        return emitLoop (*l);
        if (auto v = statement.getAsVariableDeclaration())  return emitVariableInitialisation (*v);
        if (auto i = statement.getAsIfStatement())          return emitIfStatement (*i);
        if (auto a = statement.getAsAssignment())           return emitAssignment (*a);
        if (auto r = statement.getAsReturnStatement())      return emitReturn (*r);
        if (auto c = statement.getAsFunctionCall())         return builder.addExpressionAsStatement (createFunctionCall (*c));
        if (auto b = statement.getAsBreakStatement())       return emitBreak (*b);
        if (auto c = statement.getAsContinueStatement())    return emitContinue (*c);
        if (auto o = statement.getAsInPlaceOperator())      return emitInPlaceOp (*o);
        if (auto p = statement.getAsPreOrPostIncOrDec())    return emitIncOrDecStatement (AST::castToValueRef (p->target), p->isIncrement ? 1 : -1);
        if (auto b = statement.getAsForwardBranch())        return emitForwardBranch (*b, createValueReader (b->condition));

        CMAJ_ASSERT (statement.isDummyStatement());
    }

    void emitStatementList (const AST::ListProperty& statements)
    {
        for (auto& s : statements)
            emitStatement (AST::castToRef<AST::Statement> (s));
    }

    void emitBlock (const AST::ScopeBlock& block)
    {
        resolveForwardBranches (block);
        auto oldBlock = currentBlock;
        currentBlock = block;
        builder.beginBlock();
        emitStatementList (block.statements);
        builder.endBlock();
        currentBlock = oldBlock;
        resolveBreaks (block);
    }

    void resolveBreaks (const AST::Statement& lastBlock)
    {
        auto resolve = [this, &lastBlock] (const UnresolvedBreak& b) -> bool
        {
            if (b.loopOrBlockToBreakFrom != lastBlock)
                return false;

            builder.resolveBreak (b.breakToResolve);
            return true;
        };

        unresolvedBreaks.erase (std::remove_if (unresolvedBreaks.begin(),
                                                unresolvedBreaks.end(),
                                                resolve),
                                unresolvedBreaks.end());
    }

    void emitAssignment (const AST::Assignment& a)
    {
        emitAssignment (a.target.getObjectRef(), a.source);
    }

    void emitAssignment (const AST::Object& target, const AST::Property& newValue)
    {
        if (auto s = target.getAsGetArraySlice())
            return emitWriteToSlice (*s, AST::castToValueRef (newValue));

        auto targetType = target.getAsValueBase()->getResultType();
        auto& sourceValue = *newValue.getObjectRef().getAsValueBase();
        auto& sourceType = *sourceValue.getResultType();

        emitAssignment (target, createCastIfNeeded (*targetType, sourceType, createValueReader (newValue)),
                        canReadMultipleTimes (sourceValue));
    }

    void emitAssignment (const AST::Object& target, ValueReader&& newValue, bool canSourceBeReadMultipleTimes)
    {
        if (auto r = target.getAsVariableReference())    return emitAssignment (AST::castToRef<AST::VariableDeclaration> (r->variable), std::move (newValue), canSourceBeReadMultipleTimes);
        if (auto v = target.getAsVariableDeclaration())  return emitWriteToVariable (*v, std::move (newValue));
        if (auto v = target.getAsNamedReference())       return emitAssignment (v->target, std::move (newValue), canSourceBeReadMultipleTimes);
        if (auto e = target.getAsGetElement())           return emitWriteToElement (*e, std::move (newValue));
        if (auto s = target.getAsGetArraySlice())        return emitWriteToSlice (*s, std::move (newValue), false, canSourceBeReadMultipleTimes);
        if (auto m = target.getAsGetStructMember())      return emitWriteToStructMember (*m, std::move (newValue));
    }

    void emitVariableInitialisation (const AST::VariableDeclaration& v)
    {
        CMAJ_ASSERT (! v.getType()->isReference());

        if (v.initialValue != nullptr)
            builder.addLocalVariableDeclaration (v, createValueReader (v.initialValue), false);
        else
            builder.addLocalVariableDeclaration (v, {}, true);
    }

    void emitWriteToVariable (const AST::VariableDeclaration& target, ValueReader newValue)
    {
        builder.addAssignToReference (builder.createVariableReference (target), newValue);
    }

    void emitWriteToStructMember (const AST::GetStructMember& m, ValueReader newValue)
    {
        builder.addAssignToReference (createStructMemberReference (m, false), newValue);
    }

    void emitWriteToElement (const AST::GetElement& e, ValueReader newValue)
    {
        auto& parent = AST::castToValueRef (e.parent);
        auto parentRef = createValueReference (parent, false);
        auto index = createIndexReader (AST::castToValueRef (e.getSingleIndex()));

        if (auto s = AST::castToSkippingReferences<AST::StructType> (parent.getResultType()))
            return emitWriteToTuple (*s, parentRef, index, AST::castToRef<AST::StructType> (s->tupleType), newValue);

        emitWriteToElement (parentRef, index, newValue);
    }

    void emitWriteToElement (ValueReference parent, ValueReader index, ValueReader newValue)
    {
        builder.addAssignToReference (builder.createElementReference (parent, index), newValue);
    }

    void emitWriteToTuple (const AST::StructType& structType, ValueReference object, ValueReader index,
                           const AST::StructType& tupleType, ValueReader newValue)
    {
        for (size_t i = 0; i < structType.memberTypes.size(); ++i)
        {
            auto destMember = builder.createStructMemberReference (structType, object,
                                                                   structType.getMemberName (i), static_cast<int64_t> (i));

            if (newValue)
            {
                auto sourceMember = builder.createStructMemberReader (tupleType, newValue,
                                                                      tupleType.getMemberName (i), static_cast<int64_t> (i));
                emitWriteToElement (destMember, index, sourceMember);
            }
            else
            {
                emitWriteToElement (destMember, index, {});
            }
        }
    }

    static bool canReadMultipleTimes (const AST::ValueBase& v)
    {
        return v.isCompileTimeConstant() || v.isVariableReference();
    }

    void emitWriteToSlice (const AST::GetArraySlice& slice, ValueReader newValue,
                           bool valueIsElement, bool canSourceBeReadMultipleTimes)
    {
        auto parentSize = slice.getParentSize();
        CMAJ_ASSERT (parentSize.has_value());
        auto sliceSize = slice.getSliceSize();
        CMAJ_ASSERT (sliceSize.has_value());
        auto parent = createValueReference (slice.parent, false);
        auto rangeStart = createReaderIfNotZeroConstant (slice.start);

        if (! canSourceBeReadMultipleTimes)
        {
            auto& sliceType = *slice.getResultType();
            auto& tempType = valueIsElement ? *sliceType.getArrayOrVectorElementType() : sliceType;
            newValue = createTempVariableReader (tempType, newValue, true);
        }

        createLoopOrUnrolledList (static_cast<int32_t> (*sliceSize), [&] (ValueReader index)
        {
            auto destIndex = createAddInt32 (index, rangeStart);

            if (valueIsElement)
                emitWriteToElement (parent, destIndex, newValue);
            else
                emitWriteToElement (parent, destIndex, builder.createElementReader (newValue, index));
        });
    }

    void emitWriteToSlice (const AST::GetArraySlice& slice, const AST::ValueBase& newValue)
    {
        if (auto v = newValue.getAsConstantAggregate())
        {
            if (v->values.size() == 1)
            {
                auto& source = AST::castToValueRef (v->values[0]);
                auto value = createValueReader (source);
                return emitWriteToSlice (slice, value, true, true);
            }

            auto t = createTempVariableReader (*v->getResultType(), createValueReader (newValue), false);
            return emitWriteToSlice (slice, t, false, true);
        }

        return emitWriteToSlice (slice, createValueReader (newValue), false, canReadMultipleTimes (newValue));
    }

    void emitIfStatement (const AST::IfStatement& i)
    {
        auto conditionValue = createValueReaderWithFlushedOpsStack (i.context.allocator.boolType, createValueReader (i.condition));

        auto& trueBlock = AST::castToRef<AST::ScopeBlock> (i.trueBranch);
        auto hasTrueBranch = ! trueBlock.statements.empty();
        bool hasFalseBranch = i.falseBranch != nullptr && ! AST::castToRef<AST::ScopeBlock> (i.falseBranch).statements.empty();

        auto status = builder.beginIfStatement (std::move (conditionValue), hasTrueBranch, hasFalseBranch);

        emitStatementList (trueBlock.statements);

        if (hasFalseBranch)
        {
            auto& falseBlock = AST::castToRef<AST::ScopeBlock> (i.falseBranch);
            builder.addElseStatement (status);
            emitStatementList (falseBlock.statements);
        }

        builder.endIfStatement (status);
    }

    void emitReturn (const AST::ReturnStatement& r)
    {
        if (r.value == nullptr)
            return builder.addReturnVoid();

        auto& resultValue = AST::castToValueRef (r.value);
        auto& functionReturnType = AST::castToTypeBaseRef (r.getParentFunction().returnType);
        builder.addReturnValue (createValueReaderWithFlushedOpsStack (functionReturnType, createValueReader (resultValue)));
    }

    void emitLoop (const AST::LoopStatement& loop)
    {
        // We're expecting these to have been refactored by replaceWrapTypesAndLoopCounters()
        CMAJ_ASSERT (loop.numIterations == nullptr && loop.condition == nullptr && loop.initialisers.empty());

        auto oldLoop = currentLoop;
        currentLoop = loop;

        auto status = builder.beginLoop();
        auto& loopBody = AST::castToRef<AST::ScopeBlock> (loop.body);

        if (containsContinueStatement (loop))
            emitBlock (loopBody);
        else
            emitStatementList (loopBody.statements);

        if (loop.iterator != nullptr)
            emitStatement (loop.iterator);

        builder.endLoop (status);
        currentLoop = oldLoop;
        resolveBreaks (loop);
    }

    void emitContinue (const AST::ContinueStatement& c)
    {
        auto& loop = AST::castToRef<AST::LoopStatement> (c.targetBlock);
        emitBreak (AST::castToRef<AST::Statement> (loop.body));
    }

    void emitBreak (const AST::BreakStatement& b)
    {
        emitBreak (AST::castToRef<AST::Statement> (b.targetBlock));
    }

    void emitBreak (AST::Statement& loopOrBlockToBreakFrom)
    {
        if (currentLoop == loopOrBlockToBreakFrom)
            if (builder.addBreakFromCurrentLoop())
                return;

        unresolvedBreaks.push_back ({ loopOrBlockToBreakFrom, builder.addBreak() });
    }

    void emitInPlaceOp (const AST::InPlaceOperator& op)
    {
        emitAssignment (op.target, createBinaryOp (op.op.get(),
                                                   AST::castToValueRef (op.target),
                                                   AST::castToValueRef (op.source)), false);
    }

    void emitIncOrDecStatement (AST::ValueBase& target, int32_t delta)
    {
        emitIncOrDecStatement (target.getResultType()->skipConstAndRefModifiers(),
                               createValueReference (target, false), delta);
    }

    void emitIncOrDecStatement (const AST::TypeBase& type, ValueReference target, int32_t delta)
    {
        if (type.isPrimitiveInt())
            return builder.addAddValueToInteger (target, delta);

        auto& deltaValue = type.allocateConstantValue (type.context);
        auto targetReader = builder.createReaderForReference (target);

        if (auto vec = type.getAsVectorType())
        {
            auto numElements = vec->resolveSize();

            if (! builder.canPerformVectorBinaryOp())
            {
                auto& elementType = vec->getElementType();
                auto& elementDeltaValue = elementType.allocateConstantValue (elementType.context);
                elementDeltaValue.setFromValue (choc::value::createInt32 (delta));

                createLoopOrUnrolledList (static_cast<int32_t> (numElements), [&] (ValueReader index)
                {
                    emitWriteToElement (target, index,
                                        builder.createBinaryOp (AST::BinaryOpTypeEnum::Enum::add,
                                                                { elementType, elementType },
                                                                builder.createElementReader (targetReader, index),
                                                                createConstantReader (elementDeltaValue)));
                });

                return;
            }

            deltaValue.setFromValue (choc::value::createVector (numElements, [=] (uint32_t) { return delta; }));
        }
        else
        {
            deltaValue.setFromValue (choc::value::createInt32 (delta));
        }

        builder.addAssignToReference (target, builder.createBinaryOp (AST::BinaryOpTypeEnum::Enum::add,
                                                                      { type, type },
                                                                      targetReader,
                                                                      createConstantReader (deltaValue)));
    }

    //==============================================================================
    using ForwardBranchPlaceholder = typename Builder::ForwardBranchPlaceholder;
    using ForwardBranchTarget      = typename Builder::ForwardBranchTarget;

    struct UnresolvedForwardBranch
    {
        UnresolvedForwardBranch (const AST::ForwardBranch& fb, ForwardBranchPlaceholder&& p)
            : placeholder (std::move (p)),
              targetBlocks (fb.targetBlocks.getAsObjectTypeList<AST::ScopeBlock>()),
              unresolvedBranchCount (targetBlocks.size())
        {
            branchTargets.resize (unresolvedBranchCount);
        }

        ForwardBranchPlaceholder placeholder;
        AST::ObjectRefVector<AST::ScopeBlock> targetBlocks;
        std::vector<ForwardBranchTarget> branchTargets;
        size_t unresolvedBranchCount;
    };

    std::vector<UnresolvedForwardBranch> unresolvedForwardBranches;

    void emitForwardBranch (const AST::ForwardBranch& fb, ValueReader condition)
    {
        if (! fb.targetBlocks.empty())
            unresolvedForwardBranches.emplace_back (fb, builder.beginForwardBranch (std::move (condition), fb.targetBlocks.size()));
    }

    bool resolveForwardBranch (UnresolvedForwardBranch& b, const AST::ScopeBlock& block)
    {
        size_t index = 0;

        for (auto targetBlock : b.targetBlocks)
        {
            if (targetBlock.getPointer() == std::addressof (block))
            {
                b.branchTargets[index] = builder.createForwardBranchTarget (b.placeholder, index);
                b.unresolvedBranchCount--;
            }

            ++index;
        }

        if (b.unresolvedBranchCount != 0)
            return false;

        builder.resolveForwardBranch (b.placeholder, b.branchTargets);
        return true;
    }

    void resolveForwardBranches (const AST::ScopeBlock& block)
    {
        auto resolve = [this, &block] (UnresolvedForwardBranch& b) -> bool
        {
            return resolveForwardBranch (b, block);
        };

        unresolvedForwardBranches.erase (std::remove_if (unresolvedForwardBranches.begin(),
                                                         unresolvedForwardBranches.end(),
                                                         resolve),
                                         unresolvedForwardBranches.end());
    }

    //==============================================================================
    ValueReader     createValueReader    (const AST::Property& value)                  { return createValueReader (value.getObjectRef()); }
    ValueReference  createValueReference (const AST::Property& value, bool isConst)    { return createValueReference (value.getObjectRef(), isConst); }

    ValueReader createValueReader (const AST::Object& value)
    {
        if (auto c = value.getAsConstantValueBase())    return createConstantReader (*c);
        if (auto c = value.getAsCast())                 return createCast (*c);
        if (auto c = value.getAsFunctionCall())         return createFunctionCall (*c);
        if (auto u = value.getAsUnaryOperator())        return createUnaryOp (u->op, AST::castToValueRef (u->input));
        if (auto b = value.getAsBinaryOperator())       return createBinaryOp (b->op, AST::castToValueRef (b->lhs), AST::castToValueRef (b->rhs));
        if (auto t = value.getAsTernaryOperator())      return createTernaryOp (*t);
        if (auto r = value.getAsVariableReference())    return builder.createVariableReader (AST::castToRef<AST::VariableDeclaration> (r->variable));
        if (auto v = value.getAsVariableDeclaration())  return builder.createVariableReader (*v);
        if (auto v = value.getAsNamedReference())       return createValueReader (v->target);
        if (auto e = value.getAsGetElement())           return createElementReader (*e);
        if (auto s = value.getAsGetArraySlice())        return createSliceReader (*s);
        if (auto m = value.getAsGetStructMember())      return createStructMemberReader (*m);
        if (auto m = value.getAsValueMetaFunction())    return createValueMetaFunction (*m);
        if (auto p = value.getAsPreOrPostIncOrDec())    return createPreOrPostIncOrDec (AST::castToValueRef (p->target), p->isIncrement, p->isPost);

        CMAJ_ASSERT_FALSE;
    }

    ValueReference createTempVariableReference (const AST::TypeBase& type,
                                                ValueReader value, bool ensureZeroInitialised,
                                                std::string_view name = "_temp")
    {
        auto& tempVariable = const_cast<AST::ScopeBlock&> (*currentBlock).allocateChild<AST::VariableDeclaration>();
        tempVariable.variableType = AST::VariableTypeEnum::Enum::local;
        tempVariable.name = tempVariable.getStringPool().get (name);
        tempVariable.declaredType.referTo (type);

        setVariableNeedsAddress (tempVariable);
        return builder.createLocalTempVariable (tempVariable, std::move (value), ensureZeroInitialised);
    }

    ValueReader createTempVariableReader (const AST::TypeBase& type,
                                          ValueReader value, bool ensureZeroInitialised,
                                          std::string_view name = "_temp")
    {
        auto& tempVariable = const_cast<AST::ScopeBlock&> (*currentBlock).allocateChild<AST::VariableDeclaration>();
        tempVariable.variableType = AST::VariableTypeEnum::Enum::local;
        tempVariable.name = tempVariable.getStringPool().get (name);
        tempVariable.declaredType.referTo (type);

        builder.addLocalVariableDeclaration (tempVariable, std::move (value), ensureZeroInitialised);
        return builder.createVariableReader (tempVariable);
    }

    ValueReference createValueReference (const AST::Object& value, bool isConst)
    {
        if (auto r = value.getAsVariableReference())    return builder.createVariableReference (AST::castToRef<AST::VariableDeclaration> (r->variable));
        if (auto v = value.getAsVariableDeclaration())  return builder.createVariableReference (*v);
        if (auto v = value.getAsNamedReference())       return createValueReference (v->target, isConst);
        if (auto c = value.getAsStateUpcast())          return createStateUpcast (*c);

        if (auto e = value.getAsGetElement())
        {
            auto& parent = AST::castToValueRef (e->parent);

            if (parent.getResultType()->getAsStructType() == nullptr)
                if (auto ref = builder.createElementReference (createValueReference (e->parent, isConst),
                                                               createValueReader (e->getSingleIndex())))
                    return ref;
        }
        else if (auto m = value.getAsGetStructMember())
        {
            if (auto ref = createStructMemberReference (*m, isConst))
                return ref;
        }
        else
        {
            CMAJ_ASSERT (isConst || value.getAsGetArraySlice() != nullptr);
        }

        auto& v = AST::castToValueRef (value);
        return createValueReferenceViaTemporary (*v.getResultType(),
                                                 createValueReader (v),
                                                 isConst ? nullptr : ptr<const AST::ValueBase> (v));
    }

    ValueReference createValueReferenceViaTemporary (const AST::TypeBase& variableType,
                                                     ValueReader sourceValue,
                                                     ptr<const AST::ValueBase> resultTarget)
    {
        auto tempVariable = createTempVariableReference (variableType, sourceValue, true);

        if (resultTarget != nullptr)
        {
            postStatementOperationStack.push_back ([this, resultTarget, tempVariable] () mutable
            {
                emitAssignment (*resultTarget, builder.createReaderForReference (tempVariable), true);
            });
        }

        return tempVariable;
    }

    ValueReader createConstantReader (const AST::ConstantValueBase& value)
    {
        auto& type = value.getResultType()->skipConstAndRefModifiers();

        // if we've got an aggregate that resolves to a slice, a null value would
        // throw away its original size
        if (value.isZero() && ! (type.isSlice() && value.isConstantAggregate()))
            return createNullConstantReader (type);

        if (auto p = type.getAsPrimitiveType())
        {
            if (p->isPrimitiveInt32())       { auto v = value.getAsInt32();   CMAJ_ASSERT (v.has_value());  return builder.createConstantInt32 (*v); }
            if (p->isPrimitiveInt64())       { auto v = value.getAsInt64();   CMAJ_ASSERT (v.has_value());  return builder.createConstantInt64 (*v); }
            if (p->isPrimitiveFloat32())     { auto v = value.getAsFloat32(); CMAJ_ASSERT (v.has_value());  return builder.createConstantFloat32 (*v); }
            if (p->isPrimitiveFloat64())     { auto v = value.getAsFloat64(); CMAJ_ASSERT (v.has_value());  return builder.createConstantFloat64 (*v); }
            if (p->isPrimitiveBool())        { auto v = value.getAsBool();    CMAJ_ASSERT (v.has_value());  return builder.createConstantBool (*v); }
        }

        if (auto s = value.getAsConstantString())
            return builder.createConstantString (s->value.get());

        if (auto agg = value.getAsConstantAggregate())
            return builder.createConstantAggregate (*agg);

        if (auto e = value.getAsConstantEnum())
            return builder.createConstantInt32 (static_cast<int32_t> (e->index.get()));

        CMAJ_ASSERT_FALSE;
        return {};
    }

    ValueReader createNullConstantReader (const AST::TypeBase& type)
    {
        if (type.isPrimitiveInt32())     return builder.createConstantInt32 (0);
        if (type.isPrimitiveInt64())     return builder.createConstantInt64 (0);
        if (type.isPrimitiveFloat32())   return builder.createConstantFloat32 (0);
        if (type.isPrimitiveFloat64())   return builder.createConstantFloat64 (0);
        if (type.isPrimitiveBool())      return builder.createConstantBool (false);
        if (type.isPrimitiveString())    return builder.createConstantString ({});
        if (type.isEnum())               return builder.createConstantInt32 (0);

        CMAJ_ASSERT (type.isFixedSizeAggregate() || type.isSlice());
        return builder.createNullConstant (type);
    }

    ValueReader createIndexReader (const AST::ValueBase& index)
    {
        auto& indexType = *index.getResultType();
        return createCastIfNeeded (index.context.allocator.int32Type, indexType, index);
    }

    ValueReader createElementReader (const AST::TypeBase& parentType, ValueReader parent, ValueReader index)
    {
        if (auto s = AST::castToSkippingReferences<AST::StructType> (parentType.skipConstAndRefModifiers()))
            return createTupleReader (*s, parent, index, AST::castToRef<AST::StructType> (s->tupleType));

        return builder.createElementReader (parent, index);
    }

    ValueReader createElementReader (const AST::GetElement& e)
    {
        CMAJ_ASSERT (e.indexes.size() == 1);
        auto& parent = AST::castToValueRef (e.parent);
        auto& index = AST::castToValueRef (e.getSingleIndex());
        return createElementReader (*parent.getResultType(), createValueReader (parent), createIndexReader (index));
    }

    ValueReader createUnaryOp (AST::UnaryOpTypeEnum::Enum opType, const AST::ValueBase& input)
    {
        auto& inputType = *input.getResultType();

        if (inputType.isVector())
            if (! builder.canPerformVectorUnaryOp())
                return createVectorUnaryOp (opType, inputType, input);

        return builder.createUnaryOp (opType, inputType, createValueReader (input));
    }

    ValueReader createVectorUnaryOp (AST::UnaryOpTypeEnum::Enum opType,
                                     const AST::TypeBase& inputType,
                                     const AST::ValueBase& input)
    {
        auto& vectorType = AST::castToRef<AST::VectorType> (inputType);
        auto& elementType = vectorType.getElementType();
        auto numElements = vectorType.resolveSize();

        auto tempVariableRef = createTempVariableReference (vectorType, {}, false);

        auto inputReader = createValueReader (input);

        createLoopOrUnrolledList (static_cast<int32_t> (numElements), [&] (ValueReader index)
        {
            auto sourceElement = builder.createElementReader (inputReader, index);

            emitWriteToElement (tempVariableRef, index,
                                builder.createUnaryOp (opType, elementType, sourceElement));
        });

        return builder.createReaderForReference (tempVariableRef);
    }

    ValueReader createBinaryOp (AST::BinaryOpTypeEnum::Enum opType,
                                const AST::ValueBase& lhs, const AST::ValueBase& rhs)
    {
        auto& typeA = *lhs.getResultType();
        auto& typeB = *rhs.getResultType();

        if (auto opTypes = AST::TypeRules::getBinaryOperatorTypes (opType, typeA, typeB))
        {
            if (opTypes.operandType.isVector())
            {
                if (! builder.canPerformVectorBinaryOp())
                    return createVectorBinaryOp (opType, opTypes, lhs, rhs);
            }
            else
            {
                // handle special cases for && and || to implement short-circuiting
                bool isLogicalAnd = (opType == AST::BinaryOpTypeEnum::Enum::logicalAnd);
                bool isLogicalOr  = (opType == AST::BinaryOpTypeEnum::Enum::logicalOr);

                if (isLogicalAnd || isLogicalOr)
                {
                    // avoid using a ternary if the RHS is just a trivial variable reference or constant
                    if (! (rhs.isVariableReference() || rhs.constantFold() != nullptr))
                        return createTernaryFromLogicalAndOrOr (lhs, rhs, isLogicalAnd);
                }
            }

            auto lhsValue = createCastIfNeeded (opTypes.operandType, typeA, lhs);
            auto rhsValue = createCastIfNeeded (opTypes.operandType, typeB, rhs);

            // should have already been turned into a call
            CMAJ_ASSERT (opType != AST::BinaryOpTypeEnum::Enum::exponent);

            return createBinaryOp (opType, opTypes, lhsValue, rhsValue);
        }

        return {};
    }

    ValueReader createBinaryOp (AST::BinaryOpTypeEnum::Enum opType,
                                const AST::TypeRules::BinaryOperatorTypes& opTypes,
                                ValueReader lhs, ValueReader rhs)
    {
        return builder.createBinaryOp (opType, opTypes, std::move (lhs), std::move (rhs));
    }

    ValueReader createVectorBinaryOp (AST::BinaryOpTypeEnum::Enum opType,
                                      const AST::TypeRules::BinaryOperatorTypes& opTypes,
                                      const AST::ValueBase& a, const AST::ValueBase& b)
    {
        auto& vectorType = AST::castToRef<AST::VectorType> (opTypes.resultType);
        auto& resultElementType = vectorType.getElementType();
        auto& operandElementType = *opTypes.operandType.getArrayOrVectorElementType();
        auto numElements = vectorType.resolveSize();

        auto tempVariableRef = createTempVariableReference (vectorType, {}, false);

        auto& typeA = *a.getResultType();
        auto& typeB = *b.getResultType();

        auto valueA = createValueReader (a);
        auto valueB = createTempVariableReader (typeB, createValueReader (b), false);

        createLoopOrUnrolledList (static_cast<int32_t> (numElements), [&] (ValueReader index)
        {
            auto elementA = typeA.isVector() ? createCastIfNeeded (operandElementType, *typeA.getArrayOrVectorElementType(), builder.createElementReader (valueA, index))
                                             : createCastIfNeeded (operandElementType, typeA, valueA);

            auto elementB = typeB.isVector() ? createCastIfNeeded (operandElementType, *typeB.getArrayOrVectorElementType(), builder.createElementReader (valueB, index))
                                             : createCastIfNeeded (operandElementType, typeB, valueB);

            emitWriteToElement (tempVariableRef, index,
                                createBinaryOp (opType, { resultElementType, operandElementType }, elementA, elementB));
        });

        return builder.createReaderForReference (tempVariableRef);
    }

    static bool mustTernaryBeConvertedToIfStatement (const AST::ValueBase& trueValue,
                                                     const AST::ValueBase& falseValue)
    {
        AST::SideEffects sideEffects;
        sideEffects.add (trueValue);
        sideEffects.add (falseValue);

        return sideEffects.modifiesLocalVariables
            || sideEffects.modifiesStateVariables;
    }

    ValueReader createTernaryOp (const AST::ValueBase& condition,
                                 const AST::ValueBase& trueValue,
                                 const AST::ValueBase& falseValue)
    {
        if (mustTernaryBeConvertedToIfStatement (trueValue, falseValue))
        {
            auto resultVariable = createTempVariableReference (*trueValue.getResultType(), {}, false);

            auto ifStatus = builder.beginIfStatement (createValueReader (condition), true, true);
            builder.addAssignToReference (resultVariable, createValueReader (trueValue));
            builder.addElseStatement (ifStatus);
            builder.addAssignToReference (resultVariable, createValueReader (falseValue));
            builder.endIfStatement (ifStatus);

            return builder.createReaderForReference (resultVariable);
        }

        return builder.createTernaryOp (createValueReader (condition),
                                        createValueReader (trueValue),
                                        createValueReader (falseValue));
    }

    ValueReader createTernaryOp (const AST::TernaryOperator& t)
    {
        return createTernaryOp (AST::castToValueRef (t.condition),
                                AST::castToValueRef (t.trueValue),
                                AST::castToValueRef (t.falseValue));
    }

    ValueReader createTernaryFromLogicalAndOrOr (const AST::ValueBase& lhs, const AST::ValueBase& rhs, bool isAnd)
    {
        auto& context = lhs.context;

        if (! rhs.getResultType()->isPrimitiveBool())
        {
            // NB: must create a cast using AST so that createTernaryOp takes care of converting
            // it to a ValueReader later on, so that short-circuited evaluation order is respected
            auto& castToBool = context.allocate<AST::Cast>();
            castToBool.targetType.createReferenceTo (context.allocator.boolType);
            castToBool.arguments.addReference (rhs);

            return createTernaryFromLogicalAndOrOr (lhs, castToBool, isAnd);
        }

        return isAnd ? createTernaryOp (lhs, rhs, context.allocator.createConstantBool (false))
                     : createTernaryOp (lhs, context.allocator.createConstantBool (true), rhs);
    }

    ValueReader createCastIfNeeded (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, ValueReader value)
    {
        if (targetType.isSameType (sourceType, AST::TypeBase::ComparisonFlags::duckTypeStructures))
            return value;

        return createCast (targetType, sourceType, std::move (value));
    }

    ValueReader createCastIfNeeded (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, const AST::ValueBase& value)
    {
        if (targetType.isSameType (sourceType, AST::TypeBase::ComparisonFlags::duckTypeStructures))
            return createValueReader (value);

        if (targetType.isSlice())
        {
            if (sourceType.isFixedSizeArray())
            {
                auto& sourceArrayType = AST::castToRef<const AST::ArrayType> (sourceType.skipConstAndRefModifiers());
                auto& sourceElementType = sourceArrayType.getInnermostElementTypeRef();

                if (value.getSourceVariable() == nullptr)
                {
                    auto temp = createTempVariableReader (sourceArrayType, createValueReader (value), false);
                    return builder.createSliceFromArray (sourceElementType, temp, 0, sourceArrayType.resolveSize());
                }

                return createSliceFromValue (sourceElementType, sourceArrayType, value);
            }

            CMAJ_ASSERT (targetType.isSameType (sourceType, AST::TypeBase::ComparisonFlags::ignoreConst
                                                             | AST::TypeBase::ComparisonFlags::ignoreReferences
                                                             | AST::TypeBase::ComparisonFlags::duckTypeStructures));
            return createValueReader (value);
        }

        if (auto constValue = value.constantFold())
        {
            if (constValue != value)
            {
                auto castConst = AST::Cast::castConstant (value.context.allocator, targetType, *constValue, false);
                return createCastIfNeeded (targetType, *castConst->getResultType(), *castConst);
            }
        }

        return createCastIfNeeded (targetType, sourceType, createValueReader (value));
    }

    ValueReader createCast (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, ValueReader sourceValue)
    {
        if (targetType.isSameType (sourceType, AST::TypeBase::ComparisonFlags::ignoreConst
                                                | AST::TypeBase::ComparisonFlags::ignoreReferences
                                                | AST::TypeBase::ComparisonFlags::duckTypeStructures))
            return sourceValue;

        CMAJ_ASSERT (AST::TypeRules::canCastTo (targetType, sourceType));
        CMAJ_ASSERT (! targetType.isSlice()); // for slices, need to go via the createCastIfNeeded() that takes a ValueBase as its source

        if (targetType.isSlice())
        {
            CMAJ_ASSERT (sourceType.isFixedSizeArray());
            auto& sourceArrayType = AST::castToRef<const AST::ArrayType> (sourceType.skipConstAndRefModifiers());
            return builder.createSliceFromArray (sourceArrayType.getInnermostElementTypeRef(), sourceValue, 0, sourceArrayType.resolveSize());
        }

        if (targetType.isFixedSizeAggregate())
        {
            if (targetType.isStructType())
            {
                AST::ObjectRefVector<const AST::TypeBase> types;
                choc::SmallVector<ValueReader, 4> values;
                types.push_back (sourceType);
                values.push_back (sourceValue);
                return createAggregateCast (targetType, types, values);
            }

            auto targetSize = targetType.getFixedSizeAggregateNumElements();

            if (targetType.isVectorOrArray()
                 && sourceType.isVectorOrArray()
                 && targetSize == sourceType.getFixedSizeAggregateNumElements()
                 && AST::TypeRules::canCastTo (*targetType.getArrayOrVectorElementType(),
                                               *sourceType.getArrayOrVectorElementType()))
            {
                return createArrayOrVectorCast (targetType, sourceType, sourceValue);
            }

            auto tempVariableRef = createTempVariableReference (targetType, {}, false);

            auto& destElementType = *targetType.getArrayOrVectorElementType();
            sourceValue = createCastIfNeeded (destElementType, sourceType, sourceValue);

            auto valueReader = createTempVariableReader (*targetType.getArrayOrVectorElementType(), sourceValue, false);

            createLoopOrUnrolledList (static_cast<int32_t> (targetSize), [&] (ValueReader index)
            {
                emitWriteToElement (tempVariableRef, index, valueReader);
            });

            return builder.createReaderForReference (tempVariableRef);
        }

        return builder.createValueCast (targetType, sourceType, sourceValue);
    }

    ValueReader createCast (const AST::Cast& c)
    {
        if (auto folded = c.constantFold())
            return createConstantReader (*folded);

        auto& targetType = AST::castToTypeBaseRef (c.targetType);

        auto numArgs = c.arguments.size();

        if (numArgs == 0)
            return createNullConstantReader (targetType);

        auto& firstArg = AST::castToValueRef (c.arguments.front());
        auto& firstArgType = *firstArg.getResultType();

        auto structType = targetType.getAsStructType();

        if (numArgs == 1)
        {
            if (targetType.isSlice())
                return createCastIfNeeded (targetType, firstArgType, firstArg);

            auto isCastToSingleElementStruct = [&]
            {
                if (structType == nullptr || structType->getFixedSizeAggregateNumElements() != 1)
                    return false;

                auto& firstMemberType = *structType->getAggregateElementType (0);
                return AST::TypeRules::canCastTo (firstMemberType, firstArgType);
            };

            if (! isCastToSingleElementStruct())
                return createCast (targetType, firstArgType, createValueReader (firstArg));
        }

        AST::ObjectRefVector<const AST::TypeBase> types;
        choc::SmallVector<ValueReader, 4> values;

        for (auto& arg : c.arguments)
        {
            auto& a = AST::castToValueRef (arg);
            types.push_back (*a.getResultType());
            values.push_back (createValueReader (a));
        }

        return createAggregateCast (targetType, types, values);
    }

    ValueReader createSliceFromValue (const AST::TypeBase& elementType, const AST::ArrayType& sourceType, const AST::ValueBase& value)
    {
        if (auto slice = value.getAsGetArraySlice())
        {
            auto& parentArray = AST::castToValueRef (slice->parent);
            auto& parentArrayType = *parentArray.getResultType();
            CMAJ_ASSERT (parentArrayType.isFixedSizeArray());

            if (auto range = slice->getResultRange (parentArrayType.getArrayOrVectorSize (0)))
            {
                auto offset = range->start;
                auto size = range->end - offset;

                return builder.createSliceFromArray (elementType, createValueReader (parentArray),
                                                     static_cast<uint32_t> (offset), static_cast<uint32_t> (size));
            }
        }

        return builder.createSliceFromArray (elementType, createValueReader (value), 0, sourceType.resolveSize());
    }

    ValueReader createArrayOrVectorCast (const AST::TypeBase& targetType, const AST::TypeBase& sourceType, ValueReader sourceValue)
    {
        auto tempVariableRef = createTempVariableReference (targetType, {}, false);

        auto numElements = targetType.getFixedSizeAggregateNumElements();
        auto& destElementType = *targetType.getArrayOrVectorElementType();
        auto& sourceElementType = *sourceType.getArrayOrVectorElementType();

        createLoopOrUnrolledList (static_cast<int32_t> (numElements), [&] (ValueReader index)
        {
            auto source = createCastIfNeeded (destElementType, sourceElementType,
                                              createElementReader (sourceType, sourceValue, index));
            emitWriteToElement (tempVariableRef, index, source);
        });

        return builder.createReaderForReference (tempVariableRef);
    }

    ValueReader createAggregateCast (const AST::TypeBase& targetType,
                                     const AST::ObjectRefVector<const AST::TypeBase>& sourceTypes,
                                     choc::span<ValueReader> sourceValues)
    {
        CMAJ_ASSERT (targetType.isFixedSizeAggregate());
        auto numArgs = sourceValues.size();

        if (numArgs == 0)
            return createNullConstantReader (targetType);

        auto targetSize = targetType.getFixedSizeAggregateNumElements();
        CMAJ_ASSERT (numArgs <= targetSize);

        auto tempVariableRef = createTempVariableReference (targetType, {}, numArgs < targetSize);

        auto structType = targetType.getAsStructType();

        for (size_t i = 0; i < numArgs; ++i)
        {
            auto& sourceElementType = sourceTypes[i].get();
            auto& destElementType = *targetType.getAggregateElementType (i);
            auto sourceValue = createCastIfNeeded (destElementType, sourceElementType, sourceValues[i]);

            if (structType != nullptr)
            {
                builder.addAssignToReference (builder.createStructMemberReference (*structType, tempVariableRef,
                                                                                   structType->getMemberName (i),
                                                                                   static_cast<int64_t> (i)),
                                              sourceValue);
            }
            else
            {
                emitWriteToElement (tempVariableRef,
                                    builder.createConstantInt32 (static_cast<int32_t> (i)),
                                    sourceValue);
            }
        }

        return builder.createReaderForReference (tempVariableRef);
    }

    ValueReference createStateUpcast (const AST::StateUpcast& c)
    {
        return builder.createStateUpcast (*AST::castToTypeBaseRef (c.targetType).skipConstAndRefModifiers().getAsStructType(),
                                          *AST::castToValueRef (c.argument).getResultType()->skipConstAndRefModifiers().getAsStructType(),
                                          createValueReference (c.argument, false));
    }

    ValueReader createTupleReader (const AST::StructType& structType, ValueReader object,
                                   ValueReader index, const AST::StructType& tupleType)
    {
        AST::ObjectRefVector<const AST::TypeBase> types;
        choc::SmallVector<ValueReader, 4> values;

        for (size_t i = 0; i < tupleType.memberTypes.size(); ++i)
        {
            types.push_back (AST::castToTypeBaseRef (tupleType.memberTypes[i]));
            auto sourceArray = builder.createStructMemberReader (structType, object, structType.getMemberName(i),
                                                                 static_cast<int64_t> (i));
            values.push_back (createElementReader (structType.getMemberType(i), sourceArray, index));
        }

        return createAggregateCast (tupleType, types, values);
    }

    void flushPostStatementOps()
    {
        while (! postStatementOperationStack.empty())
        {
            auto op = std::move (postStatementOperationStack.back());
            postStatementOperationStack.pop_back();
            op();
        }
    }

    ValueReader createValueReaderWithFlushedOpsStack (const AST::TypeBase& type, ValueReader&& value)
    {
        if (postStatementOperationStack.empty())
            return std::move (value);

        // if the current expression has generated pending operations, need to store a
        // copy of the result and flush the ops before returning the result..
        auto result = createTempVariableReader (type, std::move (value), false);
        flushPostStatementOps();
        return result;
    }

    ValueReader createFunctionCall (const AST::FunctionCall& call)
    {
        auto intrinsic = call.getIntrinsicType();

        auto& targetFunction = *call.getTargetFunction();
        auto paramTypes = targetFunction.getParameterTypes();
        std::vector<FunctionCallArgValue> argValues;

        CMAJ_ASSERT (call.arguments.size() == paramTypes.size());

        for (size_t i = 0; i < call.arguments.size(); ++i)
        {
            auto& paramType = paramTypes[i].get();
            argValues.push_back ({ paramType });
            auto& argEntry = argValues.back();

            auto& arg = AST::castToValueRef (call.arguments[i]);
            auto& argType = *arg.getResultType();

            if (paramType.isReference())
            {
                if (paramType.isSameType (argType, AST::TypeBase::ComparisonFlags::ignoreConst
                                                    | AST::TypeBase::ComparisonFlags::ignoreReferences
                                                    | AST::TypeBase::ComparisonFlags::duckTypeStructures))
                {
                    argEntry.valueReference = createValueReference (arg, paramType.isConst());
                }

                if (! (argEntry.valueReference && argEntry.valueReference.isPointer()))
                {
                    auto& coreParamType = paramType.skipConstAndRefModifiers();
                    argEntry.valueReference = createValueReferenceViaTemporary (coreParamType,
                                                                                createCastIfNeeded (coreParamType, argType, arg),
                                                                                paramType.isConst() ? nullptr : ptr<const AST::ValueBase> (arg));
                }
            }
            else
            {
                argEntry.valueReader = createCastIfNeeded (paramType, argType, arg);
            }
        }

        if (intrinsic != AST::Intrinsic::Type::unknown)
            if (auto backendImplementation = builder.createIntrinsicCall (intrinsic, argValues, AST::castToTypeBaseRef (targetFunction.returnType)))
                return backendImplementation;

        return builder.createFunctionCall (targetFunction, getFunctionName (targetFunction), argValues);
    }

    ValueReader createStructMemberReader (const AST::GetStructMember& m)
    {
        auto& structType = AST::castToRef<AST::StructType> (AST::castToValueRef (m.object)
                                                              .getResultType()->skipConstAndRefModifiers());

        return builder.createStructMemberReader (structType, createValueReader (m.object),
                                                 m.member.get(), structType.indexOfMember (m.member.get()));
    }

    ValueReference createStructMemberReference (const AST::GetStructMember& m, bool isConst)
    {
        auto& structType = AST::castToRef<AST::StructType> (AST::castToValueRef (m.object)
                                                              .getResultType()->skipConstAndRefModifiers());

        return builder.createStructMemberReference (structType, createValueReference (m.object, isConst),
                                                    m.member.get(), structType.indexOfMember (m.member.get()));
    }

    ValueReader createSliceReader (const AST::GetArraySlice& slice)
    {
        auto parentSize = slice.getParentSize();
        CMAJ_ASSERT (parentSize.has_value());
        auto sliceSize = slice.getSliceSize();
        CMAJ_ASSERT (sliceSize.has_value());

        auto& parent = AST::castToValueRef (slice.parent);
        auto& parentType = parent.getResultType()->skipConstAndRefModifiers();
        auto parentReader = createValueReader (parent);

        CMAJ_ASSERT (! parentType.isSlice()); // TODO when the compiler supports this
        CMAJ_ASSERT (parentType.isVectorOrArray());

        auto& elementType = *parentType.getArrayOrVectorElementType();
        auto numElements = static_cast<int32_t> (*sliceSize);

        ptr<AST::TypeBase> resultType;

        if (parentType.isArrayType())
        {
            resultType = AST::createArrayOfType (slice.context, elementType, numElements);
        }
        else
        {
            auto& newType = slice.context.allocate<AST::VectorType>();
            newType.elementType.referTo (elementType);
            newType.numElements.createConstant (numElements);
            resultType = newType;
        }

        auto tempVariableRef = createTempVariableReference (*resultType, {}, false);
        auto sourceStartIndex = createReaderIfNotZeroConstant (slice.start);

        createLoopOrUnrolledList (numElements, [&] (ValueReader index)
        {
            auto sourceIndex = createAddInt32 (index, sourceStartIndex);
            auto sourceValue = createElementReader (parentType, parentReader, sourceIndex);
            emitWriteToElement (tempVariableRef, index, sourceValue);
        });

        return builder.createReaderForReference (tempVariableRef);
    }

    ValueReader createReaderIfNotZeroConstant (const AST::ObjectProperty& v)
    {
        if (auto c = AST::getAsFoldedConstant (v))
            if (c->isZero())
                return {};

        return createValueReader (v);
    }

    ValueReader createAddInt32 (ValueReader a, ValueReader b)
    {
        return b ? createBinaryOp (AST::BinaryOpTypeEnum::Enum::add, { currentBlock->context.allocator.int32Type,
                                                                       currentBlock->context.allocator.int32Type }, a, b)
                 : a;
    }

    template <typename CreateOperation>
    void createLoopOrUnrolledList (int32_t numIterations, CreateOperation&& createOp)
    {
        if (numIterations > maxNumUnrolledIterations)
        {
            auto& context = currentBlock->context;
            auto indexVar = createTempVariableReference (context.allocator.int32Type, {}, true, "_index");

            auto& loop = context.allocate<AST::LoopStatement>();
            auto loopStatus = builder.beginLoop();
            auto oldLoop = currentLoop;
            currentLoop = loop;

            auto indexReader = builder.createReaderForReference (indexVar);
            createOp (indexReader);

            auto isIndexAtEnd = createBinaryOp (AST::BinaryOpTypeEnum::Enum::equals,
                                                { context.allocator.boolType, context.allocator.int32Type },
                                                indexReader, builder.createConstantInt32 (numIterations - 1));

            auto ifStatus = builder.beginIfStatement (isIndexAtEnd, true, false);
            emitBreak (loop);
            builder.endIfStatement (ifStatus);

            builder.addAddValueToInteger (indexVar, 1);

            builder.endLoop (loopStatus);
            currentLoop = oldLoop;
            resolveBreaks (loop);
        }
        else
        {
            for (int32_t i = 0; i < numIterations; ++i)
                createOp (builder.createConstantInt32 (i));
        }
    }

    ValueReader createPreOrPostIncOrDec (AST::ValueBase& target, bool increment, bool isPost)
    {
        int32_t delta = increment ? 1 : -1;
        auto& type = target.getResultType()->skipConstAndRefModifiers();
        auto ref = createValueReference (target, false);

        if (isPost)
        {
            postStatementOperationStack.push_back ([this, &type, ref, delta]
            {
                emitIncOrDecStatement (type, ref, delta);
            });
        }
        else
        {
            emitIncOrDecStatement (type, ref, delta);
        }

        return builder.createReaderForReference (ref);
    }

    ValueReader createValueMetaFunction (const AST::ValueMetaFunction& m)
    {
        if (m.isGetSizeOp())
            if (auto v = AST::castToValue (m.getSource()))
                if (auto type = v->getResultType())
                    if (type->skipConstAndRefModifiers().isSlice())
                        return builder.createGetSliceSize (createValueReader (*v));

        CMAJ_ASSERT_FALSE;
        return {};
    }
};

}
