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

/// Coalesces chained connections and replaces delay connections with nodes
/// to implement delays
inline void simplifyGraphConnections (AST::Program& program)
{
    struct SimplifyConnectionPass  : public passes::PassAvoidingGenericFunctionsAndModules
    {
        using super = PassAvoidingGenericFunctionsAndModules;
        using super::visit;

        SimplifyConnectionPass (AST::Program& p) : super (p) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::Graph& graph) override
        {
            super::visit (graph);

            nextDelayID = 1;

            transformConnectionList (graph, graph.connections);
        }

    private:
        int nextDelayID = 1;

        void transformConnectionList (AST::Graph& graph, AST::ListProperty& connectionList)
        {
            for (;;)
            {
                bool connectionTransformed = false;

                for (size_t i = 0; i < connectionList.size();)
                {
                    if (auto connection = connectionList[i].getAsObjectType<AST::Connection>())
                    {
                        if (transformConnection (graph, connectionList, *connection))
                        {
                            connectionTransformed = true;
                            connectionList.remove (i);
                        }
                        else
                        {
                            i++;
                        }
                    }
                    else if (auto cl = connectionList[i].getAsObjectType<AST::ConnectionList>())
                    {
                        transformConnectionList (graph, cl->connections);
                        i++;
                    }
                    else if (auto connectionIf = connectionList[i].getAsObjectType<AST::ConnectionIf>())
                    {
                        if (auto trueList = AST::castTo<AST::ConnectionList> (connectionIf->trueConnections))
                            transformConnectionList (graph, trueList->connections);

                        if (auto falseList = AST::castTo<AST::ConnectionList> (connectionIf->falseConnections))
                            transformConnectionList (graph, falseList->connections);

                        i++;
                    }
                    else
                    {
                        i++;
                    }
                }

                if (! connectionTransformed)
                    break;
            }
        }

        bool transformConnection (AST::Graph& graph, AST::ListProperty& connectionList, AST::Connection& connection)
        {
            return splitChainedConnections (connectionList, connection)
                    || removeDelayIfPresent (graph, connectionList, connection);
        }

        bool splitChainedConnections (AST::ListProperty& connectionList, AST::Connection& connection)
        {
            if (connection.sources.size() != 1)
                return false;

            auto conn = AST::castToSkippingReferences<AST::Connection> (connection.sources.front());

            if (conn == nullptr)
                return false;

            connectionList.addReference (*conn);

            // Update the original connection and re-add it - the caller will remove it's previous entry in the connection list
            AST::RemappedObjects remappedObjects;
            connection.sources.reset();
            connection.sources.deepCopy (conn->dests, remappedObjects);

            connectionList.addReference (connection);

            return true;
        }

        bool removeDelayIfPresent (AST::Graph& graph, AST::ListProperty& connectionList, AST::Connection& connection)
        {
            if (connection.delayLength == nullptr)
                return false;

            auto& sourceEndpointDeclaration = getSourceEndpointDeclaration (connection);

            auto connectionDataTypes = getConnectionDataTypes (connection);

            auto& graphNode = graph.allocateChild<AST::GraphNode>();
            graphNode.nodeName = graphNode.getStringPool().get ("_delay" + std::to_string (nextDelayID++));

            auto& args = graph.allocateChild<AST::ExpressionList>();

            if (connectionDataTypes.size() == 1 && ! connectionDataTypes[0]->isVoid())
                args.items.addReference (connectionDataTypes[0]);

            args.items.addReference (*connection.delayLength->getAsValueBase());

            if (sourceEndpointDeclaration.isEvent())
                args.items.addChildObject (graph.context.allocator.createConstantInt32 (100));

            auto& processor = graph.allocateChild<AST::CallOrCast>();

            if (sourceEndpointDeclaration.isStream())
                processor.functionOrType.referTo (AST::createIdentifierPath (graph.context, { getStdLibraryNamespaceName(), "intrinsics", "delay", "StreamDelay" }));
            else if (sourceEndpointDeclaration.isEvent())
                processor.functionOrType.referTo (getEventDelayProcessor (graph, connectionDataTypes));
            else
                processor.functionOrType.referTo (AST::createIdentifierPath (graph.context, { getStdLibraryNamespaceName(), "intrinsics", "delay", "ValueDelay" }));

            processor.arguments.referTo (args);

            graphNode.processorType.referTo (processor);
            graph.nodes.addReference (graphNode);

            auto& connection1 = graph.allocateChild<AST::Connection>();
            auto& connection2 = graph.allocateChild<AST::Connection>();

            connection1.sources.shallowCopy (connection.sources);
            connection1.dests.addReference (AST::createReference (graph.context, graphNode));

            connection2.sources.addReference (AST::createReference (graph.context, graphNode));
            connection2.dests.shallowCopy (connection.dests);

            connectionList.addReference (connection1);
            connectionList.addReference (connection2);

            return true;
        }

        AST::Expression& getEventDelayProcessor (AST::Graph& graph, AST::ObjectRefVector<const AST::TypeBase> types)
        {
            if (types.size() == 1 && ! types[0]->isVoid())
                return AST::createIdentifierPath (graph.context, { getStdLibraryNamespaceName(), "intrinsics", "delay", "EventDelay" });

            // Create a template processor supporting all of the data types

            auto ns = graph.findParentOfType<AST::Namespace>();
            auto& processor = AST::createProcessorInNamespace (*ns, ns->getStringPool().get ("MultiEventDelay"));

            auto& delayLengthParameter    = AST::createProcessorSpecialisationVariable (processor, "delayLength", processor.context.allocator.createInt32Type(), {});
            auto& delayLengthParameterRef = AST::createVariableReference (processor, delayLengthParameter);

            auto& bufferSizeParameter     = AST::createProcessorSpecialisationVariable (processor, "bufferSize", processor.context.allocator.createInt32Type(), {});

            AST::createEndpointDeclaration (processor, processor.getStrings().in, true, AST::EndpointTypeEnum::Enum::event, types);
            auto& outputEndpoint = AST::createEndpointDeclaration (processor, processor.getStrings().out, false, AST::EndpointTypeEnum::Enum::event, types);

            auto& queuedEventType = AST::createStruct (processor, "QueuedEvent");
            queuedEventType.addMember ("eventTime", processor.context.allocator.createInt32Type());
            queuedEventType.addMember ("eventType", processor.context.allocator.createInt32Type());

            auto& eventBuffer   = AST::createStateVariable (processor, "eventBuffer", AST::createArrayOfType (processor, queuedEventType, bufferSizeParameter), {});
            auto& readPos       = AST::createStateVariable (processor, "readPos", AST::createBoundedType (processor, bufferSizeParameter), {});
            auto& writePos      = AST::createStateVariable (processor, "writePos", AST::createBoundedType (processor, bufferSizeParameter), {});
            auto& bufferEntries = AST::createStateVariable (processor, "bufferEntries", processor.context.allocator.createInt32Type(), {});
            auto& currentTime   = AST::createStateVariable (processor, "currentTime", processor.context.allocator.createInt32Type(), {});

            auto& enqueueFunction = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), "enqueue");

            {
                auto enqueueEventValueRef = AST::addFunctionParameter (enqueueFunction, queuedEventType, "e");
                auto& enqueueEventBlock = *enqueueFunction.getMainBlock();

                auto& eventBufferRef   = AST::createVariableReference (enqueueFunction, eventBuffer);
                auto& writePosRef      = AST::createVariableReference (enqueueFunction, writePos);
                auto& bufferEntriesRef = AST::createVariableReference (enqueueFunction, bufferEntries);
                auto& currentTimeRef   = AST::createVariableReference (enqueueFunction, currentTime);

                auto& ifCondition = AST::createBinaryOp (enqueueEventBlock.context, AST::BinaryOpTypeEnum::Enum::lessThan,
                                                         AST::createVariableReference (enqueueFunction, bufferEntries),
                                                         AST::createVariableReference (processor, bufferSizeParameter));
                auto& ifBlock = enqueueFunction.allocateChild<AST::ScopeBlock>();
                auto& ifStatement = AST::createIfStatement (enqueueEventBlock.context, ifCondition, ifBlock);
                enqueueEventBlock.addStatement (ifStatement);

                AST::addAssignment (ifBlock, AST::createGetStructMember (enqueueEventBlock, enqueueEventValueRef, "eventTime"), AST::createBinaryOp (enqueueEventBlock.context, AST::BinaryOpTypeEnum::Enum::add, currentTimeRef, delayLengthParameterRef));
                AST::addAssignment (ifBlock, AST::createGetElement (ifBlock, eventBufferRef, writePosRef), enqueueEventValueRef);
                ifBlock.addStatement (AST::createPreInc (ifBlock.context, writePosRef));
                ifBlock.addStatement (AST::createPreInc (ifBlock.context, bufferEntriesRef));
            }

            auto& emitEventFunction = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), "emitEvent");

            {
                auto emitEventValueRef = AST::addFunctionParameter (emitEventFunction, queuedEventType, "e");
                auto& emitEventBlock = *emitEventFunction.getMainBlock();
                auto& emitEndpointInstance = AST::createEndpointInstance (emitEventBlock, outputEndpoint);

                int item = 0;

                for (auto type : types)
                {
                    auto eventVariableName = "v" + std::to_string (item);

                    // Add type to EventType struct
                    if (! type->isVoid())
                        queuedEventType.addMember (eventVariableName, type);

                    // Create event handler for type
                    {
                        auto& eventHandler = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), "in");
                        eventHandler.isEventHandler = true;

                        AST::VariableRefGenerator valueParam;

                        if (! type->isVoid())
                            valueParam = AST::addFunctionParameter (eventHandler, type, "v");

                        auto& block = *eventHandler.getMainBlock();
                        auto& queuedEvent = AST::createLocalVariableRef (block, "event", queuedEventType, {});

                        block.addStatement (AST::createAssignment (block.context, AST::createGetStructMember (block, queuedEvent, "eventType"), block.context.allocator.createConstantInt32 (item)));

                        if (! type->isVoid())
                            block.addStatement (AST::createAssignment (block.context, AST::createGetStructMember (block, queuedEvent, eventVariableName), valueParam));

                        block.addStatement (AST::createFunctionCall (block.context, enqueueFunction, queuedEvent));
                    }

                    // Handle type in emitEvent
                    {
                        auto& eventType  = AST::createGetStructMember (emitEventBlock, emitEventValueRef, "eventType");

                        ptr<AST::ValueBase> eventValue;

                        if (! type->isVoid())
                            eventValue = AST::createGetStructMember (emitEventBlock, emitEventValueRef, eventVariableName);

                        auto& writeToEndpoint = AST::createWriteToEndpoint (emitEventBlock.context, emitEndpointInstance, {}, eventValue);
                        auto& ifCondition = AST::createBinaryOp (emitEventBlock.context, AST::BinaryOpTypeEnum::Enum::equals, eventType, emitEventBlock.context.allocator.createConstantInt32 (item));
                        auto& ifStatement = AST::createIfStatement (emitEventBlock.context, ifCondition, writeToEndpoint);
                        emitEventBlock.addStatement (ifStatement);
                    }

                    item++;
                }
            }

            auto& emitEvents = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), "emitEvents");

            // emitEvents
            {
                auto& mainBlock = *emitEvents.getMainBlock();
                auto& loopBlock = mainBlock.allocateChild<AST::ScopeBlock>();

                auto& eventBufferRef   = AST::createVariableReference (emitEvents, eventBuffer);
                auto& readPosRef       = AST::createVariableReference (emitEvents, readPos);
                auto& bufferEntriesRef = AST::createVariableReference (emitEvents, bufferEntries);
                auto& currentTimeRef   = AST::createVariableReference (emitEvents, currentTime);

                loopBlock.addStatement (AST::createFunctionCall (loopBlock.context, emitEventFunction, AST::createGetElement (loopBlock, eventBufferRef, readPosRef)));
                loopBlock.addStatement (AST::createPreInc (loopBlock.context, readPosRef));
                loopBlock.addStatement (AST::createPreDec (loopBlock.context, bufferEntriesRef));

                auto& loopStatement = loopBlock.allocateChild<AST::LoopStatement>();
                auto& bufferEntriesCondition = AST::createBinaryOp (mainBlock.context, AST::BinaryOpTypeEnum::Enum::greaterThan, bufferEntriesRef, mainBlock.context.allocator.createConstantInt32 (0));
                auto& readEventTime = AST::createGetStructMember (mainBlock, AST::createGetElement (mainBlock, eventBufferRef, readPosRef), "eventTime");
                auto& eventTimeMatchesCondition = AST::createBinaryOp (mainBlock.context, AST::BinaryOpTypeEnum::Enum::equals, readEventTime, currentTimeRef);

                loopStatement.condition.referTo (AST::createBinaryOp (mainBlock.context, AST::BinaryOpTypeEnum::Enum::logicalAnd, bufferEntriesCondition, eventTimeMatchesCondition));
                loopStatement.body.referTo (loopBlock);
                mainBlock.addStatement (loopStatement);
            }

            // run
            {
                auto& fn = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), "main");
                auto& mainBlock = *fn.getMainBlock();

                auto& currentTimeRef   = AST::createVariableReference (fn, currentTime);

                auto& loopBlock = mainBlock.allocateChild<AST::ScopeBlock>();
                loopBlock.addStatement (loopBlock.allocateChild<AST::Advance>());
                loopBlock.addStatement (AST::createFunctionCall (loopBlock.context, emitEvents));
                loopBlock.addStatement (AST::createPreInc (loopBlock.context, currentTimeRef));
                loopBlock.addStatement (loopBlock.allocateChild<AST::Advance>());

                auto& loopStatement = mainBlock.allocateChild<AST::LoopStatement>();
                loopStatement.body.referTo (loopBlock);

                mainBlock.addStatement (loopStatement);
            }

            return AST::createReference (graph, processor);
        }

        const AST::EndpointDeclaration& getSourceEndpointDeclaration (AST::Connection& connection)
        {
            return getSourceEndpointDeclaration (connection.sources.front());
        }

        const AST::EndpointDeclaration& getSourceEndpointDeclaration (AST::Property& source)
        {
            if (auto conn = AST::castToSkippingReferences<AST::Connection> (source))
                return getSourceEndpointDeclaration (*conn);

            if (auto element = AST::castToSkippingReferences<AST::GetElement> (source))
            {
                if (auto value = AST::castToSkippingReferences <AST::ValueBase> (element->parent))
                {
                    auto endpoints = GraphConnectivityModel::getUsedEndpointInstances (*value);

                    CMAJ_ASSERT (endpoints.size() == 1);

                    return *endpoints[0]->getEndpoint (true);
                }

                return *AST::castToSkippingReferences<AST::EndpointInstance> (element->parent)->getEndpoint (true);
            }

            if (auto value = AST::castToSkippingReferences<AST::ValueBase> (source))
            {
                auto endpointInstances = GraphConnectivityModel::getUsedEndpointInstances (*value);
                return *endpointInstances[0]->getEndpoint (true);
            }

            return *AST::castToSkippingReferences<AST::EndpointInstance> (source)->getEndpoint (true);
        }

        AST::ObjectRefVector<const AST::TypeBase> getConnectionDataTypes (AST::Connection& connection)
        {
            AST::ObjectRefVector<const AST::TypeBase> sourceTypes, destTypes;

            for (auto& source : connection.sources)
            {
                if (auto cast = AST::castTo<AST::Cast> (source))
                {
                    if (auto t = cast->getResultType())
                    {
                        addUnique (sourceTypes, *t);
                        continue;
                    }
                }

                auto& endpointDeclaration = getSourceEndpointDeclaration (source);
                auto dataTypes = endpointDeclaration.dataTypes.getAsListProperty()->getAsObjectTypeList<AST::TypeBase>();
                addUnique (sourceTypes, dataTypes);
            }

            for (auto& dest : connection.dests)
            {
                auto& endpointDeclaration = getSourceEndpointDeclaration (dest);
                auto dataTypes = endpointDeclaration.dataTypes.getAsListProperty()->getAsObjectTypeList<AST::TypeBase>();
                addUnique (destTypes, dataTypes);
            }

            return intersection (sourceTypes, destTypes);
        }

        void addUnique (AST::ObjectRefVector<const AST::TypeBase>& list, const AST::TypeBase& item)
        {
            bool duplicate = false;

            for (auto existingItem : list)
                if (item.isSameType (existingItem, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                    duplicate = true;

            if (! duplicate)
                list.push_back (item);
        }

        void addUnique (AST::ObjectRefVector<const AST::TypeBase>& list, AST::ObjectRefVector<AST::TypeBase>& itemsToAdd)
        {
            for (auto item : itemsToAdd)
            {
                bool duplicate = false;

                for (auto existingItem : list)
                    if (item->isSameType (existingItem, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                        duplicate = true;

                if (! duplicate)
                    list.push_back (item);
            }
        }

        AST::ObjectRefVector<const AST::TypeBase> intersection (AST::ObjectRefVector<const AST::TypeBase>& a, AST::ObjectRefVector<const AST::TypeBase>& b)
        {
            AST::ObjectRefVector<const AST::TypeBase> result;

            for (auto a1 : a)
            {
                bool matched = false;

                for (auto b1 : b)
                    if (a1->isSameType (b1, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                        matched = true;

                if (matched)
                    result.push_back (a1);
            }

            return result;
        }
    };

    SimplifyConnectionPass (program).visitObject (program.rootNamespace);
}

}
