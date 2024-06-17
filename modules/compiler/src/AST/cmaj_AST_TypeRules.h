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


enum class CastType
{
    identity,
    impossible,
    numericLossless,
    numericReduction,
    arrayLossless,
    arrayReduction,
    widenToArray,
    sliceOfArray,
    vectorSize1ToScalar,
    clampValue,
    wrapValue
};

//==============================================================================
struct TypeRules
{
    static bool isSilentCast (CastType type)
    {
        return type == CastType::identity
            || type == CastType::numericLossless
            || type == CastType::arrayLossless
            || type == CastType::widenToArray
            || type == CastType::vectorSize1ToScalar
            || type == CastType::sliceOfArray;
    }

    static CastType getCastType (const TypeBase& dest, const TypeBase& source)
    {
        if (auto p = dest.getAsPrimitiveType())     return getCastToPrimitiveType (*p, source);
        if (auto a = dest.getAsArrayType())         return getCastToArrayType (*a, source);
        if (auto v = dest.getAsVectorType())        return getCastToVectorType (*v, source);
        if (auto s = dest.getAsStructType())        return getCastToStructType (*s, source);
        if (auto b = dest.getAsBoundedType())       return getCastToBoundedType (*b, source);
        if (auto e = dest.getAsEnumType())          return getCastToEnumType (*e, source);

        if (auto mcr = dest.getAsMakeConstOrRef())
        {
            if (mcr->isNonConstReference() && source.isConst())
                return CastType::impossible;

            return getCastType (*mcr->getSource(), source);
        }

        CMAJ_ASSERT_FALSE;
    }

    static bool areTypesIdentical (const TypeBase& dest, const TypeBase& source)  { return getCastType (dest, source) == CastType::identity; }
    static bool canCastTo (const TypeBase& dest, const TypeBase& source)          { return getCastType (dest, source) != CastType::impossible; }
    static bool canSilentlyCastTo (const TypeBase& dest, const TypeBase& source)  { return isSilentCast (getCastType (dest, source)); }

    /// Permits literal constants that can be converted to the taget type without losing accuracy
    static bool canSilentlyCastTo (const TypeBase& destType, const ValueBase& value)
    {
        auto& sourceType = *value.getResultType();
        auto constValue = value.getAsConstantValueBase();

        if (sourceType.isSlice())
            return destType.isSlice()
                    && areTypesIdentical (*destType.getArrayOrVectorElementType(),
                                          *sourceType.getArrayOrVectorElementType());

        if (auto mcr = destType.getAsMakeConstOrRef())
            if (mcr->isNonConstReference() && value.isCompileTimeConstant())
                return false;

        if (canSilentlyCastTo (destType, sourceType))
            return true;

        if (constValue != nullptr)
            return canTruncateValueWithoutLossOfAccuracy (destType, *constValue);

        return false;
    }

    static bool canTruncateValueWithoutLossOfAccuracy (const TypeBase& dest, const ConstantValueBase& value)
    {
        if (auto b = dest.getAsBoundedType())
            if (auto size = value.getAsInt64())
                return b->isValidBoundedIntIndex (*size);

        if (dest.isPrimitiveFloat32() || dest.isPrimitiveComplex32())
        {
            if (value.getAsConstantBool() == nullptr)
            {
                if (auto f64 = value.getAsConstantFloat64())   return canLosslesslyConvertToFloat32 (f64->value.get());
                if (auto i64 = value.getAsInt64())             return canLosslesslyConvertToFloat32 (*i64);
            }
        }

        if (dest.isPrimitiveInt32())
        {
            if (auto f64 = value.getAsConstantFloat64())  return f64->value.get() == static_cast<int32_t> (f64->value.get());
            if (auto f32 = value.getAsConstantFloat32())  return f32->value.get() == static_cast<int32_t> (f32->value.get());
            if (auto i64 = value.getAsConstantInt64())    return i64->value.get() == static_cast<int32_t> (i64->value.get());
        }

        if (dest.isPrimitiveInt64())
        {
            if (auto f64 = value.getAsConstantFloat64())  return f64->value.get() == static_cast<double> (static_cast<int64_t> (f64->value.get()));
            if (auto f32 = value.getAsConstantFloat32())  return f32->value.get() == static_cast<float> (static_cast<int64_t> (f32->value.get()));
        }

        if (auto v = dest.getAsVectorType())
            if (v->isSize1())
                return canTruncateValueWithoutLossOfAccuracy (v->getElementType(), value);

        return false;
    }

    enum class ArgumentSuitability
    {
        perfect,
        constnessDifference,
        requiresCast,
        impossible,
    };

    static ArgumentSuitability getArgumentSuitability (const TypeBase& dest, const TypeBase& source, bool isSourceReferenceable)
    {
        if (dest.isNonConstReference() && (! isSourceReferenceable || source.isConst()))
            return ArgumentSuitability::impossible;

        if (auto destArray = dest.getAsArrayType())
        {
            if (auto sourceArray = source.getAsArrayType())
            {
                if (destArray->isSlice())
                    if (destArray->getInnermostElementTypeRef().isSameType (sourceArray->getInnermostElementTypeRef(),
                                                                            TypeBase::ComparisonFlags::failOnAllDifferences))
                        return ArgumentSuitability::perfect;

                if (sourceArray->isSlice())
                    return ArgumentSuitability::impossible;
            }
        }

        if (dest.isSameType (source, TypeBase::ComparisonFlags::ignoreConst
                                   | TypeBase::ComparisonFlags::ignoreReferences
                                   | TypeBase::ComparisonFlags::ignoreVectorSize1))
        {
            if (dest.isConstReference())
                return isSourceReferenceable && ! source.isConst() ? ArgumentSuitability::constnessDifference
                                                                   : ArgumentSuitability::perfect;

             return ArgumentSuitability::perfect;
        }

        if (source.isBoundedType() && dest.isPrimitiveInt())
            return ArgumentSuitability::perfect;

        if (canSilentlyCastTo (dest, source))
            return ArgumentSuitability::requiresCast;

        return ArgumentSuitability::impossible;
    }

    static bool areTypesEquivalentAsFunctionParameters (const TypeBase& type1,
                                                        const TypeBase& type2)
    {
        if (type1.isNonConstReference() && type2.isConstReference()) return false;
        if (type2.isNonConstReference() && type1.isConstReference()) return false;

        return type1.isSameType (type2, TypeBase::ComparisonFlags::ignoreConst
                                      | TypeBase::ComparisonFlags::ignoreReferences
                                      | TypeBase::ComparisonFlags::ignoreVectorSize1);
    }

    template <typename NumericType>
    static bool canLosslesslyConvertToFloat32 (NumericType v)
    {
        return v == static_cast<NumericType> (static_cast<float> (v));
    }

    static bool isSuitableTypeForUnaryOp (UnaryOpTypeEnum::Enum op, const TypeBase& t)
    {
        if (auto v = t.skipConstAndRefModifiers().getAsVectorType())
            return isSuitableTypeForUnaryOp (op, v->getElementType());

        if (op == UnaryOpTypeEnum::Enum::negate)       return t.isPrimitiveInt() || t.isPrimitiveFloat() || t.isPrimitiveComplex();
        if (op == UnaryOpTypeEnum::Enum::bitwiseNot)   return t.isPrimitiveInt();
        if (op == UnaryOpTypeEnum::Enum::logicalNot)   return t.isPrimitiveBool();

        CMAJ_ASSERT_FALSE;
    }

    struct BinaryOperatorTypes
    {
        const TypeBase& resultType;
        const TypeBase& operandType;

        operator bool() const   { return ! resultType.isVoid(); }

        static BinaryOperatorTypes invalid (const Object& o)
        {
            return { o.context.allocator.voidType,
                     o.context.allocator.voidType };
        }
    };

    static bool isTypeSuitableForBinaryOp (const TypeBase& t)
    {
        return ! (t.isStruct() || t.isArray() || t.isPrimitiveString() || t.isEnum());
    }

    static bool areTypesSuitableForBinaryOp (const TypeBase& a, const TypeBase& b)
    {
        return isTypeSuitableForBinaryOp (a) && isTypeSuitableForBinaryOp (b);
    }

    static BinaryOperatorTypes getBinaryOperatorTypes (BinaryOpTypeEnum::Enum op, const TypeBase& a, const TypeBase& b)
    {
        if (auto ref = a.getAsMakeConstOrRef())  return getBinaryOperatorTypes (op, *ref->getSource(), b);
        if (auto ref = b.getAsMakeConstOrRef())  return getBinaryOperatorTypes (op, a, *ref->getSource());

        if ((a.getAsVectorType() != nullptr || b.getAsVectorType() != nullptr) && ! isLogicalOperator (op))
            return getVectorOperatorTypes (op, a, b);

        if (isLogicalOperator (op))       return getTypesForLogicalOp    (a.skipConstAndRefModifiers(), b.skipConstAndRefModifiers());
        if (isBitwiseOperator (op))       return getTypesForBitwiseOp    (a.skipConstAndRefModifiers(), b.skipConstAndRefModifiers());
        if (isEqualityOperator (op))      return getTypesForEqualityOp   (a.skipConstAndRefModifiers(), b.skipConstAndRefModifiers());
        if (isComparisonOperator (op))    return getTypesForComparisonOp (a.skipConstAndRefModifiers(), b.skipConstAndRefModifiers());
        if (isArithmeticOperator (op))    return getCommonTypeForOp      (a.skipConstAndRefModifiers(), b.skipConstAndRefModifiers(), op);

        CMAJ_ASSERT_FALSE;
    }

    static BinaryOperatorTypes getVectorOperatorTypes (BinaryOpTypeEnum::Enum op, const TypeBase& a, const TypeBase& b)
    {
        auto sizeA = a.getVectorSize();
        auto sizeB = b.getVectorSize();

        if (sizeA != sizeB && sizeA != 1 && sizeB != 1)
            return BinaryOperatorTypes::invalid (a);

        auto v1 = a.getAsVectorType();
        auto v2 = b.getAsVectorType();

        auto& elementTypeA = (v1 != nullptr ? v1->getElementType() : a);
        auto& elementTypeB = (v2 != nullptr ? v2->getElementType() : b);

        auto types = getBinaryOperatorTypes (op, elementTypeA, elementTypeB);

        if (types)
        {
            auto& size = castToRef<Expression> (v1 != nullptr ? v1->numElements
                                                              : v2->numElements);

            auto getPrimitive = [] (const TypeBase& t) -> const PrimitiveType&
            {
                if (t.isVectorSize1())
                    return castToRef<const PrimitiveType> (*t.getArrayOrVectorElementType());

                return castToRef<const PrimitiveType> (t);
            };

            return { a.context.allocator.createVectorType (getPrimitive (types.resultType), size),
                     a.context.allocator.createVectorType (getPrimitive (types.operandType), size) };
        }

        return types;
    }

    static BinaryOperatorTypes getCommonTypeForOp (const TypeBase& a, const TypeBase& b, BinaryOpTypeEnum::Enum op)
    {
        if (areTypesSuitableForBinaryOp (a, b))
        {
            if (a.isPrimitiveBool() || b.isPrimitiveBool())
                if (! isLogicalOperator (op))
                    return BinaryOperatorTypes::invalid (a);

            if (op == BinaryOpTypeEnum::Enum::modulo)
                if (a.isComplexOrVectorOfComplex() || b.isComplexOrVectorOfComplex())
                    return BinaryOperatorTypes::invalid (a);

            if (a.isIdentical (b))
                return { a, a };

            if (canSilentlyCastTo (a, b)) return { a, a };
            if (canSilentlyCastTo (b, a)) return { b, b };

            // Allow silent promotion of ints/bound types to float
            if (a.isPrimitiveFloat() && (b.isPrimitiveInt() || b.isBoundedType())) return { a, a };
            if (b.isPrimitiveFloat() && (a.isPrimitiveInt() || a.isBoundedType())) return { b, b };

            // Allow silent promotion of ints to complex
            if (a.isPrimitiveComplex() && b.isPrimitiveInt()) return { a, a };
            if (b.isPrimitiveComplex() && a.isPrimitiveInt()) return { b, b };
        }

        return BinaryOperatorTypes::invalid (a);
    }

    static BinaryOperatorTypes getTypesForLogicalOp (const TypeBase& a, const TypeBase& b)
    {
        if (areTypesSuitableForBinaryOp (a, b))
            return { a.context.allocator.boolType,
                     a.context.allocator.boolType };

        return BinaryOperatorTypes::invalid (a);
    }

    static BinaryOperatorTypes getTypesForEqualityOp (const TypeBase& a, const TypeBase& b)
    {
        // Special case for strings and enums - they support ==, != but are
        // unordered, so you can't do other comparisons
        if (a.isPrimitiveString() && b.isPrimitiveString())
            return { a.context.allocator.boolType, a };

        if (a.isEnum() && b.isEnum() && a.isSameType (b, TypeBase::ComparisonFlags::ignoreConst
                                                          | TypeBase::ComparisonFlags::ignoreReferences
                                                          | TypeBase::ComparisonFlags::ignoreVectorSize1))
            return { a.context.allocator.boolType, a };

        if (a.isPrimitiveComplex() || b.isPrimitiveComplex())
            return getTypesForBoolResult (a, b);

        return getTypesForComparisonOp (a, b);
    }

    static BinaryOperatorTypes getTypesForComparisonOp (const TypeBase& a, const TypeBase& b)
    {
        if (a.isComplexOrVectorOfComplex() || b.isComplexOrVectorOfComplex())
            return BinaryOperatorTypes::invalid (a);

        if (a.isBoundedType())   return getTypesForComparisonOp (a.context.allocator.int32Type, b);
        if (b.isBoundedType())   return getTypesForComparisonOp (a, a.context.allocator.int32Type);

        return getTypesForBoolResult (a, b);
    }

    static BinaryOperatorTypes getTypesForBoolResult (const TypeBase& a, const TypeBase& b)
    {
        if (auto types = getCommonTypeForOp (a, b, BinaryOpTypeEnum::Enum::logicalAnd))
            return { a.context.allocator.boolType, types.operandType };

        return BinaryOperatorTypes::invalid (a);
    }

    static bool isTypeSuitableForBitwiseOp (const TypeBase& t)
    {
        return t.isPrimitiveInt() && isTypeSuitableForBinaryOp (t);
    }

    static BinaryOperatorTypes getTypesForBitwiseOp (const TypeBase& a, const TypeBase& b)
    {
        if (auto ref = a.getAsMakeConstOrRef())  return getTypesForBitwiseOp (*ref->getSource(), b);
        if (auto ref = b.getAsMakeConstOrRef())  return getTypesForBitwiseOp (a, *ref->getSource());

        if (a.isBoundedType())   return getTypesForBitwiseOp (a.context.allocator.int32Type, b);
        if (b.isBoundedType())   return getTypesForBitwiseOp (a, a.context.allocator.int32Type);

        if (isTypeSuitableForBitwiseOp (a)
             && isTypeSuitableForBitwiseOp (b))
        {
            auto& intType = a.isScalar64() ? a.context.allocator.int64Type
                                           : a.context.allocator.int32Type;

            return { intType, intType };
        }

        return BinaryOperatorTypes::invalid (a);
    }

    static bool arraySizeTypeIsOK (const TypeBase& sizeType)
    {
        return (sizeType.isPrimitiveInt() || sizeType.isBoundedType()) && ! sizeType.isReference();
    }

    static bool canBeSafelyCastToArraySize (int64_t size)
    {
        return size > 0 && (uint64_t) size <= maxArraySize;
    }

    static ArraySize castToArraySize (int64_t size)
    {
        CMAJ_ASSERT (canBeSafelyCastToArraySize (size));
        return static_cast<ArraySize> (size);
    }

    template <typename Thrower, typename SizeType>
    static ArraySize checkArraySizeAndThrowErrorIfIllegal (Thrower&& errorContext, SizeType size)
    {
        if (! canBeSafelyCastToArraySize ((int64_t) size))
            throwError (errorContext, size > 0 ? Errors::tooManyElements()
                                               : Errors::illegalArraySize());

        return static_cast<ArraySize> (size);
    }

    template <typename Thrower>
    static ArraySize checkAndGetArraySize (Thrower&& errorContext, const ConstantValueBase& size)
    {
        if (arraySizeTypeIsOK (*size.getResultType()))
            if (auto sz = size.getAsInt64())
                return checkArraySizeAndThrowErrorIfIllegal (errorContext, *sz);

        throwError (errorContext, Errors::nonIntegerArraySize());
    }

    template <typename Thrower>
    static ArraySize checkAndGetVectorSize (Thrower&& errorContext, const ConstantValueBase& size)
    {
        if (arraySizeTypeIsOK (*size.getResultType()))
        {
            if (auto sz = size.getAsInt64())
            {
                if (*sz < 1 || *sz > static_cast<int64_t> (maxVectorSize))
                    throwError (errorContext, Errors::illegalVectorSize());

                return static_cast<ArraySize> (*sz);
            }
        }

        throwError (errorContext, Errors::nonIntegerArraySize());
    }

    template <typename Thrower>
    static int64_t checkAndGetArrayIndex (Thrower&& errorContext, const ConstantValueBase& index)
    {
        if (arraySizeTypeIsOK (*index.getResultType()))
            if (auto i = index.getAsInt64())
                return *i;

        throwError (errorContext, Errors::nonIntegerArrayIndex());
    }

    template <typename Thrower>
    static void checkConstantArrayIndex (Thrower&& errorContext, int64_t index, ArraySize arraySize, bool canEqualSize)
    {
        if (index < 0 || index > (canEqualSize ? (int64_t) arraySize : (int64_t) arraySize - 1))
            throwError (errorContext, Errors::indexOutOfRange());
    }

    static ArraySize convertArrayOrVectorIndexToValidRange (int64_t arraySize, int64_t value)
    {
        return value < 0 ? static_cast<ArraySize> (arraySize + value)
                         : static_cast<ArraySize> (value);
    }

    static ArraySize convertArrayOrVectorIndexToWrappedIndex (int64_t arraySize, int64_t value)
    {
        if (arraySize == 0)
            return 0;

        auto i = value % arraySize;
        return i < 0 ? static_cast<ArraySize> (i + arraySize) : static_cast<ArraySize> (i);
    }

    static IntegerRange normaliseArrayOrVectorIndexRange (int64_t arraySize, IntegerRange range)
    {
        if (arraySize == 0)
            return {};

        range.start = wrap (range.start, arraySize);

        if (range.end != arraySize)
            range.end = wrap (range.end, arraySize);

        CMAJ_ASSERT (range.end >= range.start);
        return range;
    }

    template <typename IntType>
    static ArraySize convertArrayOrVectorIndexToValidRange (const TypeBase& arrayType, IntType value, uint32_t dimensionIndex)
    {
        return convertArrayOrVectorIndexToValidRange (static_cast<int64_t> (arrayType.getArrayOrVectorSize (dimensionIndex)),
                                                      static_cast<int64_t> (value));
    }

    template <typename Thrower>
    static ArraySize checkAndGetArrayIndex (Thrower&& errorContext, const ConstantValueBase& index,
                                            const TypeBase& arrayOrVectorType, uint32_t dimensionIndex, bool canEqualSize)
    {
        auto fixedIndex = checkAndGetArrayIndex (errorContext, index);

        if (arrayOrVectorType.isVector() || arrayOrVectorType.isFixedSizeArray())
        {
            auto i = convertArrayOrVectorIndexToValidRange (arrayOrVectorType, fixedIndex, dimensionIndex);
            checkConstantArrayIndex (errorContext, (int64_t) i, arrayOrVectorType.getArrayOrVectorSize (dimensionIndex), canEqualSize);
            return i;
        }

        return static_cast<ArraySize> (fixedIndex);
    }

    static bool canCopyResultTypeToGlobal (const TypeBase& target, const TypeBase& source, bool areSlicesGlobal)
    {
        if (auto da = castTo<ArrayType> (target.skipConstAndRefModifiers()))
        {
            if (auto sa = castTo<ArrayType> (source.skipConstAndRefModifiers()))
            {
                if (da->isSlice())
                    return areSlicesGlobal && sa->isSlice();

                return canCopyResultTypeToGlobal (da->getInnermostElementTypeRef(), sa->getInnermostElementTypeRef(), areSlicesGlobal);
            }

            return false;
        }

        if (auto ds = castTo<StructType> (target.skipConstAndRefModifiers()))
        {
            if (ds->memberTypes.empty())
                return true;

            if (auto ss = castTo<StructType> (source.skipConstAndRefModifiers()))
            {
                CMAJ_ASSERT (ds->memberTypes.size() == ss->memberTypes.size());

                for (size_t i = 0; i < ds->memberTypes.size(); ++i)
                    if (! canCopyResultTypeToGlobal (ds->getMemberType(i), ss->getMemberType(i), areSlicesGlobal))
                        return false;

                return true;
            }

            return canCopyResultTypeToGlobal (ds->getMemberType(0), source, areSlicesGlobal);
        }

        return true;
    }

    //==============================================================================
    enum class ComparisonResultClass
    {
        unknown,
        alwaysTrue,
        alwaysFalse
    };

    static ComparisonResultClass getComparisonResultClass (const Expression& lhs, const Expression& rhs, BinaryOpTypeEnum::Enum op)
    {
        if (isComparisonOperator (op))
        {
            auto a = IntegerRange::forObject (lhs);
            auto b = IntegerRange::forObject (rhs);

            if (a.isValid() && b.isValid())
            {
                struct Zones
                {
                    bool canBeLess, canBeEqual, canBeGreater;

                    ComparisonResultClass getResult (bool less, bool equal, bool greater) const
                    {
                        bool canBeTrue = (less && canBeLess) || (equal && canBeEqual) || (greater && canBeGreater);
                        bool canBeFalse = (! less && canBeLess) || (! equal && canBeEqual) || (! greater && canBeGreater);

                        if (canBeTrue && ! canBeFalse)
                            return ComparisonResultClass::alwaysTrue;

                        if (canBeFalse && ! canBeTrue)
                            return ComparisonResultClass::alwaysFalse;

                        return ComparisonResultClass::unknown;
                    }
                };

                Zones zones { a.canBeBelow (b), a.canBeEqual (b), b.canBeBelow (a) };

                if (op == BinaryOpTypeEnum::Enum::lessThan)             return zones.getResult (true,  false, false);
                if (op == BinaryOpTypeEnum::Enum::lessThanOrEqual)      return zones.getResult (true,  true,  false);
                if (op == BinaryOpTypeEnum::Enum::greaterThan)          return zones.getResult (false, false, true);
                if (op == BinaryOpTypeEnum::Enum::greaterThanOrEqual)   return zones.getResult (false, true,  true);
            }
        }

        return ComparisonResultClass::unknown;
    }

private:
    TypeRules() = delete;

    static CastType getCastToPrimitiveType (const PrimitiveType& dest, const TypeBase& source)
    {
        if (auto primSource = source.getAsPrimitiveType())
        {
            switch (dest.type.get())
            {
                case PrimitiveTypeEnum::Enum::int32:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::identity;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::int64:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::identity;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::float32:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::identity;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::float64:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::identity;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::complex32:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::identity;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::complex64:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::numericLossless;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::identity;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::boolean:
                    switch (primSource->type.get())
                    {
                        case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::int32:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::int64:       return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float32:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                        case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                        case PrimitiveTypeEnum::Enum::boolean:     return CastType::identity;
                        case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                        default:                                   CMAJ_ASSERT_FALSE;
                    }

                case PrimitiveTypeEnum::Enum::void_:
                case PrimitiveTypeEnum::Enum::string:
                    return primSource->type.get() == dest.type.get() ? CastType::identity
                                                                     : CastType::impossible;

                default:
                    CMAJ_ASSERT_FALSE;
            }
        }

        if (source.isBoundedType())
        {
            switch (dest.type.get())
            {
                case PrimitiveTypeEnum::Enum::void_:       return CastType::impossible;
                case PrimitiveTypeEnum::Enum::int32:       return CastType::numericLossless;
                case PrimitiveTypeEnum::Enum::int64:       return CastType::numericLossless;
                case PrimitiveTypeEnum::Enum::float32:     return CastType::numericReduction;
                case PrimitiveTypeEnum::Enum::float64:     return CastType::numericReduction;
                case PrimitiveTypeEnum::Enum::complex32:   return CastType::impossible;
                case PrimitiveTypeEnum::Enum::complex64:   return CastType::impossible;
                case PrimitiveTypeEnum::Enum::boolean:     return CastType::impossible;
                case PrimitiveTypeEnum::Enum::string:      return CastType::impossible;
                default:                                   CMAJ_ASSERT_FALSE;
            }
        }

        if (auto ref = source.getAsMakeConstOrRef())
            return getCastType (dest, *ref->getSource());

        if (auto vec = source.getAsVectorType())
            if (vec->isSize1())
                return getCastType (dest, vec->getElementType());

        return CastType::impossible;
    }

    static CastType getCastToArrayType (const ArrayType& dest, const TypeBase& source)
    {
        auto& destElementType = dest.getElementType (0);

        if (auto sourceArray = source.getAsArrayType())
        {
            auto sourceDimensions = sourceArray->getNumDimensions();
            auto destDimensions = dest.getNumDimensions();

            auto elementCastType = getCastType (destElementType, sourceArray->getElementType (0));

            if (sourceDimensions != destDimensions)
            {
                if (dest.isSlice())
                    return CastType::impossible;

                if (sourceArray->resolveFlattenedSize() != dest.resolveFlattenedSize())
                {
                    if (destDimensions == sourceDimensions + 1)
                        if (getCastType (destElementType, *sourceArray) != CastType::impossible)
                            return CastType::widenToArray;

                    return CastType::impossible;
                }

                elementCastType = getCastType (dest.getInnermostElementTypeRef(), sourceArray->getInnermostElementTypeRef());
            }

            if (dest.isSlice())
            {
                if (elementCastType == CastType::identity)
                {
                    if (sourceArray->isSlice())
                        return CastType::identity;

                    return CastType::sliceOfArray;
                }
            }
            else if (! sourceArray->isSlice() && dest.resolveFlattenedSize() == sourceArray->resolveFlattenedSize())
            {
                if (elementCastType == CastType::numericReduction)      return CastType::arrayReduction;
                if (elementCastType == CastType::numericLossless)       return CastType::arrayLossless;
                if (elementCastType == CastType::vectorSize1ToScalar)   return CastType::arrayLossless;
                if (elementCastType == CastType::widenToArray)          return CastType::arrayLossless;
                if (elementCastType == CastType::identity)              return CastType::identity;
            }
        }

        if (auto sourcePrim = source.getAsPrimitiveType())
        {
            if (getCastType (destElementType, *sourcePrim) != CastType::impossible)
                return CastType::widenToArray;

            return CastType::impossible;
        }

        if (auto sourceVec = source.getAsVectorType())
        {
            if (getCastType (destElementType, *sourceVec) != CastType::impossible)
                return CastType::widenToArray;

            if (getCastType (destElementType, sourceVec->getElementType()) != CastType::impossible)
                if (sourceVec->isSize1())
                    return CastType::widenToArray;

            return CastType::impossible;
        }

        if (! dest.isSlice())
            if (auto sourceStruct = source.getAsStructType())
                if (destElementType.isIdentical (*sourceStruct))
                    return CastType::widenToArray;

        if (auto ref = source.getAsMakeConstOrRef())
            return getCastType (dest, *ref->getSource());

        return CastType::impossible;
    }

    static CastType getCastToVectorType (const VectorType& dest, const TypeBase& source)
    {
        auto& destElementType = dest.getElementType();

        if (auto sourcePrim = source.getAsPrimitiveType())
        {
            auto elementCastType = getCastType (destElementType, *sourcePrim);

            if (elementCastType != CastType::impossible)
            {
                if (dest.isSize1())
                    return elementCastType;

                return CastType::widenToArray;
            }

            return CastType::impossible;
        }

        if (auto sourceVec = source.getAsVectorType())
        {
            auto& sourceElementType = sourceVec->getElementType();
            auto elementCast = getCastType (destElementType, sourceElementType);

            if (dest.getSize().isIdentical (sourceVec->getSize()))
            {
                if (elementCast == CastType::identity)          return CastType::identity;
                if (elementCast == CastType::numericReduction)  return CastType::arrayReduction;
                if (elementCast == CastType::numericLossless)   return CastType::arrayLossless;
            }

            if (sourceVec->isSize1() && elementCast != CastType::impossible)
                return CastType::widenToArray;

            return CastType::impossible;
        }

        if (auto ref = source.getAsMakeConstOrRef())
            return getCastType (dest, *ref->getSource());

        return CastType::impossible;
    }

    static CastType getCastToStructType (const StructType& dest, const TypeBase& source)
    {
        if (dest.isSameType (source, TypeBase::ComparisonFlags::ignoreConst | TypeBase::ComparisonFlags::ignoreReferences))
        {
            if (dest.isNonConstReference() && ! source.isNonConstReference())
                return CastType::impossible;

            if (dest.isReference() && ! source.isReference())
                return CastType::impossible;

            return CastType::identity;
        }

        return CastType::impossible;
    }

    static CastType getCastToBoundedType (const BoundedType& dest, const TypeBase& source)
    {
        auto boundedSource = source.getAsBoundedType();

        if (boundedSource != nullptr && dest.getBoundedIntLimit() >= boundedSource->getBoundedIntLimit())
            return CastType::identity;

        if (boundedSource != nullptr || source.isPrimitiveInt() || source.isPrimitiveFloat())
            return dest.isClamp ? CastType::clampValue
                                : CastType::wrapValue;

        return CastType::impossible;
    }

    static CastType getCastToEnumType (const EnumType& dest, const TypeBase& source)
    {
        if (dest.isSameType (source, TypeBase::ComparisonFlags::ignoreConst | TypeBase::ComparisonFlags::ignoreReferences))
            if (! (dest.isNonConstReference() && ! source.isNonConstReference()))
                return CastType::identity;

        return CastType::impossible;
    }
};
