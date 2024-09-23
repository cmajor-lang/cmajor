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

inline AST::ProcessorBase& cloneProcessor (AST::ProcessorBase& originalProcessor, std::string_view newName, bool isTopLevelProcessor)
{
    auto& parentNamespace = originalProcessor.getParentNamespace();
    auto& blockProcessor = parentNamespace.allocateChild<AST::Graph>();

    parentNamespace.subModules.addReference (blockProcessor);

    blockProcessor.name = blockProcessor.getStringPool().get (newName);

    auto& graphNode = blockProcessor.allocateChild<AST::GraphNode>();
    graphNode.nodeName = graphNode.getStrings().processor;
    graphNode.processorType.createReferenceTo (originalProcessor);
    blockProcessor.nodes.addReference (graphNode);

    auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (blockProcessor);

    // Populate stateType
    {
        stateType.addMember (blockProcessor.getStrings()._state, EventHandlerUtilities::getOrCreateStateStructType (originalProcessor));

        for (auto e : originalProcessor.getInputEndpoints (true))
            if (e->isValue())
                ValueStreamUtilities::addStateStructMember (blockProcessor, stateType, e, isTopLevelProcessor);
    }

    for (auto p : originalProcessor.endpoints)
        blockProcessor.endpoints.addClone (p);

    for (auto eventEndpoint : blockProcessor.getEventEndpoints (true))
    {
        if (eventEndpoint->isOutput())
        {
            for (auto& type : eventEndpoint->dataTypes.iterateAs<AST::TypeBase>())
            {
                auto functionName = blockProcessor.getStringPool().get (EventHandlerUtilities::getWriteEventFunctionName (eventEndpoint));

                if (auto sourceFunction = EventHandlerUtilities::findEventFunctionForType (originalProcessor, functionName, type, eventEndpoint->isArray()))
                {
                    auto& writeEventFn = AST::createFunctionInModule (blockProcessor,
                                                                      blockProcessor.context.allocator.voidType,
                                                                      functionName);

                    AST::addFunctionParameter (writeEventFn, stateType, writeEventFn.getStrings()._state, true, false);

                    if (! type.isVoid())
                    {
                        auto valueParam = AST::addFunctionParameter (writeEventFn, type, writeEventFn.getStrings().value, true, true);

                        if (isTopLevelProcessor)
                        {
                            auto& mainBlock = *writeEventFn.getMainBlock();

                            auto& writeToEndpoint = mainBlock.allocateChild<AST::WriteToEndpoint>();
                            writeToEndpoint.target.createReferenceTo (eventEndpoint);
                            writeToEndpoint.value.referTo (valueParam);
                            mainBlock.addStatement (writeToEndpoint);
                        }
                    }
                    else
                    {
                        if (isTopLevelProcessor)
                        {
                            auto& mainBlock = *writeEventFn.getMainBlock();

                            auto& writeToEndpoint = mainBlock.allocateChild<AST::WriteToEndpoint>();
                            writeToEndpoint.target.createReferenceTo (eventEndpoint);
                            mainBlock.addStatement (writeToEndpoint);
                        }
                    }

                    EventHandlerUtilities::addUpcastCall (*sourceFunction, writeEventFn, false);
                }
            }
        }
        else
        {
            for (auto& type : eventEndpoint->dataTypes.iterateAs<AST::TypeBase>())
            {
                bool isArray = eventEndpoint->isArray();

                if (auto targetFunction = EventHandlerUtilities::findEventFunctionForType (originalProcessor, eventEndpoint->name, type, isArray))
                {
                    // Create event forwarding function
                    auto& forwardEvent = AST::createFunctionInModule (blockProcessor, blockProcessor.context.allocator.voidType, eventEndpoint->name);

                    AST::VariableRefGenerator indexParam;

                    auto stateParam = AST::addFunctionParameter (forwardEvent, stateType, forwardEvent.getStrings()._state, true, false);

                    if (isArray)
                        indexParam = AST::addFunctionParameter (forwardEvent, targetFunction->context.allocator.int32Type, "index", false, false);

                    forwardEvent.isEventHandler = true;
                    auto& mainBlock = *forwardEvent.getMainBlock();

                    if (type.isVoid())
                    {
                        auto& functionCall = isArray ? AST::createFunctionCall (mainBlock.context,
                                                                                *targetFunction,
                                                                                AST::createGetStructMember (mainBlock.context, stateParam, "_state"),
                                                                                indexParam)
                                                     : AST::createFunctionCall (mainBlock.context,
                                                                                *targetFunction,
                                                                                AST::createGetStructMember (mainBlock.context, stateParam, "_state"));

                        mainBlock.addStatement (functionCall);
                    }
                    else
                    {
                        auto eventParam = AST::addFunctionParameter (forwardEvent, type,      forwardEvent.getStrings().value, false, false);

                        auto& functionCall = isArray ? AST::createFunctionCall (mainBlock.context,
                                                                                *targetFunction,
                                                                                AST::createGetStructMember (mainBlock.context, stateParam, "_state"),
                                                                                indexParam,
                                                                                eventParam) :
                                                       AST::createFunctionCall (mainBlock.context,
                                                                                *targetFunction,
                                                                                AST::createGetStructMember (mainBlock.context, stateParam, "_state"),
                                                                                eventParam);


                        mainBlock.addStatement (functionCall);
                    }
                }
            }
        }
    }

    return blockProcessor;
}


inline AST::ProcessorBase& createBlockTransformProcessor (AST::ProcessorBase& originalProcessor, uint32_t maxBlockSize)
{
    auto& blockProcessor = cloneProcessor (originalProcessor, originalProcessor.getName(), true);
    originalProcessor.setName (originalProcessor.getStringPool().get ("_" + std::string (originalProcessor.getName())));

    auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (blockProcessor);

    stateType.addMember (stateType.getStringPool().get (EventHandlerUtilities::getCurrentFrameStateMemberName()),
                         blockProcessor.context.allocator.int32Type,
                         0);

    ValueStreamUtilities::addEventStreamSupport (blockProcessor);

    auto& ioType = EventHandlerUtilities::getOrCreateIoStructType (blockProcessor);

    // Populate ioType
    {
        auto& wrappedIoType = EventHandlerUtilities::getOrCreateIoStructType (originalProcessor);

        for (size_t i = 0; i < wrappedIoType.getFixedSizeAggregateNumElements(); i++)
        {
            auto&  arrayType = AST::createArrayOfType (blockProcessor,
                                                       *wrappedIoType.getAggregateElementType (i),
                                                       static_cast<int32_t> (maxBlockSize));

            ioType.addMember (wrappedIoType.memberNames[i].getAsStringProperty()->get(), arrayType);
        }
    }

    // _initialise
    {
        auto& initialise = AST::createExportedFunction (blockProcessor,
                                                        blockProcessor.context.allocator.voidType,
                                                        blockProcessor.getStrings().systemInitFunctionName);

        auto stateParam       = AST::addFunctionParameter (initialise, stateType, initialise.getStrings()._state, true, false);
        auto processorIDParam = AST::addFunctionParameter (initialise, initialise.context.allocator.int32Type,   blockProcessor.getStrings().initFnProcessorIDParamName, true);
        auto sessionIDParam   = AST::addFunctionParameter (initialise, initialise.context.allocator.int32Type,   blockProcessor.getStrings().initFnSessionIDParamName);
        auto frequencyParam   = AST::addFunctionParameter (initialise, initialise.context.allocator.float64Type, blockProcessor.getStrings().initFnFrequencyParamName);

        auto mainBlock = initialise.getMainBlock();

        if (auto initFn = originalProcessor.findSystemInitFunction())
            mainBlock->addStatement (AST::createFunctionCall (initialise.context, *initFn,
                                                              AST::createGetStructMember (initialise.context, stateParam, "_state"),
                                                              processorIDParam,
                                                              sessionIDParam,
                                                              frequencyParam));
    }

    // _advance function definition
    {
        auto& advance = AST::createExportedFunction (blockProcessor,
                                                     blockProcessor.context.allocator.voidType,
                                                     blockProcessor.getStrings().systemAdvanceFunctionName);

        auto stateParam  = AST::addFunctionParameter (advance, stateType, advance.getStrings()._state, true, false);
        auto ioParam     = AST::addFunctionParameter (advance, ioType,    advance.getStrings()._io, true, false);
        auto framesParam = AST::addFunctionParameter (advance, blockProcessor.context.allocator.int32Type, advance.getStrings()._frames, false, false);

        auto& mainBlock = *advance.getMainBlock();

        auto& currentFrame = AST::createGetStructMember (blockProcessor, stateParam,
                                                         EventHandlerUtilities::getCurrentFrameStateMemberName());

        auto& loop = advance.allocateChild<AST::LoopStatement>();
        auto& loopBlock = loop.allocateChild<AST::ScopeBlock>();

        loop.body.referTo (loopBlock);

        auto& breakStatement = loopBlock.allocateChild<AST::BreakStatement>();
        breakStatement.targetBlock.referTo (loop);

        auto& ifStatement = AST::createIfStatement (loopBlock.context,
                                                    AST::createBinaryOp (loopBlock.context,
                                                                         AST::BinaryOpTypeEnum::Enum::equals,
                                                                         currentFrame,
                                                                         framesParam),
                                                    breakStatement);

        loopBlock.addStatement (ifStatement);

        if (auto updateRampsBlock = ValueStreamUtilities::addUpdateRampsCall (blockProcessor, loopBlock, stateParam))
        {
            for (auto input : blockProcessor.getInputEndpoints (true))
            {
                if (input->isValue())
                {
                    auto& block = ValueStreamUtilities::dataTypeCanBeInterpolated (input) ? *updateRampsBlock : loopBlock;

                    block.addStatement (AST::createAssignment (block.context,
                                                               ValueStreamUtilities::getStateStructMember (block.context, input, AST::createGetStructMember (block.context, stateParam, "_state"), false),
                                                               ValueStreamUtilities::getStateStructMember (block.context, input, stateParam, true)));
                }
            }
        }

        auto& ioVariable = AST::createLocalVariable (loopBlock, "ioCopy", EventHandlerUtilities::getOrCreateIoStructType (originalProcessor), {});

        // Populate input streams
        for (auto input : blockProcessor.getInputEndpoints (true))
            if (input->isStream())
                loopBlock.addStatement (AST::createAssignment (loopBlock.context,
                                                               AST::createGetStructMember (loopBlock,
                                                                                           AST::createVariableReference (loopBlock.context, ioVariable),
                                                                                           input->getName()),
                                                               AST::createGetElement (loopBlock,
                                                                                      AST::createGetStructMember (loopBlock, ioParam, input->getName()),
                                                                                      currentFrame)));

        // Call advance
        if (auto mainFunction = originalProcessor.findMainFunction())
        {
            loopBlock.addStatement (AST::createFunctionCall (loopBlock.context,
                                                             *mainFunction,
                                                             AST::createGetStructMember (loopBlock.context, stateParam, "_state"),
                                                             AST::createVariableReference (loopBlock.context, ioVariable)));
        }

        // Populate outputs
        for (auto output : blockProcessor.getOutputEndpoints (true))
        {
            if (output->isStream())
                loopBlock.addStatement (AST::createAssignment (loopBlock.context,
                                                               AST::createGetElement (loopBlock,
                                                                                      AST::createGetStructMember (loopBlock, ioParam, output->getName()),
                                                                                      currentFrame),
                                                               AST::createGetStructMember (loopBlock, AST::createVariableReference (loopBlock.context, ioVariable), output->getName())));
        }

        loopBlock.addStatement (AST::createPreInc (loopBlock.context, currentFrame));
        mainBlock.addStatement (loop);

        for (auto output : blockProcessor.getOutputEndpoints (true))
            if (output->isValue())
            {
                auto memberName = StreamUtilities::getEndpointStateMemberName (output);

                mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                               AST::createGetStructMember (mainBlock, stateParam, memberName),
                                                               AST::createGetStructMember (mainBlock, AST::createGetStructMember (mainBlock.context, stateParam, "_state"), memberName)));
            }

        mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                       currentFrame,
                                                       mainBlock.context.allocator.createConstantInt32 (0)));
    }

    return blockProcessor;
}

}
