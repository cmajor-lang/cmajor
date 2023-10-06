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
struct MoveStateVariablesToStruct  : public AST::NonParameterisedObjectVisitor
{
    using super = AST::NonParameterisedObjectVisitor;
    using super::visit;

    struct NodeInfo
    {
        ptr<AST::VariableReference> stateVariable;
        ptr<AST::VariableReference> ioVariable;
    };

    using NodeInfoFn = std::function<NodeInfo(const AST::GraphNode&)>;

    MoveStateVariablesToStruct (AST::ProcessorBase& p, uint32_t e, bool i, NodeInfoFn n)
        : super (p.context.allocator), processor (p), eventBufferSize (e), isTopLevelProcessor (i), nodeInfoFn (n)
    {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    AST::ProcessorBase& processor;
    uint32_t eventBufferSize;
    bool isTopLevelProcessor;
    NodeInfoFn nodeInfoFn;
    ptr<AST::Function> currentFunction;
    ptr<AST::StructType> processorStateType, ioStateType;
    ptr<AST::MakeConstOrRef> processorStateTypeRef, ioStateTypeRef;

    using EventHandlerFunctions = std::vector<ptr<AST::Function>>;
    std::unordered_map<const AST::EndpointDeclaration*, EventHandlerFunctions> eventFunctions;

    std::unordered_map<const AST::EndpointDeclaration*, bool> visitedEndpoints;

    std::vector<ref<AST::FunctionCall>> functionCallsToCheck;

    ptr<AST::Function> getParentFunction (const AST::Object& o)
    {
        if (auto f = o.findParentOfType<AST::Function>())
            return f;

        return currentFunction;
    }

    void visit (AST::Processor& p) override
    {
        transform (p);
    }

    void visit (AST::Graph& g) override
    {
        transform (g);
    }

    template<class ProcessorType>
    void transform (ProcessorType& p)
    {
        if (p.getAsProcessorBase() != std::addressof (processor))
            return;

        if (isTopLevelProcessor)
            ValueStreamUtilities::addEventStreamSupport (p);

        {
            processorStateType = EventHandlerUtilities::getOrCreateStateStructType (p);

            for (auto stateVariable : p.stateVariables.template getAsObjectTypeList<AST::VariableDeclaration>())
            {
                if (! stateVariable->isConstant || stateVariable->isInitialisedInInit)
                {
                    auto name = stateVariable->name.get();
                    size_t suffix = 2;

                    while (processorStateType->indexOfMember (name) >= 0)
                        name = p.getStringPool().get (std::string (stateVariable->name.get()) + "_" + std::to_string (suffix++));

                    stateVariable->name.set (name);
                    processorStateType->memberNames.addString (name);
                    processorStateType->memberTypes.addReference (*stateVariable->getType());
                    p.stateVariables.removeObject (stateVariable);
                }
            }

            processorStateTypeRef = p.template allocateChild<AST::MakeConstOrRef>();
            processorStateTypeRef->source.referTo (*processorStateType);
            processorStateTypeRef->makeRef = true;
        }

        {
            ioStateType = EventHandlerUtilities::getOrCreateIoStructType (p);

            for (auto& endpointDeclaration : p.endpoints.template iterateAs<AST::EndpointDeclaration>())
            {
                auto memberName = StreamUtilities::getEndpointStateMemberName (endpointDeclaration);

                if (endpointDeclaration.isStream())
                {
                    if (ioStateType->hasMember (memberName))
                        continue;

                    CMAJ_ASSERT (endpointDeclaration.dataTypes.size() == 1);

                    auto& type = AST::castToTypeBaseRef (endpointDeclaration.dataTypes[0]);

                    if (endpointDeclaration.isArray())
                        ioStateType->addMember (memberName, AST::createArrayOfType (p, type, *endpointDeclaration.getArraySize()));
                    else
                        ioStateType->addMember (memberName, type);
                }
                else if (endpointDeclaration.isValue())
                {
                    if (processorStateType->hasMember (memberName))
                        continue;

                    CMAJ_ASSERT (endpointDeclaration.dataTypes.size() == 1);

                    auto& type = ValueStreamUtilities::getTypeForValueEndpoint (p, endpointDeclaration, isTopLevelProcessor);

                    processorStateType->addMember (memberName, type);
                }
                else if (endpointDeclaration.isEvent() && endpointDeclaration.isOutput() && isTopLevelProcessor)
                {
                    auto& eventStruct = AST::createStruct (p, "_eventValue_" + std::string (endpointDeclaration.getName()));

                    eventStruct.addMember ("frame", p.context.allocator.createInt32Type());
                    eventStruct.addMember ("type", p.context.allocator.createInt32Type());

                    size_t typeEntry = 0;

                    for (auto t : endpointDeclaration.dataTypes)
                    {
                        if (! AST::castToTypeBaseRef (t).isVoid())
                            eventStruct.addMember ("value_" + std::to_string (typeEntry), AST::castToTypeBaseRef (t));

                        typeEntry++;
                    }

                    auto& eventStructArrayType = AST::createArrayOfType (p, eventStruct, static_cast<int32_t> (eventBufferSize));

                    processorStateType->addMember (EventHandlerUtilities::getEventCountStateMemberName (endpointDeclaration), p.context.allocator.createInt32Type());
                    processorStateType->addMember (endpointDeclaration.getName(), eventStructArrayType);
                }
            }

            ioStateTypeRef = p.context.template allocate<AST::MakeConstOrRef>();
            ioStateTypeRef->source.referTo (*ioStateType);
            ioStateTypeRef->makeRef = true;
        }

        super::visit (p);

        checkFunctionCalls();
        functionCallsToCheck.clear();
    }

    void visit (AST::Function& f) override
    {
        if (! processorStateType || f.getParentScope() != processor)
            return;

        if (f.isSystemInitFunction() || f.isEventHandler)
            getOrCreateFunctionStateParameter (f);

        // main() requires state and io variable
        if (f.isMainFunction())
        {
            auto& stateParam = getOrCreateFunctionStateParameter (f);
            getOrCreateFunctionIOParameter (f);

            if (isTopLevelProcessor)
            {
                auto& updateRampsBlock = f.allocateChild<AST::ScopeBlock>();
                ValueStreamUtilities::addUpdateRampsCall (processor, updateRampsBlock, stateParam);
                f.getMainBlock()->addStatement (updateRampsBlock, 0);
            }
        }

        auto previousFunction = currentFunction;
        currentFunction = f;
        super::visit (f);
        currentFunction = previousFunction;
    }

    void visit (AST::FunctionCall& fc) override
    {
        super::visit (fc);

        if (auto p = fc.getTargetFunction()->findParentOfType<AST::ProcessorBase>())
            if (p == processor)
                functionCallsToCheck.push_back ( { fc });
    }


    void visit (AST::Advance& a) override
    {
        super::visit (a);

        visitedEndpoints.clear();
    }

    void visit (AST::WriteToEndpoint& w) override
    {
        super::visit (w);

        CMAJ_ASSERT (w.isChildOf (processor));

        auto& endpointDeclaration = *w.getEndpoint();

        if (endpointDeclaration.isStream() || endpointDeclaration.isValue())
        {
            if (auto endpointInstance = w.getEndpointInstance())
            {
                if (endpointInstance->getNode().isArray())
                {
                    if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (endpointInstance->node))
                    {
                        auto& structMember = getTargetValueForEndpoint (w.context, *endpointInstance, getElement->getSingleIndex());
                        w.replaceWith (transformWriteToEndpoint (w, endpointDeclaration, structMember));
                    }
                    else
                    {
                        auto& scopeBlock = w.context.allocate<AST::ScopeBlock>();

                        for (int i = 0; i < *endpointInstance->getNode().getArraySize(); i++)
                        {
                            auto& structMember = getTargetValueForEndpoint (w.context, *endpointInstance, w.context.allocator.createConstant (i));
                            scopeBlock.addStatement (transformWriteToEndpoint (w, endpointDeclaration, structMember));
                        }

                        w.replaceWith (scopeBlock);
                    }
                }
                else
                {
                    w.replaceWith (transformWriteToEndpoint (w, endpointDeclaration, getTargetValueForEndpoint (w.context, *endpointInstance, {})));
                }
            }
            else
            {
                w.replaceWith (transformWriteToEndpoint (w, endpointDeclaration, getTargetValueForEndpoint (w.context, endpointDeclaration)));
            }
        }
        else if (endpointDeclaration.isEvent())
        {
            ptr<const AST::TypeBase> type = w.context.allocator.createVoidType();

            if (auto value = AST::castTo<AST::ValueBase> (w.value))
                type = validation::expectSilentCastPossible (AST::getContext (w.value), validation::getTypeListOrThrowError (endpointDeclaration.dataTypes), *value);
            else
                type = validation::expectSilentCastPossible (AST::getContext (w), validation::getTypeListOrThrowError (endpointDeclaration.dataTypes), w.context.allocator.createVoidType());

            if (auto eventHandlerFunction = findEventHandlerFunction (w, *type))
            {
                auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (w.target);

                auto& stateParameter = endpointInstance != nullptr
                                     ? getOrCreateFunctionStateParameterMember (w.context, *getParentFunction (w), endpointInstance->getNode().getName())
                                     : getOrCreateFunctionStateParameter (*getParentFunction (w));

                if (endpointInstance != nullptr && endpointInstance->getNode().isArray())
                {
                    if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (endpointInstance->node))
                    {
                        auto& stateElement = AST::createGetElement (w.context, stateParameter, getElement->getSingleIndex(), true, true);
                        w.replaceWith (transformWriteToEndpoint (w, endpointDeclaration, *eventHandlerFunction, stateElement));
                    }
                    else
                    {
                        auto& scopeBlock = w.context.allocate<AST::ScopeBlock>();

                        for (int i = 0; i < *endpointInstance->getNode().getArraySize(); i++)
                        {
                            auto& stateElement = AST::createGetElement (w.context, stateParameter, i);
                            scopeBlock.addStatement (transformWriteToEndpoint (w, endpointDeclaration, *eventHandlerFunction, stateElement));
                        }

                        w.replaceWith (scopeBlock);
                    }
                }
                else
                {
                    w.replaceWith (transformWriteToEndpoint (w, endpointDeclaration, *eventHandlerFunction, stateParameter));
                }
            }
            else
            {
                auto& noopStatement = w.context.allocate<AST::NoopStatement>();
                w.replaceWith (noopStatement);
            }
        }
    }

    AST::Statement& transformWriteToEndpoint (AST::WriteToEndpoint& w,
                                              const AST::EndpointDeclaration& endpointDeclaration,
                                              AST::ValueBase& structMember)
    {
        auto accumulate = endpointDeclaration.isStream();
        auto value = AST::castToSkippingReferences<AST::ValueBase> (w.value);

        if (endpointDeclaration.isArray() && w.targetIndex == nullptr)
        {
            auto& scopeBlock = w.context.allocate<AST::ScopeBlock>();

            bool isArrayValue = value->getResultType()->isArray();

            for (int i = 0; i < *endpointDeclaration.getArraySize(); i++)
            {
                auto valueToUse = isArrayValue ? AST::createGetElement (w.context, value, i) : value;

                auto& a = StreamUtilities::createAccumulateOrAssign (w,
                                                                     AST::createGetElement (w.context, structMember, i),
                                                                     *valueToUse,
                                                                     accumulate);

                scopeBlock.addStatement (a);
            }

            return scopeBlock;
        }

        if (w.targetIndex != nullptr)
        {
            return StreamUtilities::createAccumulateOrAssign (w,
                                                              AST::createGetElement (w.context, structMember, w.targetIndex),
                                                              *value,
                                                              accumulate);
        }


        return StreamUtilities::createAccumulateOrAssign (w,
                                                          structMember,
                                                          *value,
                                                          accumulate);
    }

    AST::Statement& transformWriteToEndpoint (const AST::WriteToEndpoint& w,
                                              const AST::EndpointDeclaration& endpointDeclaration,
                                              AST::Function& eventHandlerFunction,
                                              AST::ValueBase& stateParameter)
    {
        CMAJ_ASSERT (endpointDeclaration.isEvent());

        auto value = AST::castTo<AST::ValueBase> (w.value);

        if (endpointDeclaration.isArray() && w.targetIndex == nullptr)
        {
            auto& scopeBlock = w.context.allocate<AST::ScopeBlock>();

            for (int i = 0; i < *endpointDeclaration.getArraySize(); i++)
            {
                auto& functionCall = AST::createFunctionCall (w,
                                                              eventHandlerFunction,
                                                              stateParameter,
                                                              w.context.allocator.createConstantInt32 (i));

                if (value != nullptr)
                    functionCall.arguments.addReference (*value);

                scopeBlock.addStatement (functionCall);
            }

            return scopeBlock;
        }
        else
        {
            auto& functionCall = w.context.allocate<AST::FunctionCall>();
            functionCall.targetFunction.referTo (eventHandlerFunction);
            functionCall.arguments.addReference (stateParameter);

            if (w.targetIndex != nullptr)
                functionCall.arguments.addReference (*w.targetIndex->getAsValueBase());

            if (value != nullptr)
                functionCall.arguments.addReference (*value);

            return functionCall;
        }
    }


    void visit (AST::Connection&) override
    {
        // Don't process connections
    }

    void visit (AST::ReadFromEndpoint& r) override
    {
        super::visit (r);

        auto& endpointInstance = *AST::castToSkippingReferences<AST::EndpointInstance> (r.endpointInstance);
        auto& endpointDeclaration = *endpointInstance.getEndpoint (true);

        if (endpointDeclaration.isStream() || endpointDeclaration.isValue())
        {
            auto& value = getValueForEndpoint (r.context, endpointInstance);
            r.replaceWith (value);
        }
    }

    void visit (AST::VariableReference& r) override
    {
        if (! currentFunction)
            return;

        auto& v = r.getVariable();

        if (v.isStateVariable() && ! v.hasStaticConstantValue())
        {
            CMAJ_ASSERT (! v.isParameter());

            if (auto moduleBase = v.getParentScopeRef().getAsModuleBase())
            {
                if (moduleBase == processor.getAsModuleBase())
                {
                    auto& stateVariable = getOrCreateFunctionStateParameter (*getParentFunction (r));

                    auto& structMember = getParentFunction(r)->context.allocate<AST::GetStructMember>();
                    structMember.object.referTo (stateVariable);
                    structMember.member = v.getName();

                    r.replaceWith (structMember);
                }
            }
        }
    }

private:
    ptr<AST::Function> findEventHandlerFunction (AST::WriteToEndpoint& w,
                                                 const AST::TypeBase& valueDataType)
    {
        auto& endpointDeclaration = *w.getEndpoint();
        auto& eventFunctionArray = eventFunctions[std::addressof (endpointDeclaration)];
        auto index = endpointDeclaration.getDataTypeIndex (valueDataType);

        CMAJ_ASSERT (index);

        auto dataType = endpointDeclaration.getDataTypes()[*index];

        if (auto instance = AST::castToSkippingReferences<AST::EndpointInstance> (w.target))
            return EventHandlerUtilities::findEventFunctionForType (instance->getProcessor(),
                                                                    instance->getResolvedEndpoint().getName(),
                                                                    dataType,
                                                                    instance->getResolvedEndpoint().isArray());

        if (eventFunctionArray.size() < endpointDeclaration.dataTypes.size())
            eventFunctionArray.resize (endpointDeclaration.dataTypes.size());

        if (eventFunctionArray[*index])
            return eventFunctionArray[*index];

        auto& function = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(),
                                                      w.getStringPool().get (EventHandlerUtilities::getWriteEventFunctionName (endpointDeclaration)));
        auto& stateParam = getOrCreateFunctionStateParameter (function);

        if (endpointDeclaration.isArray())
            AST::addFunctionParameter (function, function.context.allocator.createInt32Type(), function.getStrings().index, false, false);

        bool shouldBeConstRefParam = dataType->isReference() || ! dataType->isPrimitive();

        AST::VariableRefGenerator valueParam;

        if (! dataType->isVoid())
            valueParam = AST::addFunctionParameter (function, dataType, function.getStrings().value,
                                                    shouldBeConstRefParam, shouldBeConstRefParam);

        auto& block = *function.getMainBlock();

        if (isTopLevelProcessor)
        {
            auto& eventCount = AST::createGetStructMember (block.context, stateParam,
                                                           EventHandlerUtilities::getEventCountStateMemberName (endpointDeclaration));

            auto& trueBranch = block.allocateChild<AST::ScopeBlock>();
            auto& ifStatement = AST::createIfStatement (block.context,
                                                        AST::createBinaryOp(block.context,
                                                                            AST::BinaryOpTypeEnum::Enum::lessThan,
                                                                            eventCount,
                                                                            block.context.allocator.createConstantInt32 (static_cast<int32_t> (eventBufferSize))),
                                                        trueBranch);

            auto& structMember = AST::createGetStructMember (block.context, stateParam, endpointDeclaration.getName());
            auto& arrayElement = AST::createGetElement (trueBranch.context, structMember, eventCount);

            if (EventHandlerUtilities::getOrCreateStateStructType (processor)
                  .hasMember (w.getStringPool().get (EventHandlerUtilities::getCurrentFrameStateMemberName())))
            {
                trueBranch.addStatement (AST::createAssignment (trueBranch.context,
                                                                AST::createGetStructMember (trueBranch.context, arrayElement, "frame"),
                                                                AST::createGetStructMember (trueBranch.context, stateParam,
                                                                                            EventHandlerUtilities::getCurrentFrameStateMemberName())));
            }

            trueBranch.addStatement (AST::createAssignment (trueBranch.context,
                                                            AST::createGetStructMember (trueBranch.context, arrayElement, "type"),
                                                            trueBranch.context.allocator.createConstantInt32 (static_cast<int32_t> (*index))));

            if (! dataType->isVoid())
                trueBranch.addStatement (AST::createAssignment (trueBranch.context,
                                                                AST::createGetStructMember (trueBranch.context, arrayElement, "value_" + std::to_string (*index)),
                                                                valueParam));

            block.addStatement (ifStatement);
            block.addStatement (AST::createPreInc (block.context, eventCount));
        }

        eventFunctionArray[*index] = function;
        return function;
    }

    AST::ValueBase& getTargetValueForEndpoint (AST::ObjectContext& context, const AST::EndpointDeclaration& e)
    {
        auto parentFunction = getParentFunction (*context.parentScope);

        if (e.isValue())
            return ValueStreamUtilities::getStateStructMember (context, e, getOrCreateFunctionStateParameter (*parentFunction), isTopLevelProcessor);

        auto& structMember = context.allocate<AST::GetStructMember>();
        structMember.object.setChildObject (getOrCreateFunctionIOParameter (*parentFunction));
        structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (e));

        return structMember;
    }


    AST::ValueBase& getBaseMember (AST::ObjectContext& context, const AST::EndpointInstance& e, const NodeInfo& nodeInfo)
    {
        auto& endpointDeclaration = *e.getEndpoint (e.isSource());

        if (endpointDeclaration.isValue())
            return AST::createGetStructMember (context,
                                               getOrCreateFunctionStateParameter (*getParentFunction (e)),
                                               nodeInfo.stateVariable->getName());

        if (nodeInfo.ioVariable->getVariable().isStateVariable())
            return AST::createGetStructMember (context,
                                               getOrCreateFunctionStateParameter (*getParentFunction (e)),
                                               nodeInfo.ioVariable->getName());

        return *nodeInfo.ioVariable;
    }

    AST::ValueBase& getTargetValueForEndpoint (AST::ObjectContext& context, const AST::EndpointInstance& e, ptr<AST::Object> index)
    {
        auto& endpointDeclaration = *e.getEndpoint (e.isSource());

        if (e.isParentEndpoint())
            return getTargetValueForEndpoint (context, endpointDeclaration);

        auto& node = e.getNode();

        if (index == nullptr)
            index = e.getNodeIndex();

        auto& baseMember = getBaseMember (context, e, nodeInfoFn (node));

        if (node.isArray() && ! index)
        {
            auto size = node.getArraySize();
            CMAJ_ASSERT (size);

            auto& arrayType = AST::createArrayOfType (context, endpointDeclaration.getSingleDataType(), *size);

            auto& cast = context.allocate<AST::Cast>();
            cast.targetType.createReferenceTo (arrayType);

            for (int i = 0; i < node.getArraySize(); i++)
            {
                auto& structMember = context.allocate<AST::GetStructMember>();
                structMember.object.setChildObject (getVariableIndex (context, baseMember, context.allocator.createConstant(i)));
                structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (endpointDeclaration));
                cast.arguments.addReference (structMember);
            }

            return cast;
        }
        else
        {
            auto& structMember = context.allocate<AST::GetStructMember>();

            structMember.object.setChildObject (getVariableIndex (context, baseMember, index));
            structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (endpointDeclaration));

            return structMember;
        }
    }


    AST::ValueBase& getValueForEndpoint (AST::ObjectContext& context, const AST::EndpointDeclaration& e)
    {
        auto parentFunction = getParentFunction (*context.parentScope);

        if (e.isValue())
            return ValueStreamUtilities::getStateStructMember (context, e, getOrCreateFunctionStateParameter (*parentFunction), isTopLevelProcessor);

        auto& structMember = context.allocate<AST::GetStructMember>();
        structMember.object.setChildObject (getOrCreateFunctionIOParameter (*parentFunction));
        structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (e));

        return structMember;
    }

    static AST::ValueBase& getVariableIndex (AST::ObjectContext& context, AST::ValueBase& variable, ptr<AST::Object> index)
    {
        if (index != nullptr)
            return AST::createGetElement (context, variable, *index, true);

        return variable;
    }


    AST::ValueBase& getValueForEndpoint (AST::ObjectContext& context, const AST::EndpointInstance& e)
    {
        auto& endpointDeclaration = *e.getEndpoint (e.isSource());

        if (e.isParentEndpoint())
            return getValueForEndpoint (context, endpointDeclaration);

        auto& node = e.getNode();
        auto nodeIndex = e.getNodeIndex();
        auto nodeInfo = nodeInfoFn (node);

        if (endpointDeclaration.isValue())
        {
            auto& endpointStateMember = AST::createGetStructMember (context,
                                                                    getOrCreateFunctionStateParameter (*getParentFunction (e)),
                                                                    nodeInfo.stateVariable->getName());

            if (node.isArray() && ! nodeIndex)
            {
                auto size = node.getArraySize();
                CMAJ_ASSERT (size);

                auto& arrayType = AST::createArrayOfType (context, endpointDeclaration.getSingleDataType(), *size);

                auto& cast = context.allocate<AST::Cast>();
                cast.targetType.createReferenceTo (arrayType);

                for (int i = 0; i < node.getArraySize(); i++)
                {
                    auto& structMember = context.allocate<AST::GetStructMember>();
                    structMember.object.setChildObject (getVariableIndex (context, endpointStateMember, context.allocator.createConstant(i)));
                    structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (endpointDeclaration));
                    cast.arguments.addReference (structMember);
                }

                return cast;
            }
            else
            {
                return AST::createGetStructMember (context,
                                                   getVariableIndex (context, endpointStateMember, nodeIndex),
                                                   StreamUtilities::getEndpointStateMemberName (endpointDeclaration));
            }
        }

        if (node.isArray() && ! nodeIndex)
        {
            auto size = node.getArraySize();
            CMAJ_ASSERT (size);

            auto& arrayType = AST::createArrayOfType (context, endpointDeclaration.getSingleDataType(), *size);

            auto& cast = context.allocate<AST::Cast>();
            cast.targetType.createReferenceTo (arrayType);

            for (int i = 0; i < node.getArraySize(); i++)
            {
                auto& structMember = context.allocate<AST::GetStructMember>();
                structMember.object.setChildObject (getVariableIndex (context, *nodeInfo.ioVariable, context.allocator.createConstant(i)));
                structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (endpointDeclaration));
                cast.arguments.addReference (structMember);
            }

            return cast;
        }
        else
        {
            auto& structMember = context.allocate<AST::GetStructMember>();

            if (nodeInfo.ioVariable->getVariable().isStateVariable())
            {
                structMember.object.setChildObject (AST::createGetStructMember (context,
                                                                                getOrCreateFunctionStateParameter (*getParentFunction (e)),
                                                                                nodeInfo.ioVariable->getName()));

                if (nodeIndex)
                    structMember.object.referTo (AST::createGetElement (context, structMember.object, *nodeIndex));
            }
            else
            {
                structMember.object.setChildObject (getVariableIndex (context, *nodeInfo.ioVariable, nodeIndex));
            }

            structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (endpointDeclaration));

            return structMember;
        }
    }

    AST::ValueBase& getOrCreateFunctionStateParameter (AST::Function& f)
    {
        if (! hasStateParameter (f))
            return AST::addFunctionParameter (f, *processorStateType, f.getStrings()._state, true, false, 0);

        return AST::createVariableReference (f.context, f.parameters[0]);
    }

    AST::ValueBase& getOrCreateFunctionStateParameterMember (AST::ObjectContext& context, AST::Function& f, AST::PooledString name)
    {
        auto& structMember = context.allocate<AST::GetStructMember>();

        structMember.object.setChildObject (getOrCreateFunctionStateParameter (f));
        structMember.member = name;

        return structMember;
    }

    bool hasStateParameter (AST::Function& f)
    {
        for (auto& param : f.parameters)
            if (auto variable = AST::castTo<AST::VariableDeclaration> (param))
                if (AST::TypeRules::areTypesIdentical (*processorStateTypeRef, *variable->getType()))
                    return true;

        return false;
    }

    AST::VariableReference& getOrCreateFunctionIOParameter (AST::Function& f)
    {
        // First ensure it has the StateVariable
        getOrCreateFunctionStateParameter (f);

        if (! hasIoParameter (f))
        {
            auto& functionIoParameter = f.allocateChild<AST::VariableDeclaration>();

            functionIoParameter.name = functionIoParameter.getStrings()._io;
            functionIoParameter.variableType = AST::VariableTypeEnum::Enum::parameter;
            functionIoParameter.declaredType.referTo (*ioStateTypeRef);

            // Second parameter
            f.parameters.addReference (functionIoParameter, 1);
        }

        return AST::createVariableReference (f.context, f.getParameter (1));
    }

    bool hasIoParameter (AST::Function& f)
    {
        for (auto& param : f.iterateParameters())
            if (AST::TypeRules::areTypesIdentical (*ioStateTypeRef, *param.getType()))
                return true;

        return false;
    }

    void checkFunctionCalls()
    {
        for (auto f : functionCallsToCheck)
        {
            auto parentFunction = f->findParentOfType<AST::Function>();

            if (hasIoParameter (*f->getTargetFunction()))
                if (! hasIoArgument (f))
                    f->arguments.addReference (getOrCreateFunctionIOParameter (*parentFunction), 0);

            if (hasStateParameter (*f->getTargetFunction()))
                if (! hasStateArgument (f))
                    f->arguments.addReference (getOrCreateFunctionStateParameter (*parentFunction), 0);
        }
    }

    bool hasIoArgument (AST::FunctionCall& functionCall)
    {
        for (auto& argument : functionCall.arguments)
            if (auto variableReference = AST::castToSkippingReferences<AST::VariableReference> (argument))
                if (AST::TypeRules::areTypesIdentical (*ioStateTypeRef, *variableReference->getResultType()))
                    return true;

        return false;
    }

    bool hasStateArgument (AST::FunctionCall& functionCall)
    {
        for (auto& argument : functionCall.arguments)
            if (auto variableReference = AST::castToSkippingReferences<AST::VariableReference> (argument))
                if (AST::TypeRules::areTypesIdentical (*processorStateTypeRef, *variableReference->getResultType()))
                    return true;

        return false;
    }
};

inline void moveStateVariablesToStruct (AST::ProcessorBase& processor, uint32_t eventBufferSize, bool isTopLevelProcessor, MoveStateVariablesToStruct::NodeInfoFn fn)
{
    MoveStateVariablesToStruct (processor, eventBufferSize, isTopLevelProcessor, fn).visitObject (processor);
}

inline void moveStateVariablesToStruct (AST::ProcessorBase& processor, uint32_t eventBufferSize, bool isTopLevelProcessor)
{
    MoveStateVariablesToStruct (processor,
                                eventBufferSize,
                                isTopLevelProcessor,
                                [&] (const AST::GraphNode&) -> MoveStateVariablesToStruct::NodeInfo { return {}; }).visitObject (processor);
}

}
