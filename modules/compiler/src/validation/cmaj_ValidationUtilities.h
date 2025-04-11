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


namespace cmaj::validation
{

static inline void failIfModule (const AST::Object& o)
{
    if (AST::castToSkippingReferences<AST::ProcessorBase> (o) != nullptr)
        throwError (o, Errors::processorReferenceNotAllowed());

    if (o.getAsModuleBase() != nullptr)
        throwError (o, Errors::namespaceNotAllowed());
}

template <typename ObjectOrContext1, typename ObjectOrContext2>
static void throwErrorWithPreviousDeclaration (const ObjectOrContext1& context,
                                               const ObjectOrContext2& previous,
                                               const DiagnosticMessage& message)
{
    DiagnosticMessageList list;

    if (AST::getContext (context).isInSystemModule())
    {
        list.add (previous, message);
    }
    else
    {
        list.add (context, message);

        if (! AST::getContext (previous).isInSystemModule())
            list.add (previous, Errors::seePreviousDeclaration());
    }

    throwError (list);
}

static inline void throwIfStaticAssertionFails (const AST::StaticAssertion& s)
{
    if (auto conditionResult = AST::getAsFoldedConstant (s.condition))
    {
        auto asBool = conditionResult->getAsBool();

        CMAJ_ASSERT (asBool);

        if (! *asBool)
        {
            auto message = s.error.toString();

            if (message.empty())
                throwError (s, Errors::staticAssertionFailure(), true);
            else
                throwError (s, Errors::staticAssertionFailureWithMessage (message), true);
        }
    }
}

static inline void throwAmbiguousNameError (const AST::ObjectContext& c, AST::PooledString name,
                                            choc::span<ref<AST::Object>> objectsFound)
{
    DiagnosticMessageList messages;

    messages.add (c, Errors::ambiguousSymbol (name));

    for (auto& o : objectsFound)
        messages.add (o, Errors::seePossibleCandidate());

    throwError (messages);
}

template <typename ObjectOrContext>
static const AST::TypeBase& getAsTypeOrThrowError (ObjectOrContext&& errorContext, const AST::Object& o)
{
    auto t = AST::castToSkippingReferences<AST::TypeBase> (o);

    if (t == nullptr)
    {
        if (AST::castToSkippingReferences<AST::ProcessorBase> (o) != nullptr)
            throwError (errorContext, Errors::cannotUseProcessorAsType());

        throwError (errorContext, Errors::expectedType());
    }

    return *t;
}

static inline const AST::TypeBase& getAsTypeOrThrowError (const AST::Property& p)
{
    return getAsTypeOrThrowError (p, p.getObjectRef());
}

static inline const AST::TypeBase& getVariableTypeOrThrowError (const AST::VariableDeclaration& v)
{
    auto t = v.getType();

    if (t == nullptr || ! t->isResolved())
        throwError (v, Errors::cannotResolveTypeOfExpression());

    return *t;
}

static inline AST::ObjectRefVector<const AST::TypeBase> getParameterTypesOrThrowError (const AST::Function& fn)
{
    AST::ObjectRefVector<const AST::TypeBase> paramTypes;

    for (auto& param : fn.iterateParameters())
    {
        auto& type = getVariableTypeOrThrowError (param);

        if (type.isVoid())
            throwError (param, Errors::parameterCannotBeVoid());

        if (fn.isEventHandler && type.isNonConstReference())
            throwError (param, Errors::eventParamsCannotBeNonConstReference());

        paramTypes.push_back (type);
    }

    return paramTypes;
}

static inline AST::ObjectRefVector<const AST::TypeBase> getTypeListOrThrowError (const AST::ListProperty& list)
{
    AST::ObjectRefVector<const AST::TypeBase> result;

    for (auto& item : list)
        result.push_back (getAsTypeOrThrowError (item));

    return result;
}

static inline const AST::ValueBase& getAsValueOrThrowError (const AST::ObjectContext& errorContext,
                                                            const AST::Object& o)
{
    auto v = AST::castToValue (o);

    if (v == nullptr)
    {
        if (auto endpoint = AST::castToSkippingReferences<AST::EndpointDeclaration> (o))
        {
            if (endpoint->isInput)
            {
                if (endpoint->endpointType == AST::EndpointTypeEnum::Enum::event)
                    throwError (errorContext, Errors::cannotReadFromEventInput());
            }
            else
            {
                throwError (errorContext, Errors::cannotReadFromOutput());
            }

            throwError (errorContext, Errors::endpointsCanOnlyBeUsedInMain());
        }

        if (AST::castToSkippingReferences<AST::ProcessorBase> (o) != nullptr)
            throwError (errorContext, Errors::cannotUseProcessorAsValue());

        throwError (errorContext, Errors::expectedValue());
    }

    return *v;
}

static inline const AST::ValueBase& getAsValueOrThrowError (const AST::ObjectContext& errorContext,
                                                            const AST::Property& p)
{
    return getAsValueOrThrowError (errorContext, p.getObjectRef());
}

static inline const AST::ValueBase& getAsValueOrThrowError (const AST::Object& o)    { return getAsValueOrThrowError (AST::getContext (o), o); }
static inline const AST::ValueBase& getAsValueOrThrowError (const AST::Property& p)  { return getAsValueOrThrowError (AST::getContext (p), p); }

static inline const AST::TypeBase& getResultTypeOfValueOrThrowError (const AST::ObjectContext& errorContext,
                                                                     const AST::Object& value)
{
    auto type = getAsValueOrThrowError (errorContext, value).getResultType();

    if (type == nullptr)
    {
        failIfModule (value);
        throwError (errorContext, Errors::expectedValue());
    }

    return *type;
}

static const AST::TypeBase& getResultTypeOfValueOrThrowError (const AST::Object& value)
{
    return getResultTypeOfValueOrThrowError (AST::getContext (value), value);
}

static const AST::TypeBase& getResultTypeOfValueOrThrowError (const AST::Property& p)
{
    return getResultTypeOfValueOrThrowError (p.getObjectRef());
}

static void checkTypeIsResolved (const AST::TypeBase& type)
{
    if (! type.isResolved())
        throwError (type, Errors::cannotResolveTypeOfExpression());
}

static inline bool hasAssignableAddress (const AST::Property& p)
{
    if (auto value = AST::castToValue (p))
    {
        if (auto getElement = AST::castTo<AST::GetElement> (*value))
        {
            if (getResultTypeOfValueOrThrowError (getElement->parent).isSlice())
                return false;

            if (! hasAssignableAddress (getElement->parent))
                return false;
        }

        if (auto sourceVar = value->getSourceVariable())
            return ! sourceVar->isCompileTimeConstant();
    }

    return false;
}

static inline const AST::ValueBase& getAssignableValueOrThrowError (const AST::Property& p,
                                                                    std::string_view operatorSymbol,
                                                                    bool isAssignment)
{
    if (auto value = AST::castToValue (p))
    {
        if (auto getElement = AST::castTo<AST::GetElement> (*value))
        {
            auto& element = getAssignableValueOrThrowError (getElement->parent, operatorSymbol, isAssignment);
            if (element.getResultType()->isConst())
                throwError (AST::getContextOfStartOfExpression (p),
                            isAssignment ? Errors::assignmentToNonAssignableTarget (operatorSymbol)
                                         : Errors::operatorNeedsAssignableTarget (operatorSymbol));
        }

        if (value->getResultType()->isConst())
            throwError (AST::getContextOfStartOfExpression (p),
                        isAssignment ? Errors::assignmentToNonAssignableTarget (operatorSymbol)
                                     : Errors::operatorNeedsAssignableTarget (operatorSymbol));

        if (auto sourceVar = value->getSourceVariable())
            if (! sourceVar->isCompileTimeConstant())
                return *value;
    }

    throwError (AST::getContextOfStartOfExpression (p),
                isAssignment ? Errors::assignmentToNonAssignableTarget (operatorSymbol)
                             : Errors::operatorNeedsAssignableTarget (operatorSymbol));
}

static ptr<const AST::ConstantValueBase> getAsFoldedConstant (const AST::Object& o)
{
    return getAsValueOrThrowError (o).constantFold();
}

static const AST::ConstantValueBase& getAsConstantOrThrowError (const AST::Object& o)
{
    auto asConst = getAsFoldedConstant (o);

    if (asConst == nullptr)
        throwError (o, Errors::expectedConstant());

    return *asConst;
}

static int64_t getConstantArraySizeOrThrowError (const AST::ObjectProperty& arraySize, int64_t maxSizeAllowed)
{
    auto& size = getAsConstantOrThrowError (arraySize.getObjectRef());
    auto& type = getResultTypeOfValueOrThrowError (arraySize);

    if (! type.isPrimitiveInt())
        throwError (arraySize, Errors::nonIntegerArraySize());

    auto intSize = AST::getAsInt64 (size);

    if (intSize < 1 || intSize > maxSizeAllowed)
        throwError (arraySize, Errors::illegalArraySize());

    return intSize;
}

static inline int64_t getOptionalArraySizeOrThrowError (const AST::ObjectProperty& arraySize, int64_t maxSizeAllowed)
{
    if (arraySize != nullptr)
        return getConstantArraySizeOrThrowError (arraySize, maxSizeAllowed);

    return 1;
}

static inline std::optional<AST::ArraySize> getConstantWrappingSizeToApplyToIndex (const AST::GetElement& g, uint32_t dimensionIndex)
{
    if (auto parentValue = AST::castToValue (g.parent))
    {
        if (auto parentType = parentValue->getResultType())
        {
            if ((parentType->isArray() || parentType->isVector()) && ! parentType->isSlice())
            {
                CMAJ_ASSERT (dimensionIndex < g.indexes.size());
                auto& indexValue = AST::castToValueRef (g.indexes[dimensionIndex]);
                auto& indexType = indexValue.getResultType()->skipConstAndRefModifiers();
                auto arraySize = parentType->getArrayOrVectorSize (dimensionIndex);

                if (auto bounded = AST::castToSkippingReferences<AST::BoundedType> (indexType))
                    if (bounded->getBoundedIntLimit() <= arraySize)
                        return {};

                return arraySize;
            }
        }
    }

    if (auto graphNode = AST::castToSkippingReferences<AST::GraphNode> (g.parent))
        return graphNode->getArraySize();

    return {};
}

static void checkNumberOfConstructorElements (const AST::ObjectContext& errorLocation,
                                              const AST::TypeBase& target, size_t numberAvailable)
{
    if (numberAvailable != 0 && target.isFixedSizeAggregate())
    {
        if (target.isVectorOrArray() && numberAvailable == target.getArrayOrVectorSize(0))
            return;

        if (target.isStruct() && numberAvailable == target.getFixedSizeAggregateNumElements())
            return;

        if (numberAvailable == 1 && (target.isVectorOrArray() || target.isStruct()))
            return;

        throwError (errorLocation, Errors::wrongNumArgsForAggregate (AST::print (target, AST::PrintOptionFlags::useShortNames)));
    }
}

static bool inline checkStructMember (const AST::TypeBase& objectType, AST::PooledString memberName,
                                      const AST::ObjectContext* objectContext, const AST::ObjectContext* nameContext)
{
    if (objectType.isComplexOrVectorOfComplex())
    {
        if (memberName != "real" && memberName != "imag")
        {
            if (nameContext != nullptr)
                throwError (*nameContext, Errors::unknownMemberInComplex (memberName, AST::print (objectType, AST::PrintOptionFlags::useShortNames)));

            return false;
        }
    }
    else
    {
        auto structType = objectType.getAsStructType();

        if (structType == nullptr)
        {
            if (objectContext != nullptr)
                throwError (*objectContext, Errors::expectedStructForDotOperator());

            return false;
        }

        auto memberIndex = structType->indexOfMember (memberName);

        if (memberIndex < 0)
        {
            if (nameContext != nullptr)
                throwError (*nameContext, Errors::unknownMemberInStruct (memberName, structType->getFullyQualifiedReadableName()));

            return false;
        }
    }

    return true;
}

static std::string printType (const AST::TypeBase& type)
{
    return AST::print (type, static_cast<AST::PrintOptionFlags> (AST::PrintOptionFlags::skipAliases
                                                                 | AST::PrintOptionFlags::useFullyQualifiedNames));
}

static void expectCastPossible (const AST::ObjectContext& errorLocation,
                                const AST::TypeBase& targetType, const AST::TypeBase& sourceType,
                                bool mustBeSilent = false)
{
    checkTypeIsResolved (targetType);
    checkTypeIsResolved (sourceType);

    if (! AST::TypeRules::canCastTo (targetType, sourceType))
        throwError (errorLocation, Errors::cannotCastType (printType (sourceType),
                                                           printType (targetType)));

    if (mustBeSilent && ! AST::TypeRules::canSilentlyCastTo (targetType, sourceType))
        throwError (errorLocation, Errors::cannotImplicitlyCastType (printType (sourceType),
                                                                     printType (targetType)));
}

static void expectCastPossible (const AST::ObjectContext& errorLocation,
                                const AST::TypeBase& targetType, const AST::ValueBase& sourceValue,
                                bool mustBeSilent = false)
{
    auto& sourceType = getResultTypeOfValueOrThrowError (errorLocation, sourceValue);

    if (auto constantValue = sourceValue.constantFold())
    {
        checkTypeIsResolved (targetType);
        checkTypeIsResolved (sourceType);

        if (! AST::TypeRules::canCastTo (targetType, sourceType))
            throwError (errorLocation, Errors::cannotCastValue (AST::print (*constantValue),
                                                                printType (sourceType),
                                                                printType (targetType)));

        if (! AST::TypeRules::canSilentlyCastTo (targetType, *constantValue))
        {
            if (AST::TypeRules::canSilentlyCastTo (targetType, *constantValue))
                throwError (errorLocation, Errors::cannotImplicitlyCastValue (AST::print (*constantValue),
                                                                              printType (sourceType),
                                                                              printType (targetType)));
            
            throwError (errorLocation, Errors::cannotImplicitlyCastValue (AST::print (*constantValue),
                                                                          printType (sourceType),
                                                                          printType (targetType)));
        }
    }
    else
    {
        expectCastPossible (errorLocation, targetType, sourceType, mustBeSilent);
    }
}

static inline void expectCastPossible (const AST::ObjectContext& errorLocation, const AST::TypeBase& targetType,
                                       choc::span<ref<AST::Object>> sourceValues, bool mustBeSilent = false)
{
    if (auto mcr = targetType.getAsMakeConstOrRef())
        if (! mcr->makeRef)
            return expectCastPossible (errorLocation, *mcr->getSource(), sourceValues, mustBeSilent);

    checkNumberOfConstructorElements (errorLocation, targetType, sourceValues.size());

    if (auto array = targetType.getAsArrayType())
    {
        if (sourceValues.size() == 1)
        {
            auto& sourceType = getResultTypeOfValueOrThrowError (sourceValues.front());

            if (sourceType.isArray())
                return expectCastPossible (errorLocation, targetType, sourceType, mustBeSilent);
        }

        auto& elementType = array->getElementType (0);

        for (auto& item : sourceValues)
            expectCastPossible (item->context, elementType,
                                getAsValueOrThrowError (item.get()), mustBeSilent);
    }
    else if (auto vector = targetType.getAsVectorType())
    {
        if (sourceValues.size() == 1)
        {
            auto& sourceType = getResultTypeOfValueOrThrowError (sourceValues.front());

            if (sourceType.isVector())
                return expectCastPossible (errorLocation, targetType, sourceType, mustBeSilent);
        }

        auto& elementType = vector->getElementType();

        for (auto& item : sourceValues)
            expectCastPossible (item->context, elementType,
                                getAsValueOrThrowError (item.get()), mustBeSilent);
    }
    else if (auto s = targetType.getAsStructType())
    {
        if (sourceValues.size() == 1)
        {
            auto& sourceType = getResultTypeOfValueOrThrowError (sourceValues.front());

            if (sourceType.isSameType (*s, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                return;
        }

        for (size_t i = 0; i < sourceValues.size(); ++i)
            expectCastPossible (sourceValues[i]->context,
                                s->getMemberType(i),
                                getAsValueOrThrowError (sourceValues[i].get()), mustBeSilent);
    }
    else if (targetType.isPrimitiveComplex())
    {
        if (sourceValues.size() == 2)
        {
            for (size_t i = 0; i < sourceValues.size(); ++i)
                expectCastPossible (sourceValues[i]->context,
                                    targetType.isPrimitiveComplex32() ? targetType.context.allocator.float32Type
                                                                      : targetType.context.allocator.float64Type,
                                    getAsValueOrThrowError (sourceValues[i].get()), mustBeSilent);
        }
        else if (sourceValues.size() == 1)
        {
            expectCastPossible (sourceValues.front()->context, targetType,
                                getAsValueOrThrowError (sourceValues.front().get()), mustBeSilent);
        }
        else
        {
            throwError (sourceValues[2], Errors::wrongNumberOfComplexInitialisers());
        }
    }
    else if (sourceValues.size() == 1)
    {
        expectCastPossible (errorLocation, targetType, getAsValueOrThrowError (sourceValues.front()), mustBeSilent);
    }
    else if (sourceValues.size() != 0)
    {
        throwError (errorLocation, Errors::cannotCastListToType (AST::print (targetType)));
    }
}

static void expectSilentCastPossible (const AST::ObjectContext& errorLocation,
                                      const AST::TypeBase& targetType,
                                      const AST::ValueBase& sourceValue)
{
    expectCastPossible (errorLocation, targetType, sourceValue, true);
}

static inline const AST::TypeBase& expectSilentCastPossible (const AST::ObjectContext& errorLocation,
                                                             choc::span<ref<const AST::TypeBase>> targetTypes,
                                                             const AST::Expression& sourceValue)
{
    auto& value = getAsValueOrThrowError (sourceValue);
    auto sourceType = value.getResultType();

    AST::ObjectRefVector<const AST::TypeBase> possibleTypes;

    for (auto& type : targetTypes)
    {
        if (sourceType->skipConstAndRefModifiers().isSameType (type, AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
            return type; // stop if there's an exact match

        if (AST::TypeRules::canSilentlyCastTo (type, value))
            possibleTypes.push_back (type);
    }

    if (possibleTypes.size() == 1)
        return possibleTypes.front();

    std::vector<std::string> typeDescs;

    for (auto& type : targetTypes)
        typeDescs.push_back (AST::print (type.get()));

    auto targetDesc = choc::text::joinStrings (typeDescs, ", ");

    if (possibleTypes.size() == 0)
        throwError (errorLocation, Errors::cannotImplicitlyCastType (printType (*sourceType), targetDesc));

    throwError (errorLocation, Errors::ambiguousCastBetween (AST::print (*sourceType), targetDesc));
}

static inline const AST::TypeBase& expectSilentCastPossible (const AST::ObjectContext& errorLocation,
                                                             choc::span<ref<const AST::TypeBase>> targetTypes,
                                                             const AST::TypeBase& sourceType)
{
    AST::ObjectRefVector<const AST::TypeBase> possibleTypes;

    for (auto& type : targetTypes)
    {
        if (sourceType.skipConstAndRefModifiers().isSameType (type, AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
            return type; // stop if there's an exact match
    }

    if (possibleTypes.size() == 1)
        return possibleTypes.front();

    std::vector<std::string> typeDescs;

    for (auto& type : targetTypes)
        typeDescs.push_back (AST::print (type.get()));

    auto targetDesc = choc::text::joinStrings (typeDescs, ", ");

    if (possibleTypes.size() == 0)
        throwError (errorLocation, Errors::cannotImplicitlyCastType (printType (sourceType), targetDesc));

    throwError (errorLocation, Errors::ambiguousCastBetween (AST::print (sourceType), targetDesc));
}

static inline void expectBoolValue (const AST::Object& o)
{
    expectSilentCastPossible (o.context, o.context.allocator.boolType, getAsValueOrThrowError (o));
}

static inline void checkBinaryOperands (const AST::ObjectContext& errorContext,
                                        const AST::ValueBase& lhs, const AST::ValueBase& rhs,
                                        const AST::BinaryOpTypeEnum::Enum op,
                                        std::string_view opSymbol,
                                        bool isInPlaceOp)
{
    auto& lhsType = getResultTypeOfValueOrThrowError (lhs);
    auto& rhsType = getResultTypeOfValueOrThrowError (rhs);

    if (auto types = AST::TypeRules::getBinaryOperatorTypes (op, lhsType, rhsType))
    {
        if (isInPlaceOp)
            expectCastPossible (AST::getContext (lhs), lhsType, types.resultType,
                                lhsType.getAsBoundedType() == nullptr);

        expectCastPossible (AST::getContext (lhs), types.operandType, lhs, isInPlaceOp);
        expectCastPossible (AST::getContext (rhs), types.operandType, rhs, isInPlaceOp);

        if (op == AST::BinaryOpTypeEnum::Enum::modulo || op == AST::BinaryOpTypeEnum::Enum::divide)
            if (auto constDivisor = rhs.constantFold())
                if (constDivisor->isZero())
                    throwError (rhs, op == AST::BinaryOpTypeEnum::Enum::modulo ? Errors::moduloZero()
                                                                               : Errors::divideByZero());
    }
    else
    {
        if (lhsType.isArray() && rhsType.isArray())
            throwError (errorContext, Errors::illegalOperatorForArray (opSymbol));

        throwError (errorContext, Errors::illegalTypesForBinaryOperator (opSymbol,
                                                                         AST::print (lhsType),
                                                                         AST::print (rhsType)));
    }

    auto resultClass = AST::TypeRules::getComparisonResultClass (lhs, rhs, op);

    if (resultClass == AST::TypeRules::ComparisonResultClass::alwaysTrue)   throwError (errorContext, Errors::comparisonAlwaysTrue());
    if (resultClass == AST::TypeRules::ComparisonResultClass::alwaysFalse)  throwError (errorContext, Errors::comparisonAlwaysFalse());
}

static inline void checkAnnotation (const AST::Annotation& a)
{
    for (auto& v : a.values)
    {
        auto val = AST::castToValue (v);

        if (val == nullptr || getAsFoldedConstant (*val) == nullptr)
            throwError (v, Errors::unresolvedAnnotation());
    }
}

static bool containsVoidType (const AST::TypeBase& type)
{
    if (type.isVoid())
        return true;

    for (auto& t : type.getResolvedReferencedTypes())
        if (containsVoidType (t))
            return true;

    return false;
}

static bool containsBoundedType (const AST::TypeBase& type)
{
    if (type.isBoundedType())
        return true;

    for (auto& t : type.getResolvedReferencedTypes())
        if (containsBoundedType (t))
            return true;

    return false;
}

static inline void sanityCheckType (const AST::TypeBase& t)
{
    if (auto a = AST::castTo<AST::ArrayType> (t))
    {
        auto& type = a->getInnermostElementTypeRef();

        if (type.isVoid())         throwError (a, Errors::arrayElementCannotBeVoid());
        if (type.isReference())    throwError (a, Errors::arrayTypeCannotBeReference());
        if (type.isArray())        throwError (a, Errors::unimplementedFeature ("Nested multi-dimensional arrays"));
    }
    else if (auto v = AST::castTo<AST::VectorType> (t))
    {
        if (auto numElements = AST::castTo<AST::ConstantValueBase> (v->numElements))
            if (! numElements->getResultType()->isPrimitiveInt())
                throwError (v->numElements, Errors::nonIntegerArraySize());

        if (auto type = v->getArrayOrVectorElementType())
        {
            if (type->isVoid())
                throwError (v, Errors::vectorElementCannotBeVoid());

            if (type->isResolved() && ! type->canBeVectorElementType())
                throwError (v, Errors::illegalTypeForVectorElement());
        }
    }
}

static inline void checkEndpointTypeList (const AST::EndpointDeclaration& endpoint, bool isMainProcessor)
{
    int64_t arraySize = 0;

    if (endpoint.arraySize != nullptr)
        arraySize = getConstantArraySizeOrThrowError (endpoint.arraySize, (int64_t) AST::maxEndpointArraySize);

    if (endpoint.isStream())
        if (endpoint.dataTypes.size() > 1)
            throwError (endpoint, Errors::noMultipleTypesOnEndpoint());

    AST::ObjectRefVector<const AST::TypeBase> previousTypes;

    for (auto& dataType : endpoint.dataTypes)
    {
        auto& type = getAsTypeOrThrowError (dataType);

        if (! type.isResolved())
            throwError (type, Errors::endpointTypeNotResolved());

        sanityCheckType (type);

        if (type.isConst())
            throwError (dataType, Errors::endpointTypeCannotBeConst());

        if (type.isReference())
            throwError (dataType, Errors::endpointTypeCannotBeReference());

        for (auto& previous : previousTypes)
            if (type.isSameType (previous, AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                throwError (dataType, Errors::duplicateTypesInList (AST::print (previous),
                                                                    AST::print (type)));

        previousTypes.push_back (type);

        if (arraySize != 0 && type.isArray())
            throwError (dataType, Errors::noArraysOnArrayEndpoint());

        if (endpoint.isEvent() && type.isVoid())
            continue;

        if (containsVoidType (type))
            throwError (dataType, Errors::illegalTypeForEndpoint (AST::print (type)));

        if (endpoint.isInput && isMainProcessor && containsBoundedType (type))
            throwError (dataType, Errors::illegalTypeForTopLevelEndpoint (AST::print (type)));

        if (endpoint.isStream())
            if (type.isConst() || type.isReference() || ! type.isScalar())
                throwError (endpoint.dataTypes.front(),
                            Errors::illegalTypeForEndpoint (AST::print (type)));
    }
}

static inline void checkAliasTargetType (const AST::Alias& alias, bool isForProcessorSpecialisation)
{
    if (auto value = alias.target.getObject())
    {
        if (! value->isSyntacticObject()) // leave syntax errors for the validator
        {
            if (alias.aliasType == AST::AliasTypeEnum::Enum::typeAlias)
            {
                auto& t = getAsTypeOrThrowError (alias.target);

                if (t.isReference())
                    throwError (alias.target, isForProcessorSpecialisation ? Errors::processorParamsCannotBeReference()
                                                                           : Errors::usingCannotBeReference());
            }
            else if (alias.aliasType == AST::AliasTypeEnum::Enum::processorAlias)
            {
                if (AST::castToSkippingReferences<AST::ProcessorBase> (value) == nullptr)
                    throwError (value, Errors::expectedProcessorName());
            }
            else
            {
                CMAJ_ASSERT (alias.aliasType == AST::AliasTypeEnum::Enum::namespaceAlias);

                if (AST::castToSkippingReferences<AST::Namespace> (value) == nullptr)
                    throwError (value, Errors::expectedNamespaceName());
            }
        }
    }
}

static inline ptr<AST::Object> getSpecialisationParamDefault (const AST::Object& param)
{
    if (auto alias = AST::castToSkippingReferences<AST::Alias> (param))
    {
        validation::checkAliasTargetType (*alias, true);
        return alias->target.getObject();
    }

    if (auto var = AST::castToSkippingReferences<AST::VariableDeclaration> (param))
    {
        auto value = var->initialValue.getObject();

        if (value != nullptr && ! value->isSyntacticObject()) // leave syntax errors for the validator
            (void) validation::getAsValueOrThrowError (var->initialValue);

        if (auto type = var->getType())
            if (type->isReference())
                throwError (var->declaredType, Errors::processorParamsCannotBeReference());

        return value;
    }

    CMAJ_ASSERT_FALSE;
}

static inline void checkSpecialisationParams (AST::ModuleBase& module)
{
    bool hasHadDefault = false;

    for (auto& param : module.specialisationParams)
    {
        if (getSpecialisationParamDefault (param->getObjectRef()) != nullptr)
        {
            hasHadDefault = true;
        }
        else
        {
            if (hasHadDefault)
                throwError (param, Errors::defaultParamsMustBeAtTheEnd());
        }
    }
}


//==============================================================================
/// Counts the number of times a writable value such as a variable or struct member is
/// read or written within a given statement
static inline size_t countUsesOfValueWithinStatement (AST::Object& statementToSearch,
                                                      const AST::ValueBase& targetValue)
{
    struct ValueMatcher  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ValueMatcher (AST::Allocator& a) : super (a) {}

        ptr<const AST::VariableDeclaration> variableToMatch;
        AST::PooledString memberToMatch;
        size_t totalAccesses = 0;

        void visit (AST::VariableReference& v) override
        {
            if (variableToMatch == v.getVariable())
                ++totalAccesses;
        }

        void visit (AST::GetStructMember& m) override
        {
            if (! memberToMatch.empty())
            {
                if (auto vr = AST::castTo<AST::VariableReference> (m.object))
                {
                    if (variableToMatch == vr->getVariable())
                    {
                        if (m.member.get() == memberToMatch)
                            ++totalAccesses;

                        return;
                    }
                }
            }

            return super::visit (m);
        }
    };

    ValueMatcher matcher (statementToSearch.context.allocator);
    matcher.variableToMatch = targetValue.getSourceVariable();

    if (auto m = targetValue.getAsGetStructMember())
    {
        if (auto vr = AST::castTo<AST::VariableReference> (m->object))
        {
            matcher.variableToMatch = vr->getVariable();
            matcher.memberToMatch = m->member;
        }
    }

    matcher.visitObject (statementToSearch);
    return matcher.totalAccesses;
}

//==============================================================================
struct VariableAssignmentVisitor  : public AST::NonParameterisedObjectVisitor
{
    using super = AST::NonParameterisedObjectVisitor;
    using super::visit;

    VariableAssignmentVisitor (AST::Allocator& a) : super (a) {}

    int writing = 0, functionParam = 0;
    bool shouldStop = false;
    ptr<AST::ValueBase> assignedValue;

    /// Return true to continue, false to stop
    virtual bool handleWrite (AST::VariableDeclaration&, ptr<AST::ValueBase> valueWritten, bool isFunctionCallOutParam) = 0;

    void visit (AST::VariableReference& v) override
    {
        if (writing + functionParam > 0)
            if (! handleWrite (v.getVariable(), assignedValue, functionParam > 0))
                shouldStop = true;
    }

    void visit (AST::VariableDeclaration& v) override
    {
        if (auto init = AST::castToValue (v.initialValue))
            if (! handleWrite (v, init, false))
                shouldStop = true;
    }

    void visit (AST::ScopeBlock& s) override
    {
        if (! shouldStop)
            super::visit (s);
    }

    void visit (AST::Assignment& a) override
    {
        if (shouldStop)
            return;

        ++writing;
        assignedValue = AST::castToValue (a.source);
        visitObject (a.target);
        assignedValue = {};
        --writing;
        visitObject (a.source);
    }

    void visit (AST::PreOrPostIncOrDec& p) override
    {
        if (shouldStop)
            return;

        ++writing;
        visitObject (p.target);
        --writing;
    }

    void visit (AST::InPlaceOperator& p) override
    {
        if (shouldStop)
            return;

        ++writing;
        visitObject (p.target);
        --writing;
        visitObject (p.source);
    }

    void visit (AST::FunctionCall& fc) override
    {
        if (shouldStop)
            return;

        auto& f = *fc.getTargetFunction();
        auto paramTypes = f.getParameterTypes();

        for (size_t i = 0; i < f.parameters.size(); ++i)
        {
            if (paramTypes[i]->isNonConstReference())
            {
                ++functionParam;
                visitObject (fc.arguments[i].getObjectRef());
                --functionParam;
            }
        }
    }
};

//==============================================================================
static inline bool isVariableWrittenWithinFunction (const AST::Function& fn, const AST::VariableDeclaration& v)
{
    struct VariableChecker  : public VariableAssignmentVisitor
    {
        VariableChecker (const AST::VariableDeclaration& va)
            : VariableAssignmentVisitor (va.context.allocator), variable (va) {}

        bool handleWrite (AST::VariableDeclaration& v, ptr<AST::ValueBase>, bool) override
        {
            if (std::addressof (v) == std::addressof (variable))
            {
                written = true;
                return false;
            }

            return true;
        }

        const AST::VariableDeclaration& variable;
        bool written = false;
    };

    VariableChecker checker (v);
    checker.visitObject (fn.mainBlock);
    return checker.written;
}

//==============================================================================
struct OutOfScopeSourcesForValue
{
    OutOfScopeSourcesForValue (const AST::ValueBase& value,
                               const AST::TypeBase& destinationType,
                               const AST::Statement* destinationStatement)
        : targetType (destinationType), targetStatement (destinationStatement)
    {
        addSources (value);
    }

    AST::ObjectRefVector<const AST::Object> sourcesFound;

private:
    ref<const AST::TypeBase> targetType;
    const AST::Statement* targetStatement;

    void addSource (const AST::Object& o)
    {
        if (! sourcesFound.contains (o))
            sourcesFound.push_back (o);
    }

    void addSourcesFromVariable (const AST::VariableDeclaration& v, const AST::ValueBase& value)
    {
        if (v.isGlobal())
            return;

        if (! doesLocalVariableOutliveTarget (v))
        {
            for (auto& s : v.sourcesOfNonGlobalData)
                addSource (s);

            if (AST::areFixedSizeArraysCopiedToSlices (targetType, *value.getResultType()))
                addSource (value);
        }
    }

    bool doesLocalVariableOutliveTarget (const AST::VariableDeclaration& v) const
    {
        if (targetStatement == nullptr)
            return false;

        if (v.isParameter())
            return v.getParentFunction().getMainBlock()->containsStatement (*targetStatement);

        return doesStatementOutliveTarget (v);
    }

    bool doesStatementOutliveTarget (const AST::Statement& s) const
    {
        auto statementParentBlock = s.findParentOfType<AST::ScopeBlock>();
        auto targetIndex = statementParentBlock->findIndexOfStatementContaining (*targetStatement);

        if (targetIndex < 0)
            return false;

        auto variableIndex = statementParentBlock->findIndexOfStatementContaining (s);
        CMAJ_ASSERT (variableIndex >= 0);
        return variableIndex < targetIndex;
    }

    void addSources (const AST::ValueBase& value, const AST::TypeBase& destType, const AST::Statement* destStatement)
    {
        auto oldType = targetType;
        auto oldStatement = targetStatement;
        targetType = destType;
        targetStatement = destStatement;
        addSources (value);
        targetType = oldType;
        targetStatement = oldStatement;
    }

    void addSources (const AST::ValueBase& value)
    {
        if (! targetType->containsSlice() || value.isConstantValueBase())
            return;

        if (auto vr = value.getAsVariableReference())
            return addSourcesFromVariable (vr->getVariable(), value);

        if (auto fc = value.getAsFunctionCall())
        {
            if (AST::areFixedSizeArraysCopiedToSlices (targetType, *fc->getResultType()))
                addSource (value);

            return;
        }

        if (auto c = value.getAsCast())
        {
            if (c->arguments.empty())
                return;

            if (targetType->isSlice())
            {
                if (c->isCompileTimeConstant())
                    return;

                if (c->arguments.size() > 1)
                    addSource (value);
                else
                    addSources (AST::castToValueRef (c->arguments.front()));
            }

            if (auto gs = AST::castTo<AST::StructType> (targetType->skipConstAndRefModifiers()))
            {
                CMAJ_ASSERT (gs->memberTypes.size() == c->arguments.size());

                for (size_t i = 0; i < c->arguments.size(); ++i)
                    if (auto v = AST::castToValue (c->arguments[i]))
                        addSources (*v, gs->getMemberType(i), nullptr);

                return;
            }

            if (auto ga = AST::castTo<AST::ArrayType> (targetType->skipConstAndRefModifiers()))
            {
                auto& elementType = *ga->getArrayOrVectorElementType();

                for (auto& arg : c->arguments.iterateAs<AST::ValueBase>())
                    addSources (arg, elementType, nullptr);

                return;
            }

            return addSource (value);
        }

        if (auto ge = value.getAsGetElement())
        {
            if (auto v = ge->getSourceVariable())
                return addSourcesFromVariable (*v, value);

            return addSource (value);
        }

        if (auto gs = value.getAsGetArrayOrVectorSlice())
            return addSources (AST::castToValueRef (gs->parent));

        if (auto gm = value.getAsGetStructMember())
        {
            if (auto v = gm->getSourceVariable())
                return addSourcesFromVariable (*v, value);

            return addSource (value);
        }

        if (auto t = value.getAsTernaryOperator())
        {
            addSources (AST::castToValueRef (t->trueValue));
            addSources (AST::castToValueRef (t->falseValue));
            return;
        }

        if (value.isStateUpcast())
            return;

        addSource (value);
    }
};

//==============================================================================
static inline void markLocalVariablesWhichMayReferToLocalSlices (AST::Namespace& root)
{
    struct Visitor  : public VariableAssignmentVisitor
    {
        Visitor (AST::Allocator& a) : VariableAssignmentVisitor (a) {}

        bool handleWrite (AST::VariableDeclaration& v, ptr<AST::ValueBase> value, bool isFunctionCallOutParam) override
        {
            if (v.isGlobal())
                return true;

            if (isFunctionCallOutParam)
            {
                if (! v.sourcesOfNonGlobalData.contains (v))
                {
                    v.sourcesOfNonGlobalData.push_back (v);
                    anyChanges = true;
                }

                return true;
            }

            if (value != nullptr)
            {
                OutOfScopeSourcesForValue sources (*value, *value->getResultType(), nullptr);

                for (auto& s : sources.sourcesFound)
                {
                    if (! v.sourcesOfNonGlobalData.contains (s))
                    {
                        v.sourcesOfNonGlobalData.push_back (s);
                        anyChanges = true;
                    }
                }
            }

            return true;
        }

        bool anyChanges = false;
    };

    std::vector<ref<AST::Function>> functionsWithSliceParams;

    root.visitAllFunctions (true, [&] (AST::Function& fn)
    {
        for (auto& p : fn.iterateParameters())
        {
            if (p.getType()->containsSlice())
            {
                functionsWithSliceParams.push_back (fn);
                break;
            }
        }
    });

    for (;;)
    {
        bool anyChanges = false;

        root.visitAllFunctions (true, [&] (AST::Function& fn)
        {
            Visitor visitor (fn.context.allocator);
            visitor.visitObject (fn.mainBlock);
            anyChanges = anyChanges || visitor.anyChanges;
        });

        for (auto& fn : functionsWithSliceParams)
        {
            for (auto r = fn->firstReferrer; r != nullptr; r = r->next)
            {
                if (auto call = AST::castToSkippingReferences<AST::FunctionCall> (r->referrer.owner))
                {
                    for (size_t i = 0; i < fn->parameters.size(); ++i)
                    {
                        auto& param = AST::castToRefSkippingReferences<AST::VariableDeclaration> (fn->parameters[i]);
                        auto& paramType = *param.getType();

                        if (paramType.containsSlice())
                        {
                            auto& arg = AST::castToValueRef (call->arguments[i]);

                            OutOfScopeSourcesForValue sources (arg, paramType, nullptr);

                            for (auto& s : sources.sourcesFound)
                            {
                                if (! param.sourcesOfNonGlobalData.contains (s))
                                {
                                    param.sourcesOfNonGlobalData.push_back (s);
                                    anyChanges = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (! anyChanges)
            break;
    }
}

//==============================================================================
static inline void checkExpressionForRecursion (const AST::Object& object)
{
    std::vector<const AST::Object*> visited, objectsToVisit;
    objectsToVisit.push_back (std::addressof (object));

    for (;;)
    {
        if (objectsToVisit.empty())
            return;

        auto o = objectsToVisit.back();
        objectsToVisit.pop_back();

        if (o == nullptr)
        {
            visited.pop_back();
            continue;
        }

        if (std::find (visited.begin(), visited.end(), o) != visited.end())
        {
            std::vector<AST::PooledString> names;

            for (auto& item : visited)
                if (! item->getOriginalName().empty())
                    names.push_back (item->getOriginalName());

            if (! o->getOriginalName().empty())
                names.push_back (o->getOriginalName());

            names.erase (std::unique (names.begin(), names.end()), names.end());

            if (names.empty())      throwError (*o, Errors::recursiveExpression());
            if (names.size() == 1)  throwError (*o, Errors::recursiveReference (names.front()));

            throwError (*o, Errors::recursiveReferences (names.back(), names[names.size() - 2]));
        }

        objectsToVisit.push_back (nullptr);
        visited.push_back (o);

        if (auto a = o->getAsAlias())
        {
            if (auto target = a->target.getObject())
                objectsToVisit.push_back (target.get());
        }
        else if (auto t = o->getAsTypeBase())
        {
            for (auto& ref : t->getResolvedReferencedTypes())
                objectsToVisit.push_back (ref.getPointer());
        }
        else if (auto m = o->getAsModuleBase())
        {
            for (auto& alias : m->aliases)
                objectsToVisit.push_back (std::addressof (alias->getObjectRef()));
        }
        else if (auto r = o->getAsNamedReference())
        {
            objectsToVisit.push_back (std::addressof (r->target.getObjectRef()));
        }
    }
}

//==============================================================================
static inline void checkVariableInitialiserForRecursion (AST::VariableDeclaration& v)
{
    struct VariableRecursionCheck  : public AST::Visitor
    {
        using super = AST::Visitor;
        using super::visit;

        VariableRecursionCheck (AST::Allocator& a) : super (a) {}

        void visit (AST::VariableReference& v) override
        {
            auto& targetVariable = v.getVariable();

            for (auto& visited : visitStack)
                if (AST::castTo<AST::VariableDeclaration> (*visited) == targetVariable)
                    throwError (v.context, Errors::initialiserRefersToTarget (v.getName()));

            super::visit (v);
        }

        void visit (AST::Function&) override   {}
        void visit (AST::StructType&) override {}
    };

    if (v.initialValue != nullptr)
        VariableRecursionCheck (v.context.allocator).visitObject (v);
}

//==============================================================================
static inline void checkNodesForRecursion (const AST::ProcessorBase& p)
{
    struct NodeVisitorStack
    {
        const AST::ProcessorBase* processorBase = {};
        const NodeVisitorStack* previous = {};

        bool contains (const AST::ProcessorBase& p) const
        {
            for (auto s = this; s != nullptr; s = s->previous)
                if (s->processorBase == std::addressof (p))
                    return true;

            return false;
        }

        static void check (const AST::ProcessorBase& p, const NodeVisitorStack* stack)
        {
            if (stack != nullptr && stack->contains (p))
                throwError (p, Errors::recursiveReference (p.getOriginalName()));

            for (auto& node : p.nodes)
            {
                auto& n = AST::castToRefSkippingReferences<AST::GraphNode> (node);

                if (auto targetProcessorBase = AST::castToSkippingReferences<AST::ProcessorBase> (n.processorType))
                {
                    const NodeVisitorStack newStack { std::addressof (p), stack };
                    check (*targetProcessorBase, std::addressof (newStack));
                }
            }
        }
    };

    NodeVisitorStack::check (p, nullptr);
}

//==============================================================================
struct DuplicateNameChecker
{
    DuplicateNameChecker()
    {
        startNewScope();
    }

    ptr<const AST::ObjectContext> findExisting (AST::PooledString nameToCheck) const
    {
        for (auto& list : names)
        {
            auto item = list->find (nameToCheck);

            if (item != list->end())
                return item->second;
        }

        return {};
    }

    bool isInTopScope (AST::PooledString nameToCheck) const
    {
        if (names.empty())
            return {};

        return names.back()->find (nameToCheck) != names.back()->end();
    }

    template <typename Context>
    void add (AST::PooledString name, const Context& context)
    {
        (*names.back())[name] = AST::getContext (context);
    }

    void startNewScope()
    {
        names.push_back (std::make_unique<std::unordered_map<AST::PooledString, ptr<const AST::ObjectContext>>>());
    }

    void endScope()
    {
        names.pop_back();
    }

    template <typename Context>
    void checkWithoutAdding (AST::PooledString nameToCheck, const Context& context)
    {
        if (! nameToCheck.empty())
            if (auto existing = findExisting (nameToCheck))
                throwErrorWithPreviousDeclaration (context, *existing, Errors::nameInUse (nameToCheck));
    }

    template <typename Context>
    void checkAndAdd (AST::PooledString nameToCheck, const Context& context)
    {
        checkWithoutAdding (nameToCheck, context);
        add (nameToCheck, context);
    }

    template <typename Context>
    void emitWarningForShadowedVariable (AST::PooledString nameToCheck, Context&& context)
    {
        if (! nameToCheck.empty())
        {
            if (auto existing = findExisting (nameToCheck))
            {
                if (isInTopScope (nameToCheck))
                    throwError (Errors::nameInUse (nameToCheck)
                                 .withLocation (AST::getContext (context).getFullLocation()));

                emitMessage (Errors::localVariableShadow (nameToCheck)
                             .withLocation (AST::getContext (context).getFullLocation()));
            }

            add (nameToCheck, context);
        }
    }

    void checkList (const AST::ListProperty& list)
    {
        for (auto& item : list)
            checkAndAdd (item->getObjectRef().getName(), AST::getContext (item));
    }

    void checkFunctions (const AST::ListProperty& list)
    {
        for (auto& f : list)
        {
            auto& fn = AST::castToFunctionRef (f);

            if (! fn.isEventHandler)
                checkWithoutAdding (fn.getName(), fn.context);
        }
    }

    std::vector<std::unique_ptr<std::unordered_map<AST::PooledString, ptr<const AST::ObjectContext>>>> names;
};

//==============================================================================
struct DuplicateFunctionChecker
{
    std::unordered_map<std::string, AST::ObjectRefVector<const AST::Function>> functions;

    void checkList (const AST::ListProperty& list)
    {
        for (auto& f : list)
            check (AST::castToFunctionRef (f));
    }

    void checkList (choc::span<ref<AST::Function>> list)
    {
        for (auto& f : list)
            check (f.get());
    }

    void check (const AST::Function& fn)
    {
        if (! fn.isGenericOrParameterised())
        {
            auto nameAndNumArgs = std::to_string (fn.getNumParameters()) + "_" + std::string (fn.getName());
            auto& existing = functions[nameAndNumArgs];

            for (auto& f : existing)
            {
                if (areFunctionParametersEquivalent (f, fn))
                {
                    if (fn.isMainFunction())
                        throwError (fn, Errors::multipleMainFunctions());
                    else
                        throwError (fn, Errors::duplicateFunction());
                }
            }

            existing.push_back (fn);
        }
    }

    static bool areFunctionParametersEquivalent (const AST::Function& f1, const AST::Function& f2)
    {
        auto numParams = f1.getNumParameters();

        for (size_t i = 0; i < numParams; ++i)
            if (! isParameterEquivalent (f1.getParameter(i).declaredType.getObjectRef(),
                                         f2.getParameter(i).declaredType.getObjectRef()))
                return false;

        return true;
    }

    static bool isParameterEquivalent (const AST::Object& param1, const AST::Object& param2)
    {
        if (param1.isIdentical (param2))
            return true;

        if (auto type1 = AST::castToTypeBase (param1))
            if (auto type2 = AST::castToTypeBase (param2))
                return AST::TypeRules::areTypesEquivalentAsFunctionParameters (*type1, *type2);

        return false;
    }
};

//==============================================================================
/// Figures out the set of ways in which a statement can exit
struct StatementExitMethods
{
    StatementExitMethods (const AST::Statement& s)
    {
        bool lastStatementWasReturn = findBreakAndReturns (s, false);

        if (lastStatementWasReturn)
            exitsDirectly = false;
    }

    bool notAllControlPathsReturn() const   { return exitsDirectly || exitsWithBreak || doesNotExit(); }
    bool doesNotExit() const                { return ! (exitsDirectly || exitsWithReturn || exitsWithBreak); }

    bool exitsDirectly = true;
    bool exitsWithReturn = false;
    bool exitsWithBreak = false;

private:
    struct Scope
    {
        const AST::Object& scope;
        bool containedBreak;
    };

    std::vector<Scope> scopes;

    bool findBreakAndReturns (const AST::Object& s, bool lastWasReturn)
    {
        if (auto loop = s.getAsLoopStatement())
        {
            scopes.push_back ({ s, false });
            lastWasReturn = findBreakAndReturns (loop->body.getObjectRef(), false);
            bool loopHasBreak = scopes.back().containedBreak;
            bool loopIsInfinite = loop->isInfinite();

            if (loopHasBreak)
                exitsDirectly = true;
            else if (loopIsInfinite)
                exitsDirectly = false;

            scopes.pop_back();
            return lastWasReturn && loopIsInfinite && ! loopHasBreak;
        }

        if (auto scope = s.getAsScopeBlock())
        {
            if (! scope->statements.empty())
            {
                if (! scope->label.get().empty())
                    scopes.push_back ({ s, false });

                for (auto& statement : scope->statements)
                    lastWasReturn = findBreakAndReturns (statement->getObjectRef(), lastWasReturn);

                if (! scope->label.get().empty())
                    scopes.pop_back();
            }

            return lastWasReturn;
        }

        if (auto i = s.getAsIfStatement())
        {
            bool lastWasReturn1 = findBreakAndReturns (i->trueBranch.getObjectRef(), false);
            bool lastWasReturn2 = false;

            if (i->falseBranch != nullptr)
                lastWasReturn2 = findBreakAndReturns (i->falseBranch.getObjectRef(), false);

            return lastWasReturn1 && lastWasReturn2;
        }

        if (auto b = s.getAsBreakStatement())
        {
            auto targetBlock = AST::castTo<AST::Statement> (b->targetBlock);

            for (size_t i = scopes.size(); i > 0; --i)
            {
                auto& parentScope = scopes[i - 1];

                bool isTargetBlock = (targetBlock == parentScope.scope
                                        || (targetBlock == nullptr && parentScope.scope.getAsLoopStatement() != nullptr));

                parentScope.containedBreak = true;

                if (isTargetBlock)
                    return false;
            }

            exitsWithBreak = true;
            return false;
        }

        if (s.getAsReturnStatement() != nullptr)
        {
            exitsWithReturn = true;
            return true;
        }

        return lastWasReturn;
    }
};

}
