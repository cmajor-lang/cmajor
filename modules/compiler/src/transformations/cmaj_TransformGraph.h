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
struct FlattenGraph
{
    struct Renderer
    {
        Renderer (AST::ProcessorBase& g, ProcessorInfo::GetInfo getInfo) : graph (g), getProcessorInfo (getInfo)
        {
            initFunction = g.findSystemInitFunction();
            mainFunction = g.findMainFunction();

            if (initFunction == nullptr)
            {
                initFunction = AST::createExportedFunction (graph, graph.context.allocator.voidType, g.getStrings().systemInitFunctionName);
                AST::addFunctionParameter (*initFunction, graph.context.allocator.int32Type,         g.getStrings().initFnProcessorIDParamName, true);
                AST::addFunctionParameter (*initFunction, graph.context.allocator.int32Type,         g.getStrings().initFnSessionIDParamName);
                AST::addFunctionParameter (*initFunction, graph.context.allocator.float64Type,       g.getStrings().initFnFrequencyParamName);
            }

            if (mainFunction == nullptr)
                mainFunction = AST::createExportedFunction (graph, graph.context.allocator.createVoidType(), g.getStrings().mainFunctionName);


            processorGraphOutput = mainFunction->context.allocate<AST::ScopeBlock>();
        }

        EndpointInterpolationStrategy getEndpointInterpolationStrategy (AST::GraphNode& node) const
        {
            EndpointInterpolationStrategy strategy;

            if (auto g = AST::castTo<AST::Graph> (graph))
            {
                for (auto& connection : g->connections.iterateAs<AST::Connection>())
                {
                    if (connection.interpolation != AST::InterpolationTypeEnum::Enum::none)
                    {
                        // Connections have been simplified by now so there is no chaining to worry about
                        for (auto& source : connection.sources)
                        {
                            if (auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (source))
                            {
                                if (endpointInstance->hasNode (node))
                                    strategy.emplace (endpointInstance->getEndpoint (true).get(), connection.interpolation);
                            }
                            else if (auto valueBase = AST::castToSkippingReferences<AST::ValueBase> (source))
                            {
                                auto endpointInstances = GraphConnectivityModel::getUsedEndpointInstances (*valueBase);

                                for (auto i : endpointInstances)
                                    if (i->hasNode (node))
                                        strategy.emplace (i->getEndpoint (true).get(), connection.interpolation);
                            }
                        }

                        for (auto& dest : connection.dests.iterateAs<AST::EndpointInstance>())
                            if (dest.hasNode (node))
                                strategy.emplace (dest.getEndpoint (false).get(), connection.interpolation);
                    }
                }
            }

            return strategy;
        }

        static bool isDelayNode (const AST::GraphNode& node)
        {
            return node.getName().get().substr (0, 6) == "_delay";
        }

        void addNode (AST::GraphNode& node, bool useStateForIO)
        {
            if (node.getClockMultiplier() != 1.0)
            {
                int32_t multiplierRatio = 1, dividerRatio = 1;

                if (auto v = AST::getAsFoldedConstant (node.clockMultiplierRatio))
                    multiplierRatio = *v->getAsInt32();

                if (auto v = AST::getAsFoldedConstant (node.clockDividerRatio))
                    dividerRatio = *v->getAsInt32();

                auto& oversamplingWrapper = OversamplingTransformation::buildOversamplingTransform (*node.getProcessorType(),
                                                                                                    getEndpointInterpolationStrategy (node),
                                                                                                    multiplierRatio,
                                                                                                    dividerRatio);

                getProcessorInfo (oversamplingWrapper).usesProcessorId = getProcessorInfo (*node.getProcessorType()).usesProcessorId;

                node.processorType.createReferenceTo (oversamplingWrapper);
            }

            CMAJ_ASSERT (nodeInstanceInfoMap.find (std::addressof (node)) == nodeInstanceInfoMap.end());

            auto processorType = node.getProcessorType();
            auto& processorInfo = getProcessorInfo (*processorType);
            auto nodeName = std::string (node.getName());

            auto arraySize = node.getArraySize();

            auto& stateVariableDeclaration = AST::createStateVariable (graph, nodeName,
                                                                       getStateStruct (*processorType, arraySize), {});

            {
                auto& block = *initFunction->getMainBlock();
                auto& stateVariable = AST::createVariableReference (block.context, stateVariableDeclaration);
                auto systemInitFn = processorType->findSystemInitFunction();

                auto& processorIDParamRef = AST::createVariableReference (block.context, initFunction->getParameter (node.getStrings().initFnProcessorIDParamName));
                auto& sessionIDParamRef   = AST::createVariableReference (block.context, initFunction->getParameter (node.getStrings().initFnSessionIDParamName));
                auto& frequencyParamRef   = AST::createVariableReference (block.context, initFunction->getParameter (node.getStrings().initFnFrequencyParamName));

                if (arraySize)
                {
                    addLoop (block, *arraySize, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                    {
                        auto& arrayElement = AST::createGetElement (loopBlock, stateVariable, index);

                        writeInstanceIndexToState (loopBlock, arrayElement, index);

                        if (processorInfo.usesProcessorId)
                            writeProcessorIdToState (loopBlock, arrayElement, processorIDParamRef);

                        if (systemInitFn)
                            loopBlock.addStatement (AST::createFunctionCall (loopBlock, *systemInitFn, arrayElement,
                                                                             processorIDParamRef, sessionIDParamRef, frequencyParamRef));
                    });

                    if (processorInfo.usesProcessorId)
                        nextProcessorId += *arraySize;
                }
                else
                {
                    if (processorInfo.usesProcessorId)
                    {
                        writeProcessorIdToState (block, stateVariable, processorIDParamRef);
                        nextProcessorId++;
                    }

                    if (systemInitFn)
                        block.addStatement (AST::createFunctionCall (block, *systemInitFn, stateVariable,
                                                                     processorIDParamRef, sessionIDParamRef, frequencyParamRef));
                }
            }

            ptr<AST::VariableReference> ioVariable;

            if (! useStateForIO)
            {
                ioVariable = AST::createLocalVariableRef (*mainFunction->getMainBlock(),
                                                          nodeName + "_io",
                                                          getIOStruct (*processorType, arraySize), {});
            }
            else
            {
                auto& io = AST::createStateVariable (graph, nodeName + "_io", getIOStruct (*processorType, arraySize), {});

                ioVariable = AST::createVariableReference (graph.context, io);
            }

            InstanceInfo newInstance { AST::createVariableReference (mainFunction->context, stateVariableDeclaration),
                                       *ioVariable,
                                       mainFunction->context.allocate<AST::ScopeBlock>() };

            nodeInstanceInfoMap[std::addressof (node)] = std::make_unique<InstanceInfo> (std::move (newInstance));
            nodesToRender.push_back (std::addressof (node));

            if (isDelayNode (node))
                delayNodes.push_back (std::addressof (node));
        }

        void writeProcessorIdToState (AST::ScopeBlock& block, AST::ValueBase& stateVariable, AST::ValueBase& processorIDParam)
        {
            auto& stateMember = AST::createGetStructMember (block, stateVariable, EventHandlerUtilities::getIdMemberName());
            block.addStatement (AST::createPreInc(block.context, processorIDParam));
            AST::addAssignment (block, stateMember, processorIDParam);
        }

        void writeInstanceIndexToState (AST::ScopeBlock& block, AST::ValueBase& stateVariable, AST::ValueBase& index)
        {
            auto& stateMember = AST::createGetStructMember (block, stateVariable, EventHandlerUtilities::getInstanceIndexMemberName());
            AST::addAssignment (block, stateMember, index);
        }


        void addConnection (AST::EndpointInstance& source, ptr<AST::ConstantValueBase> sourceIndex,
                            AST::EndpointInstance& dest,   ptr<AST::ConstantValueBase> destIndex)
        {
            if (! dest.isParentEndpoint())
            {
                if (! source.isParentEndpoint())
                {
                    auto& instanceInfo = getInfoForNode (dest.getNode());
                    instanceInfo.dependencies.push_back (source.getNode());
                }
            }

            if (source.getEndpoint (true)->isEvent())
            {
                auto compatibleTypes = findTypeIntersection (source, dest);

                bool sourceEndpointIsArray = source.getEndpoint (true)->isArray();

                for (auto type : compatibleTypes)
                {
                    if (source.isParentEndpoint())
                    {
                        auto& eventHandlerFn = getOrCreateEventHandlerFunction (graph, source, type, sourceEndpointIsArray);

                        if (dest.isParentEndpoint())
                            addWriteToEventEndpoint (eventHandlerFn, sourceIndex, dest, destIndex);
                        else
                            addCallToEventHandlerIfPresent (eventHandlerFn, sourceIndex, dest, destIndex, type);
                    }
                    else
                    {
                        auto nodeIsArray = source.getNode().getArraySize().has_value();

                        // If the source has an event handler for this type
                        if (auto fn = EventHandlerUtilities::findEventFunctionForType (source, type, true))
                        {
                            auto& targetFn = createUniqueEventFunction (type, sourceEndpointIsArray || nodeIsArray);

                            EventHandlerUtilities::addUpcastCall (*fn, targetFn, sourceEndpointIsArray || nodeIsArray);

                            if (dest.isParentEndpoint())
                                addWriteToEventEndpoint (targetFn, sourceIndex, dest, destIndex);
                            else
                                addCallToEventHandlerIfPresent (targetFn, sourceIndex, dest, destIndex, type);
                        }
                    }
                }
            }
            else
            {
                if (! dest.isParentEndpoint())
                {
                    auto& instanceInfo = getInfoForNode (dest.getNode());
                    writeTo (instanceInfo.steps, dest, destIndex, source, sourceIndex);
                }
                else
                {
                    writeTo (processorGraphOutput, dest, destIndex, source, sourceIndex);
                }
            }
        }

        void addConnection (AST::Expression& source,     ptr<AST::ConstantValueBase> sourceIndex,
                            AST::EndpointInstance& dest, ptr<AST::ConstantValueBase> destIndex)
        {
            if (! dest.isParentEndpoint())
            {
                auto& instanceInfo = getInfoForNode (dest.getNode());
                instanceInfo.addDependencies (source);
                writeTo (instanceInfo.steps, dest, destIndex, source, sourceIndex);
            }
            else
            {
                writeTo (processorGraphOutput, dest, destIndex, source, sourceIndex);
            }
        }

        void populateMainFunction()
        {
            // Ensure delay nodes are rendered first
            for (auto& node : delayNodes)
            {
                auto& instanceInfo = getInfoForNode (*node);
                addRunCall (*mainFunction->getMainBlock(), *node);
                instanceInfo.hasBeenRun = true;
            }

            for (auto& node : nodesToRender)
                ensureNodeIsRendered (*node);

            // Re-render the delay nodes, with inputs populated
            for (auto& node : delayNodes)
            {
                auto& instanceInfo = getInfoForNode (*node);
                instanceInfo.hasBeenRun = false;
                ensureNodeIsRendered (*node);
            }

            mainFunction->getMainBlock()->addStatement (*processorGraphOutput);
        }

        struct InstanceInfo
        {
            ref<AST::VariableReference> stateVariable;
            ref<AST::VariableReference> ioVariable;
            ptr<AST::ScopeBlock> steps;
            AST::ObjectRefVector<const AST::GraphNode> dependencies;
            AST::ObjectRefVector<const AST::GraphNode> delayDependencies;
            bool hasBeenRun = false;

            void addDependencies (AST::Expression& source)
            {
                if (auto expr = AST::castToSkippingReferences<AST::ValueBase> (source))
                {
                    auto instances = GraphConnectivityModel::getUsedEndpointInstances (*expr);

                    for (auto i : instances)
                        if (! i->isParentEndpoint())
                            dependencies.push_back (i->getNode());
                }
            }
        };

        InstanceInfo& getInfoForNode (const AST::GraphNode& node)
        {
            auto i = nodeInstanceInfoMap.find (std::addressof (node));
            CMAJ_ASSERT (i != nodeInstanceInfoMap.end());
            return *i->second;
        }

    private:
        AST::Function& getOrCreateEventHandlerFunction (AST::ProcessorBase& processor,
                                                        AST::EndpointInstance& endpointInstance,
                                                        const AST::TypeBase& type, bool isArray)
        {
            if (auto fn = EventHandlerUtilities::findEventFunctionForType (processor, endpointInstance.getEndpoint (true)->getName(), type, isArray))
                return *fn;

            auto& function = AST::createFunctionInModule (graph, graph.context.allocator.createVoidType(),
                                                          endpointInstance.getEndpoint (true)->getName());
            function.isEventHandler = true;

            AST::addFunctionParameter (function, EventHandlerUtilities::getOrCreateStateStructType (graph), "_state", true);

            if (isArray)
                AST::addFunctionParameter (function, graph.context.allocator.createInt32Type(), "index");

            if (! type.isVoid())
                AST::addFunctionParameter (function, type, "value");

            return function;
        }

        AST::Function& createUniqueEventFunction (const AST::TypeBase& type, bool isArray)
        {
            auto uniqueName = graph.getStringPool().get (AST::createUniqueName ("_forwardEvent", graph.functions));
            auto& function = AST::createFunctionInModule (graph, graph.context.allocator.createVoidType(), uniqueName);

            AST::addFunctionParameter (function, EventHandlerUtilities::getOrCreateStateStructType (graph), "_state", true);

            if (isArray)
                AST::addFunctionParameter (function, graph.context.allocator.createInt32Type(), "index");

            if (! type.isVoid())
                AST::addFunctionParameter (function, type, "value");

            return function;
        }

        void addWriteToEndpoint (AST::ScopeBlock& block, AST::EndpointInstance& dest,
                                 ptr<AST::ValueBase> destIndex, ptr<AST::VariableReference> value)
        {
            block.addStatement (AST::createWriteToEndpoint (block.context, dest, destIndex, value));
        }

        void addWriteToEventEndpoint (AST::Function& fn, ptr<AST::ConstantValueBase> sourceIndex,
                                      AST::EndpointInstance& dest, ptr<AST::ConstantValueBase> destIndex)
        {
            auto destEndpointArraySize = dest.getEndpoint (false)->getArraySize();
            auto destNodeArraySize = dest.isParentEndpoint() ? std::optional<int>() : dest.getNode().getArraySize();

            auto sourceIsArray = fn.parameters.size() == 3;
            auto sourceHasIndex = sourceIndex != nullptr;
            auto destIsArray = destEndpointArraySize || destNodeArraySize;
            auto destHasIndex = destIndex != nullptr;

            auto& block = getSourceBlock (fn, sourceIndex);

            auto valueParam = fn.parameters.findObjectWithName (fn.getStrings().value);
            ptr<AST::VariableReference> valueArgument;

            if (valueParam)
                valueArgument = AST::createVariableReference (block.context, valueParam);

            auto indexParam = fn.parameters.findObjectWithName (fn.getStrings().index);

            if (destHasIndex)
            {
                // -> 1 using dest index
                addWriteToEndpoint (block, dest, destIndex, valueArgument);
            }
            else if (! destIsArray)
            {
                // -> 1, no index
                addWriteToEndpoint (block, dest, nullptr, valueArgument);
            }
            else
            {
                // We have an array destination, but no destIndex
                if (sourceIsArray && (! sourceHasIndex))
                {
                    // 1 -> 1 using function index
                    addWriteToEndpoint (block, dest, AST::createVariableReference (block.context, indexParam), valueArgument);
                }
                else
                {
                    // 1 -> N fanout
                    auto arraySize = destEndpointArraySize ? destEndpointArraySize.value() : destNodeArraySize.value();

                    addLoop (block, arraySize, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                    {
                        addWriteToEndpoint (loopBlock, dest, index, valueArgument);
                    });
                }
            }
        }

        AST::ScopeBlock& getSourceBlock (AST::Function& fn, ptr<AST::ConstantValueBase> sourceIndex)
        {
            auto block = fn.getMainBlock();

            if (fn.parameters.size() != 3 || ! sourceIndex)
                return *block;

            // Only call the event handler if the indexParam matches the sourceIndex
            auto indexParam = fn.parameters.findObjectWithName (fn.getStrings().index);

            auto& ifStatement = block->allocateChild<AST::IfStatement>();
            auto& trueBlock = block->allocateChild<AST::ScopeBlock>();

            ifStatement.trueBranch.setChildObject (trueBlock);

            auto& condition = ifStatement.allocateChild<AST::BinaryOperator>();
            condition.lhs.createReferenceTo (*indexParam);
            condition.rhs.createConstant (*sourceIndex->getAsInt32());
            condition.op = AST::BinaryOpTypeEnum::Enum::equals;
            ifStatement.condition.setChildObject (condition);

            block->addStatement (ifStatement);

            return trueBlock;
        }


        void addEventHandlerCall (AST::ScopeBlock& block,
                                  AST::Function& eventHandler,
                                  AST::ValueBase& stateMember,
                                  AST::EndpointInstance& dest,
                                  ptr<AST::ValueBase> destIndex,
                                  ptr<AST::VariableReference> value)
        {
            auto destEndpointArraySize = dest.getEndpoint (false)->getArraySize();
            auto destEndpointIsArray = destEndpointArraySize;

            if (destEndpointIsArray)
            {
                if (destIndex)
                {
                    auto& functionCall = AST::createFunctionCall (block, eventHandler);
                    functionCall.arguments.addReference (stateMember);
                    functionCall.arguments.addReference (*destIndex);

                    if (value)
                        functionCall.arguments.addReference (*value);

                    block.addStatement (functionCall);
                }
                else
                {
                    addLoop (block, *destEndpointArraySize, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                    {
                        auto& functionCall = AST::createFunctionCall (loopBlock, eventHandler);
                        functionCall.arguments.addReference (stateMember);
                        functionCall.arguments.addReference (index);

                        if (value)
                            functionCall.arguments.addReference (*value);

                        loopBlock.addStatement (functionCall);
                    });
                }
            }
            else
            {
                CMAJ_ASSERT (destIndex == nullptr);

                auto& functionCall = AST::createFunctionCall (block, eventHandler);
                functionCall.arguments.addReference (stateMember);

                if (value)
                    functionCall.arguments.addReference (*value);

                block.addStatement (functionCall);
            }
        }

        void addCallToEventHandlerIfPresent (AST::Function& fn, ptr<AST::ConstantValueBase> sourceIndex,
                                             AST::EndpointInstance& dest, ptr<AST::ConstantValueBase> destIndex,
                                             const AST::TypeBase& type)
        {
            auto eventHandler = EventHandlerUtilities::findEventFunctionForType (dest, type, false);

            if (! eventHandler)
                return;

            auto& block = getSourceBlock (fn, sourceIndex);

            auto& stateArgument = AST::createVariableReference (block.context, fn.parameters.findObjectWithName (fn.getStrings()._state));
            auto valueParam = fn.parameters.findObjectWithName (fn.getStrings().value);
            ptr<AST::VariableReference> valueArgument;

            if (valueParam)
                valueArgument = AST::createVariableReference (block.context, valueParam);

            std::optional<int> destNodeArraySize;

            if (auto graphNode = AST::castToSkippingReferences<AST::GraphNode> (dest.node))
                destNodeArraySize = graphNode->getArraySize();

            auto destNodeIsArray = destNodeArraySize;
            auto destEndpointArraySize = dest.getEndpoint (false)->getArraySize();
            auto destEndpointIsArray = destEndpointArraySize;

            auto sourceIsArray = fn.parameters.size() == 3;

            auto& stateMember = block.allocateChild<AST::GetStructMember>();
            stateMember.object.setChildObject (stateArgument);
            stateMember.member.set (dest.getNode().getName());

            if (destNodeIsArray)
            {
                if (sourceIsArray)
                {
                    auto& indexArgument = AST::createVariableReference (block.context, fn.parameters.findObjectWithName (fn.getStrings().index));
                    auto& stateNodeElement = AST::createGetElement (block, stateMember, indexArgument);

                    addEventHandlerCall (block, *eventHandler, stateNodeElement, dest, destIndex, valueArgument);
                }
                else
                {
                    addLoop (block, *destNodeArraySize, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                    {
                        auto& stateNodeElement = AST::createGetElement (loopBlock, stateMember, index);
                        addEventHandlerCall (loopBlock, *eventHandler, stateNodeElement, dest, destIndex, valueArgument);
                    });
                }
            }
            else
            {
                ref<AST::ValueBase> nodeState = stateMember;

                if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (dest.node))
                    nodeState = AST::createGetElement (block, stateMember, getElement->getSingleIndex());

                if (destEndpointIsArray && destIndex == nullptr && sourceIndex == nullptr && sourceIsArray)
                {
                    auto& indexArgument = AST::createVariableReference (block.context, fn.parameters.findObjectWithName (fn.getStrings().index));
                    addEventHandlerCall (block, *eventHandler, nodeState, dest, indexArgument, valueArgument);
                }
                else
                {
                    addEventHandlerCall (block, *eventHandler, nodeState, dest, destIndex, valueArgument);
                }
            }
        }

        AST::ObjectRefVector<const AST::TypeBase> findTypeIntersection (AST::EndpointInstance& source,
                                                                        AST::EndpointInstance& dest)
        {
            AST::ObjectRefVector<const AST::TypeBase> result;

            auto sourceTypes = source.getEndpoint (true)->getDataTypes();
            auto destTypes = dest.getEndpoint (false)->getDataTypes();

            for (auto& sourceType : sourceTypes)
                for (auto& destType : destTypes)
                    if (AST::TypeRules::areTypesIdentical (destType.get(), sourceType.get()))
                        result.push_back (sourceType);

            return result;
        }

        void ensureNodeIsRendered (const AST::GraphNode& node)
        {
            auto& instanceInfo = getInfoForNode (node);

            if (instanceInfo.hasBeenRun)
                return;

            instanceInfo.hasBeenRun = true;

            for (auto& dependency : instanceInfo.dependencies)
                ensureNodeIsRendered (dependency);

            mainFunction->getMainBlock()->addStatement (*instanceInfo.steps);

            addRunCall (*mainFunction->getMainBlock(), node);
        }

        AST::ValueBase& getStructMember (ptr<AST::ScopeBlock> block,
                                          AST::EndpointInstance& endpointInstance,
                                          AST::ValueBase& index, bool isSource)
        {
            auto endpoint = endpointInstance.getEndpoint (isSource);
            auto nodeIndex = getNodeIndex (endpointInstance);
            auto nodeArraySize = getGraphNodeArraySize (endpointInstance);
            auto endpointArraySize = getEndpointArraySize (endpointInstance, isSource);

            CMAJ_ASSERT (! (endpointArraySize && nodeArraySize));

            if (endpointInstance.isParentEndpoint())
            {
                auto& streamRead = block->allocateChild<AST::ReadFromEndpoint>();
                streamRead.endpointInstance.referTo (endpointInstance);
                return streamRead;
            }

            auto& instanceInfo = getInfoForNode (endpointInstance.getNode());

            ref<AST::ValueBase> expr = endpoint->isStream() ? instanceInfo.ioVariable
                                                            : instanceInfo.stateVariable;

            if (nodeArraySize)
                expr = AST::createGetElement (block, expr, index);

            if (nodeIndex)
                expr = AST::createGetElement (block, expr, *nodeIndex);

            auto& structMember = block->allocateChild<AST::GetStructMember>();
            structMember.object.setChildObject (expr);
            structMember.member = structMember.getStringPool().get (StreamUtilities::getEndpointStateMemberName (*endpoint));

            if (endpointArraySize)
                return AST::createGetElement (block, structMember, index);

            return structMember;
        }

        static std::optional<int32_t> getNodeIndex (AST::EndpointInstance& endpointInstance)
        {
            if (auto getElement = AST::castToSkippingReferences<AST::GetElement> (endpointInstance.node))
                return AST::getAsFoldedConstant (getElement->getSingleIndex())->getAsInt32();

            return {};
        }

        AST::ValueBase& getIndexValue (ptr<AST::ConstantValueBase> i, AST::ValueBase& defaultValue)
        {
            if (i != nullptr)
                return *i;

            return defaultValue;
        }

        void writeTo (ptr<AST::ScopeBlock> block, AST::EndpointInstance& dest,
                      ptr<AST::ConstantValueBase> destIndex, AST::EndpointInstance& source,
                      ptr<AST::ConstantValueBase> sourceIndex)
        {
            auto itemsToCopy = 1;

            if (getArraySize (source, true) && ! sourceIndex)
                itemsToCopy = *getArraySize (source, true);

            if (getArraySize (dest, false) && ! destIndex)
                itemsToCopy = *getArraySize (dest, false);

            addLoop (block, itemsToCopy, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
            {
                auto& sourceMember = getStructMember (loopBlock, source, getIndexValue (sourceIndex, index), true);

                if (dest.isParentEndpoint())
                    writeToEndpoint (loopBlock, dest, getIndexValue (destIndex, index), sourceMember);
                else
                    writeToStructMember (loopBlock, getStructMember (loopBlock, dest, getIndexValue (destIndex, index), false),
                                         sourceMember, dest.getEndpoint (false)->isValue());
            });
        }

        AST::ValueBase& getSourceItem (ptr<AST::ScopeBlock> block, AST::ValueBase& v, AST::ValueBase& index)
        {
            if (! v.getResultType()->isArray())
                return v;

            return AST::createGetElement (block->context, v, index);
        }

        void writeTo (ptr<AST::ScopeBlock> block, AST::EndpointInstance& dest,
                      ptr<AST::ConstantValueBase> destIndex, AST::Expression& source, ptr<AST::ConstantValueBase> sourceIndex)
        {
            if (auto value = AST::castToSkippingReferences<AST::ValueBase> (source))
            {
                auto itemsToCopy = 1;

                bool sourceIsArray = value->getResultType()->isArray();

                if (sourceIsArray && ! sourceIndex)
                    itemsToCopy = static_cast<int> (value->getResultType()->getArrayOrVectorSize(0));

                if (getArraySize (dest, false) && ! destIndex)
                    itemsToCopy = *getArraySize (dest, false);

                auto sourceValue = value;

                if (itemsToCopy > 1)
                {
                    auto& v = AST::createLocalVariable (*block, "v", *value);
                    v.isConstant = true;
                    sourceValue = AST::createVariableReference (block->context, v);
                }

                addLoop (block, itemsToCopy, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                {
                    if (dest.isParentEndpoint())
                        writeToEndpoint (loopBlock, dest, getIndexValue (destIndex, index), getSourceItem (loopBlock, *sourceValue, getIndexValue (sourceIndex, index)));
                    else
                        writeToStructMember (loopBlock, getStructMember (loopBlock, dest, getIndexValue (destIndex, index), false),
                                             getSourceItem (loopBlock, *sourceValue, getIndexValue (sourceIndex, index)), dest.getEndpoint (false)->isValue());
                });
            }
            else
            {
                CMAJ_ASSERT_FALSE;
            }
        }

        static std::optional<int> getEndpointArraySize (AST::EndpointInstance& endpointInstance, bool isSource)
        {
            auto endpoint = endpointInstance.getEndpoint (isSource);

            if (endpoint->isArray())
                return endpoint->getArraySize();

            return {};
        }

        static std::optional<int> getGraphNodeArraySize (AST::EndpointInstance& endpointInstance)
        {
            if (! endpointInstance.isParentEndpoint()
                 && endpointInstance.getNode().isArray()
                 && ! getNodeIndex (endpointInstance))
                return endpointInstance.getNode().getArraySize();

            return {};
        }

        static std::optional<int> getArraySize (AST::EndpointInstance& endpointInstance, bool isSource)
        {
            return std::max (getEndpointArraySize (endpointInstance, isSource),
                             getGraphNodeArraySize (endpointInstance));
        }

        void writeToStructMember (ptr<AST::ScopeBlock> block,
                                  AST::ValueBase& dest,
                                  AST::ValueBase& source,
                                  bool overwrite)
        {
            block->addStatement (StreamUtilities::createAccumulateOrAssign (*block, dest, source, ! overwrite));
        }

        void writeToEndpoint (ptr<AST::ScopeBlock> block,
                              AST::EndpointInstance& endpointInstance,
                              AST::ValueBase& destIndex, AST::Expression& source)
        {
            auto& dest = *endpointInstance.endpoint->getAsNamedReference();

            auto endpointArraySize = getEndpointArraySize (endpointInstance, false);

            auto& streamWrite = block->allocateChild<AST::WriteToEndpoint>();

            streamWrite.target.setChildObject (dest);
            streamWrite.value.setChildObject (source);

            if (endpointArraySize)
                streamWrite.targetIndex.referTo (destIndex);

            block->addStatement (streamWrite);
        }

        void addLoop (ptr<AST::ScopeBlock> block, int arraySize, std::function<void(AST::ScopeBlock&, AST::ValueBase&)> populateLoop, bool unroll = false)
        {
            if (unroll || arraySize <= 4)
            {
                for (int i = 0; i < arraySize; i++)
                    populateLoop (*block, block->context.allocator.createConstant (i));
            }
            else
            {
                auto& index = block->allocateChild<AST::VariableDeclaration>();
                index.variableType = AST::VariableTypeEnum::Enum::local;
                index.name = index.getStringPool().get ("i");
                index.declaredType.referTo (AST::createBoundedType (*block, block->context.allocator.createConstant (arraySize), false));

                auto& loop = block->allocateChild<AST::LoopStatement>();
                auto& loopBlock = block->allocateChild<AST::ScopeBlock>();
                loop.numIterations.referTo (index);
                loop.body.referTo (loopBlock);

                populateLoop (loopBlock, AST::createVariableReference (block, index));
                block->addStatement (loop);
            }
        }

        void addRunCall (ptr<AST::ScopeBlock> block, const AST::GraphNode& node)
        {
            if (auto processorMainFunction = node.getProcessorType()->findMainFunction())
            {
                auto& instanceInfo = getInfoForNode (node);

                if (auto arraySize = node.getArraySize())
                {
                    addLoop (block, *arraySize, [&] (AST::ScopeBlock& loopBlock, AST::ValueBase& index)
                    {
                        addRunCall (loopBlock,
                                    processorMainFunction,
                                    AST::createGetElement (block, instanceInfo.stateVariable, index),
                                    AST::createGetElement (block, instanceInfo.ioVariable, index));
                    });
                }
                else
                {
                    addRunCall (block, processorMainFunction,
                                instanceInfo.stateVariable, instanceInfo.ioVariable);
                }
            }
        }

        static void addRunCall (ptr<AST::ScopeBlock> block, ptr<AST::Function> mainFunction,
                                AST::ValueBase& stateVariable, AST::ValueBase& ioVariable)
        {
            auto& functionCall = AST::createFunctionCall (block, *mainFunction, stateVariable, ioVariable);
            block->addStatement (functionCall);
        }

        static ptr<AST::TypeBase> getStateStruct (AST::ProcessorBase& processor, std::optional<int> arraySize)
        {
            if (auto s = processor.findStruct (processor.getStrings().stateStructName))
            {
                if (arraySize)
                    return AST::createArrayOfType (processor, *s, *arraySize);

                return *s;
            }

            return {};
        }

        static ptr<AST::TypeBase> getIOStruct (AST::ProcessorBase& processor, std::optional<int> arraySize)
        {
            if (auto s = processor.findStruct (processor.getStrings().ioStructName))
            {
                if (arraySize)
                    return AST::createArrayOfType (processor, *s, *arraySize);

                return *s;
            }

            return {};
        }

        AST::ProcessorBase& graph;
        ProcessorInfo::GetInfo getProcessorInfo;
        ptr<AST::Function> initFunction, mainFunction;
        int32_t nextProcessorId = 1;

        std::unordered_map<const AST::GraphNode*, std::unique_ptr<InstanceInfo>> nodeInstanceInfoMap;
        std::vector<const AST::GraphNode*> nodesToRender, delayNodes;
        ptr<AST::ScopeBlock> processorGraphOutput;
    };

    static void flattenGraph (AST::Graph& graph, ProcessorInfo::GetInfo getInfo, uint32_t eventBufferSize, bool isTopLevelProcessor)
    {
        Renderer renderer (graph, getInfo);

        for (auto& i : graph.nodes)
            if (auto node = AST::castTo<AST::GraphNode> (i))
                renderer.addNode (*node, false);

        graph.visitConnections ([&] (AST::Connection& c)
        {
            addConnection (renderer, c);
        });

        renderer.populateMainFunction();

        moveStateVariablesToStruct (graph, eventBufferSize, isTopLevelProcessor, [&] (const AST::GraphNode& node) -> MoveStateVariablesToStruct::NodeInfo
                                    {
                                        auto& info = renderer.getInfoForNode (node);

                                        return { info.stateVariable.get(), info.ioVariable.get() };
                                    });
    }

    static void addProcessorNodes (AST::ProcessorBase& p, ProcessorInfo::GetInfo getInfo, uint32_t eventBufferSize, bool isTopLevelProcessor)
    {
        Renderer renderer (p, getInfo);

        for (auto& i : p.nodes)
            if (auto node = AST::castTo<AST::GraphNode> (i))
                renderer.addNode (*node, true);

        if (! isTopLevelProcessor)
        {
            moveStateVariablesToStruct (p, eventBufferSize, isTopLevelProcessor, [&] (const AST::GraphNode& node) -> MoveStateVariablesToStruct::NodeInfo
            {
                auto& info = renderer.getInfoForNode (node);

                return { info.stateVariable.get(), info.ioVariable.get() };
            });
        }
    }

    static void addConnection (Renderer& renderer, AST::Connection& connection)
    {
        // Delays should have been converted into delay graph nodes by the SimplifyGraphConnections transformation
        CMAJ_ASSERT (connection.delayLength == nullptr);

        for (auto source : connection.sources)
        {
            if (auto conn = AST::castToSkippingReferences<AST::Connection> (source))
            {
                addConnection (renderer, *conn);

                for (auto connectionSource : conn->dests)
                {
                    auto sourceEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (connectionSource);

                    addConnection (renderer, *sourceEndpointInstance, {}, connection);
                }
            }
            else if (auto element = AST::castToSkippingReferences<AST::GetElement> (source))
            {
                auto sourceIndex = AST::getAsFoldedConstant (element->getSingleIndex());

                if (auto endpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent))
                    addConnection (renderer, *endpointInstance, sourceIndex, connection);
                else if (AST::castToSkippingReferences<AST::ReadFromEndpoint> (element->parent))
                    addConnection (renderer, *element, {}, connection);
                else
                    addConnection (renderer, *AST::castToSkippingReferences<AST::Expression> (element->parent), sourceIndex, connection);
            }
            else if (auto sourceEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (source))
            {
                addConnection (renderer, *sourceEndpointInstance, {}, connection);
            }
            else if (auto sourceExpression = AST::castToSkippingReferences<AST::Expression> (source))
            {
                addConnection (renderer, *sourceExpression, {}, connection);
            }
        }
    }

    static void addConnection (Renderer& renderer, AST::EndpointInstance& sourceEndpointInstance,
                               ptr<AST::ConstantValueBase> sourceIndex, AST::Connection& connection)
    {
        for (auto dest : connection.dests)
        {
            if (auto element = AST::castToSkippingReferences<AST::GetElement> (dest))
            {
                auto destEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent);
                auto destIndex = AST::getAsFoldedConstant (element->getSingleIndex());

                renderer.addConnection (sourceEndpointInstance, sourceIndex, *destEndpointInstance, destIndex);
            }
            else if (auto destEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (dest))
            {
                renderer.addConnection (sourceEndpointInstance, sourceIndex, *destEndpointInstance, {});
            }
        }
    }

    static void addConnection (Renderer& renderer, AST::Expression& sourceExpression, ptr<AST::ConstantValueBase> sourceIndex, AST::Connection& connection)
    {
        for (auto dest : connection.dests)
        {
            if (auto element = AST::castToSkippingReferences<AST::GetElement> (dest))
            {
                auto destEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent);
                auto destIndex = AST::getAsFoldedConstant (element->getSingleIndex());

                renderer.addConnection (sourceExpression, sourceIndex, *destEndpointInstance, destIndex);
            }
            else if (auto destEndpointInstance = AST::castToSkippingReferences<AST::EndpointInstance> (dest))
            {
                renderer.addConnection (sourceExpression, sourceIndex, *destEndpointInstance, {});
            }
        }
    }
};

inline void flatten (AST::Program& program, AST::ProcessorBase& processor,
                     bool isTopLevelProcessor, ProcessorInfo::GetInfo getInfo,
                     uint32_t eventBufferSize,
                     bool useForwardBranch)
{
    // First ensure all nodes are flattened
    for (auto& n : processor.nodes)
    {
        if (auto node = AST::castTo<AST::GraphNode> (n))
        {
            auto& original = *node->getProcessorType();
            auto& clone = AST::createClonedSiblingModule (original, false);
            node->processorType.createReferenceTo (clone);

            if (node->getArraySize().has_value())
            {
                getInfo (clone).isProcessorArray = node->getArraySize().has_value();

                AST::createStateVariable (*clone.getAsProcessorBase(), EventHandlerUtilities::getInstanceIndexMemberName(),
                                          clone.context.allocator.createInt32Type(), {});
            }

            flatten (program, *node->getProcessorType(), false, getInfo, eventBufferSize, useForwardBranch);

            original.findParentNamespace()->subModules.removeObject (original);
        }
    }

    if (auto graph = processor.getAsGraph())
    {
        FlattenGraph::flattenGraph (*graph, getInfo, eventBufferSize, isTopLevelProcessor);
    }
    else
    {
        moveVariablesToState (processor);
        moveProcessorPropertiesToState (processor, getInfo, std::addressof (program.getMainProcessor()) == std::addressof (processor));
        FlattenGraph::addProcessorNodes (processor, getInfo, eventBufferSize, isTopLevelProcessor);
        removeResetCalls (processor);
        removeAdvanceCalls (processor, useForwardBranch);
        moveStateVariablesToStruct (processor, eventBufferSize, isTopLevelProcessor);
    }
}

inline void flattenGraph (AST::Program& program,
                          uint32_t maxBlockSize,
                          uint32_t eventBufferSize,
                          bool useForwardBranch)
{
    ProcessorInfoManager processorInfoManager;

    bool isBlockProcessor = maxBlockSize > 1;

    flatten (program, program.getMainProcessor(), ! isBlockProcessor,
             processorInfoManager.getProcessorInfo(), eventBufferSize, useForwardBranch);

    if (isBlockProcessor)
    {
        auto& blockProcessor = createBlockTransformProcessor (program.getMainProcessor(), maxBlockSize);
        moveStateVariablesToStruct (blockProcessor, eventBufferSize, true);

        program.setMainProcessor (blockProcessor);
    }

    canonicaliseLoopsAndBlocks (program);
}

}
