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

#include "cmaj_ValidationUtilities.h"
#include "../utilities/cmaj_GraphConnectivityModel.h"

namespace cmaj::validation
{
    //==============================================================================
    struct PostLoad  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        PostLoad (AST::Allocator& a, AST::ProcessorBase& p) : super (a), mainProcessor(p) {}

        static bool check (AST::Program& program)
        {
            PostLoad v (program.allocator, program.getMainProcessor());
            v.visitObject (program.rootNamespace);

            if (! v.visitedMainProcessor)
                throwError (program.getMainProcessor(), Errors::mainProcessorCannotBeUnparameterised());

            return ! v.needToRunFullTests;
        }

        bool visitedMainProcessor = false;

    private:
        AST::ProcessorBase& mainProcessor;
        bool needToRunFullTests = false;

        void visit (AST::Processor& p) override
        {
            super::visit (p);

            if (std::addressof (p) == std::addressof (mainProcessor))
                visitedMainProcessor = true;
        }

        void visit (AST::Graph& g) override
        {
            super::visit (g);

            if (std::addressof (g) == std::addressof (mainProcessor))
                visitedMainProcessor = true;
        }

        void visit (AST::GetArrayOrVectorSlice& s) override
        {
            super::visit (s);

            if (auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (s.parent))
                throwError (endpointInstance, Errors::unimplementedFeature ("Slices of endpoints"));

            if (auto graphNode = AST::castToSkippingReferences<AST::GraphNode> (s.parent))
                throwError (graphNode, Errors::unimplementedFeature ("Slices of graph nodes"));

            if (auto startValue = AST::castToValue (s.start))
                if (! startValue->isCompileTimeConstant())
                    throwError (startValue, Errors::unimplementedFeature ("Dynamic slice indexes"));

            if (auto endValue = AST::castToValue (s.end))
                if (! endValue->isCompileTimeConstant())
                    throwError (endValue, Errors::unimplementedFeature ("Dynamic slice indexes"));
        }

        void visit (AST::WriteToEndpoint& w) override
        {
            super::visit (w);

            if (auto value = AST::castToValue (w.value))
            {
                if (! value->getResultType())
                    throwError (w.value, Errors::writeValueTypeNotResolved());
            }
            else
            {
                if (auto endpointDeclaration = w.getEndpoint())
                    if (endpointDeclaration->getName() == w.context.allocator.strings.consoleEndpointName)
                        throwError (w.value, Errors::writeValueTypeNotResolved());
            }
        }

        void visit (AST::VariableDeclaration& v) override
        {
            super::visit (v);

            if (v.isExternal)
            {
                if (auto t = v.getType())
                {
                    if (! t->isResolved())
                        throwError (t, Errors::externalTypeNotResolved());

                    if (auto mc = t->getAsMakeConstOrRef())
                        if (mc->makeConst)
                            throwError (mc, Errors::noConstOnExternals());
                }

                if (v.initialValue == nullptr)
                    throwError (v, Errors::unresolvedExternal (v.getName()));
            }
        }

        void visit (AST::EndpointDeclaration& endpoint) override
        {
            super::visit (endpoint);
            checkEndpointTypeList (endpoint, false);

            if (endpoint.isHoistedEndpoint())
            {
                auto& hoistedPath = AST::castToRef<AST::HoistedEndpointPath> (endpoint.childPath);

                if (hoistedPath.pathSections.size() < (hoistedPath.wildcardPattern.hasDefaultValue() ? 2 : 1))
                    throwError (endpoint.childPath, Errors::expectedStreamTypeOrEndpoint());

                if (auto hoistedEndpoint = AST::castTo<AST::EndpointDeclaration> (hoistedPath.pathSections.back()))
                {
                    if (hoistedEndpoint->isInput && ! endpoint.isInput)
                        throwError (endpoint.childPath, Errors::cannotHoistInputEndpoint());

                    if (! hoistedEndpoint->isInput && endpoint.isInput)
                        throwError (endpoint.childPath, Errors::cannotHoistOutputEndpoint());

                    for (auto& pathSection : hoistedPath.pathSections)
                        if (AST::castTo<AST::GetElement> (pathSection))
                            throwError (pathSection, Errors::unimplementedFeature ("Exposing child endpoints which use processor arrays"));
                }
                else
                {
                    needToRunFullTests = true;
                }
            }
        }

        void visit (AST::ArrayType& a) override
        {
            super::visit (a);
            sanityCheckType (a);
        }

        void visit (AST::Annotation& a) override
        {
            super::visit (a);

            if (visitStackContains<AST::EndpointDeclaration>())
                checkAnnotation (a);
        }

        void visit (AST::ConnectionIf& c) override
        {
            super::visit (c);
            throwError (c, Errors::unimplementedFeature ("Non-constant connection condition"));
        }
    };

    //==============================================================================
    struct PostLink  : public AST::Visitor
    {
        using super = AST::Visitor;
        using super::visit;
        bool allowTopLevelSlices = false;
        bool allowExternalFunctions = false;

        static void check (AST::Program& program, uint64_t stackSizeLimit, bool allowSlices, bool allowExternFns)
        {
            PostLink v (program, allowSlices, allowExternFns);
            v.checkForMultipleMainProcessors();
            v.visitObject (program.rootNamespace);
            v.checkFunctionBehaviour (stackSizeLimit);
        }

        static constexpr uint64_t defaultMaxStackSize = 5 * 1024 * 1024;

    private:
        PostLink (AST::Program& p, bool allowSlices, bool allowExternFns)
             : AST::Visitor (p.allocator), allowTopLevelSlices (allowSlices), allowExternalFunctions (allowExternFns), program (p)
        {
        }

        const AST::Program& program;
        bool hasScannedVariablesForLocalSlices = false;

        void checkForMultipleMainProcessors()
        {
            DiagnosticMessageList messages;

            for (auto& processor : program.getAllProcessors())
                if (! (processor->isSystemModule() || processor->isGenericOrParameterised()))
                    if (auto annotation = AST::castTo<AST::Annotation> (processor->annotation))
                        if (annotation->getPropertyAs<bool> ("main"))
                            messages.add (processor, Errors::multipleProcessorsMarkedAsMain());

            if (messages.size() > 1)
                throwError (messages);
        }

        //==============================================================================
        void visit (AST::Identifier& o) override
        {
            throwError (o, Errors::unresolvedSymbol (o.name.get()));
        }

        void visit (AST::NamespaceSeparator& ns) override
        {
            throwError (AST::getContextOfStartOfExpression (ns),
                        Errors::unresolvedSymbol (AST::print (ns)));
        }

        void visit (AST::Advance& a) override
        {
            super::visit (a);

            if (a.node != nullptr)
            {
                if (AST::castToSkippingReferences<AST::GraphNode> (a.node))
                    return;

                if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (a.node))
                {
                    if (auto graphNode = AST::castToSkippingReferences<AST::GraphNode> (getElement->parent))
                    {
                        return;
                    }
                }

                throwError (a, Errors::invalidAdvanceArgumentType());
            }
        }


        void visit (AST::MakeConstOrRef& m) override
        {
            super::visit (m);
            (void) getAsTypeOrThrowError (m.source);
        }

        void visit (AST::ArrayType& a) override
        {
            super::visit (a);

            (void) getAsTypeOrThrowError (a.elementType);
            sanityCheckType (a);

            for (auto& size : a.dimensionList)
            {
                auto folded = getAsFoldedConstant (size->getObjectRef());

                if (folded == nullptr)
                    throwError (size, Errors::arraySizeMustBeConstant());

                AST::TypeRules::checkAndGetArraySize (size, *folded);
            }
        }

        void visit (AST::BoundedType& b) override
        {
            super::visit (b);

            auto limit = getAsFoldedConstant (b.limit);

            if (limit == nullptr)
                throwError (b.limit, Errors::wrapOrClampSizeMustBeConstant());

            AST::TypeRules::checkAndGetArraySize (b.limit, *limit);
        }

        void visit (AST::VectorType& v) override
        {
            super::visit (v);

            (void) getAsTypeOrThrowError (v.elementType);
            sanityCheckType (v);

            auto& numElements = getAsConstantOrThrowError (v.numElements);
            AST::TypeRules::checkAndGetVectorSize (v.numElements, numElements);
        }

        void visit (AST::Assignment& a) override
        {
            super::visit (a);

            auto& target = getAssignableValueOrThrowError (a.target, "=", true);
            auto& targetType = getResultTypeOfValueOrThrowError (target).skipConstAndRefModifiers();
            auto& source = getAsValueOrThrowError (a.source);

            expectSilentCastPossible (a.context, targetType, source);

            if (targetType.containsSlice())
            {
                ensureVariablesScannedForLocalSlices();
                auto& targetVariable = *target.getSourceVariable();
                OutOfScopeSourcesForValue outOfScope (source, targetType, std::addressof (targetVariable));

                if (! outOfScope.sourcesFound.empty())
                    throwLocalDataError (outOfScope, AST::getContext (a.source), Errors::cannotAssignSliceToWiderScope());
            }
        }

        void visit (AST::BinaryOperator& o) override
        {
            super::visit (o);

            auto& lhs = getAsValueOrThrowError (o.lhs);
            auto& rhs = getAsValueOrThrowError (o.rhs);

            checkBinaryOperands (AST::getContext (o), lhs, rhs, o.op, o.getSymbol(), false);
        }

        void visit (AST::InPlaceOperator& o) override
        {
            super::visit (o);

            auto symbol = std::string (o.getSymbol()) + "=";
            auto& target = getAssignableValueOrThrowError (o.target, symbol, true);
            auto& source = getAsValueOrThrowError (o.source);

            checkBinaryOperands (AST::getContext (o), target, source, o.op, symbol, true);
        }

        void visit (AST::CallOrCast& cc) override
        {
            auto args = cc.arguments.getAsObjectList();

            for (auto& arg : args)
                visitObject (arg);

            if (AST::castToSkippingReferences<AST::ProcessorBase> (cc.functionOrType) != nullptr)
                throwError (cc.functionOrType, Errors::cannotUseProcessorAsFunction());

            for (auto& arg : args)
                if (AST::castToTypeBase (arg) != nullptr)
                    throwError (arg, Errors::typeReferenceNotAllowed());

            // avoid visiting the RHS of a dot, as it'll give us a less useful message
            if (auto dot = AST::castTo<AST::DotOperator> (cc.functionOrType))
                visitProperty (dot->lhs);
            else
                visitProperty (cc.functionOrType);

            throwError (cc, Errors::cannotResolveFunctionOrCast());
        }

        void visit (AST::BracketedSuffix& b) override
        {
            super::visit (b);

            if (AST::castToSkippingReferences<AST::EndpointInstance> (b.parent) != nullptr
                 || AST::castToSkippingReferences<AST::EndpointDeclaration> (b.parent) != nullptr)
                throwError (b, Errors::unimplementedFeature ("Endpoint value sub-elements"));

            auto& parentType = getResultTypeOfValueOrThrowError (b.parent);

            if (! parentType.isVectorOrArray())
                throwError (b.parent, Errors::expectedArrayOrVectorForBracketOp());

            throwError (b, Errors::cannotResolveBracketedExpression());
        }

        void visit (AST::ChevronedSuffix& o) override
        {
            super::visit (o);

            failIfModule (o.parent);
            throwError (o, Errors::cannotResolveVectorSize());
        }

        void visit (AST::ExpressionList& o) override
        {
            super::visit (o);
            throwError (o, Errors::cannotResolveBracketedExpression());
        }

        void visit (AST::BreakStatement& b) override
        {
            if (! b.isChildOf (b.targetBlock.getObjectRef()))
                throwError (b, Errors::breakMustBeInsideALoop());
        }

        void visit (AST::ContinueStatement& c) override
        {
            if (! c.isChildOf (c.targetBlock.getObjectRef()))
                throwError (c, Errors::continueMustBeInsideALoop());
        }

        void visit (AST::LoopStatement& loop) override
        {
            super::visit (loop);

            if (! loop.initialisers.empty())
            {
                for (auto& i : loop.initialisers)
                    if (AST::castToVariableDeclaration (i) == nullptr && AST::castToAssignment (i) == nullptr)
                        throwError (i, Errors::expectedVariableDecl());
            }

            if (! loop.condition.hasDefaultValue())
                expectBoolValue (loop.condition);

            if (auto numIterations = loop.numIterations.getObject())
            {
                if (auto var = numIterations->getAsVariableDeclaration())
                {
                    if (! var->getType()->isBoundedType())
                        throwError (numIterations, Errors::rangeBasedForMustBeWrapType());
                }
                else
                {
                    auto& countValue = getAsValueOrThrowError (*numIterations);

                    expectSilentCastPossible (numIterations->context, numIterations->context.allocator.int64Type, countValue);

                    if (auto constCount = countValue.constantFold())
                        if (auto asInt = constCount->getAsInt64())
                            if (*asInt < 0)
                                throwError (loop.numIterations, Errors::loopCountMustBePositive());
                }
            }
        }

        void visit (AST::DotOperator& d) override
        {
            if (AST::castToSkippingReferences<AST::EndpointDeclaration> (d.lhs) != nullptr)
                throwError (d, Errors::noSuchOperationOnEndpoint());

            if (AST::castToSkippingReferences<AST::ProcessorBase> (d.lhs) != nullptr)
                throwError (d, Errors::noSuchOperationOnProcessor());

            if (auto object = AST::castToValue (d.lhs))
            {
                auto& objectType = getResultTypeOfValueOrThrowError (*object).skipConstAndRefModifiers();
                AST::PooledString memberName;

                if (auto name = AST::castTo<AST::Identifier> (d.rhs))
                    memberName = name->name;

                checkStructMember (objectType, memberName, std::addressof (AST::getContext (d.lhs)), std::addressof (AST::getContext (d.rhs)));
            }

            super::visit (d);

            getAsValueOrThrowError (d.lhs);
            throwError (d, Errors::invalidDotArguments());
        }

        void visit (AST::FunctionCall& fc) override
        {
            for (auto& arg : fc.arguments)
                if (AST::castToTypeBase (arg) != nullptr)
                    throwError (arg, Errors::typeReferenceNotAllowed());

            if (auto i = AST::castTo<AST::Identifier> (fc.targetFunction))
            {
                visitProperty (fc.arguments); // an error in an argument would be more useful than just saying the function isn't found
                throwError (i, Errors::unknownFunction (i->name.get()));
            }

            auto targetFunction = fc.getTargetFunction();

            if (targetFunction == nullptr)
            {
                visitProperty (fc.arguments); // an error in an argument would be more useful than just saying the function isn't found
                throwError (fc.targetFunction, Errors::cannotResolveFunction());
            }

            if (auto processorScope = targetFunction->findSelfOrParentOfType<AST::ProcessorBase>())
                if (fc.findSelfOrParentOfType<AST::ModuleBase>() != AST::castTo<AST::ModuleBase> (processorScope))
                    throwError (fc, Errors::functionOutOfScope());

            super::visit (fc);

            if (targetFunction->isGenericOrParameterised())
            {
                auto callDescription = AST::getFunctionCallDescription (targetFunction->getName(), fc);
                throwError (fc, Errors::cannotResolveGenericFunction (callDescription));
            }

            if (fc.arguments.size() > 0)
            {
                auto paramTypes = getParameterTypesOrThrowError (*targetFunction);

                for (size_t i = 0; i < fc.arguments.size(); ++i)
                {
                    auto& arg = fc.arguments[i];
                    auto& paramType = paramTypes[i].get();

                    if (paramType.isNonConstReference())
                        if (! hasAssignableAddress (arg))
                            throwError (arg, Errors::cannotPassConstAsNonConstRef());

                    expectCastPossible (AST::getContext (arg), paramType, getAsValueOrThrowError (arg), false);
                }
            }
        }

        void visit (AST::Cast& c) override
        {
            super::visit (c);

            expectCastPossible (AST::getContext (c),
                                getAsTypeOrThrowError (c.targetType),
                                c.arguments.getAsObjectList(),
                                c.onlySilentCastsAllowed);
        }

        void visit (AST::Function& f) override
        {
            if (f.isGenericOrParameterised())
                return;

            super::visit (f);

            if (f.isEventHandler)
            {
                auto processor = f.findParentOfType<AST::ProcessorBase>();

                if (processor == nullptr)
                    throwError (f, Errors::noEventFunctionsAllowed());

                if (processor->isGraph())
                    if (connectionExistsFromEndpoint (*processor->getAsGraph(), f.getName()))
                        throwError (f, Errors::cannotMixEventFunctionsAndConnections());

                AST::NameSearch ns;
                ns.nameToFind              = f.getName();
                ns.findVariables           = false;
                ns.findTypes               = false;
                ns.findFunctions           = false;
                ns.findNamespaces          = false;
                ns.findProcessors          = false;
                ns.findNodes               = false;
                ns.findEndpoints           = true;
                ns.onlyFindLocalVariables  = false;
                processor->performLocalNameSearch (ns, nullptr);

                if (ns.itemsFound.size() != 1)
                    throwError (f, Errors::noSuchInputEvent (f.getName()));

                auto endpoint = ns.itemsFound.front()->getAsEndpointDeclaration();

                if (endpoint == nullptr || (! endpoint->isInput) || (! endpoint->isEvent()))
                    throwError (f, Errors::noSuchInputEvent (f.getName()));

                auto paramTypes = getParameterTypesOrThrowError (f);

                std::optional<size_t> eventTypeParamIndex;

                if (endpoint->arraySize != nullptr)
                {
                    if (paramTypes.size() < 1 || ! paramTypes[0]->isPrimitiveInt32())
                        throwError (f, Errors::eventFunctionIndexInvalid());

                    if (paramTypes.size() > 2)
                        throwError (f, Errors::eventFunctionInvalidArguments());

                    if (paramTypes.size() == 2)
                        eventTypeParamIndex = 1;
                }
                else
                {
                    if (paramTypes.size() > 1)
                        throwError (f, Errors::eventFunctionInvalidArguments());

                    if (paramTypes.size() == 1)
                        eventTypeParamIndex = 0;
                }

                auto& eventType = eventTypeParamIndex ? paramTypes[eventTypeParamIndex.value()]->skipConstAndRefModifiers()
                                                      : f.context.allocator.createVoidType();

                if (! endpointTypesInclude (*endpoint, eventType))
                    throwError (f, Errors::eventFunctionInvalidType (f.name.get(), AST::print (eventType)));
            }

            if (f.isMainFunction() || f.isUserInitFunction())
            {
                if (! f.getParentModule().isProcessor())
                    throwError (f, Errors::graphCannotContainMainOrInitFunctions());

                if (! getAsTypeOrThrowError (f.returnType).isVoid())
                    throwError (f, Errors::functionMustBeVoid (f.getName()));

                if (! f.parameters.empty())
                    throwError (f, Errors::functionHasParams (f.getName()));
            }

            DuplicateNameChecker nameChecker;

            for (auto& param : f.iterateParameters())
                nameChecker.checkAndAdd (param.name.get(), param.context);

            if (auto mainBlock = f.getMainBlock())
            {
                checkStatementsInBlock (nameChecker, *mainBlock);
            }
            else if (f.isExternal)
            {
                checkExternalFunction (f);
            }
            else if (! f.isIntrinsic())
            {
                throwError (f, Errors::functionHasNoImplementation());
            }

            auto& returnType = getAsTypeOrThrowError (f.returnType);

            if (returnType.isConst() && ! returnType.isArray())
                throwError (f.returnType, Errors::functionReturnTypeCannotBeConst());

            if (returnType.isReference())
                throwError (f.returnType, Errors::cannotReturnReferenceType());

            if (! returnType.isVoid())
                checkFunctionReturns (f);
        }

        void checkExternalFunction (const AST::Function& f)
        {
            auto isTypeSuitableForExternalFunctionParam = [] (const AST::TypeBase& type) -> bool
            {
                return (type.isPrimitive() || type.isSlice())
                    && ! (type.isConst() || type.isReference() || type.isPrimitiveString()
                        || type.isPrimitiveComplex() || type.isVoid() || type.isEnum());
            };

            auto isTypeSuitableForExternalFunctionReturn = [] (const AST::TypeBase& type) -> bool
            {
                return (type.isVoid() || type.isPrimitive())
                    && ! (type.isConst() || type.isReference() || type.isPrimitiveString()
                        || type.isPrimitiveComplex() || type.isEnum());
            };

            if (! isTypeSuitableForExternalFunctionReturn (getAsTypeOrThrowError (f.returnType)))
                throwError (f.returnType, Errors::externalFunctionCannotUseParamType());

            auto paramTypes = f.getParameterTypes();

            for (size_t i = 0; i < paramTypes.size(); ++i)
                if (! isTypeSuitableForExternalFunctionParam (paramTypes[i]))
                    throwError (f.parameters[i], Errors::externalFunctionCannotUseParamType());

            if (program.externalFunctionManager.findResolvedFunction (f) == nullptr)
                throwError (f, Errors::unresolvedExternalFunction (f.getName()));

            if (! allowExternalFunctions)
                throwError (f, Errors::externalFunctionsNotSupported());
        }

        //==============================================================================
        static void checkFunctionReturns (const AST::Function& f)
        {
            if (auto mainBlock = f.getMainBlock())
                if (StatementExitMethods (*mainBlock).notAllControlPathsReturn())
                    throwError (f, Errors::notAllControlPathsReturnAValue (f.getName()));
        }

        static bool endpointTypesInclude (const AST::EndpointDeclaration& endpoint, const AST::TypeBase& type)
        {
            for (auto& endpointType : endpoint.dataTypes)
            {
                auto& t = getAsTypeOrThrowError (endpointType);
                checkTypeIsResolved (t);

                if (t.isSameType (type, AST::TypeBase::ComparisonFlags::ignoreConst
                                         | AST::TypeBase::ComparisonFlags::ignoreReferences
                                         | AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                    return true;
            }

            return false;
        }

        static void checkGetElementParentType (const AST::Object& object, const AST::TypeBase& objectType, bool isAtFunction)
        {
            if (! objectType.isVectorOrArray())
            {
                if (isAtFunction)
                    throwError (object, Errors::wrongTypeForAtMethod());

                if (AST::castToSkippingReferences<AST::EndpointDeclaration> (object) != nullptr)
                    throwError (object, Errors::cannotUseBracketOnEndpoint());

                throwError (object, Errors::expectedArrayOrVectorForBracketOp());
            }
        }

        static void checkArrayIndex (const AST::ObjectContext& errorContext, const AST::ValueBase& index,
                                     const AST::TypeBase& indexType, const AST::TypeBase& objectType,
                                     uint32_t dimensionIndex, bool canEqualSize)
        {
            if (! (indexType.isPrimitiveInt() || indexType.isBoundedType()))
                throwError (errorContext, Errors::nonIntegerArrayIndex());

            if (auto constIndex = index.constantFold())
                AST::TypeRules::checkAndGetArrayIndex (errorContext, *constIndex, objectType, dimensionIndex, canEqualSize);
        }

        void checkGraphOrEndpointIndex (AST::GetElement& e, AST::ArraySize arraySize)
        {
            if (e.indexes.size() > 1)
                throwError (e.indexes[1], Errors::wrongNumberOfArrayIndexes ("1", std::to_string (e.indexes.size())));

            auto& index      = getAsValueOrThrowError (e.getSingleIndex());
            auto& indexType  = getResultTypeOfValueOrThrowError (index).skipConstAndRefModifiers();

            if (! (indexType.isPrimitiveInt() || indexType.isBoundedType()))
                throwError (e, Errors::nonIntegerArrayIndex());

            if (auto constIndex = index.constantFold())
                AST::TypeRules::checkConstantArrayIndex (e.context, *constIndex->getAsInt64(), arraySize, false);
            else if (! indexType.isBoundedType() || indexType.getAsBoundedType()->getBoundedIntLimit() > arraySize)
                if (! e.isAtFunction)
                    emitMessage (Errors::indexHasRuntimeOverhead().withLocation (AST::getContext (e.getSingleIndexProperty()).getFullLocation()));
        }

        void visit (AST::GetElement& e) override
        {
            super::visit (e);

            if (auto graphNode = AST::castToSkippingReferences<AST::GraphNode> (e.parent))
            {
                if (! graphNode->getArraySize())
                    throwError (e, Errors::notANodeArray());

                checkGraphOrEndpointIndex (e, static_cast<AST::ArraySize> (*graphNode->getArraySize()));
            }
            else if (auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (e.parent))
            {
                auto& endpointDeclaration = endpointInstance->getResolvedEndpoint();

                if (! endpointDeclaration.isArray())
                    throwError (e, Errors::notAnEndpointArray());

                checkGraphOrEndpointIndex (e, static_cast<AST::ArraySize> (*endpointDeclaration.getArraySize()));
            }
            else
            {
                auto& parent     = getAsValueOrThrowError (e.parent);
                auto& parentType = getResultTypeOfValueOrThrowError (parent).skipConstAndRefModifiers();

                checkGetElementParentType (e.parent, parentType, e.isAtFunction);

                auto numIndexes = e.indexes.size();
                auto numDimensionsNeeded = parentType.getNumDimensions();

                if (numIndexes > numDimensionsNeeded || numIndexes == 0)
                {
                    auto& context = e.indexes.size() > numDimensionsNeeded ? AST::getContext (e.indexes[numDimensionsNeeded])
                                                                           : AST::getContext (e);
                    auto numFound = std::to_string (e.indexes.size());
                    auto numNeeded = std::to_string (numDimensionsNeeded);

                    if (e.isAtFunction)
                        throwError (context, Errors::wrongNumArgsForAtMethod (numNeeded));
                    else
                        throwError (context, Errors::wrongNumberOfArrayIndexes (numNeeded, numFound));
                }

                for (uint32_t i = 0; i < numIndexes; ++i)
                {
                    auto& index = getAsValueOrThrowError (e.indexes[i]);
                    auto& indexType  = getResultTypeOfValueOrThrowError (index).skipConstAndRefModifiers();

                    checkArrayIndex (AST::getContext (e.indexes[i]), index, indexType, parentType, i, false);

                    if (! e.isAtFunction && getConstantWrappingSizeToApplyToIndex (e, i).has_value())
                        if (AST::castToConstant (index) == nullptr)
                            emitMessage (Errors::indexHasRuntimeOverhead().withLocation (AST::getContext (e.indexes[i]).getFullLocation()));
                }
            }
        }

        void visit (AST::GetArrayOrVectorSlice& s) override
        {
            super::visit (s);

            auto& object     = getAsValueOrThrowError (s.parent);
            auto& objectType = getResultTypeOfValueOrThrowError (object).skipConstAndRefModifiers();
            checkGetElementParentType (s.parent, objectType, false);

            std::optional<int64_t> start, end;

            if (s.start.getPointer() != nullptr)
            {
                auto& startValue = getAsValueOrThrowError (s.start);
                auto& startType = getResultTypeOfValueOrThrowError (startValue);
                checkArrayIndex (AST::getContext (s.start), startValue, startType, objectType, 0, false);
                auto startConst = startValue.constantFold();

                if (startConst == nullptr)
                    throwError (s.start, Errors::unimplementedFeature ("Dynamic slice indexes"));

                start = startConst->getAsInt64();
            }

            if (s.end.getPointer() != nullptr)
            {
                auto& endValue = getAsValueOrThrowError (s.end);
                auto& endType = getResultTypeOfValueOrThrowError (endValue);
                checkArrayIndex (AST::getContext (s.end), endValue, endType, objectType, 0, true);
                auto endConst = endValue.constantFold();

                if (endConst == nullptr)
                    throwError (s.end, Errors::unimplementedFeature ("Dynamic slice indexes"));

                end = endConst->getAsInt64();
            }

            auto& elementType = *objectType.getArrayOrVectorElementType();

            if (! (elementType.isPrimitive() || elementType.isVector()))
                throwError (s, Errors::unimplementedFeature ("Slices of non-primitive arrays"));

            if (end && *end == 0)
                throwError (s.end, Errors::illegalSliceSize());

            auto arraySize = objectType.getFixedSizeAggregateNumElements();

            if (start && end)
                if (AST::TypeRules::convertArrayOrVectorIndexToValidRange (arraySize, *start)
                      >= AST::TypeRules::convertArrayOrVectorIndexToValidRange (arraySize, *end))
                    throwError (s, Errors::illegalSliceSize());
        }

        void visit (AST::GetStructMember& m) override
        {
            super::visit (m);
            auto& object = getAsValueOrThrowError (m.object);
            auto& objectType = getResultTypeOfValueOrThrowError (object).skipConstAndRefModifiers();

            checkStructMember (objectType, m.member.get(), std::addressof (AST::getContext (m.object)), std::addressof (AST::getContext (m)));
        }

        void visit (AST::IfStatement& i) override
        {
            if (i.isConst)
                getAsConstantOrThrowError (i.condition);

            super::visit (i);

            expectBoolValue (i.condition);
        }

        void visit (AST::PreOrPostIncOrDec& p) override
        {
            super::visit (p);

            auto& target = getAssignableValueOrThrowError (p.target, p.getOperatorSymbol(), false);
            auto& targetType = getResultTypeOfValueOrThrowError (target);

            if (! targetType.isScalar())
                throwError (p, Errors::illegalTypeForOperator (p.getOperatorSymbol()));

            if (countUsesOfValueWithinStatement (findTopOfCurrentStatement(), target) > 1)
                throwError (p.target, Errors::preIncDecCollision());
        }

        void visit (AST::Namespace& n) override
        {
            if (n.isGenericOrParameterised())
                return;

            super::visit (n);

            for (auto& c : n.constants)
                if (! AST::castToVariableDeclarationRef (c).isCompileTimeConstant())
                    throwError (c, Errors::nonConstInNamespace());
        }

        void visit (AST::Graph& g) override
        {
            if (g.isGenericOrParameterised())
                return;

            super::visit (g);

            checkEndpoints (g);

            for (auto& c : g.stateVariables)
            {
                auto& v = AST::castToVariableDeclarationRef (c);

                if (! (v.isCompileTimeConstant() || v.isExternal || v.isInternalVariable()))
                    throwError (c, Errors::nonConstInGraph());
            }

            checkNodesForRecursion (g);
            GraphConnectivityModel (g).checkAndThrowErrorIfCycleFound();
        }

        void visit (AST::Processor& p) override
        {
            if (p.isGenericOrParameterised())
                return;

            super::visit (p);

            checkEndpoints (p);
            checkNodesForRecursion (p);
            checkLatency (p.latency);

            bool mainFunctionFound = false;

            for (auto& f : p.functions.iterateAs<AST::Function>())
            {
                if (f.isMainFunction())
                {
                    if (mainFunctionFound)
                        throwError (f, Errors::multipleMainFunctions());

                    mainFunctionFound = true;
                }
            }

            if (! mainFunctionFound && p.getNumEventEndpoints (false) == 0)
            {
                auto runName = p.getStrings().run;

                for (auto& f : p.functions.iterateAs<AST::Function>())
                    if (f.hasName (runName) && getAsTypeOrThrowError (f.returnType).isVoid() && f.getNumParameters() == 0)
                        throwError (f, Errors::isRunFunctionSupposedToBeMain());

                throwError (p, Errors::processorNeedsMainFunction());
            }
        }

        void checkEndpoints (AST::ProcessorBase& p)
        {
            bool isMainProcessor = std::addressof (program.getMainProcessor()) == std::addressof (p);
            auto outputs = p.getOutputEndpoints (false);
            auto inputs = p.getInputEndpoints (false);

            if (outputs.size() == 0)
                throwError (p, Errors::processorNeedsAnOutput());

            if (isMainProcessor)
            {
                for (auto& i : inputs)
                {
                    checkEndpointTypeList (i, isMainProcessor);

                    auto& input = i.get();

                    if (input.arraySize != nullptr)
                    {
                        auto num = getAsConstantOrThrowError (input.arraySize).getAsInt64();

                        if (num.has_value())
                            throwError (input.arraySize, Errors::unimplementedFeature ("top-level arrays of inputs"));
                    }

                    if (! allowTopLevelSlices)
                        for (auto type : input.getDataTypes())
                            if (type->containsSlice())
                                throwError (type, Errors::cannotUseSlicesInTopLevel());
                }

                for (auto& o : outputs)
                {
                    checkEndpointTypeList (o, isMainProcessor);

                    auto& output = o.get();

                    if (output.arraySize != nullptr)
                    {
                        auto num = getAsConstantOrThrowError (output.arraySize).getAsInt64();

                        if (num.has_value())
                            throwError (output.arraySize, Errors::unimplementedFeature ("top-level arrays of outputs"));
                    }

                    if (! allowTopLevelSlices)
                        for (auto type : output.getDataTypes())
                            if (type->containsSlice())
                                throwError (type, Errors::cannotUseSlicesInTopLevel());
                }
            }
        }

        void checkLatency (const AST::Property& latency)
        {
            if (auto value = latency.getObject())
            {
                if (auto c = getAsFoldedConstant (*value))
                {
                    if (auto numFrames = c->getAsFloat64())
                    {
                        if (*numFrames < 0 || *numFrames > AST::maxInternalLatency)
                            throwError (latency, Errors::latencyOutOfRange());

                        return;
                    }
                }

                throwError (latency, Errors::latencyMustBeConstantIntOrFloat());
            }
        }

        void checkDelayLength (const AST::Property& length)
        {
            if (auto lengthObj = length.getObject())
            {
                if (auto delayConst = getAsFoldedConstant (*lengthObj))
                {
                    if (delayConst->getResultType()->isPrimitiveInt())
                    {
                        if (auto delayValue = delayConst->getAsInt64())
                        {
                            if (*delayValue < 1)
                                throwError (length, Errors::delayLineTooShort());

                            if (*delayValue > (int64_t) AST::maxDelayLineLength)
                                throwError (length, Errors::delayLineTooLong (std::to_string (AST::maxDelayLineLength)));

                            return;
                        }
                    }

                    throwError (length, Errors::delayLineMustHaveIntLength());
                }

                throwError (length, Errors::delayLineMustBeConstant());
            }
        }

        static int64_t getEndpointAndProcessorArrayCount (const AST::EndpointInstance& endpointInstance,
                                                          const AST::EndpointDeclaration& endpoint)
        {
            auto endpointArraySize = getOptionalArraySizeOrThrowError (endpoint.arraySize, AST::maxEndpointArraySize);

            if (auto node = AST::castToSkippingReferences<AST::GraphNode> (endpointInstance.node))
                return endpointArraySize * getOptionalArraySizeOrThrowError (node->arraySize, AST::maxProcessorArraySize);

            return endpointArraySize;
        }

        void checkConnection (const AST::Connection& connection,
                              const AST::EndpointInstance& source,
                              ptr<AST::Expression> sourceIndex,
                              const AST::EndpointInstance& dest,
                              ptr<AST::Expression> destIndex)
        {
            if (source.isChained())
                if (source.getProcessor().getOutputEndpoints (false).size() != 1)
                    throwError (source, Errors::mustBeOnlyOneEndpoint());

            if (dest.isChained())
                if (dest.getProcessor().getInputEndpoints (false).size() != 1)
                    throwError (dest, Errors::mustBeOnlyOneEndpoint());

            auto& sourceEndpoint = *source.getEndpoint (true);
            auto& destEndpoint = *dest.getEndpoint (false);

            if (sourceEndpoint.endpointType.get() != destEndpoint.endpointType.get())
                throwError (connection, Errors::cannotConnect (AST::print (source, AST::PrintOptionFlags::useShortNames), sourceEndpoint.endpointType.getEnumString(),
                                                               AST::print (dest, AST::PrintOptionFlags::useShortNames), destEndpoint.endpointType.getEnumString()));

            if (! source.isSource())
            {
                if (source.isParentEndpoint())
                    throwError (connection, Errors::cannotUseOutputAsConnectionSource (AST::print (source, AST::PrintOptionFlags::useShortNames)));
                else
                    throwError (connection, Errors::cannotConnectFromAnInput (AST::print (source, AST::PrintOptionFlags::useShortNames),
                                                                              AST::print (dest, AST::PrintOptionFlags::useShortNames)));
            }

            if (! dest.isDestination())
            {
                if (dest.isParentEndpoint())
                    throwError (connection, Errors::cannotUseInputAsConnectionDestination (AST::print (dest, AST::PrintOptionFlags::useShortNames)));
                else
                    throwError (connection, Errors::cannotConnectToAnOutput (AST::print (source, AST::PrintOptionFlags::useShortNames),
                                                                             AST::print (dest, AST::PrintOptionFlags::useShortNames)));
            }

            auto sourceArrayCount = getEndpointAndProcessorArrayCount (source, sourceEndpoint);
            auto destArrayCount   = getEndpointAndProcessorArrayCount (dest, destEndpoint);

            if (sourceIndex == nullptr && destIndex == nullptr)
                if (sourceArrayCount != destArrayCount)
                    if (sourceArrayCount != 1 && destArrayCount != 1)
                        throwError (connection, Errors::cannotConnectEndpointArrays());

            auto sourceTypes = sourceEndpoint.getDataTypes();
            auto destTypes = destEndpoint.getDataTypes();

            size_t matchingTypes = 0;

            for (auto& sourceType : sourceTypes)
                for (auto& destType : destTypes)
                    if (AST::TypeRules::areTypesIdentical (destType.get(), sourceType.get()))
                        ++matchingTypes;

            if (matchingTypes == 0)
            {
                auto getTypeListDesc = [] (auto& list) -> std::string
                {
                    std::string result;

                    for (auto& type : list)
                    {
                        if (! result.empty())
                            result += ", ";

                        result += AST::print (type.get(), AST::PrintOptionFlags::useShortNames);
                    }

                    return result;
                };

                throwError (connection, Errors::cannotConnect (AST::print (source, AST::PrintOptionFlags::useShortNames), getTypeListDesc (sourceTypes),
                                                               AST::print (dest, AST::PrintOptionFlags::useShortNames), getTypeListDesc (destTypes)));
            }
        }

        bool endpointTypesCompatible (const AST::EndpointTypeEnum::Enum& source, const AST::EndpointTypeEnum::Enum& dest)
        {
            if (source == dest)
                return true;

            // Special case, connecting a value to a stream is ok
            if (source == AST::EndpointTypeEnum::Enum::value &&
                dest == AST::EndpointTypeEnum::Enum::stream)
                return true;

            return false;
        }

        void checkConnection (const AST::Connection& connection,
                              AST::ValueBase& source,
                              const AST::EndpointInstance& dest)
        {
            if (dest.isChained())
                if (dest.getProcessor().getInputEndpoints (false).size() != 1)
                    throwError (dest, Errors::mustBeOnlyOneEndpoint());

            auto& destEndpoint = *dest.getEndpoint (false);

            if (! dest.isDestination())
            {
                if (dest.isParentEndpoint())
                    throwError (connection, Errors::cannotUseInputAsConnectionDestination (AST::print (dest, AST::PrintOptionFlags::useShortNames)));
                else
                    throwError (connection, Errors::cannotConnectToAnOutput (AST::print (source, AST::PrintOptionFlags::useShortNames),
                                                                             AST::print (dest, AST::PrintOptionFlags::useShortNames)));
            }

            auto usedEndpointInstances = GraphConnectivityModel::getUsedEndpointInstances (source);

            if (usedEndpointInstances.empty() && destEndpoint.isEvent())
                    throwError (connection, Errors::cannotConnectExpressionToEventHandler());

            for (auto sourceInstance : usedEndpointInstances)
            {
                auto sourceEndpoint = sourceInstance->getEndpoint (true);

                if (! endpointTypesCompatible (sourceEndpoint->endpointType.get(), destEndpoint.endpointType.get()))
                    throwError (connection, Errors::cannotConnect (AST::print (source, AST::PrintOptionFlags::useShortNames), sourceEndpoint->endpointType.getEnumString(),
                                                                   AST::print (dest, AST::PrintOptionFlags::useShortNames), destEndpoint.endpointType.getEnumString()));
            }

            auto& sourceType = *source.getResultType();
            auto destTypes = destEndpoint.getDataTypes();

            size_t matchingTypes = 0;

            for (auto& destType : destTypes)
            {
                if (AST::TypeRules::areTypesIdentical (destType.get(), sourceType))
                    ++matchingTypes;
                else if (sourceType.isArray() && AST::TypeRules::areTypesIdentical (destType.get(), *sourceType.getArrayOrVectorElementType()))
                    ++matchingTypes;
            }

            if (matchingTypes == 0)
            {
                auto getTypeListDesc = [] (auto& list) -> std::string
                {
                    std::string result;

                    for (auto& type : list)
                    {
                        if (! result.empty())
                            result += ", ";

                        result += AST::print (type.get(), AST::PrintOptionFlags::useShortNames);
                    }

                    return result;
                };

                throwError (connection, Errors::cannotConnect (AST::print (source, AST::PrintOptionFlags::useShortNames), AST::print (sourceType, AST::PrintOptionFlags::useShortNames),
                                                               AST::print (dest, AST::PrintOptionFlags::useShortNames), getTypeListDesc (destTypes)));
            }
        }

        void checkConnectionEndpointArrayIndex (const AST::EndpointInstance& endpointInstance, const AST::ObjectProperty& index)
        {
            auto& endpoint = *endpointInstance.getEndpoint (true);
            auto indexValue = AST::TypeRules::checkAndGetArrayIndex (index, getAsConstantOrThrowError (index));

            if (endpoint.isArray())
            {
                auto size = endpoint.getArraySize();
                CMAJ_ASSERT (size.has_value());
                auto arraySize = AST::TypeRules::castToArraySize (*size);
                AST::TypeRules::checkConstantArrayIndex (index, indexValue, arraySize, false);
                return;
            }

            if (endpoint.dataTypes.size() == 1 && endpoint.getSingleDataType().isVectorType())
            {
                AST::TypeRules::checkConstantArrayIndex (index, indexValue, endpoint.getSingleDataType().getVectorSize(), false);
                return;
            }

            throwError (index, Errors::notAnEndpointArray());
        }

        void checkNodeArrayIndex (const AST::GraphNode& node, const AST::ObjectProperty& index)
        {
            if (! node.isArray())
                throwError (index, Errors::notANodeArray());

            auto size = node.getArraySize();
            CMAJ_ASSERT (size.has_value());
            auto arraySize = AST::TypeRules::castToArraySize (*size);

            if (auto constant = index->getAsConstantValueBase())
            {
                auto indexValue = AST::TypeRules::checkAndGetArrayIndex (index, *constant);
                AST::TypeRules::checkConstantArrayIndex (index, indexValue, arraySize, false);
            }
            else
            {
                auto indexType = index->getAsValueBase()->getResultType();

                if (! indexType->isBoundedType() || indexType->getAsBoundedType()->getBoundedIntLimit() > arraySize)
                    emitMessage (Errors::indexHasRuntimeOverhead().withLocation (index->context.getFullLocation()));
            }
        }

        bool connectionUsesNamedSource (const AST::Connection& connection, std::string_view name)
        {
            for (auto& source: connection.sources)
            {
                if (auto sourceConnection = AST::castTo<AST::Connection> (source))
                {
                    if (connectionUsesNamedSource (*sourceConnection, name))
                        return true;
                }
                else if (auto endpointInstance = AST::castTo<AST::EndpointInstance> (source))
                {
                    if (endpointInstance->getEndpoint (true)->getName() == name)
                        return true;
                }
            }

            return false;
        }

        bool connectionExistsFromEndpoint (const AST::Graph& graph, std::string_view name)
        {
            for (auto& connection: graph.connections.iterateAs<AST::Connection>())
                if (connectionUsesNamedSource (connection, name))
                    return true;

            return false;
        }

        ptr<AST::Expression> getOptionalGetElementIndex (AST::Object& instance)
        {
            if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (instance))
            {
                CMAJ_ASSERT (getElement->indexes.size() == 1);

                return AST::castToSkippingReferences<AST::Expression> (getElement->indexes[0]);
            }

            return {};
        }

        const AST::EndpointInstance& getAndCheckEndpointInstance (AST::Object& instance)
        {
            if (auto e = AST::castToSkippingReferences<AST::EndpointInstance> (instance))
            {
                if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (e->node))
                    checkNodeArrayIndex (AST::castToRefSkippingReferences<AST::GraphNode> (getElement->parent), getElement->getSingleIndexProperty());

                return *e;
            }

            if (auto sourceIndex = AST::castToSkippingReferences<AST::GetElement> (instance))
            {
                if (auto p = AST::castToSkippingReferences<AST::EndpointInstance> (sourceIndex->parent))
                {
                    checkConnectionEndpointArrayIndex (*p, sourceIndex->getSingleIndexProperty());
                    return *p;
                }

                if (auto r = AST::castToSkippingReferences<AST::ReadFromEndpoint> (sourceIndex->parent))
                {
                    if (auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (r->endpointInstance))
                    {
                        checkConnectionEndpointArrayIndex (*endpointInstance, sourceIndex->getSingleIndexProperty());
                        return *endpointInstance;
                    }
                }

                throwError (instance, Errors::unimplementedFeature ("Endpoint value sub-elements"));
            }

            if (auto valueBase = AST::castToSkippingReferences<AST::ValueBase> (instance))
            {
                auto endpointInstances = GraphConnectivityModel::getUsedEndpointInstances (*valueBase);

                if (endpointInstances.empty())
                    throwError (valueBase, Errors::invalidEndpointSpecifier());

                CMAJ_ASSERT (endpointInstances.size() == 1);

                return endpointInstances[0];
            }

            visitObject (instance);
            throwError (instance, Errors::invalidEndpointSpecifier());
        }

        void checkConnection (const AST::Connection& connection, AST::Object& source, AST::Object& dest)
        {
            if (auto sourceConnection = AST::castTo<AST::Connection> (source))
            {
                CMAJ_ASSERT (sourceConnection->dests.size() == 1);
                return checkConnection (connection, sourceConnection->dests.front().getObjectRef(), dest);
            }

            if (! AST::castTo<AST::GetElement> (source))
            {
                if (auto value = AST::castTo<AST::ValueBase> (source))
                {
                    auto& destInstance = getAndCheckEndpointInstance (dest);
                    checkConnection (connection, *value, destInstance);
                    return;
                }
            }

            auto& sourceInstance = getAndCheckEndpointInstance (source);
            auto  sourceIndex    = getOptionalGetElementIndex (source);
            auto& destInstance   = getAndCheckEndpointInstance (dest);
            auto  destIndex      = getOptionalGetElementIndex (dest);

            checkConnection (connection, sourceInstance, sourceIndex, destInstance, destIndex);
        }

        void visit (AST::Connection& c) override
        {
            super::visit (c);
            checkDelayLength (c.delayLength);

            CMAJ_ASSERT (c.sources.size() == 1 || c.dests.size() == 1); // many-to-many not yet implemented

            for (auto& source : c.sources)
                for (auto& dest : c.dests)
                    checkConnection (c, source->getObjectRef(), dest->getObjectRef());
        }

        void visit (AST::GraphNode& node) override
        {
            super::visit (node);

            if (node.getProcessorType() == nullptr)
                throwError (node.processorType, Errors::expectedProcessorName());

            getOptionalArraySizeOrThrowError (node.arraySize, (int64_t) AST::maxProcessorArraySize);

            if (node.clockMultiplierRatio != nullptr)
            {
                CMAJ_ASSERT (node.clockDividerRatio == nullptr);
                checkClockRatio (node.clockMultiplierRatio);
            }

            if (node.clockDividerRatio != nullptr)
            {
                CMAJ_ASSERT (node.clockMultiplierRatio == nullptr);
                checkClockRatio (node.clockDividerRatio);
            }
        }

        void checkClockRatio (const AST::Property& ratio)
        {
            auto& value = getAsConstantOrThrowError (ratio.getObjectRef());
            auto& type = getResultTypeOfValueOrThrowError (value);

            if (! type.isPrimitiveInt())
                throwError (ratio, Errors::ratioMustBeInteger());

            auto constRatio = AST::getAsInt64 (value);

            if (constRatio < 1 || constRatio > 512)
                throwError (ratio, Errors::ratioOutOfRange());

            if (! choc::math::isPowerOf2 (constRatio))
                throwError (ratio, Errors::ratioMustBePowerOf2());
        }

        void visit (AST::ProcessorProperty& o) override       { super::visit (o); }

        void visit (AST::ReturnStatement& o) override
        {
            super::visit (o);
            auto& functionReturnType = getAsTypeOrThrowError (o.getParentFunction().returnType);

            if (o.value != nullptr)
            {
                if (functionReturnType.isVoid())
                    throwError (o.value, Errors::voidFunctionCannotReturnValue());

                auto& value = getAsValueOrThrowError (o.value);
                expectSilentCastPossible (o.context, functionReturnType, value);

                if (functionReturnType.containsSlice())
                {
                    ensureVariablesScannedForLocalSlices();
                    OutOfScopeSourcesForValue outOfScope (value, functionReturnType, nullptr);

                    if (! outOfScope.sourcesFound.empty())
                        throwLocalDataError (outOfScope, AST::getContext (o.value), Errors::cannotReturnLocalReference());
                }
            }
            else
            {
                if (! functionReturnType.isVoid())
                    throwError (o, Errors::nonVoidFunctionMustReturnValue (AST::print (functionReturnType)));
            }
        }

        static void checkStatementsInBlock (DuplicateNameChecker& nameChecker, AST::ScopeBlock& block)
        {
            for (auto& s : block.statements)
            {
                auto& statement = s->getObjectRef();

                if (auto subBlock = statement.getAsScopeBlock())
                {
                    if (! subBlock->label.get().empty())
                        nameChecker.checkAndAdd (subBlock->label.get(), subBlock->context);

                    nameChecker.startNewScope();
                    checkStatementsInBlock (nameChecker, *subBlock);
                    nameChecker.endScope();
                }
                else if (auto v = statement.getAsVariableDeclaration())
                {
                    nameChecker.emitWarningForShadowedVariable (v->name.get(), v->context);
                }
                else if (auto loop = statement.getAsLoopStatement())
                {
                    if (! loop->label.get().empty())
                        nameChecker.checkAndAdd (loop->label.get(), loop->context);

                    if (auto loopBlock = loop->body->getAsScopeBlock())
                    {
                        nameChecker.startNewScope();
                        checkStatementsInBlock (nameChecker, *loopBlock);
                        nameChecker.endScope();
                    }
                }
                else if (auto alias = statement.getAsAlias())
                {
                    nameChecker.checkAndAdd (alias->name.get(), alias->context);
                }
                else if (AST::castToTypeBase (statement) != nullptr || statement.getAsStatement() == nullptr)
                {
                    throwError (statement, Errors::expectedStatement());
                }
                else if (auto value = statement.getAsValueBase())
                {
                    if (auto u = value->getAsUnaryOperator())
                        throwError (statement, Errors::resultOfOperatorIsUnused (u->getSymbol()));

                    if (auto b = value->getAsBinaryOperator())
                        throwError (statement, Errors::resultOfOperatorIsUnused (b->getSymbol()));

                    if (value->getAsCast() != nullptr)
                        throwError (statement, Errors::resultOfCastIsUnused());

                    if (! (value->isFunctionCall() || value->isPreOrPostIncOrDec()))
                        throwError (statement, Errors::expressionHasNoEffect());
                }
                else if (auto forwardBranch = statement.getAsForwardBranch())
                {
                    if (s != block.statements.front())
                        throwError (statement, Errors::forwardBranchMustBeFirstStatement());

                    auto& condition = getAsValueOrThrowError (forwardBranch->condition);

                    if (! condition.getResultType()->isPrimitiveInt32())
                        throwError (forwardBranch->condition, Errors::forwardBranchMustBeInt32());
                }
                else if (AST::castToSkippingReferences<AST::Namespace> (statement) != nullptr)
                {
                    throwError (statement, Errors::expressionHasNoEffect());
                }
            }
        }

        void visit (AST::ScopeBlock& b) override
        {
            super::visit (b);
        }

        void visit (AST::StaticAssertion& s) override
        {
            super::visit (s);

            expectBoolValue (s.condition);
            getAsConstantOrThrowError (s.condition);
            throwIfStaticAssertionFails (s);
        }

        void visit (AST::StructType& s) override
        {
            super::visit (s);

            DuplicateNameChecker nameChecker;

            for (size_t i = 0; i < s.memberNames.size(); ++i)
            {
                nameChecker.checkAndAdd (s.getMemberName (i), s.context);

                auto& memberType = getAsTypeOrThrowError (s.memberTypes[i]);

                if (memberType.isVoid())      throwError (s.memberTypes[i], Errors::memberCannotBeVoid());
                if (memberType.isConst())     throwError (s.memberTypes[i], Errors::memberCannotBeConst());
                if (memberType.isReference()) throwError (s.memberTypes[i], Errors::memberCannotBeReference());
            }
        }

        void visit (AST::TernaryOperator& t) override
        {
            super::visit (t);

            if (AST::castTo<AST::ScopeBlock> (getPreviousObjectOnVisitStack()) != nullptr)
                throwError (t, Errors::ternaryCannotBeStatement());

            expectBoolValue (t.condition);

            auto& trueType  = getResultTypeOfValueOrThrowError (t.trueValue);
            auto& falseType = getResultTypeOfValueOrThrowError (t.falseValue);

            checkTypeIsResolved (trueType);
            checkTypeIsResolved (falseType);

            if (trueType.isVoid())   throwError (t.trueValue, Errors::ternaryCannotBeVoid());
            if (falseType.isVoid())  throwError (t.falseValue, Errors::ternaryCannotBeVoid());

            if (! trueType.isSameType (falseType, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
            {
                if (! (AST::TypeRules::canSilentlyCastTo (trueType, falseType)
                        || AST::TypeRules::canSilentlyCastTo (falseType, trueType)))
                    throwError (t, Errors::ternaryTypesMustMatch (AST::print (trueType),
                                                                  AST::print (falseType)));
            }
        }

        void visit (AST::Alias& a) override
        {
            super::visit (a);
            validation::checkExpressionForRecursion (a);

            switch (a.aliasType.get())
            {
                case AST::AliasTypeEnum::Enum::typeAlias:
                    (void) getAsTypeOrThrowError (a.target);
                    validation::checkAliasTargetType (a, false);
                    break;

                case AST::AliasTypeEnum::Enum::processorAlias:
                    if (AST::castToSkippingReferences<AST::ProcessorBase> (a.target) == nullptr)
                        throwError (a.target, Errors::expectedProcessorName());
                    break;

                case AST::AliasTypeEnum::Enum::namespaceAlias:
                    if (AST::castToSkippingReferences<AST::Namespace> (a.target) == nullptr)
                        throwError (a.target, Errors::expectedNamespaceName());
                    break;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }

        void visit (AST::VariableDeclaration& v) override
        {
            super::visit (v);

            if (v.declaredType != nullptr)
            {
                auto& declaredType = getAsTypeOrThrowError (v.declaredType);

                if (declaredType.isVoid())
                    throwError (v, v.isParameter() ? Errors::parameterCannotBeVoid()
                                                   : Errors::variableCannotBeVoid());

                if (v.isExternal)
                {
                    if (auto mc = declaredType.getAsMakeConstOrRef())
                        if (mc->makeConst)
                            throwError (mc, Errors::noConstOnExternals());

                    if (v.initialValue == nullptr)
                        throwError (v, Errors::unresolvedExternal (v.getName()));
                }

                if (declaredType.isReference() && ! v.isParameter())
                    throwError (v.declaredType.getObjectRef().getLocationOfStartOfExpression(),
                                Errors::variableTypeCannotBeReference());
            }

            if (v.initialValue != nullptr)
            {
                auto& initialValue = getAsValueOrThrowError (v.initialValue);

                if (getResultTypeOfValueOrThrowError (initialValue).isVoid())
                    throwError (initialValue, v.isParameter() ? Errors::parameterCannotBeVoid()
                                                              : Errors::variableCannotBeVoid());

                if (auto declaredType = AST::castToTypeBase (v.declaredType))
                    expectSilentCastPossible (initialValue.context, *declaredType, initialValue);
            }
            else
            {
                CMAJ_ASSERT (v.declaredType != nullptr);
            }
        }

        void visit (AST::VariableReference& ref) override
        {
            super::visit (ref);
            auto& v = ref.getVariable();

            if (! (v.isCompileTimeConstant() || v.isExternal || v.isInternalVariable()))
                if (v.findParentModule() != ref.findParentModule())
                    throwError (ref, Errors::cannotUseVarFromOtherProcessor());
        }

        void visit (AST::TypeMetaFunction& o) override
        {
            super::visit (o);
            (void) getAsTypeOrThrowError (o, o.source);
        }

        void visit (AST::ValueMetaFunction& o) override
        {
            super::visit (o);

            if (o.getSourceType() == nullptr)
                (void) getAsTypeOrThrowError (o.arguments.front());

            auto& source = o.getSource();

            if (o.isGetSizeOp() && AST::castToTypeBase (source) != nullptr)
                if (AST::ValueMetaFunction::getSize (source) == nullptr)
                    throwError (o, Errors::badTypeForMetafunction (o.op.getString(), AST::print (source)));

            if (o.isGetNumDimensionsOp() && AST::castToTypeBase (source) != nullptr)
                if (! AST::ValueMetaFunction::getNumDimensions (source).has_value())
                    throwError (o, Errors::badTypeForMetafunction (o.op.getString(), AST::print (source)));

            if (! o.hasCorrectNumArguments())
                throwError (o, Errors::wrongNumArgsForMetafunction (o.op.getString()));
        }

        void visit (AST::UnaryOperator& u) override
        {
            super::visit (u);

            if (! AST::TypeRules::isSuitableTypeForUnaryOp (u.op, getResultTypeOfValueOrThrowError (u.input)))
                throwError (u.input, Errors::wrongTypeForUnary());
        }

        void visit (AST::ReadFromEndpoint& r) override
        {
            super::visit (r);

            auto endpoint = r.getEndpointDeclaration();

            if (endpoint->isEvent())
                throwError (r.endpointInstance, Errors::cannotReadFromEventInput());

            if (endpoint->isArray())
                if (auto instance = AST::castToSkippingReferences<AST::EndpointInstance> (r.endpointInstance))
                    if (instance->getNodeArraySize())
                        throwError (r.endpointInstance, Errors::cannotReadFromArrayEndpoint());

            getAndCheckEndpointInstance (*AST::castToSkippingReferences<AST::EndpointInstance> (r.endpointInstance));
        }

        void visit (AST::WriteToEndpoint& w) override
        {
            super::visit (w);

            ptr<const AST::ValueBase> value;

            if (w.value != nullptr)
                value = getAsValueOrThrowError (w.value);

            auto endpoint = w.getEndpoint();

            if (endpoint == nullptr)
                throwError (w.target, Errors::expectedEndpoint());

            if (endpoint->isInput && w.findParentModule() == endpoint->findParentModule())
                throwError (w.target, Errors::cannotWriteToOwnInput());

            if (w.targetIndex != nullptr)
            {
                if (! endpoint->isArray())
                    throwError (w.targetIndex, Errors::notAnEndpointArray());

                auto& targetIndex = getAsValueOrThrowError (w.targetIndex);
                auto& indexType  = getResultTypeOfValueOrThrowError (targetIndex).skipConstAndRefModifiers();

                if (! (indexType.isPrimitiveInt() || indexType.isBoundedType()))
                    throwError (w, Errors::nonIntegerArrayIndex());

                if (auto constIndex = getAsFoldedConstant (w.targetIndex))
                {
                    auto size = endpoint->isArray() ? AST::getAsInt64 (getAsConstantOrThrowError (endpoint->arraySize)) : 1;

                    AST::TypeRules::checkConstantArrayIndex (AST::getContext (w.targetIndex), AST::getAsInt64 (*constIndex),
                                                             AST::TypeRules::castToArraySize (size), false);
                }
                else
                {
                    if (! indexType.isBoundedType() || indexType.getAsBoundedType()->getBoundedIntLimit() > static_cast<AST::ArraySize> (*endpoint->getArraySize()))
                        emitMessage (Errors::indexHasRuntimeOverhead().withLocation (targetIndex.context.getFullLocation()));
                }
            }

            if (w.value == nullptr)
            {
                expectSilentCastPossible (AST::getContext (w), endpoint->getDataTypes (true), w.context.allocator.createVoidType());
                return;
            }

            auto& typeUsed = expectSilentCastPossible (AST::getContext (w.value), endpoint->getDataTypes (true), *value);

            if (typeUsed.containsSlice())
            {
                ensureVariablesScannedForLocalSlices();
                OutOfScopeSourcesForValue outOfScope (*value, typeUsed, nullptr);

                if (! outOfScope.sourcesFound.empty())
                    throwLocalDataError (outOfScope, AST::getContext (w.value), Errors::cannotSendLocalReference());
            }
        }

        void visit (AST::Annotation& a) override
        {
            super::visit (a);
            checkAnnotation (a);
        }

        void visit (AST::NoopStatement& o) override           { super::visit (o); }

        //==============================================================================
        void checkFunctionBehaviour (uint64_t stackSizeLimit)
        {
            AST::FunctionInfoGenerator functionInfo;

            functionInfo.generate (program);

            if (stackSizeLimit == 0)
                stackSizeLimit = defaultMaxStackSize;

            if (functionInfo.maximumStackSize > stackSizeLimit)
                cmaj::throwError (Errors::maximumStackSizeExceeded (choc::text::getByteSizeDescription (functionInfo.maximumStackSize),
                                                                    choc::text::getByteSizeDescription (stackSizeLimit)));

            functionInfo.throwErrorIfRecursive();

            program.visitAllFunctions (true, [] (AST::Function& f)
            {
                auto& info = AST::FunctionInfoGenerator::getInfo (f);

                if (info.isOnlyCalledFromMain())
                {
                    if (f.isMainFunction() && info.advanceCall == nullptr)
                        throwError (f, Errors::mainFunctionMustCallAdvance());
                }
                else
                {
                    if (info.advanceCall != nullptr)
                        throwError (info.advanceCall, Errors::advanceCannotBeCalledHere());

                    if (auto endpointAccess = info.getStreamAccessStatement())
                    {
                        if (info.calledFromInit)
                            throwError (endpointAccess, Errors::endpointsCannotBeUsedDuringInit());

                        if (info.calledFromEvent)
                            throwError (endpointAccess, Errors::streamsCannotBeUsedInEventCallbacks());

                        throwError (endpointAccess, Errors::endpointsCanOnlyBeUsedInMain());
                    }

                    if (auto endpointAccess = info.getValueReadAccessStatement())
                    {
                        if (info.calledFromInit)
                            throwError (endpointAccess, Errors::endpointsCannotBeUsedDuringInit());

                        if (info.calledFromEvent)
                            throwError (endpointAccess, Errors::valuesCannotBeUsedInEventCallbacks());
                    }

                    if (info.calledFromInit)
                        if (auto eventAccess = info.getEventAccessStatement())
                            throwError (eventAccess, Errors::endpointsCannotBeUsedDuringInit());
               }

                if (info.advanceCall != nullptr)
                    for (auto param : f.getParameterTypes())
                        if (param->containsSlice())
                            throwError (param, Errors::paramCannotContainSlice());

                checkForInfiniteLoops (f);
            });
        }

        static void checkForInfiniteLoops (AST::Function& f)
        {
            f.visitObjectsInScope ([&f] (AST::Object& o)
            {
                if (auto loop = o.getAsLoopStatement())
                    if (StatementExitMethods (*loop).doesNotExit())
                        if (! statementMayCallAdvance (*loop))
                            throwError (loop, Errors::functionContainsAnInfiniteLoop (f.getName()));
            });
        }

        static bool statementMayCallAdvance (AST::Statement& statement)
        {
            bool advanceCalled = false;

            statement.visitObjectsInScope ([&advanceCalled] (const AST::Object& o)
            {
                if (auto fc = o.getAsFunctionCall())
                {
                    if (AST::FunctionInfoGenerator::getInfo (*fc->getTargetFunction()).advanceCall != nullptr)
                        advanceCalled = true;
                }
                else if (o.isAdvance())
                {
                    advanceCalled = true;
                }
            });

            return advanceCalled;
        }

        void ensureVariablesScannedForLocalSlices()
        {
            if (! hasScannedVariablesForLocalSlices)
            {
                hasScannedVariablesForLocalSlices = true;
                markLocalVariablesWhichMayReferToLocalSlices (program.rootNamespace);
            }
        }

        void throwLocalDataError (const OutOfScopeSourcesForValue& outOfScope, AST::ObjectContext& context, DiagnosticMessage&& error)
        {
            DiagnosticMessageList list;
            list.add (context, std::move (error));

            choc::SmallVector<CodeLocation, 8> locationsSeen;
            locationsSeen.push_back (context.location);

            for (auto& source : outOfScope.sourcesFound)
            {
                auto loc = AST::getContext (source).location;

                if (! locationsSeen.contains (loc))
                {
                    locationsSeen.push_back (loc);
                    list.add (source, Errors::seeSourceOfLocalData());
                }
            }

            throwError (list);
        }
    };
}
