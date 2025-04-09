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

struct AddWrapFunctions  : public AST::NonParameterisedObjectVisitor
{
    using super = AST::NonParameterisedObjectVisitor;
    using super::visit;

    AddWrapFunctions (AST::Namespace& root, bool onlyApplyForModifiers_)
      : super (root.context.allocator), rootNamespace (root),
        intrinsicsNamespace (*findIntrinsicsNamespace (root)),
        onlyApplyForModifiers (onlyApplyForModifiers_)
    {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::BinaryOperator& b) override
    {
        super::visit (b);

        if (onlyApplyForModifiers)
            return;

        insertWrapFunctionIfNeeded (b, b);
    }

    void visit (AST::Cast& c) override
    {
        super::visit (c);

        if (onlyApplyForModifiers)
            return;

        if (c.arguments.size() == 1)
            insertWrapFunctionIfNeeded (c, AST::castToValueRef (c.arguments.front()));
    }

    void visit (AST::PreOrPostIncOrDec& p) override
    {
        super::visit (p);

        auto& target = AST::castToValueRef (p.target);
        auto& type = target.getResultType()->skipConstAndRefModifiers();

        if (auto bounded = type.getAsBoundedType())
        {
            auto& function = getOrCreateBoundedPreOrPostIncFunction (p.isIncrement, p.isPost,
                                                                     bounded->isClamp, bounded->getBoundedIntLimit());
            p.replaceWith (AST::createFunctionCall (p, function, target));
        }
    }

    void visit (AST::InPlaceOperator& op) override
    {
        super::visit (op);

        auto& target = AST::castToValueRef (op.target);
        auto& type = target.getResultType()->skipConstAndRefModifiers();

        if (auto bounded = type.getAsBoundedType())
        {
            auto& resultValue = AST::createBinaryOp (op, op.op.get(), target, AST::castToValueRef (op.source));
            auto& boundedResult = createWrapOrClampExpression (resultValue, *bounded);
            auto& assignment = AST::createAssignment (op.context, target, boundedResult);

            op.replaceWith (assignment);
        }
    }

    void visit (AST::GetElement& g) override
    {
        super::visit (g);

        if (onlyApplyForModifiers)
            return;

        bool anyWrapsAdded = false;

        for (uint32_t i = 0; i < g.indexes.size(); ++i)
        {
            if (auto wrapSizeNeeded = validation::getConstantWrappingSizeToApplyToIndex (g, i))
            {
                auto& index = AST::castToValueRef (g.indexes[i]);
                auto knownRange = index.getKnownIntegerRange();

                if (knownRange.isValid() && AST::IntegerRange { 0, *wrapSizeNeeded }.contains (knownRange))
                    continue; // no need to wrap

                auto& wrapped = createWrapOrClampExpression (index, false, *wrapSizeNeeded);
                g.indexes[i].getAsObjectProperty()->referTo (wrapped);
                anyWrapsAdded = true;
            }
        }

        if (anyWrapsAdded)
            return;
    }

    void visit (AST::WriteToEndpoint& w) override
    {
        super::visit (w);

        if (onlyApplyForModifiers)
            return;

        if (w.targetIndex != nullptr)
        {
            auto endpointArraySize = static_cast<AST::ArraySize> (*w.getEndpoint()->getArraySize());
            auto index = AST::castToValue (w.targetIndex);
            auto indexType = index->getResultType();

            if (! indexType->isBoundedType() || indexType->getAsBoundedType()->getBoundedIntLimit() > endpointArraySize)
            {
                auto& wrapped = createWrapOrClampExpression (*index, false, endpointArraySize);
                w.targetIndex.referTo (wrapped);
            }
        }
    }

    void insertWrapFunctionIfNeeded (AST::ValueBase& valueToReplace, AST::ValueBase& sourceValue)
    {
        if (auto type = valueToReplace.getResultType())
            if (auto bounded = type->skipConstAndRefModifiers().getAsBoundedType())
                valueToReplace.replaceWith ([&]() -> AST::ValueBase&
                                            { return createWrapOrClampExpression (sourceValue, *bounded); });
    }

    static ptr<AST::ValueBase> createConstantWrappedIndex (AST::Object& index, bool isClamp, AST::ArraySize size)
    {
        if (auto constIndex = AST::getAsFoldedConstant (index))
        {
            if (auto intIndex = constIndex->getAsInt64())
            {
                auto unwrappedIndex = static_cast<int64_t> (*intIndex);

                auto wrappedIndex = isClamp ? AST::clamp (unwrappedIndex, static_cast<int64_t> (size))
                                            : AST::wrap  (unwrappedIndex, static_cast<int64_t> (size));

                if (wrappedIndex == unwrappedIndex)
                    return constIndex;

                return index.context.allocator.createConstantInt32 (static_cast<int32_t> (wrappedIndex));
            }
        }

        return {};
    }

    AST::ValueBase& createWrapOrClampExpression (AST::ValueBase& index, const AST::BoundedType& boundedType)
    {
        return createWrapOrClampExpression (index, boundedType.isClamp, boundedType.getBoundedIntLimit());
    }

    AST::ValueBase& createWrapOrClampExpression (AST::ValueBase& index, bool isClamp, AST::ArraySize size)
    {
        if (auto constIndex = createConstantWrappedIndex (index, isClamp, size))
            return *constIndex;

        if (! isClamp && choc::math::isPowerOf2 (size))
            return AST::createBinaryOp (index.context, AST::BinaryOpTypeEnum::Enum::bitwiseAnd,
                                        AST::createCastIfNeeded (index.context.allocator.int32Type, AST::castToRef<AST::ValueBase> (index)),
                                        index.context.allocator.createConstantInt32 (static_cast<int32_t> (size - 1)));

        auto& function = getOrCreateWrapOrClampFunction (isClamp, size);
        return AST::createFunctionCall (index.context, function, index);
    }

    AST::Function& createIntrinsicsFunctionReturningBoundedType (AST::PooledString name, bool isClamp, AST::ArraySize size)
    {
        auto& resultType = intrinsicsNamespace.context.allocate<AST::BoundedType>();
        resultType.limit.referTo (intrinsicsNamespace.context.allocator.createConstantInt32 (static_cast<int32_t> (size)));
        resultType.isClamp = isClamp;

        return AST::createFunctionInModule (intrinsicsNamespace, resultType, name);
    }

    AST::Function& getOrCreateWrapOrClampFunction (bool isClamp, AST::ArraySize size)
    {
        CMAJ_ASSERT (size > 0);
        auto name = intrinsicsNamespace.getStringPool().get ((isClamp ? "_clamp_" : "_wrap_") + std::to_string (size));

        if (auto f = intrinsicsNamespace.findFunction (name, 1))
            return *f;

        auto& f = createIntrinsicsFunctionReturningBoundedType (name, isClamp, size);
        auto paramRef = AST::addFunctionParameter (f, allocator.int32Type, "n");

        auto& mainBlock = *f.getMainBlock();
        auto& sizeConst = allocator.createConstantInt32 (static_cast<int32_t> (size));

        if (isClamp)
            createClampFunction (mainBlock, paramRef, sizeConst);
        else
            createWrapFunction (mainBlock, paramRef, sizeConst);

        CMAJ_ASSERT (intrinsicsNamespace.findFunction (name, 1) == f);
        return f;
    }

    void createWrapFunction (AST::ScopeBlock& block, AST::VariableReference& param, AST::ConstantValueBase& size)
    {
        auto& context = block.context;
        auto& nModSize = AST::createBinaryOp (context, AST::BinaryOpTypeEnum::Enum::modulo, param, size);
        auto& x = AST::createLocalVariableRef (block, "x", nModSize);

        auto& xLessThanZero = AST::createBinaryOp (context, AST::BinaryOpTypeEnum::Enum::lessThan,
                                                   x, allocator.createConstantInt32 (0));
        auto& xPlusSize = AST::createAdd (context, x, size);

        AST::addReturnStatement (block, AST::createTernary (context, xLessThanZero, xPlusSize, x));
    }

    void createClampFunction (AST::ScopeBlock& block, AST::VariableReference& param, AST::ConstantValueBase& size)
    {
        auto& context = block.context;

        auto& zero = allocator.createConstantInt32 (0);
        auto& sizeMinus1 = allocator.createConstantInt32 (*size.getAsInt32() - 1);
        auto& nLessThanZero = AST::createBinaryOp (context, AST::BinaryOpTypeEnum::Enum::lessThan, param, zero);
        auto& nGreaterThanSizeMinus1 = AST::createBinaryOp (context, AST::BinaryOpTypeEnum::Enum::greaterThan, param, sizeMinus1);

        auto& t1 = AST::createTernary (context, nLessThanZero, zero, param);
        auto& t2 = AST::createTernary (context, nGreaterThanSizeMinus1, sizeMinus1, t1);

        AST::addReturnStatement (block, t2);
    }

    AST::Function& getOrCreateBoundedPreOrPostIncFunction (bool isIncrement, bool isPost, bool isClamp, AST::ArraySize size)
    {
        CMAJ_ASSERT (size > 0);
        std::string name = (isClamp ? "_clamped_" : "_wrapped_");
        name += (isPost ? "post_" : "pre_");
        name += (isIncrement ? "inc_" : "dec_");
        name += std::to_string (size);

        if (auto f = intrinsicsNamespace.findFunction (name, 1))
            return *f;

        auto& f = createIntrinsicsFunctionReturningBoundedType (intrinsicsNamespace.getStringPool().get (name), isClamp, size);

        auto& paramType = allocator.allocate<AST::MakeConstOrRef> (f.context);
        paramType.source.referTo (allocator.createInt32Type());
        paramType.makeRef = true;

        auto paramRef = AST::addFunctionParameter (f, paramType, "n");

        auto& mainBlock = *f.getMainBlock();
        auto& context = mainBlock.context;

        auto op = isIncrement ? AST::BinaryOpTypeEnum::Enum::add
                              : AST::BinaryOpTypeEnum::Enum::subtract;
        auto& one = allocator.createConstantInt32 (1);

        auto& resultValue = AST::createBinaryOp (context, op, paramRef, one);
        auto& boundedResult = createWrapOrClampExpression (resultValue, isClamp, size);

        if (isPost)
        {
            auto& result = AST::createLocalVariableRef (mainBlock, "result", paramRef);
            AST::addAssignment (mainBlock, paramRef, boundedResult);
            AST::addReturnStatement (mainBlock, result);
        }
        else
        {
            auto& result = AST::createLocalVariableRef (mainBlock, "result", boundedResult);
            AST::addAssignment (mainBlock, paramRef, result);
            AST::addReturnStatement (mainBlock, result);
        }

        return f;
    }

    AST::Namespace& rootNamespace;
    AST::Namespace& intrinsicsNamespace;
    bool onlyApplyForModifiers;
};


//==============================================================================
static inline void ensureWrapModifiersAreInRange (AST::Program& program, AST::Object& o)
{
    AddWrapFunctions (program.rootNamespace, true).visitObject (o);
}

//==============================================================================
static inline void replaceWrapTypes (AST::Program& program)
{
    struct ReplaceWrapTypes  : public AST::Visitor
    {
        using super = AST::Visitor;
        using super::visit;

        ReplaceWrapTypes (AST::Allocator& a) : super (a) {}

        void visit (AST::BoundedType& b) override
        {
            super::visit (b);
            b.replaceWith (b.context.allocator.createInt32Type());
        }
    };

    AddWrapFunctions (program.rootNamespace, false).visitObject (program.rootNamespace);
    ReplaceWrapTypes (program.allocator).visitObject (program.rootNamespace);
}

}
