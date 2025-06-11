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


struct ValueBase  : public Expression
{
    ValueBase (const ObjectContext& c) : Expression (c) {}
    ~ValueBase() override = default;

    ValueBase* getAsValueBase() override                           { return this; }
    const ValueBase* getAsValueBase() const override               { return this; }
    bool isValueBase() const override                              { return true; }

    virtual ptr<const TypeBase> getResultType() const = 0;

    virtual bool isCompileTimeConstant() const                     { return false; }
    virtual ptr<ConstantValueBase> constantFold() const            { return {}; }
    virtual ptr<VariableDeclaration> getSourceVariable() const     { return {}; }

    virtual IntegerRange getKnownIntegerRange() const
    {
        if (auto t = getResultType())
            return t->getAddressableIntegerRange();

        return {};
    }
};

//==============================================================================
struct FunctionCall  : public ValueBase
{
    FunctionCall (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(FunctionCall, 26)

    ptr<const TypeBase> getResultType() const override
    {
        if (auto fn = getTargetFunction())
            return castToTypeBase (fn->returnType);

        return {};
    }

    bool isCompileTimeConstant() const override
    {
        return false; // TODO: pure function folding
    }

    void addSideEffects (SideEffects& effects) const override
    {
        if (auto f = getTargetFunction())
        {
            if (! effects.modifiesStateVariables)
            {
                SideEffects functionEffects;
                functionEffects.add (f);
                effects.modifiesStateVariables = functionEffects.modifiesStateVariables;
            }

            auto paramTypes = f->getParameterTypes();

            for (size_t i = 0; i < arguments.size(); ++i)
            {
                if (paramTypes[i]->isNonConstReference())
                    effects.addWritten (arguments[i]);
                else
                    effects.add (arguments[i]);
            }
        }
    }

    ptr<ConstantValueBase> constantFold() const override
    {
        auto intrinsic = getIntrinsicType();

        if (intrinsic != Intrinsic::Type::unknown)
        {
            ObjectRefVector<const ConstantValueBase> constantArgs;

            for (auto& arg : arguments)
            {
                if (auto v = castToValue (arg))
                {
                    if (auto c = v->constantFold())
                    {
                        constantArgs.push_back (*c);
                        continue;
                    }
                }

                return {};
            }

            return Intrinsic::constantFoldIntrinsicCall (intrinsic, constantArgs);
        }

        return {};
    }

    ptr<Function> getTargetFunction() const
    {
        return castToFunction (targetFunction);
    }

    mutable std::optional<Intrinsic::Type> intrinsicType;

    Intrinsic::Type getIntrinsicType() const
    {
        if (intrinsicType)
            return *intrinsicType;

        if (auto f = getTargetFunction())
        {
            intrinsicType = f->getIntrinsic();
            return *intrinsicType;
        }

        return Intrinsic::Type::unknown;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);

        for (auto& a : arguments)
            visitObjectIfPossible (a, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || arguments.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference,  targetFunction) \
        X (2, ListProperty,     arguments) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Cast  : public ValueBase
{
    Cast (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Cast, 27)

    void writeSignature (SignatureBuilder& sig) const override { sig << targetType << arguments; }

    ptr<const TypeBase> getResultType() const override    { return castToTypeBase (targetType); }

    void addSideEffects (SideEffects& effects) const override
    {
        for (auto& a : arguments)
            effects.add (a);
    }

    bool isCompileTimeConstant() const override
    {
        for (auto& arg : arguments)
            if (! AST::isCompileTimeConstant (arg))
                return false;

        return true;
    }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto destType = castToTypeBase (targetType))
        {
            if (destType->isResolved())
            {
                if (destType->containsSlice())
                    return {};

                return castConstantList (context.allocator, *destType, arguments.iterateAs<const Object>(), onlySilentCastsAllowed);
            }
        }

        return {};
    }

    static ptr<ConstantValueBase> castConstant (Allocator& a, const TypeBase& destType, const Object& source, bool onlySilentCastsAllowed)
    {
        if (! destType.isResolved())
            return {};

        if (auto constSource = getAsFoldedConstant (source))
        {
            if (onlySilentCastsAllowed)
                if (! TypeRules::canSilentlyCastTo (destType, *constSource))
                    return {};

            onlySilentCastsAllowed = false;

            if (destType.isPrimitiveBool())        return a.createConstant (constSource->getAsBool());
            if (destType.isPrimitiveInt32())       return a.createConstant (constSource->getAsInt32());
            if (destType.isPrimitiveInt64())       return a.createConstant (constSource->getAsInt64());
            if (destType.isPrimitiveFloat32())     return a.createConstant (constSource->getAsFloat32());
            if (destType.isPrimitiveFloat64())     return a.createConstant (constSource->getAsFloat64());
            if (destType.isPrimitiveComplex32())   return a.createConstant (constSource->getAsComplex32());
            if (destType.isPrimitiveComplex64())   return a.createConstant (constSource->getAsComplex64());
            if (destType.isPrimitiveString())      return a.createConstant (constSource->getAsString());
            if (destType.isEnum())                 return constSource;

            if (auto bounded = destType.getAsBoundedType())
            {
                if (auto value = constSource->getAsInt64())
                {
                    auto limit = static_cast<int64_t> (bounded->getBoundedIntLimit());
                    auto limitedValue = bounded->isClamp ? clamp (*value, limit)
                                                         : wrap  (*value, limit);

                    return a.createConstantInt32 (static_cast<int32_t> (limitedValue));
                }
            }

            if (destType.isFixedSizeAggregate())
            {
                if (auto sourceAgg = constSource->getAsConstantAggregate())
                {
                    auto& sourceType = sourceAgg->getType();
                    auto castType = TypeRules::getCastType (destType, sourceType);

                    if (castType != CastType::impossible && castType != CastType::widenToArrayLossless)
                        return castConstantList (a, destType, sourceAgg->values.iterateAs<const Object>(), onlySilentCastsAllowed);

                    if (sourceType.isSameType (destType, TypeBase::ComparisonFlags::duckTypeStructures))
                        return castConstantList (a, destType, sourceAgg->values.iterateAs<const Object>(), onlySilentCastsAllowed);
                }

                auto& agg = a.createObjectWithoutLocation<ConstantAggregate>();
                agg.type.createReferenceTo (destType);

                if (auto elementType = destType.getAggregateElementType (0))
                {
                    if (auto castValue = castConstant (a, *elementType, *constSource, onlySilentCastsAllowed))
                    {
                        agg.values.addReference (*castValue);
                        return agg;
                    }
                }
            }

            if (destType.isSlice())
            {
                auto& destElementType = *destType.getArrayOrVectorElementType();
                auto sourceAgg = AST::castTo<ConstantAggregate> (constSource);

                auto isSourceSliceable = [&]
                {
                    if (sourceAgg == nullptr)
                        return false;

                    for (auto& element : sourceAgg->values)
                        if (! castToValueRef (element).getResultType()
                                ->isSameType (destElementType, TypeBase::ComparisonFlags::failOnAllDifferences))
                            return false;

                    return true;
                };

                if (isSourceSliceable())
                    return *sourceAgg;

                if (constSource->getResultType()
                      ->isSameType (destElementType, TypeBase::ComparisonFlags::ignoreConst))
                {
                    auto& arrayType = createArrayOfType (destType.context, destElementType.skipConstAndRefModifiers(), 1);

                    auto& agg = destType.context.allocate<ConstantAggregate>();
                    agg.type.createReferenceTo (arrayType);
                    agg.setToSingleValue (*constSource);
                    return agg;
                }
            }
        }

        return {};
    }

    static ptr<ConstantValueBase> castConstantList (Allocator& a, const TypeBase& destType, ListProperty::TypedIterator<const Object> args, bool onlySilentCastsAllowed)
    {
        if (! destType.isResolved())
            return {};

        auto numArgs = args.size();

        if (numArgs == 1)
            if (auto c = castConstant (a, destType, args.front(), onlySilentCastsAllowed))
                return c;

        if (numArgs == 0)
            return destType.allocateConstantValue (a.getContextWithoutLocation());

        if (destType.isFixedSizeAggregate())
        {
            auto destSize = destType.getFixedSizeAggregateNumElements();

            if (numArgs == destSize)
            {
                auto& agg = a.createObjectWithoutLocation<ConstantAggregate>();
                agg.type.createReferenceTo (destType);
                agg.values.reserve (destSize);

                if (destType.isStruct())
                {
                    for (size_t i = 0; i < numArgs; ++i)
                    {
                        if (auto constArg = getAsFoldedConstant (args[i]))
                        {
                            if (auto elementType = destType.getAggregateElementType (i))
                            {
                                if (auto castValue = castConstant (a, *elementType, *constArg, onlySilentCastsAllowed))
                                {
                                    agg.values.addReference (*castValue);
                                    continue;
                                }
                            }
                        }

                        return {};
                    }
                }
                else
                {
                    auto innerType = destType.getArrayOrVectorElementType();

                    if (innerType == nullptr)
                        return {};

                    for (auto& arg : args)
                    {
                        if (auto constArg = getAsFoldedConstant (arg))
                        {
                            if (auto castValue = castConstant (a, *innerType, *constArg, onlySilentCastsAllowed))
                            {
                                agg.values.addReference (*castValue);
                                continue;
                            }
                        }

                        return {};
                    }
                }

                return agg;
            }
        }

        if (numArgs == 2 && destType.isPrimitiveComplex())
        {
            if (destType.isPrimitiveComplex32())
                if (auto constReal = castConstant (a, a.createFloat32Type(), args[0], true))
                    if (auto constImag = castConstant (a, a.createFloat32Type(), args[1], true))
                        return a.createConstantComplex32 (std::complex<float> (*constReal->getAsFloat32(), *constImag->getAsFloat32()));

            if (destType.isPrimitiveComplex64())
                if (auto constReal = castConstant (a, a.createFloat64Type(), args[0], true))
                    if (auto constImag = castConstant (a, a.createFloat64Type(), args[1], true))
                        return a.createConstantComplex64 (std::complex<double> (*constReal->getAsFloat64(), *constImag->getAsFloat64()));
        }

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (targetType, visit);

        for (auto& a : arguments)
            visitObjectIfPossible (a, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || targetType.containsStatement (other)
                || arguments.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  targetType) \
        X (2, ListProperty, arguments) \
        X (3, BoolProperty, onlySilentCastsAllowed) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct StateUpcast  : public ValueBase
{
    StateUpcast (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(StateUpcast, 28)

    void addSideEffects (SideEffects& effects) const override
    {
        effects.add (argument);
    }

    ptr<const TypeBase> getResultType() const override
    {
        return ptr<const TypeBase> (targetType->getAsTypeBase());
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (targetType, visit);
        visitObjectIfPossible (argument, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference,  targetType) \
        X (2, ChildObject,      argument) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct UnaryOperator  : public ValueBase
{
    UnaryOperator (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(UnaryOperator, 29)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << getSymbol() << input;
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto v = castToValue (input))
            return v->getResultType();

        return {};
    }

    bool isCompileTimeConstant() const override                 { return AST::isCompileTimeConstant (input); }
    void addSideEffects (SideEffects& effects) const override   { effects.add (input); }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto sourceVal = castToValue (input))
            if (auto sourceConst = sourceVal->constantFold())
                return applyLocationAndScopeTo (performOp (context.allocator, op, *sourceConst), context);

        return {};
    }

    std::string_view getSymbol() const
    {
        return getSymbolForOperator (op);
    }

    static std::string_view getSymbolForOperator (UnaryOpTypeEnum::Enum opType)
    {
        switch (opType)
        {
            case UnaryOpTypeEnum::Enum::negate:       return "-";
            case UnaryOpTypeEnum::Enum::logicalNot:   return "!";
            case UnaryOpTypeEnum::Enum::bitwiseNot:   return "~";

            default: CMAJ_ASSERT_FALSE; return {};
        }
    }

    static ptr<ConstantValueBase> performOp (Allocator& a, UnaryOpTypeEnum::Enum opType, const ConstantValueBase& source)
    {
        switch (opType)
        {
            case UnaryOpTypeEnum::Enum::negate:         return source.performUnaryNegate (a);
            case UnaryOpTypeEnum::Enum::logicalNot:     return source.performUnaryLogicalNot (a);
            case UnaryOpTypeEnum::Enum::bitwiseNot:     return source.performUnaryBitwiseNot (a);
            default: CMAJ_ASSERT_FALSE;
        }
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (input, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || input.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, UnaryOpTypeEnum, op) \
        X (2, ChildObject,     input) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct BinaryOperator  : public ValueBase
{
    BinaryOperator (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(BinaryOperator, 30)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << getSymbol() << lhs << rhs;
    }

    auto getOperatorTypes() const
    {
        if (auto v1 = castToValue (lhs))
            if (auto v2 = castToValue (rhs))
                if (auto t1 = v1->getResultType())
                    if (auto t2 = v2->getResultType())
                        if (t1->isResolved() && t2->isResolved())
                            return TypeRules::getBinaryOperatorTypes (op, *t1, *t2);

        return TypeRules::BinaryOperatorTypes::invalid (*this);
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto types = getOperatorTypes())
            return types.resultType;

        return {};
    }

    const ObjectContext& getLocationOfStartOfExpression() const override    { return lhs->getLocationOfStartOfExpression(); }

    bool isCompileTimeConstant() const override                 { return AST::isCompileTimeConstant (lhs) && AST::isCompileTimeConstant (rhs); }
    void addSideEffects (SideEffects& effects) const override   { effects.add (lhs); effects.add (rhs); }

    std::string_view getSymbol() const             { return getSymbolForOperator (op); }

    static std::string_view getSymbolForOperator (BinaryOpTypeEnum::Enum o)
    {
        switch (o)
        {
            case BinaryOpTypeEnum::Enum::add:                  return "+";
            case BinaryOpTypeEnum::Enum::subtract:             return "-";
            case BinaryOpTypeEnum::Enum::multiply:             return "*";
            case BinaryOpTypeEnum::Enum::exponent:             return "**";
            case BinaryOpTypeEnum::Enum::divide:               return "/";
            case BinaryOpTypeEnum::Enum::modulo:               return "%";
            case BinaryOpTypeEnum::Enum::bitwiseOr:            return "|";
            case BinaryOpTypeEnum::Enum::bitwiseAnd:           return "&";
            case BinaryOpTypeEnum::Enum::bitwiseXor:           return "^";
            case BinaryOpTypeEnum::Enum::logicalOr:            return "||";
            case BinaryOpTypeEnum::Enum::logicalAnd:           return "&&";
            case BinaryOpTypeEnum::Enum::equals:               return "==";
            case BinaryOpTypeEnum::Enum::notEquals:            return "!=";
            case BinaryOpTypeEnum::Enum::lessThan:             return "<";
            case BinaryOpTypeEnum::Enum::lessThanOrEqual:      return "<=";
            case BinaryOpTypeEnum::Enum::greaterThan:          return ">";
            case BinaryOpTypeEnum::Enum::greaterThanOrEqual:   return ">=";
            case BinaryOpTypeEnum::Enum::leftShift:            return "<<";
            case BinaryOpTypeEnum::Enum::rightShift:           return ">>";
            case BinaryOpTypeEnum::Enum::rightShiftUnsigned:   return ">>>";

            default: CMAJ_ASSERT_FALSE; return {};
        }
    }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto a = castToValue (lhs))
        {
            // handle special-cases to avoid having to evaluate failing short-circuited expressions, which are
            // allowed to contain errors in cases like static_assert
            if (op == BinaryOpTypeEnum::Enum::logicalAnd || op == BinaryOpTypeEnum::Enum::logicalOr)
            {
                if (auto constA = a->constantFold())
                {
                    if (auto result = constA->getAsBool())
                    {
                        if (op == BinaryOpTypeEnum::Enum::logicalAnd)
                        {
                            if (! *result)
                                return a->context.allocator.createConstantBool (false);
                        }
                        else
                        {
                            if (*result)
                                return a->context.allocator.createConstantBool (true);
                        }
                    }

                    if (auto b = castToValue (rhs))
                        if (auto constB = b->constantFold())
                            return applyLocationAndScopeTo (performOp (context.allocator, op, *constA, *constB, std::addressof (context), false), context);
                }
            }
            else
            {
                if (auto b = castToValue (rhs))
                    if (auto constA = a->constantFold())
                        if (auto constB = b->constantFold())
                            return applyLocationAndScopeTo (performOp (context.allocator, op, *constA, *constB, std::addressof (context), false), context);
            }
        }

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (lhs, visit);
        visitObjectIfPossible (rhs, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || lhs.containsStatement (other)
                || rhs.containsStatement (other);
    }

    static ptr<ConstantValueBase> performOp (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                             const ConstantValueBase& a, const ConstantValueBase& b,
                                             const ObjectContext* errorContext,
                                             bool returnZeroInErrorCases)
    {
        auto& typeA = *a.getResultType();
        auto& typeB = *b.getResultType();

        if (auto opTypes = TypeRules::getBinaryOperatorTypes (opType, typeA, typeB))
            return performOp (allocator, opType, opTypes.resultType, opTypes.operandType,
                              a, b, errorContext, returnZeroInErrorCases);

        return {};
    }

    static ptr<ConstantValueBase> performOp (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                             const TypeBase& resultType, const TypeBase& operandType,
                                             const ConstantValueBase& a, const ConstantValueBase& b,
                                             const ObjectContext* errorContext,
                                             bool returnZeroInErrorCases)
    {
        if (operandType.isVector())              return performVectorOp  (allocator, opType, resultType, a, b, errorContext, returnZeroInErrorCases);
        if (operandType.isPrimitiveComplex64())  return performOnComplex (allocator, opType, a.getAsComplex64(), b.getAsComplex64());
        if (operandType.isPrimitiveComplex32())  return performOnComplex (allocator, opType, a.getAsComplex32(), b.getAsComplex32());
        if (operandType.isPrimitiveFloat64())    return performOnFloats  (allocator, opType, a.getAsFloat64(),   b.getAsFloat64());
        if (operandType.isPrimitiveFloat32())    return performOnFloats  (allocator, opType, a.getAsFloat32(),   b.getAsFloat32());
        if (operandType.isPrimitiveInt64())      return performOnInts    (allocator, opType, a.getAsInt64(),     b.getAsInt64(), errorContext, returnZeroInErrorCases);
        if (operandType.isPrimitiveInt32())      return performOnInts    (allocator, opType, a.getAsInt32(),     b.getAsInt32(), errorContext, returnZeroInErrorCases);
        if (operandType.isPrimitiveBool())       return performOnBools   (allocator, opType, a.getAsBool(),      b.getAsBool());
        if (operandType.isPrimitiveString())     return performOnStrings (allocator, opType, a.getAsString(),    b.getAsString());
        if (operandType.isEnum())                return performOnEnums   (allocator, opType, a.getAsEnumIndex(), b.getAsEnumIndex());

        return {};
    }

    static ptr<ConstantValueBase> performVectorOp (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                                   const TypeBase& resultType,
                                                   const ConstantValueBase& a, const ConstantValueBase& b,
                                                   const ObjectContext* errorContext,
                                                   bool returnZeroInErrorCases)
    {
        auto numElementsA = a.getResultType()->getVectorSize();
        auto numElementsB = b.getResultType()->getVectorSize();
        CMAJ_ASSERT (numElementsA == numElementsB || numElementsA == 1 || numElementsB == 1);
        auto numElements = resultType.getVectorSize();

        auto& vec = allocator.createObjectWithoutLocation<ConstantAggregate>();
        vec.type.createReferenceTo (resultType);

        auto aggA = castTo<ConstantAggregate> (a);
        auto aggB = castTo<ConstantAggregate> (b);

        if (aggA == nullptr)
        {
            for (int64_t i = 0; i < numElements; ++i)
            {
                if (auto c2 = aggB->getAggregateElementValue(i))
                {
                    if (auto element = performOp (allocator, opType, a, *c2, errorContext, returnZeroInErrorCases))
                    {
                        vec.values.addReference (*element);
                        continue;
                    }
                }

                return {};
            }
        }
        else if (aggB == nullptr)
        {
            for (int64_t i = 0; i < numElements; ++i)
            {
                if (auto c1 = aggA->getAggregateElementValue(i))
                {
                    if (auto element = performOp (allocator, opType, *c1, b, errorContext, returnZeroInErrorCases))
                    {
                        vec.values.addReference (*element);
                        continue;
                    }
                }

                return {};
            }
        }
        else
        {
            for (int64_t i = 0; i < numElements; ++i)
            {
                if (auto c1 = aggA->getAggregateElementValue(i))
                {
                    if (auto c2 = aggB->getAggregateElementValue(i))
                    {
                        if (auto element = performOp (allocator, opType, *c1, *c2, errorContext, returnZeroInErrorCases))
                        {
                            vec.values.addReference (*element);
                            continue;
                        }
                    }
                }

                return {};
            }
        }

        return vec;
    }

    static ptr<ConstantValueBase> performOnBools (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                                  std::optional<bool> optA, std::optional<bool> optB)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::bitwiseXor:          return allocator.createConstantBool (a ^ b);
                case BinaryOpTypeEnum::Enum::bitwiseOr:
                case BinaryOpTypeEnum::Enum::logicalOr:           return allocator.createConstantBool (a || b);
                case BinaryOpTypeEnum::Enum::bitwiseAnd:
                case BinaryOpTypeEnum::Enum::logicalAnd:          return allocator.createConstantBool (a && b);
                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);

                case BinaryOpTypeEnum::Enum::lessThan:
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case BinaryOpTypeEnum::Enum::greaterThan:
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case BinaryOpTypeEnum::Enum::add:
                case BinaryOpTypeEnum::Enum::subtract:
                case BinaryOpTypeEnum::Enum::multiply:
                case BinaryOpTypeEnum::Enum::exponent:
                case BinaryOpTypeEnum::Enum::divide:
                case BinaryOpTypeEnum::Enum::modulo:
                case BinaryOpTypeEnum::Enum::leftShift:
                case BinaryOpTypeEnum::Enum::rightShift:
                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

   #if defined(__clang__)
    #define CMAJ_NO_SIGNED_INTEGER_OVERFLOW_WARNING __attribute__((no_sanitize("signed-integer-overflow")))
   #else
    #define CMAJ_NO_SIGNED_INTEGER_OVERFLOW_WARNING
   #endif

    template <typename IntType>
    static ptr<ConstantValueBase> CMAJ_NO_SIGNED_INTEGER_OVERFLOW_WARNING performOnInts (Allocator& allocator,
                                                                                         BinaryOpTypeEnum::Enum opType,
                                                                                         std::optional<IntType> optA,
                                                                                         std::optional<IntType> optB,
                                                                                         const ObjectContext* errorContext,
                                                                                         bool returnZeroInErrorCases)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::add:                 return allocator.createConstant (a + b);
                case BinaryOpTypeEnum::Enum::subtract:            return allocator.createConstant (a - b);
                case BinaryOpTypeEnum::Enum::multiply:            return allocator.createConstant (a * b);
                case BinaryOpTypeEnum::Enum::exponent:            return allocator.createConstant (std::pow (a, b));
                case BinaryOpTypeEnum::Enum::bitwiseOr:           return allocator.createConstant (a | b);
                case BinaryOpTypeEnum::Enum::bitwiseAnd:          return allocator.createConstant (a & b);
                case BinaryOpTypeEnum::Enum::bitwiseXor:          return allocator.createConstant (a ^ b);

                case BinaryOpTypeEnum::Enum::logicalOr:           return allocator.createConstantBool (a || b);
                case BinaryOpTypeEnum::Enum::logicalAnd:          return allocator.createConstantBool (a && b);
                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);
                case BinaryOpTypeEnum::Enum::lessThan:            return allocator.createConstantBool (a <  b);
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:     return allocator.createConstantBool (a <= b);
                case BinaryOpTypeEnum::Enum::greaterThan:         return allocator.createConstantBool (a >  b);
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:  return allocator.createConstantBool (a >= b);

                case BinaryOpTypeEnum::Enum::divide:
                    if (checkDivideByZero (b, errorContext))
                        return allocator.createConstant (a / b);

                    if (returnZeroInErrorCases)
                        return allocator.createConstant (IntType());

                    return {};

                case BinaryOpTypeEnum::Enum::modulo:
                    if (checkModuloZero (b, errorContext))
                        return allocator.createConstant (a % b);

                    if (returnZeroInErrorCases)
                        return allocator.createConstant (IntType());

                    return {};

                case BinaryOpTypeEnum::Enum::leftShift:
                    return allocator.createConstant (b >= 0 ? static_cast<IntType> (((uint64_t) a) << b) : IntType());

                case BinaryOpTypeEnum::Enum::rightShift:
                    return b >= 0 ? allocator.createConstant (a >> b)
                                  : allocator.createConstant (static_cast<IntType> (a >= 0 ? 0 : -1));

                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                    return sizeof (a) == sizeof (int32_t) ? allocator.createConstant (b >= 0 ? static_cast<IntType> (((uint32_t) a) >> b) : IntType())
                                                          : allocator.createConstant (b >= 0 ? static_cast<IntType> (((uint64_t) a) >> b) : IntType());

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

    template <typename FloatType>
    static ptr<ConstantValueBase> performOnFloats (Allocator& allocator,
                                                   BinaryOpTypeEnum::Enum opType,
                                                   std::optional<FloatType> optA,
                                                   std::optional<FloatType> optB)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::add:                 return allocator.createConstant (a + b);
                case BinaryOpTypeEnum::Enum::subtract:            return allocator.createConstant (a - b);
                case BinaryOpTypeEnum::Enum::multiply:            return allocator.createConstant (a * b);
                case BinaryOpTypeEnum::Enum::exponent:            return allocator.createConstant (std::pow (a, b));

                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);
                case BinaryOpTypeEnum::Enum::lessThan:            return allocator.createConstantBool (a <  b);
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:     return allocator.createConstantBool (a <= b);
                case BinaryOpTypeEnum::Enum::greaterThan:         return allocator.createConstantBool (a >  b);
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:  return allocator.createConstantBool (a >= b);
                case BinaryOpTypeEnum::Enum::divide:              return allocator.createConstant (a / b);
                case BinaryOpTypeEnum::Enum::modulo:              return allocator.createConstant (std::fmod (a, b));

                case BinaryOpTypeEnum::Enum::bitwiseOr:
                case BinaryOpTypeEnum::Enum::bitwiseAnd:
                case BinaryOpTypeEnum::Enum::bitwiseXor:
                case BinaryOpTypeEnum::Enum::leftShift:
                case BinaryOpTypeEnum::Enum::rightShift:
                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                case BinaryOpTypeEnum::Enum::logicalOr:
                case BinaryOpTypeEnum::Enum::logicalAnd:
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

    template <typename FloatType>
    static ptr<ConstantValueBase> performOnComplex (Allocator& allocator,
                                                    BinaryOpTypeEnum::Enum opType,
                                                    std::optional<std::complex<FloatType>> optA,
                                                    std::optional<std::complex<FloatType>> optB)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::add:                 return allocator.createConstant (a + b);
                case BinaryOpTypeEnum::Enum::subtract:            return allocator.createConstant (a - b);
                case BinaryOpTypeEnum::Enum::multiply:            return allocator.createConstant (a * b);
                case BinaryOpTypeEnum::Enum::exponent:            return allocator.createConstant (std::pow (a, b));
                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);
                case BinaryOpTypeEnum::Enum::divide:              return allocator.createConstant (a / b);

                case BinaryOpTypeEnum::Enum::modulo:
                case BinaryOpTypeEnum::Enum::lessThan:
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case BinaryOpTypeEnum::Enum::greaterThan:
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case BinaryOpTypeEnum::Enum::bitwiseOr:
                case BinaryOpTypeEnum::Enum::bitwiseAnd:
                case BinaryOpTypeEnum::Enum::bitwiseXor:
                case BinaryOpTypeEnum::Enum::leftShift:
                case BinaryOpTypeEnum::Enum::rightShift:
                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                case BinaryOpTypeEnum::Enum::logicalOr:
                case BinaryOpTypeEnum::Enum::logicalAnd:
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

    static ptr<ConstantValueBase> performOnStrings (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                                    std::optional<std::string_view> optA, std::optional<std::string_view> optB)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);

                case BinaryOpTypeEnum::Enum::bitwiseXor:
                case BinaryOpTypeEnum::Enum::bitwiseOr:
                case BinaryOpTypeEnum::Enum::logicalOr:
                case BinaryOpTypeEnum::Enum::bitwiseAnd:
                case BinaryOpTypeEnum::Enum::logicalAnd:
                case BinaryOpTypeEnum::Enum::lessThan:
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case BinaryOpTypeEnum::Enum::greaterThan:
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case BinaryOpTypeEnum::Enum::add:
                case BinaryOpTypeEnum::Enum::subtract:
                case BinaryOpTypeEnum::Enum::multiply:
                case BinaryOpTypeEnum::Enum::exponent:
                case BinaryOpTypeEnum::Enum::divide:
                case BinaryOpTypeEnum::Enum::modulo:
                case BinaryOpTypeEnum::Enum::leftShift:
                case BinaryOpTypeEnum::Enum::rightShift:
                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

    static ptr<ConstantValueBase> performOnEnums (Allocator& allocator, BinaryOpTypeEnum::Enum opType,
                                                  std::optional<int32_t> optA, std::optional<int32_t> optB)
    {
        if (optA && optB)
        {
            auto a = *optA;
            auto b = *optB;

            switch (opType)
            {
                case BinaryOpTypeEnum::Enum::equals:              return allocator.createConstantBool (a == b);
                case BinaryOpTypeEnum::Enum::notEquals:           return allocator.createConstantBool (a != b);

                case BinaryOpTypeEnum::Enum::bitwiseXor:
                case BinaryOpTypeEnum::Enum::bitwiseOr:
                case BinaryOpTypeEnum::Enum::logicalOr:
                case BinaryOpTypeEnum::Enum::bitwiseAnd:
                case BinaryOpTypeEnum::Enum::logicalAnd:
                case BinaryOpTypeEnum::Enum::lessThan:
                case BinaryOpTypeEnum::Enum::lessThanOrEqual:
                case BinaryOpTypeEnum::Enum::greaterThan:
                case BinaryOpTypeEnum::Enum::greaterThanOrEqual:
                case BinaryOpTypeEnum::Enum::add:
                case BinaryOpTypeEnum::Enum::subtract:
                case BinaryOpTypeEnum::Enum::multiply:
                case BinaryOpTypeEnum::Enum::exponent:
                case BinaryOpTypeEnum::Enum::divide:
                case BinaryOpTypeEnum::Enum::modulo:
                case BinaryOpTypeEnum::Enum::leftShift:
                case BinaryOpTypeEnum::Enum::rightShift:
                case BinaryOpTypeEnum::Enum::rightShiftUnsigned:
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        return {};
    }

    template <typename Type>
    static bool checkDivideByZero (Type n, const ObjectContext* errorContext)
    {
        if (n != Type())
            return true;

        if (errorContext != nullptr)
            errorContext->throwError (Errors::divideByZero());

        return false;
    }

    template <typename Type>
    static bool checkModuloZero (Type n, const ObjectContext* errorContext)
    {
        if (n != Type())
            return true;

        if (errorContext != nullptr)
            errorContext->throwError (Errors::moduloZero());

        return false;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, BinaryOpTypeEnum, op) \
        X (2, ChildObject,      lhs) \
        X (3, ChildObject,      rhs) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct TernaryOperator  : public ValueBase
{
    TernaryOperator (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(TernaryOperator, 31)

    void writeSignature (SignatureBuilder& sig) const override { sig << "?" << condition << trueValue << falseValue; }
    const ObjectContext& getLocationOfStartOfExpression() const override    { return condition->getLocationOfStartOfExpression(); }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto tv = castToValue (trueValue))
        {
            if (auto fv = castToValue (falseValue))
            {
                auto trueType = tv->getResultType();
                auto falseType = fv->getResultType();

                if (trueType == nullptr || ! trueType->isResolved())
                    return {};

                if (falseType == nullptr || ! falseType->isResolved())
                    return {};

                if (trueType->isSameType (*falseType, TypeBase::ComparisonFlags::ignoreConst | TypeBase::ComparisonFlags::ignoreReferences))
                    return trueType;

                if (TypeRules::canSilentlyCastTo (*trueType, *falseType) &&
                    TypeRules::canSilentlyCastTo (*falseType, *trueType))
                    return {};

                if (TypeRules::canSilentlyCastTo (*trueType, *falseType))
                    return trueType;

                if (TypeRules::canSilentlyCastTo (*falseType, *trueType))
                    return falseType;
            }
        }

        return {};
    }

    bool isCompileTimeConstant() const override
    {
        return AST::isCompileTimeConstant (condition)
            && AST::isCompileTimeConstant (trueValue)
            && AST::isCompileTimeConstant (falseValue);
    }

    void addSideEffects (SideEffects& effects) const override
    {
        effects.add (condition);
        effects.add (trueValue);
        effects.add (falseValue);
    }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto conditionConst = getAsFoldedConstant (condition))
        {
            if (auto v = conditionConst->getAsConstantBool())    return constantFold (v->value);
            if (auto v = conditionConst->getAsConstantInt32())   return constantFold (v->value != 0);
            if (auto v = conditionConst->getAsConstantInt64())   return constantFold (v->value != 0);
        }

        return {};
    }

    ptr<ConstantValueBase> constantFold (bool isTrue) const
    {
        if (auto t = getAsFoldedConstant (trueValue))
            if (auto f = getAsFoldedConstant (falseValue))
                return (isTrue ? t : f)->constantFold();

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (condition, visit);
        visitObjectIfPossible (trueValue, visit);
        visitObjectIfPossible (falseValue, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || condition.containsStatement (other)
                || trueValue.containsStatement (other)
                || falseValue.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, condition) \
        X (2, ChildObject, trueValue) \
        X (3, ChildObject, falseValue) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct VariableReference  : public ValueBase
{
    VariableReference (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(VariableReference, 32)

    void writeSignature (SignatureBuilder& sig) const override  { sig << variable; }
    VariableDeclaration& getVariable() const                    { return castToVariableDeclarationRef (variable); }

    PooledString getName() const override                       { return getVariable().getName(); }
    bool isCompileTimeConstant() const override                 { return getVariable().isCompileTimeConstant(); }
    void addSideEffects (SideEffects&) const override           {}

    IntegerRange getKnownIntegerRange() const override          { return getVariable().getKnownIntegerRange(); }

    ptr<ConstantValueBase> constantFold() const override
    {
        auto& v = getVariable();

        if (v.isConstant && v.initialValue != nullptr)
            return getAsFoldedConstant (v.initialValue);

        return {};
    }

    ptr<VariableDeclaration> getSourceVariable() const override
    {
        return getVariable();
    }

    ptr<const TypeBase> getPossiblyUnresolvedType() const
    {
        return getVariable().getType();
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto t = getPossiblyUnresolvedType())
            if (t->isResolved())
                return t;

        return {};
    }

    bool canConstantFoldProperty (const Property&) override   { return false; }

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference, variable) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct GetElement  : public ValueBase
{
    GetElement (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(GetElement, 69)

    Object& getSingleIndex() const
    {
        CMAJ_ASSERT (indexes.size() == 1);
        return indexes[0].getObjectRef();
    }

    ObjectProperty& getSingleIndexProperty()
    {
        CMAJ_ASSERT (indexes.size() == 1);
        return *indexes[0].getAsObjectProperty();
    }

    ptr<VariableDeclaration> getSourceVariable() const override
    {
        if (auto parentValue = castToValue (parent))
            return parentValue->getSourceVariable();

        return {};
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto parentValue = castToValue (parent))
        {
            if (auto parentType = parentValue->getResultType())
            {
                auto& t = parentType->skipConstAndRefModifiers();

                if (auto s = t.getAsStructType())
                    return castToTypeBase (s->tupleType);

                if (auto a = t.getAsArrayType())
                    return a->getElementType (static_cast<uint32_t> (indexes.size()) - 1);

                if (auto v = t.getAsVectorType())
                    return v->getInnermostElementType();
            }
        }

        return {};
    }

    bool isCompileTimeConstant() const override
    {
        if (! AST::isCompileTimeConstant (parent))
            return false;

        for (auto& i : indexes)
            if (! AST::isCompileTimeConstant (i))
                return false;

        return true;
    }

    void addSideEffects (SideEffects& effects) const override   { effects.add (parent); effects.add (indexes); }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto constParent = getAsFoldedConstant (parent))
        {
            if (auto agg = constParent->getAsConstantAggregate())
            {
                auto& aggType = agg->getType();
                bool isZero = agg->isZero();
                choc::SmallVector<ArraySize, 8> constantIndexes;

                if (aggType.isVectorOrArray() && aggType.getNumDimensions() != indexes.size())
                    return {};

                for (uint32_t i = 0; i < indexes.size(); ++i)
                {
                    if (isZero)
                    {
                        if (castToValue (indexes[i]) == nullptr)
                            return {};

                        constantIndexes.push_back (0);

                    }
                    else
                    {
                        auto indexValue = getAsFoldedConstant (indexes[i]);

                        if (indexValue == nullptr)
                            return {};

                        auto safeIndex = TypeRules::checkAndGetArrayIndex (getContext (indexes[i]), *indexValue, aggType, i, false);

                        if (safeIndex >= agg->getNumElements())
                            return {};

                        constantIndexes.push_back (safeIndex);
                    }
                }

                if (constantIndexes.size() == 1)
                    return getAsFoldedConstant (agg->getOrCreateAggregateElementValue (constantIndexes[0]));
            }
        }

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (parent, visit);
        visitObjectIfPossible (indexes, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || parent.containsStatement (other)
                || indexes.containsStatement (other);
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return parent->getLocationOfStartOfExpression();
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  parent) \
        X (2, ListProperty, indexes) \
        X (3, BoolProperty, isAtFunction) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct GetArrayOrVectorSlice  : public ValueBase
{
    GetArrayOrVectorSlice (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(GetArrayOrVectorSlice, 34)

    ptr<VariableDeclaration> getSourceVariable() const override
    {
        return castToValueRef (parent).getSourceVariable();
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto parentValue = castToValue (parent))
        {
            if (auto parentType = parentValue->getResultType())
            {
                if (auto resultSize = getSliceSize())
                {
                    parentType = parentType->skipConstAndRefModifiers();

                    if (auto arrayType = parentType->getAsArrayType())
                        return createArrayOfType (context,
                                                  arrayType->getInnermostElementTypeObject(),
                                                  static_cast<int32_t> (*resultSize));

                    if (auto vectorType = parentType->getAsVectorType())
                    {
                        auto& result = context.allocate<VectorType>();
                        result.elementType.referTo (vectorType->elementType);
                        result.numElements.setChildObject (context.allocator.createConstantInt32 (static_cast<int32_t> (*resultSize)));
                        return result;
                    }
                }
                else
                {
                    if (auto arrayType = parentType->skipConstAndRefModifiers().getAsArrayType())
                        return createSliceOfType (context, arrayType->getInnermostElementTypeRef());
                }
            }
        }

        return {};
    }

    bool isCompileTimeConstant() const override
    {
        return AST::isCompileTimeConstant (parent)
                && (start == nullptr || AST::isCompileTimeConstant (start))
                && (end   == nullptr || AST::isCompileTimeConstant (end));
    }

    void addSideEffects (SideEffects& effects) const override
    {
        effects.add (parent);
        effects.add (start);
        effects.add (end);
    }

    std::optional<IntegerRange> getResultRange (int64_t parentNumElements) const
    {
        int64_t startIndex = 0, endIndex = parentNumElements;

        if (auto s = start.getRawPointer())
        {
            if (auto asConst = getAsFoldedConstant (*s))
                startIndex = TypeRules::checkAndGetArrayIndex (s->context, *asConst);
            else
                return {};

            startIndex = TypeRules::convertArrayOrVectorIndexToValidRange (parentNumElements, startIndex);
        }

        if (auto e = end.getRawPointer())
        {
            if (auto asConst = getAsFoldedConstant (*e))
                endIndex = TypeRules::checkAndGetArrayIndex (e->context, *asConst);
            else
                return {};

            endIndex = TypeRules::convertArrayOrVectorIndexToValidRange (parentNumElements, endIndex);
        }

        return IntegerRange { startIndex, endIndex };
    }

    std::optional<int64_t> getParentSize() const
    {
        if (auto parentValue = castToValue (parent))
        {
            if (auto resultType = parentValue->getResultType())
            {
                auto& parentType = resultType->skipConstAndRefModifiers();

                if (auto arrayType = parentType.getAsArrayType())
                    if (! arrayType->isSlice())
                        if (auto sizeValue = arrayType->getConstantSize (0))
                            return sizeValue->getAsInt64();

                if (auto vectorType = parentType.getAsVectorType())
                    if (auto sizeValue = getAsFoldedConstant (vectorType->numElements))
                        return sizeValue->getAsInt64();
            }
        }

        return {};
    }

    std::optional<int64_t> getSliceSize (int64_t parentNumElements) const
    {
        if (auto range = getResultRange (parentNumElements))
            if (TypeRules::canBeSafelyCastToArraySize (range->size()))
                return range->size();

        if (auto addition = AST::castTo<AST::BinaryOperator> (end))
            if (addition->op == AST::BinaryOpTypeEnum::Enum::add)
                if (addition->lhs.getObject() == start.getObject() || addition->lhs.getObjectRef().isIdentical (start.getObjectRef()))
                    if (auto c = AST::getAsFoldedConstant (addition->rhs))
                        return c->getAsInt64();

        return {};
    }

    std::optional<int64_t> getSliceSize() const
    {
        if (auto parentSize = getParentSize())
            return getSliceSize (*parentSize);

        return {};
    }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto constParent = getAsFoldedConstant (parent))
            if (auto agg = constParent->getAsConstantAggregate())
                if (auto range = getResultRange (static_cast<int64_t> (agg->values.size())))
                    return agg->getElementSlice (*range);

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (parent, visit);
        visitObjectIfPossible (start, visit);
        visitObjectIfPossible (end, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || parent.containsStatement (other)
                || start.containsStatement (other)
                || end.containsStatement (other);
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return parent->getLocationOfStartOfExpression();
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, parent) \
        X (2, ChildObject, start) \
        X (3, ChildObject, end) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct GetStructMember  : public ValueBase
{
    GetStructMember (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(GetStructMember, 35)

    ptr<VariableDeclaration> getSourceVariable() const override
    {
        return castToValueRef (object).getSourceVariable();
    }

    ptr<const TypeBase> getResultType() const override
    {
        if (auto objectValue = castToValue (object))
        {
            if (auto objectType = objectValue->getResultType())
            {
                objectType = objectType->skipConstAndRefModifiers();

                if (auto structType = objectType->getAsStructType())
                    return structType->getTypeForMember (member.get());

                if (objectType->isComplexOrVectorOfComplex()
                     && (member.hasName (getStrings().real) || member.hasName (getStrings().imag)))
                {
                    auto& scalarType = objectType->isScalar32() ? context.allocator.float32Type
                                                                : context.allocator.float64Type;

                    if (auto vectorType = objectType->getAsVectorType())
                        return context.allocator.createVectorType (scalarType, castToExpressionRef (vectorType->numElements));

                    return scalarType;
                }
            }
        }

        return {};
    }

    bool isCompileTimeConstant() const override                 { return AST::isCompileTimeConstant (object); }
    void addSideEffects (SideEffects& effects) const override   { effects.add (object); }

    ptr<ConstantValueBase> constantFold() const override
    {
        if (auto constObject = getAsFoldedConstant (object))
        {
            if (auto agg = constObject->getAsConstantAggregate())
            {
                if (auto structType = castToSkippingReferences<StructType> (agg->type))
                {
                    auto memberIndex = structType->indexOfMember (member.get());

                    if (memberIndex < 0)
                        throwError (context, Errors::unknownMemberInStruct (member.get(), structType->name.get()));

                    auto index = static_cast<size_t> (memberIndex);

                    if (index >= agg->values.size())
                        return castToTypeBaseRef (structType->memberTypes[index]).allocateConstantValue (context);

                    if (auto memberValue = castToValue (agg->values[index]))
                        return memberValue->constantFold();
                }
            }
        }

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (object, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || object.containsStatement (other);
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return object->getLocationOfStartOfExpression();
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,    object) \
        X (2, StringProperty, member) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct PreOrPostIncOrDec  : public ValueBase
{
    PreOrPostIncOrDec (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(PreOrPostIncOrDec, 36)

    ptr<const TypeBase> getResultType() const override
    {
        if (auto v = castToValue (target))
            return v->getResultType();

        return {};
    }

    void addSideEffects (SideEffects& effects) const override               { effects.addWritten (target); }
    std::string_view getOperatorSymbol() const                              { return isIncrement ? "++" : "--"; }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (target, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || target.containsStatement (other);
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        if (isPost)
            return target->getLocationOfStartOfExpression();

        return ValueBase::getLocationOfStartOfExpression();
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, BoolProperty,  isIncrement) \
        X (2, BoolProperty,  isPost) \
        X (3, ChildObject,   target)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ReadFromEndpoint  : public ValueBase
{
    ReadFromEndpoint (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ReadFromEndpoint, 37)

    ptr<const TypeBase> getResultType() const override
    {
        if (cachedResultType)
            return cachedResultType;

        auto instance = castToSkippingReferences<EndpointInstance> (endpointInstance);

        if (auto endpointDeclaration = getEndpointDeclaration())
        {
            if (endpointDeclaration->isStream() || endpointDeclaration->isValue())
            {
                CMAJ_ASSERT (endpointDeclaration->dataTypes.size() == 1);

                if (auto t = castToTypeBase (endpointDeclaration->dataTypes[0]))
                {
                    if (t->isResolved())
                    {
                        if (endpointDeclaration->isArray())
                        {
                            if ( ! endpointDeclaration->getArraySize().has_value())
                                return {};

                            cachedResultType = createArrayOfType (endpointInstance.getObjectRef(), *t, endpointDeclaration->arraySize);
                        }
                        else if (instance && instance->getNodeArraySize())
                        {
                            cachedResultType = createArrayOfType (endpointInstance.getObjectRef(), *t, instance->getNode().arraySize);
                        }
                        else
                        {
                            cachedResultType = t;
                        }

                        return cachedResultType;
                    }
                }
            }
        }

        return {};
    }

    ptr<const GraphNode> getGraphNode() const
    {
        if (auto instance = castToSkippingReferences<EndpointInstance> (endpointInstance))
            return castTo<const AST::GraphNode> (instance->node);

        return {};
    }

    ptr<const EndpointDeclaration> getEndpointDeclaration() const
    {
        if (auto instance = castToSkippingReferences<EndpointInstance> (endpointInstance))
            return instance->getEndpoint (true);

        return {};
    }

    void addSideEffects (SideEffects&) const override    {}

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (endpointInstance, visit);
    }

    bool containsStatement (const Statement& other) const override
    {
        return this == std::addressof (other)
                || endpointInstance.containsStatement (other);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, endpointInstance) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

    mutable ptr<const TypeBase> cachedResultType;
};

//==============================================================================
struct ProcessorProperty  : public ValueBase
{
    ProcessorProperty (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ProcessorProperty, 38)

    void addSideEffects (SideEffects&) const override   {}
    bool isCompileTimeConstant() const override         { return property != ProcessorPropertyEnum::Enum::id; }

    ptr<const TypeBase> getResultType() const override
    {
        if (property == ProcessorPropertyEnum::Enum::id
             || property == ProcessorPropertyEnum::Enum::session
             || property == ProcessorPropertyEnum::Enum::latency)
            return context.allocator.int32Type;

        return context.allocator.processorFrequencyType;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ProcessorPropertyEnum, property) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
// Takes a type and returns a value
struct ValueMetaFunction  : public ValueBase
{
    ValueMetaFunction (const ObjectContext& c) : ValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ValueMetaFunction, 74)

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "meta" << op << arguments;
    }

    Object& getSource() const
    {
        return arguments.front().getObjectRef();
    }

    ptr<const TypeBase> getSourceType() const
    {
        if (auto t = castToTypeBase (getSource()))
            if (t->isResolved())
                return t;

        if (auto v = castToValue (getSource()))
            return v->getResultType();

        return {};
    }

    bool isGetSizeOp() const            { return op == ValueMetaFunctionTypeEnum::Enum::size; }
    bool isGetNumDimensionsOp() const   { return op == ValueMetaFunctionTypeEnum::Enum::numDimensions; }
    bool isAllocOp() const              { return op == ValueMetaFunctionTypeEnum::Enum::alloc; }

    bool hasCorrectNumArguments() const
    {
        return arguments.size() == (isAllocOp() ? 2 : 1);
    }

    bool isCompileTimeConstant() const override
    {
        return ! isGetSizeOp() || getSourceType()->isFixedSizeAggregate();
    }

    void addSideEffects (SideEffects&) const override   {}

    ptr<ConstantValueBase> constantFold() const override
    {
        if (! hasCorrectNumArguments())
            return {};

        if (isGetSizeOp())
            return getSize (getSource());

        if (isGetNumDimensionsOp())
        {
            if (auto num = getNumDimensions (getSource()))
                return context.allocator.createConstant (*num);

            return {};
        }

        if (auto type = getSourceType())
            return context.allocator.createConstantBool (getTypeProperty (*type, op.get()));

        return {};
    }

    static bool getTypeProperty (const AST::TypeBase& type, ValueMetaFunctionTypeEnum::Enum property)
    {
        switch (property)
        {
            case ValueMetaFunctionTypeEnum::Enum::isStruct:           return type.isStruct();
            case ValueMetaFunctionTypeEnum::Enum::isArray:            return type.isArray();
            case ValueMetaFunctionTypeEnum::Enum::isSlice:            return type.isSlice();
            case ValueMetaFunctionTypeEnum::Enum::isFixedSizeArray:   return type.isFixedSizeArray();
            case ValueMetaFunctionTypeEnum::Enum::isVector:           return type.isVector();
            case ValueMetaFunctionTypeEnum::Enum::isPrimitive:        return type.isPrimitive();
            case ValueMetaFunctionTypeEnum::Enum::isFloat:            return type.isPrimitiveFloat();
            case ValueMetaFunctionTypeEnum::Enum::isFloat32:          return type.isPrimitiveFloat32();
            case ValueMetaFunctionTypeEnum::Enum::isFloat64:          return type.isPrimitiveFloat64();
            case ValueMetaFunctionTypeEnum::Enum::isInt:              return type.isPrimitiveInt();
            case ValueMetaFunctionTypeEnum::Enum::isInt32:            return type.isPrimitiveInt32();
            case ValueMetaFunctionTypeEnum::Enum::isInt64:            return type.isPrimitiveInt64();
            case ValueMetaFunctionTypeEnum::Enum::isScalar:           return type.isScalar();
            case ValueMetaFunctionTypeEnum::Enum::isString:           return type.isPrimitiveString();
            case ValueMetaFunctionTypeEnum::Enum::isBool:             return type.isPrimitiveBool();
            case ValueMetaFunctionTypeEnum::Enum::isComplex:          return type.isPrimitiveComplex();
            case ValueMetaFunctionTypeEnum::Enum::isReference:        return type.isReference();
            case ValueMetaFunctionTypeEnum::Enum::isConst:            return type.isConst();
            case ValueMetaFunctionTypeEnum::Enum::isEnum:             return type.isEnum();

            case ValueMetaFunctionTypeEnum::Enum::size:
            case ValueMetaFunctionTypeEnum::Enum::numDimensions:
            case ValueMetaFunctionTypeEnum::Enum::alloc:
            default:
                CMAJ_ASSERT_FALSE;
        }
    }

    static ptr<ConstantValueBase> getSize (const AST::Object& o)
    {
        if (auto t = castToTypeBase (o))
        {
            if (t->isResolved())
            {
                auto& coreType = t->skipConstAndRefModifiers();

                if (auto vec = coreType.getAsVectorType())
                    return getAsFoldedConstant (vec->numElements);

                if (auto bounded = coreType.getAsBoundedType())
                    return getAsFoldedConstant (bounded->limit);

                if (auto arr = coreType.getAsArrayType())
                    if (! arr->isSlice())
                        return arr->getConstantSize (0);

                return {};
            }
        }

        if (auto value = AST::castToValue (o))
            if (auto t = value->getResultType())
                if (auto sz = getSize (*t))
                    return sz;

        if (auto vr = AST::castToSkippingReferences<AST::VariableReference> (o))
            return getSize (vr->getVariable());

        auto getSizeFromCast = [] (const AST::Cast& cast) -> ptr<ConstantValueBase>
        {
            if (auto numArgs = cast.arguments.size())
            {
                if (numArgs > 1)
                    return cast.context.allocator.createConstant (static_cast<int32_t> (numArgs));

                if (auto sourceValue = AST::castToValue (cast.arguments.front()))
                    if (auto sourceType = sourceValue->getResultType())
                        if (sourceType->isResolved() && sourceType->isFixedSizeArray())
                            return getSize (*sourceType);
            }

            return {};
        };

        if (auto v = AST::castToSkippingReferences<AST::VariableDeclaration> (o))
            if (v->isCompileTimeConstant())
                if (auto initCast = AST::castTo<AST::Cast> (v->initialValue))
                    if (auto resultType = initCast->getResultType())
                        if (resultType->isResolved() && resultType->isSlice())
                            return getSizeFromCast (*initCast);

        return {};
    }

    static std::optional<int32_t> getNumDimensions (const AST::Object& o)
    {
        if (auto t = castToTypeBase (o))
        {
            if (t->isResolved())
            {
                auto& coreType = t->skipConstAndRefModifiers();

                if (auto arr = coreType.getAsArrayType())
                    if (! arr->isSlice())
                        return arr->getNumDimensions();

                if (auto vec = coreType.getAsVectorType())
                    return vec->getNumDimensions();

                return {};
            }
        }

        if (auto value = AST::castToValue (o))
            if (auto t = value->getResultType())
                if (auto sz = getNumDimensions (*t))
                    return sz;

        if (auto vr = AST::castToSkippingReferences<AST::VariableReference> (o))
            return getNumDimensions (vr->getVariable());

        if (auto v = AST::castToSkippingReferences<AST::VariableDeclaration> (o))
            if (v->isCompileTimeConstant())
                if (auto initCast = AST::castTo<AST::Cast> (v->initialValue))
                    if (auto resultType = initCast->getResultType())
                        if (resultType->isResolved() && resultType->isSlice())
                            return 1;

        return {};
    }

    ptr<const TypeBase> getResultType() const override
    {
        return isGetSizeOp() ? context.allocator.arraySizeType
                             : context.allocator.boolType;
    }

    const ObjectContext& getLocationOfStartOfExpression() const override
    {
        return getSource().getLocationOfStartOfExpression();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (arguments, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ValueMetaFunctionTypeEnum,  op) \
        X (2, ListProperty,               arguments) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};
