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

namespace cmaj::passes
{

//==============================================================================
struct TypeResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    TypeResolver (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::BracketedSuffix& b) override
    {
        super::visit (b);

        if (auto v = AST::castToValue (b.parent))
        {
            if (auto parentType = v->getResultType())
            {
                if (parentType->isVectorOrArray())
                {
                    if (b.terms.empty())
                        throwError (b, Errors::expectedArrayIndex());

                    if (b.terms.size() == 1)
                    {
                        auto& term = AST::castToRef<AST::BracketedSuffixTerm> (b.terms[0]);

                        if (term.isRange)
                        {
                            auto& getSlice = replaceWithNewObject<AST::GetArrayOrVectorSlice> (b);
                            getSlice.parent.referTo (*v);

                            if (term.startIndex != nullptr)  getSlice.start.referTo (term.startIndex);
                            if (term.endIndex != nullptr)    getSlice.end.referTo (term.endIndex);
                            return;
                        }
                    }

                    auto& getElement = replaceWithNewObject<AST::GetElement> (b);
                    getElement.parent.referTo (*v);

                    for (auto& term : b.terms.iterateAs<AST::BracketedSuffixTerm>())
                    {
                        if (term.isRange)
                            throwError (term, Errors::unimplementedFeature ("Multi-dimensional array slices"));

                        if (term.startIndex == nullptr)
                            throwError (term, Errors::wrongNumberOfArrayIndexes (std::to_string (parentType->getNumDimensions()), "0"));

                        getElement.indexes.addChildObject (term.startIndex);
                    }

                    return;
                }
            }

            registerFailure();
            return;
        }

        if (auto parentType = AST::castToTypeBase (b.parent))
        {
            auto& arrayType = AST::getContextOfStartOfExpression (b).allocate<AST::ArrayType>();
            replaceObject (b, arrayType);
            arrayType.elementType.createReferenceTo (*parentType);

            for (auto& t : b.terms)
            {
                auto& term = AST::castToRef<AST::BracketedSuffixTerm> (t);

                if (term.endIndex != nullptr || term.isRange)
                    throwError (term, Errors::unexpectedSliceRange());

                if (term.startIndex != nullptr)
                    arrayType.dimensionList.addChildObject (term.startIndex);
                else if (b.terms.size() > 1)
                    throwError (term, Errors::unimplementedFeature ("Multi-dimensional array slices"));
            }

            return;
        }

        if (auto endpoint = AST::castToSkippingReferences<AST::EndpointInstance> (b.parent))
            return replaceWithGetElementForNode (b, *endpoint);

        if (auto endpointDeclaration = AST::castToSkippingReferences<AST::EndpointDeclaration> (b.parent))
        {
            auto& endpointInstance = b.allocateChild<AST::EndpointInstance>();
            endpointInstance.endpoint.createReferenceTo (*endpointDeclaration);
            return replaceWithGetElementForNode (b, endpointInstance);
        }

        if (auto node = AST::castToSkippingReferences<AST::GraphNode> (b.parent))
            return replaceWithGetElementForNode (b, *node);

        registerFailure();
    }

    void replaceWithGetElementForNode (AST::BracketedSuffix& b, AST::Object& parent)
    {
        if (b.terms.empty())
            throwError (b, Errors::expectedArrayIndex());

        auto& term = AST::castToRef<AST::BracketedSuffixTerm> (b.terms[0]);

        if (b.terms.size() > 1)
            throwError (b, Errors::unimplementedFeature ("Multi-dimensional indexing of endpoints"));

        if (term.isRange)
        {
            auto& getSlice = replaceWithNewObject<AST::GetArrayOrVectorSlice> (b);
            getSlice.parent.referTo (parent);

            if (term.startIndex != nullptr)  getSlice.start.referTo (term.startIndex);
            if (term.endIndex != nullptr)    getSlice.end.referTo (term.endIndex);
        }
        else
        {
            auto& getElement = replaceWithNewObject<AST::GetElement> (b);
            getElement.parent.referTo (parent);
            getElement.indexes.addReference (term.startIndex);
        }
    }

    void visit (AST::ChevronedSuffix& c) override
    {
        super::visit (c);

        if (auto parentType = AST::castToTypeBase (c.parent))
        {
            if (c.terms.size() > 1)
                throwError (c.terms[1], Errors::unimplementedFeature ("Multi-dimensional vectors"));

            auto& vectorType = replaceWithNewObject<AST::VectorType> (c);
            vectorType.elementType.setChildObject (c.parent);
            vectorType.numElements.setChildObject (c.terms[0].getObjectRef());
            validation::sanityCheckType (vectorType);
            return;
        }

        registerFailure();
    }

    void visit (AST::NamespaceSeparator& s) override
    {
        super::visit (s);

        if (auto parentType = AST::castToTypeBase (s.lhs))
            if (auto parentStruct = AST::castToSkippingReferences<AST::StructType> (parentType->skipConstAndRefModifiers()))
                if (auto name = AST::castTo<AST::Identifier> (s.rhs))
                    if (auto memberType = parentStruct->getTypeForMember (name->getName()))
                        replaceObject (s, AST::createReference (s, *memberType));
    }

    void visit (AST::MakeConstOrRef& m) override
    {
        super::visit (m);

        if (! (m.makeConst || m.makeRef))
        {
            m.replaceWith (m.source.get());
            registerChange();
            return;
        }

        if (auto nested = AST::castTo<AST::MakeConstOrRef> (m.source))
        {
            registerChange();

            if (nested->makeConst)   m.makeConst = true;
            if (nested->makeRef)     m.makeRef = true;

            m.source.referTo (nested->source);
        }

        if (auto nested = AST::castTo<AST::TypeMetaFunction> (m.source))
        {
            if (nested->op == AST::TypeMetaFunctionTypeEnum::Enum::makeConst)
            {
                registerChange();
                m.makeConst = true;
                m.source.referTo (nested->source);
            }
            else if (nested->op == AST::TypeMetaFunctionTypeEnum::Enum::makeReference)
            {
                registerChange();
                m.makeRef = true;
                m.source.referTo (nested->source);
            }
            else if (nested->op == AST::TypeMetaFunctionTypeEnum::Enum::removeConst)
            {
                if (m.makeConst)
                {
                    registerChange();
                    m.makeConst = false;
                    m.source.referTo (nested->source);
                }
            }
            else if (nested->op == AST::TypeMetaFunctionTypeEnum::Enum::removeReference)
            {
                if (m.makeRef)
                {
                    registerChange();
                    m.makeRef = false;
                    m.source.referTo (nested->source);
                }
            }
        }
    }

    void visit (AST::ArrayType& a) override
    {
        super::visit (a);

        auto& elementType = a.getInnermostElementTypeRef();

        if (auto arrayElement = AST::castToSkippingReferences<AST::ArrayType> (elementType))
        {
            a.setInnermostElementType (arrayElement->getInnermostElementTypeObject());

            for (auto& d : arrayElement->dimensionList)
                a.dimensionList.addReference (d->getObjectRef());

            registerChange();
        }
    }

    void visit (AST::Assignment& a) override
    {
        super::visit (a);

        if (auto target = AST::castToValue (a.target))
        {
            if (auto targetType = target->getResultType())
            {
                convertUntypedValueOrListToValue (a.source, *targetType, false);

                if (auto source = AST::castToValue (a.source))
                    if (auto sourceType = source->getResultType())
                        makeSilentCastExplicitIfNeeded (a.source, *targetType, *sourceType, *source);
            }
        }
    }

    void visit (AST::WriteToEndpoint& w) override
    {
        super::visit (w);

        if (auto endpoint = AST::castToSkippingReferences<AST::EndpointDeclaration> (w.target))
        {
            if (w.value != nullptr)
            {
                auto possibleTypes = endpoint->getDataTypes();

                if (possibleTypes.size() == 1)
                    convertUntypedValueOrListToValue (w.value, possibleTypes.front(), false);
            }

            if (auto value = AST::castToValue (w.value))
                if (auto valueType = value->getResultType())
                    if (auto endpointType = endpoint->getEndpointTypeForValue (false, *value))
                        makeSilentCastExplicitIfNeeded (w.value, *endpointType, *valueType, *value);
        }
    }

    void visit (AST::VariableDeclaration& v) override
    {
        super::visit (v);

        if (auto initialiser = v.initialValue.getPointer())
        {
            if (auto declaredType = AST::castToTypeBase (v.declaredType))
            {
                if (declaredType->isResolved())
                {
                    convertUntypedValueOrListToValue (v.initialValue, *declaredType, false);

                    if (! v.isConstant && v.isCompileTimeConstant())
                    {
                        v.isConstant = true;
                        registerChange();
                    }

                    if (auto initialValue = AST::castToValue (*initialiser))
                    {
                        if (auto sourceType = initialValue->getResultType())
                        {
                            if (declaredType->isSlice() &&  initialValue->isCompileTimeConstant())
                                makeSliceSizeMatchTarget (v.declaredType, *sourceType);

                            declaredType = AST::castToTypeBase (v.declaredType); // may have changed

                            if (! declaredType->isReference())
                                makeSilentCastExplicitIfNeeded (v.initialValue, *declaredType, *sourceType, *initialValue);
                        }
                    }
                }
            }
        }
    }

    void visit (AST::TernaryOperator& t) override
    {
        super::visit (t);

        if (auto trueType = AST::castToTypeBase (t.trueValue))
        {
            if (auto falseType = AST::castToTypeBase (t.falseValue))
            {
                if (auto conditionConst = AST::getAsFoldedConstant (t.condition))
                {
                    auto replaceTypeTernary = [&] (bool isTrue)
                    {
                        replaceObject (t, isTrue ? *trueType : *falseType);
                    };

                    if (auto v = conditionConst->getAsConstantBool())    return replaceTypeTernary (v->value);
                    if (auto v = conditionConst->getAsConstantInt32())   return replaceTypeTernary (v->value != 0);
                    if (auto v = conditionConst->getAsConstantInt64())   return replaceTypeTernary (v->value != 0);
                }

                registerFailure();
            }
        }

        if (auto resultType = t.getResultType())
        {
            if (auto value = AST::castToValue (t.trueValue))
                if (auto type = value->getResultType())
                    makeSilentCastExplicitIfNeeded (t.trueValue, *resultType, *type, *value);

            if (auto value = AST::castToValue (t.falseValue))
                if (auto type = value->getResultType())
                    makeSilentCastExplicitIfNeeded (t.falseValue, *resultType, *type, *value);
        }
    }

    void visit (AST::Cast& c) override
    {
        super::visit (c);

        if (AST::updateCastTypeSizeIfPossible (c))
            registerChange();

        if (auto targetType = AST::castToTypeBase (c.targetType))
        {
            if (targetType->isResolved() && targetType->isFixedSizeAggregate())
            {
                auto size = targetType->isVectorOrArray() ? targetType->getArrayOrVectorSize(0)
                                                          : targetType->getFixedSizeAggregateNumElements();
                auto numElements = std::min (c.arguments.size(), static_cast<size_t> (size));

                if (targetType->isStruct())
                {
                    for (size_t i = 0; i < numElements; ++i)
                        if (auto aggregateElementType = targetType->getAggregateElementType (i))
                            convertUntypedValueOrListToValue (*c.arguments[i].getAsObjectProperty(), *aggregateElementType, true);
                }
                else
                {
                    if (auto innerType = targetType->getAggregateElementType (0))
                        for (size_t i = 0; i < numElements; ++i)
                            convertUntypedValueOrListToValue (*c.arguments[i].getAsObjectProperty(), *innerType, true);
                }
            }
        }
    }

    void visit (AST::FunctionCall& call) override
    {
        super::visit (call);

        if (auto fn = call.getTargetFunction())
        {
            for (size_t i = 0; i < call.arguments.size(); ++i)
            {
                auto& param = fn->getParameter(i);

                if (auto paramType = param.getType())
                {
                    if (! paramType->isNonConstReference())
                    {
                        auto& arg = AST::castToValueRef (call.arguments[i]);

                        if (auto argType = arg.getResultType())
                            if (! argType->isReference())
                                makeSilentCastExplicitIfNeeded (*call.arguments[i].getAsObjectProperty(), *paramType, *argType, arg);
                    }
                }
            }
        }
    }

    void makeSilentCastExplicitIfNeeded (AST::ObjectProperty& target, const AST::TypeBase& targetType,
                                         const AST::TypeBase& sourceType, AST::ValueBase& value)
    {
        if (! (targetType.isResolved() && sourceType.isResolved()))
        {
            registerFailure();
            return;
        }

        if (targetType.isSameType (sourceType, AST::TypeBase::ComparisonFlags::ignoreConst
                                                | AST::TypeBase::ComparisonFlags::ignoreReferences))
            return;

        auto& bareTargetType = targetType.skipConstAndRefModifiers();

        if (! bareTargetType.isSlice() && ! AST::TypeRules::canSilentlyCastTo (bareTargetType, value))
            return;

        if (bareTargetType.isBoundedType() && sourceType.isPrimitiveInt())
            return;

        auto& cast = AST::getContext (target).allocate<AST::Cast>();
        cast.targetType.createReferenceTo (bareTargetType);
        cast.arguments.addReference (value);
        cast.onlySilentCastsAllowed = true;
        target.referTo (cast);
        registerChange();
    }

    void convertUntypedValueOrListToValue (AST::ObjectProperty& valueProp, const AST::TypeBase& expectedType, bool handleSingleItems)
    {
        if (! expectedType.isResolved())
            return;

        if (expectedType.isVoid())
            throwError (valueProp, Errors::targetCannotBeVoid());

        if (auto m = expectedType.getAsMakeConstOrRef())
            return convertUntypedValueOrListToValue (valueProp, m->getSourceRef(), handleSingleItems);

        auto& value = valueProp.getObjectRef();

        if (auto list = value.getAsExpressionList())
        {
            auto numSourceElements = list->items.size();

            if (numSourceElements != 0)
            {
                auto& expected = expectedType.skipConstAndRefModifiers();

                if (expected.isFixedSizeAggregate())
                {
                    auto numTargetElements = expected.getFixedSizeAggregateNumElements();

                    if (numTargetElements != numSourceElements)
                    {
                        if (auto arrayType = expected.getAsArrayType())
                            if (numTargetElements == 1 && arrayType->getNumDimensions() > 1)
                                return convertUntypedValueOrListToValue (valueProp, *arrayType->getAggregateElementType (0), handleSingleItems);

                        if (numSourceElements != 1)
                            throwError (value, Errors::wrongNumArgsForAggregate (AST::print (expectedType)));

                        value.replaceWith (list->items.front().getObjectRef());
                        registerChange();
                        return;
                    }
                }
            }

            auto& cast = replaceWithNewObject<AST::Cast> (value);
            cast.targetType.createReferenceTo (expectedType);
            cast.arguments.moveListItems (list->items);
            cast.onlySilentCastsAllowed = true;

            if (numSourceElements != 0)
            {
                if (AST::applySizeIfSlice (cast.targetType, cast.arguments.size()))
                    registerChange();

                auto& expected = AST::castToTypeBaseRef (cast.targetType);

                if (expected.isFixedSizeAggregate())
                {
                    CMAJ_ASSERT (numSourceElements == expected.getFixedSizeAggregateNumElements());

                    if (expected.isStruct())
                    {
                        for (size_t i = 0; i < numSourceElements; ++i)
                        {
                            auto& elementType = *expected.getAggregateElementType(i);
                            auto& arg = *cast.arguments[i].getAsObjectProperty();

                            convertUntypedValueOrListToValue (arg, elementType, true);

                            if (auto v = AST::castToValue (arg))
                                if (auto t = v->getResultType())
                                    makeSilentCastExplicitIfNeeded (arg, elementType, *t, *v);
                        }
                    }
                    else
                    {
                        auto& elementType = *expected.getAggregateElementType(0);

                        for (size_t i = 0; i < numSourceElements; ++i)
                        {
                            auto& arg = *cast.arguments[i].getAsObjectProperty();
                            convertUntypedValueOrListToValue (arg, elementType, true);

                            if (auto v = AST::castToValue (arg))
                                if (auto t = v->getResultType())
                                    makeSilentCastExplicitIfNeeded (arg, elementType, *t, *v);
                        }
                    }
                }
            }
        }
        else if (handleSingleItems)
        {
            if (auto v = AST::castToValue (value))
                if (auto sourceType = v->getResultType())
                    makeSilentCastExplicitIfNeeded (valueProp, expectedType, *sourceType, *v);
        }
    }

    void makeSliceSizeMatchTarget (AST::ObjectProperty& declaredType, const AST::TypeBase& sourceType)
    {
        if (auto destType = AST::castToTypeBase (declaredType))
        {
            if (destType->isSlice())
            {
                if (sourceType.isFixedSizeArray())
                {
                    if (auto elementType1 = destType->getArrayOrVectorElementType())
                    {
                        if (auto elementType2 = sourceType.getArrayOrVectorElementType())
                        {
                            if (elementType1->isSameType (*elementType2, AST::TypeBase::ComparisonFlags::ignoreConst))
                            {
                                declaredType.referTo (sourceType);
                                registerChange();
                                return;
                            }
                        }
                    }
                }

                registerFailure();
            }
        }
    }

    void visit (AST::ReturnStatement& r) override
    {
        super::visit (r);

        if (r.value != nullptr)
        {
            if (auto functionReturnType = AST::castToTypeBase (r.getParentFunction().returnType))
            {
                if (functionReturnType->isResolved())
                    if (functionReturnType->isVoid())
                        throwError (r.value, Errors::voidFunctionCannotReturnValue());

                convertUntypedValueOrListToValue (r.value, *functionReturnType, true);
            }
        }
    }

    ptr<const AST::TypeBase> getTypeMetaFunctionSourceType (AST::TypeMetaFunction& t)
    {
        if (auto sourceType = AST::castToSkippingReferences<const AST::TypeBase> (t.source))
            return sourceType;

        if (auto v = AST::castToValue (t.source))
            return v->getResultType();

        if (t.op == AST::TypeMetaFunctionTypeEnum::Enum::type)
        {
            if (auto endpoint = AST::castToSkippingReferences<AST::EndpointDeclaration> (t.source))
            {
                if (endpoint->dataTypes.size() > 1)
                    throwError (t, Errors::endpointHasMultipleTypes());

                return AST::castToTypeBase (endpoint->dataTypes.front());
            }
        }

        return {};
    }

    void replaceTypeMetaFunction (AST::TypeMetaFunction& old, const AST::TypeBase& newType)
    {
        auto& ref = AST::createReference (old.context, newType);
        return replaceObject (old, ref);
    }

    void replaceTypeMetaFunction (AST::TypeMetaFunction& old, const AST::TypeBase& newType,
                                  bool mustBeConst, bool mustBeNonConst, bool mustBeRef, bool mustBeNonRef)
    {
        ptr<const AST::TypeBase> replacementType = newType;

        if (mustBeConst || mustBeNonConst || mustBeRef || mustBeNonRef)
        {
            if (! newType.isResolved())
            {
                registerFailure();
                return;
            }

            bool isConst = newType.isConst();
            bool isRef = newType.isReference();

            if ((mustBeConst && ! isConst)
                || (mustBeNonConst && isConst)
                || (mustBeRef && ! isRef)
                || (mustBeNonRef && isRef))
            {
                replacementType = replacementType->skipConstAndRefModifiers();

                if (mustBeConst || mustBeRef)
                {
                    auto& mcr = old.context.allocate<AST::MakeConstOrRef>();
                    mcr.makeConst = mustBeConst || (mustBeNonConst ? false : isConst);
                    mcr.makeRef   = mustBeRef   || (mustBeNonRef   ? false : isRef);
                    mcr.source.referTo (*replacementType);
                    replacementType = mcr;
                }
            }
        }

        replaceTypeMetaFunction (old, *replacementType);
    }

    void visit (AST::TypeMetaFunction& t) override
    {
        super::visit (t);

        auto sourceType = getTypeMetaFunctionSourceType (t);

        if (sourceType == nullptr)
        {
            registerFailure();
            return;
        }

        switch (t.op.get())
        {
            case AST::TypeMetaFunctionTypeEnum::Enum::type:
                return replaceTypeMetaFunction (t, *sourceType);

            case AST::TypeMetaFunctionTypeEnum::Enum::makeConst:
                return replaceTypeMetaFunction (t, *sourceType, true, false, false, false);

            case AST::TypeMetaFunctionTypeEnum::Enum::makeReference:
                return replaceTypeMetaFunction (t, *sourceType, false, false, true, false);

            case AST::TypeMetaFunctionTypeEnum::Enum::removeConst:
                return replaceTypeMetaFunction (t, *sourceType, false, true, false, false);

            case AST::TypeMetaFunctionTypeEnum::Enum::removeReference:
                return replaceTypeMetaFunction (t, *sourceType, false, false, false, true);

            case AST::TypeMetaFunctionTypeEnum::Enum::elementType:
                if (auto element = getElementType (sourceType, false))
                    return replaceTypeMetaFunction (t, *element);

                if (throwOnErrors)
                    throwError (t, Errors::badTypeForElementType());

                break;

            case AST::TypeMetaFunctionTypeEnum::Enum::primitiveType:
                if (auto primitive = getPrimitiveType (sourceType))
                    return replaceTypeMetaFunction (t, *primitive);

                if (throwOnErrors)
                    throwError (t, Errors::badTypeForPrimitiveType());

                break;

            case AST::TypeMetaFunctionTypeEnum::Enum::innermostElementType:
                if (auto element = getElementType (sourceType, true))
                    return replaceTypeMetaFunction (t, *element);

                if (throwOnErrors)
                    throwError (t, Errors::badTypeForMetafunction (t.op.getString(), AST::print (*sourceType)));

                break;

            default:
                CMAJ_ASSERT_FALSE;
                break;
        }

        registerFailure();
    }

    void visit (AST::GetStructMember& e) override
    {
        if (auto v = AST::castToValue (e.object))
        {
            if (auto t = v->getResultType())
                if (t->skipConstAndRefModifiers().isStructType())
                    if (validation::checkStructMember (t->skipConstAndRefModifiers(), e.member, nullptr, nullptr))
                        return;

            auto valueMetaFunctionIndex = AST::ValueMetaFunctionTypeEnum::getEnums().getID (e.member.get());

            if (valueMetaFunctionIndex >= 0)
            {
                auto& mf = replaceWithNewObject<AST::ValueMetaFunction> (e);
                mf.op.setID (valueMetaFunctionIndex);
                mf.arguments.addReference (e.object.get());
                return;
            }
        }

        registerFailure();
    }

    static ptr<const AST::TypeBase> getElementType (ptr<const AST::TypeBase> t, bool innermostType)
    {
        if (t == nullptr)
            return {};

        if (auto mcr = t->getAsMakeConstOrRef())
            return getElementType (AST::castToTypeBase (mcr->source), innermostType);

        if (auto a = t->getAsArrayType())
            if (auto inner = a->getInnermostElementType())
                return innermostType ? *inner : a->getArrayOrVectorElementType();

        if (auto v = t->getAsVectorType())
            if (auto inner = v->getInnermostElementType())
                return innermostType ? *inner : v->getArrayOrVectorElementType();

        if (t->isPrimitiveComplex32()) return t->context.allocator.float32Type;
        if (t->isPrimitiveComplex64()) return t->context.allocator.float64Type;

        return {};
    }

    static ptr<const AST::TypeBase> getPrimitiveType (ptr<const AST::TypeBase> t)
    {
        if (t != nullptr)
        {
            if (auto m = t->getAsMakeConstOrRef())     return getPrimitiveType (AST::castToTypeBase (m->source));
            if (auto p = t->getAsPrimitiveType())      return *p;
            if (auto v = t->getAsVectorType())         return AST::castToTypeBase (v->elementType);
        }

        return {};
    }
};


}
