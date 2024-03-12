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

using EndpointInterpolationStrategy = std::unordered_map<const AST::EndpointDeclaration*, AST::InterpolationTypeEnum::Enum>;

struct OversamplingTransformation
{
    //==============================================================================
    struct Interpolator
    {
        Interpolator (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f)
            : processor (p), endpoint (e), factor (f), frameType (endpoint.getSingleDataType()), arraySize (endpoint.getArraySize())
        {
        }

        virtual ~Interpolator() = default;

        virtual void populateReset (AST::ScopeBlock& block, AST::ValueBase& stateParam) = 0;
        virtual void addInputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& source) = 0;
        virtual void getInterpolatedOutputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& target) = 0;

        std::string getEndpointStateValuesName() const
        {
            return "_" + std::string (endpoint.getName());
        }

        const AST::TypeBase& convertTypeWithArraySize (const AST::TypeBase& t)
        {
            if (! arraySize)
                return t;

            return AST::createArrayOfType (processor, t, *arraySize);
        }

        AST::ValueBase& getArrayElement (const AST::ScopeBlock& block, AST::ValueBase& v, int i)
        {
            if (! arraySize)
                return v;

            return AST::createGetElement (block.context, v, i);
        }

        int getArrayElements()
        {
            return arraySize ? *arraySize : 1;
        }

        AST::ProcessorBase& processor;
        const AST::EndpointDeclaration& endpoint;
        const int32_t factor;
        const AST::TypeBase& frameType;
        std::optional<int> arraySize;
    };

    //==============================================================================
    struct LinearUpsampler  : public Interpolator
    {
        LinearUpsampler (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f) : Interpolator (p, e, f)
        {
            auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);

            stateType.addMember (getEndpointStateValuesName(), convertTypeWithArraySize (frameType));
            stateType.addMember (getEndpointStateStepName(), convertTypeWithArraySize (frameType));
        }

        void populateReset (AST::ScopeBlock& block, AST::ValueBase& stateParam) override
        {
            AST::addAssignment (block,
                                AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()),
                                block.context.allocator.createConstantFloat32 (0.0f));

            AST::addAssignment (block,
                                AST::createGetStructMember (block.context, stateParam, getEndpointStateStepName()),
                                block.context.allocator.createConstantFloat32 (0.0f));
        }

        void addInputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& source) override
        {
            for (int i = 0; i < getArrayElements(); i++)
            {
                auto& diff = AST::createSubtract (block.context,
                                                  getArrayElement (block, source, i),
                                                  getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i));

                auto& step = AST::createMultiply (block.context, diff, block.context.allocator.createConstantFloat32 (1.0f / (float) factor));

                block.addStatement (AST::createAssignment (block.context,
                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateStepName()), i),
                                                           step));
            }
        }

        void getInterpolatedOutputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& target) override
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       target,
                                                       AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName())));

            for (int i = 0; i < getArrayElements(); i++)
            {
                block.addStatement (AST::createAssignment (block.context,
                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i),
                                                           AST::createAdd (block.context,
                                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i),
                                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateStepName()), i))));
            }

        }

    private:
        std::string getEndpointStateStepName() const
        {
            return "_" + std::string (endpoint.getName()) + "Step";
        }
    };

    //==============================================================================
    struct ValueLatch  : public Interpolator
    {
        ValueLatch (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f) : Interpolator (p, e, f)
        {
            auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);

            stateType.addMember (getEndpointStateValuesName(), convertTypeWithArraySize (frameType));
        }

        void populateReset (AST::ScopeBlock& block, AST::ValueBase& stateParam) override
        {
            AST::addAssignment (block,
                                AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()),
                                block.context.allocator.createConstantFloat32 (0.0f));
        }

        void addInputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& source) override
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()),
                                                       source));
        }

        void getInterpolatedOutputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& target) override
        {
            block.addStatement (AST::createAssignment (block.context,
                                                       target,
                                                       AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName())));
        }
    };

    //==============================================================================
    struct SincBase : public Interpolator
    {
        SincBase (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f) : Interpolator (p, e, f)
        {
            CMAJ_ASSERT (frameType.isFloatOrVectorOfFloat());

            sincStruct = getOrCreateSincStruct();
            interpolateFn = getOrCreateInterpolateFn();
            decimateFn = getOrCreateDecimateFn();

            interpolationStages = static_cast<int> (0.5 + std::log (f) / std::log (2.0));
        }

        void populateReset (AST::ScopeBlock& block, AST::ValueBase& stateParam) override
        {
            auto& zeroValue = block.context.allocate<AST::ConstantAggregate>();
            zeroValue.type.createReferenceTo (getOrCreateSincStruct());

            for (int i = 0; i < interpolationStages; i++)
            {
                auto& filterMember = AST::createGetElement (block, AST::createGetStructMember (block.context, stateParam, getFilterStateMemberName()), i);

                AST::addAssignment (block, filterMember, zeroValue);
            }
        }

        AST::PooledString getFrameTypeName (const std::string& prefix)
        {
            CMAJ_ASSERT (frameType.isPrimitive() || frameType.isVector());

            std::string name = prefix;

            if (auto vectorType = frameType.getAsVectorType())
                name = name + std::string (vectorType->getElementType().getName()) + "_" + std::to_string (vectorType->getVectorSize());
            else
                name = name + std::string (frameType.getName());

            return processor.getStringPool().get (name);
        }

        AST::TypeBase& getOrCreateSincStruct()
        {
            auto typeName = getFrameTypeName ("_Sinc_");

            if (auto t = processor.findStruct (typeName))
                return *t;

            auto& t = AST::createStruct (processor, typeName);

            t.addMember ("a0", frameType);
            t.addMember ("a1", frameType);
            t.addMember ("a2", frameType);
            t.addMember ("a3", frameType);

            t.addMember ("b0", frameType);
            t.addMember ("b1", frameType);
            t.addMember ("b2", frameType);
            t.addMember ("b3", frameType);

            return t;
        }

        AST::ValueBase& createMultiplyAdd (AST::ScopeBlock& block, std::string_view name, AST::ValueBase& filter, std::string_view a, AST::ValueBase& b, std::string_view c, float f)
        {
            auto& sub = AST::createSubtract (block.context, b, AST::createGetStructMember (block.context, filter, c));
            auto& multiply = AST::createMultiply (block.context, sub, block.context.allocator.createConstantFloat32 (f));

            return AST::createLocalVariableRef (block, name, frameType,
                                                AST::createAdd (block.context, AST::createGetStructMember (block.context, filter, a), multiply));
        }

        AST::Function& getOrCreateInterpolateFn()
        {
            auto functionName = getFrameTypeName ("_SincInterpolate_");

            if (auto fn = processor.findFunction (functionName, 4))
                return *fn;

            auto& fn = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), functionName);

            auto filterParam  = AST::addFunctionParameter (fn, getOrCreateSincStruct(), "filter", true, false);
            auto inParam      = AST::addFunctionParameter (fn, frameType, "in", false, false);
            auto out1Param    = AST::addFunctionParameter (fn, frameType, "out1", true, false);
            auto out2Param    = AST::addFunctionParameter (fn, frameType, "out2", true, false);

            auto& mainBlock = *fn.getMainBlock();

            auto& a1 = createMultiplyAdd (mainBlock, "a1", filterParam, "a0", inParam,  "a1", 0.039151597734460045f);
            auto& a2 = createMultiplyAdd (mainBlock, "a2", filterParam, "a1", a1,       "a2", 0.3026468483284934f);
            auto& a3 = createMultiplyAdd (mainBlock, "a3", filterParam, "a2", a2,       "a3", 0.6746159185469639f);

            auto& b1 = createMultiplyAdd (mainBlock, "b1", filterParam, "b0", inParam,  "b1", 0.1473771136010466f);
            auto& b2 = createMultiplyAdd (mainBlock, "b2", filterParam, "b1", b1,       "b2",  0.48246854276970014f);
            auto& b3 = createMultiplyAdd (mainBlock, "b3", filterParam, "b2", b2,       "b3", 0.8830050257693731f);

            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a0"), inParam);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a1"), a1);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a2"), a2);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a3"), a3);

            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b0"), inParam);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b1"), b1);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b2"), b2);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b3"), b3);

            AST::addAssignment (mainBlock, out1Param, a3);
            AST::addAssignment (mainBlock, out2Param, b3);

            return fn;
        }

        AST::Function& getOrCreateDecimateFn()
        {
            auto functionName = getFrameTypeName ("_SincDecimate_");

            if (auto fn = processor.findFunction (functionName, 4))
                return *fn;

            auto& fn = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), functionName);

            auto filterParam  = AST::addFunctionParameter (fn, getOrCreateSincStruct(), "state", true, false);
            auto in1Param     = AST::addFunctionParameter (fn, frameType, "in1", false, false);
            auto in2Param     = AST::addFunctionParameter (fn, frameType, "in2", false, false);
            auto outParam     = AST::addFunctionParameter (fn, frameType, "out", true, false);

            auto& mainBlock = *fn.getMainBlock();

            auto& a1 = createMultiplyAdd (mainBlock, "a1", filterParam, "a0", in2Param, "a1", 0.039151597734460045f);
            auto& a2 = createMultiplyAdd (mainBlock, "a2", filterParam, "a1", a1,       "a2", 0.3026468483284934f);
            auto& a3 = createMultiplyAdd (mainBlock, "a3", filterParam, "a2", a2,       "a3", 0.6746159185469639f);

            auto& b1 = createMultiplyAdd (mainBlock, "b1", filterParam, "b0", in1Param, "b1", 0.1473771136010466f);
            auto& b2 = createMultiplyAdd (mainBlock, "b2", filterParam, "b1", b1,       "b2",  0.48246854276970014f);
            auto& b3 = createMultiplyAdd (mainBlock, "b3", filterParam, "b2", b2,       "b3", 0.8830050257693731f);

            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a0"), in2Param);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a1"), a1);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a2"), a2);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "a3"), a3);

            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b0"), in1Param);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b1"), b1);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b2"), b2);
            AST::addAssignment (mainBlock, AST::createGetStructMember (mainBlock.context, filterParam, "b3"), b3);

            auto& sum = AST::createAdd (mainBlock.context, a3, b3);
            auto& product = AST::createMultiply (mainBlock.context, sum, mainBlock.context.allocator.createConstantFloat32 (0.5f));

            AST::addAssignment (mainBlock, outParam, product);

            return fn;
        }

    protected:
        std::string getIndexStateMemberName() const
        {
            return getEndpointStateValuesName() + "_index";
        }

        std::string getFilterStateMemberName() const
        {
            return getEndpointStateValuesName() + "_filter";
        }

        int interpolationStages;
        ptr<AST::TypeBase> sincStruct;
        ptr<AST::Function> interpolateFn, decimateFn;
    };

    //==============================================================================
    struct SincUpsampler   : public SincBase
    {
        SincUpsampler (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f) : SincBase (p, e, f)
        {
            auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);

            auto& arrayType = AST::createArrayOfType (processor, frameType, factor);
            stateType.addMember (getEndpointStateValuesName(), convertTypeWithArraySize (arrayType));
            stateType.addMember (getFilterStateMemberName(), convertTypeWithArraySize (AST::createArrayOfType (processor, *sincStruct, interpolationStages)));
            stateType.addMember (getIndexStateMemberName(), p.context.allocator.createInt32Type());
        }

        void addInputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& source) override
        {
            int stage = 0;
            int step = factor / 2;

            while (stage < interpolationStages)
            {
                int frame = 0;

                while (frame < factor)
                {
                    int arrayElements = getArrayElements();

                    for (int i = 0; i < arrayElements; i++)
                    {
                        auto& structMember  = getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i);
                        auto& filterMember = getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getFilterStateMemberName()), i);

                        auto& target1 = AST::createGetElement (block.context, structMember, frame);
                        auto& target2 = AST::createGetElement (block.context, structMember, frame + step);
                        auto& filter  = AST::createGetElement (block.context, filterMember, stage);

                        block.addStatement (AST::createFunctionCall (block.context,
                                                                     *interpolateFn,
                                                                     filter,
                                                                     (stage == 0) ? getArrayElement (block, source, i) : target1,
                                                                     target1,
                                                                     target2));
                    }

                    frame += (step * 2);
                }

                stage++;
                step /= 2;
            }

            AST::addAssignment (block,
                                AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName()),
                                block.context.allocator.createConstantInt32 (0));
        }


        void getInterpolatedOutputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& target) override
        {
            int arrayElements = getArrayElements();

            for (int i = 0; i < arrayElements; i++)
            {
                AST::addAssignment (block,
                                    getArrayElement (block, target, i),
                                    AST::createGetElement (block.context,
                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i),
                                                           AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName())));
            }

            block.addStatement (AST::createPreInc (block.context,
                                                   AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName())));
        }
    };

    //==============================================================================
    struct SincDownsampler   : public SincBase
    {
        SincDownsampler (AST::ProcessorBase& p, const AST::EndpointDeclaration& e, int32_t f) : SincBase (p, e, f)
        {
            auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);

            auto& arrayType = AST::createArrayOfType (processor, frameType, factor);
            stateType.addMember (getEndpointStateValuesName(), convertTypeWithArraySize (arrayType));
            stateType.addMember (getFilterStateMemberName(), convertTypeWithArraySize (AST::createArrayOfType (processor, *sincStruct, interpolationStages)));
            stateType.addMember (getIndexStateMemberName(), p.context.allocator.createInt32Type());
        }

        void addInputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& source) override
        {
            int arrayElements = getArrayElements();

            for (int i = 0; i < arrayElements; i++)
            {
                AST::addAssignment (block,
                                    AST::createGetElement (block.context,
                                                           getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i),
                                                           AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName())),
                                    getArrayElement (block, source, i));
            }

            block.addStatement (AST::createPreInc (block.context,
                                                   AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName())));
        }

        void getInterpolatedOutputValue (AST::ScopeBlock& block, AST::ValueBase& stateParam, AST::ValueBase& target) override
        {
            int stage = 0;
            int step = 1;

            while (stage < interpolationStages)
            {
                int frame = 0;

                while (frame < factor)
                {
                    int arrayElements = getArrayElements();

                    for (int i = 0; i < arrayElements; i++)
                    {
                        auto& structMember  = getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getEndpointStateValuesName()), i);
                        auto& filterMember = getArrayElement (block, AST::createGetStructMember (block.context, stateParam, getFilterStateMemberName()), i);

                        auto& source1 = AST::createGetElement (block.context, structMember, frame);
                        auto& source2 = AST::createGetElement (block.context, structMember, frame + step);
                        auto& filter  = AST::createGetElement (block.context, filterMember, stage);

                        block.addStatement (AST::createFunctionCall (block.context,
                                                                     *decimateFn,
                                                                     filter,
                                                                     source1,
                                                                     source2,
                                                                     (stage == (interpolationStages - 1)) ? getArrayElement (block, target, i) : source1));
                    }

                    frame += (step * 2);
                }

                stage++;
                step *= 2;
            }

            AST::addAssignment (block,
                                AST::createGetStructMember (block.context, stateParam, getIndexStateMemberName()),
                                block.context.allocator.createConstantInt32 (0));
        }
    };

    //==============================================================================
    static std::unique_ptr<Interpolator> buildInterpolator (AST::ProcessorBase& processor,
                                                            AST::EndpointDeclaration& endpoint,
                                                            AST::InterpolationTypeEnum::Enum strategy,
                                                            int32_t oversampleFactor, int32_t undersampleFactor)
    {
        if (endpoint.isOutput())
            std::swap (oversampleFactor, undersampleFactor);

        if (oversampleFactor > 1)
        {
            switch (strategy)
            {
                case AST::InterpolationTypeEnum::Enum::fast:
                case AST::InterpolationTypeEnum::Enum::linear:  return std::make_unique<LinearUpsampler> (processor, endpoint, oversampleFactor);

                case AST::InterpolationTypeEnum::Enum::none:
                case AST::InterpolationTypeEnum::Enum::latch:   return std::make_unique<ValueLatch> (processor, endpoint, oversampleFactor);

                case AST::InterpolationTypeEnum::Enum::best:
                case AST::InterpolationTypeEnum::Enum::sinc:    return std::make_unique<SincUpsampler> (processor, endpoint, oversampleFactor);

                default:
                    CMAJ_ASSERT_FALSE;
            }
        }
        else
        {
            switch (strategy)
            {
                case AST::InterpolationTypeEnum::Enum::none:
                case AST::InterpolationTypeEnum::Enum::fast:
                case AST::InterpolationTypeEnum::Enum::latch:
                case AST::InterpolationTypeEnum::Enum::linear:  return std::make_unique<ValueLatch> (processor, endpoint, undersampleFactor);

                case AST::InterpolationTypeEnum::Enum::best:
                case AST::InterpolationTypeEnum::Enum::sinc:    return std::make_unique<SincDownsampler> (processor, endpoint, undersampleFactor);

                default:
                    CMAJ_ASSERT_FALSE;
            }
        }

        return {};
    }

    //==============================================================================
    static AST::ProcessorBase& buildOversamplingTransform (AST::ProcessorBase& originalProcessor,
                                                           const EndpointInterpolationStrategy& endpointInterpolationStrategy,
                                                           int32_t oversampleFactor, int32_t undersampleFactor)
    {
        auto hasIndexMember = EventHandlerUtilities::getOrCreateStateStructType (originalProcessor).hasMember (EventHandlerUtilities::getInstanceIndexMemberName());
        auto hasIdParameter = EventHandlerUtilities::getOrCreateStateStructType (originalProcessor).hasMember (EventHandlerUtilities::getIdMemberName());

        auto& processor = cloneProcessor (originalProcessor, "_src_" + std::string (originalProcessor.getName()), false);
        auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);
        auto& ioType = EventHandlerUtilities::getOrCreateIoStructType (processor);

        // Populate ioType
        {
            auto& wrappedIoType = EventHandlerUtilities::getOrCreateIoStructType (originalProcessor);

            for (size_t i = 0; i < wrappedIoType.getFixedSizeAggregateNumElements(); ++i)
                ioType.addMember (wrappedIoType.getMemberName (i), *wrappedIoType.getAggregateElementType (i));
        }

        // Add value types to state
        for (auto& endpoint : processor.endpoints.iterateAs<AST::EndpointDeclaration>())
        {
            if (endpoint.isValue())
            {
                auto stateMemberName = cmaj::StreamUtilities::getEndpointStateMemberName (endpoint);

                if (! stateType.hasMember (stateMemberName))
                    stateType.addMember (stateMemberName, endpoint.getSingleDataType());
            }
        }

        if (hasIndexMember)
            stateType.addMember (EventHandlerUtilities::getInstanceIndexMemberName(), processor.context.allocator.createInt32Type());

        if (hasIdParameter)
            stateType.addMember (EventHandlerUtilities::getIdMemberName(), processor.context.allocator.createInt32Type());

        // _initialise
        auto originalInitFunction = originalProcessor.findSystemInitFunction();

        if (originalInitFunction || hasIndexMember || hasIdParameter)
        {
            auto& initialise      = AST::createFunctionInModule (processor, processor.context.allocator.voidType, processor.getStrings().systemInitFunctionName);
            auto stateParam       = AST::addFunctionParameter (initialise, stateType, "_state", true, false);
            auto processorIDParam = AST::addFunctionParameter (initialise, initialise.context.allocator.int32Type,   processor.getStrings().initFnProcessorIDParamName, true);
            auto sessionIDParam   = AST::addFunctionParameter (initialise, initialise.context.allocator.int32Type,   processor.getStrings().initFnSessionIDParamName);
            auto frequencyParam   = AST::addFunctionParameter (initialise, initialise.context.allocator.float64Type, processor.getStrings().initFnFrequencyParamName);

            auto mainBlock = initialise.getMainBlock();

            if (hasIndexMember)
                mainBlock->addStatement (AST::createAssignment (initialise.context,
                                                                AST::createGetStructMember (initialise.context, AST::createGetStructMember (initialise.context, stateParam, "_state"), EventHandlerUtilities::getInstanceIndexMemberName()),
                                                                AST::createGetStructMember (initialise.context, stateParam, EventHandlerUtilities::getInstanceIndexMemberName())));

            if (hasIdParameter)
                mainBlock->addStatement (AST::createAssignment (initialise.context,
                                                                AST::createGetStructMember (initialise.context, AST::createGetStructMember (initialise.context, stateParam, "_state"), EventHandlerUtilities::getIdMemberName()),
                                                                AST::createGetStructMember (initialise.context, stateParam, EventHandlerUtilities::getIdMemberName())));

            if (originalInitFunction)
                mainBlock->addStatement (AST::createFunctionCall (initialise.context, *originalInitFunction,
                                                                  AST::createGetStructMember (initialise.context, stateParam, "_state"),
                                                                  processorIDParam,
                                                                  sessionIDParam,
                                                                  frequencyParam));
        }

        // Build interpolators for our stream endpoints
        std::vector<std::unique_ptr<Interpolator>> interpolators;

        for (auto& endpoint : processor.endpoints.iterateAs<AST::EndpointDeclaration>())
            if (endpoint.isStream())
                interpolators.push_back (buildInterpolator (processor,
                                                            endpoint,
                                                            getStrategyForEndpoint (endpointInterpolationStrategy, endpoint, (oversampleFactor > 1)),
                                                            oversampleFactor,
                                                            undersampleFactor));

        // reset() definition
        {
            auto& reset     = AST::createFunctionInModule (processor, processor.context.allocator.voidType, processor.getStrings().resetFunctionName);
            auto stateParam = AST::addFunctionParameter (reset, stateType, "_state", true, false);

            auto& mainBlock = *reset.getMainBlock();

            for (auto& interpolator : interpolators)
                interpolator->populateReset (mainBlock, stateParam);

            if (auto fn = originalProcessor.findResetFunction())
                mainBlock.addStatement (AST::createFunctionCall (mainBlock.context,
                                                                 *fn,
                                                                 AST::createGetStructMember (mainBlock.context, stateParam, "_state")));
        }

        // main() definition
        {
            auto& run = AST::createFunctionInModule (processor, processor.context.allocator.voidType, processor.getStrings().mainFunctionName);
            auto stateParam = AST::addFunctionParameter (run, stateType, "_state", true, false);
            auto ioParam    = AST::addFunctionParameter (run, ioType, "_io", true, false);

            auto& mainBlock = *run.getMainBlock();

            // Copy any input values
            for (auto& endpoint : processor.endpoints.iterateAs<AST::EndpointDeclaration>())
            {
                if (endpoint.isInput && endpoint.isValue())
                {
                    auto memberName = cmaj::StreamUtilities::getEndpointStateMemberName (endpoint);

                    mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                                   AST::createGetStructMember (mainBlock.context, AST::createGetStructMember (mainBlock.context, stateParam, "_state"), memberName),
                                                                   AST::createGetStructMember (mainBlock.context, stateParam, memberName)));
                }
            }

            if (oversampleFactor > 1)
            {
                if (auto wrappedMainFunction = originalProcessor.findMainFunction())
                {
                    // Write inputs
                    for (auto& interpolator : interpolators)
                        if (interpolator->endpoint.isInput)
                            interpolator->addInputValue (mainBlock,
                                                         stateParam,
                                                         AST::createGetStructMember (mainBlock.context, ioParam, interpolator->endpoint.getName()));

                    for (int i = 0; i < oversampleFactor; i++)
                    {
                        auto& wrappedIO = AST::createLocalVariable (mainBlock, "io_" + std::to_string (i), EventHandlerUtilities::getOrCreateIoStructType (originalProcessor), {});

                        for (auto& interpolator : interpolators)
                            if (interpolator->endpoint.isInput)
                                interpolator->getInterpolatedOutputValue (mainBlock,
                                                                          stateParam,
                                                                          AST::createGetStructMember (mainBlock.context,
                                                                                                      AST::createVariableReference (mainBlock.context, wrappedIO),
                                                                                                      interpolator->endpoint.getName()));

                        mainBlock.addStatement (AST::createFunctionCall (mainBlock.context,
                                                                         *wrappedMainFunction,
                                                                         AST::createGetStructMember (mainBlock.context, stateParam, "_state"),
                                                                         AST::createVariableReference (mainBlock.context, wrappedIO)));

                        for (auto& interpolator : interpolators)
                            if (interpolator->endpoint.isOutput())
                                interpolator->addInputValue (mainBlock,
                                                             stateParam,
                                                             AST::createGetStructMember (mainBlock.context,
                                                                                         AST::createVariableReference (mainBlock.context, wrappedIO),
                                                                                         interpolator->endpoint.getName()));
                    }

                    for (auto& interpolator : interpolators)
                        if (interpolator->endpoint.isOutput())
                            interpolator->getInterpolatedOutputValue (mainBlock,
                                                                      stateParam,
                                                                      AST::createGetStructMember (mainBlock.context, ioParam, interpolator->endpoint.getName()));
                }
            }
            else
            {
                // Undersample, use latch for inputs, linear for outputs by default, track the frame as we need to occasionally call the wrapped main() function
                stateType.addMember ("_frame", processor.context.allocator.createInt32Type());

                // Write inputs
                for (auto& interpolator : interpolators)
                    if (interpolator->endpoint.isInput)
                        interpolator->addInputValue (mainBlock,
                                                     stateParam,
                                                     AST::createGetStructMember (mainBlock.context, ioParam, interpolator->endpoint.getName()));

                auto& calcFramesBlock = run.allocateChild<AST::ScopeBlock>();

                auto& ifStatement = AST::createIfStatement (mainBlock.context,
                                                            AST::createBinaryOp (mainBlock.context,
                                                                                 AST::BinaryOpTypeEnum::Enum::equals,
                                                                                 AST::createGetStructMember (mainBlock.context, stateParam, "_frame"),
                                                                                 mainBlock.context.allocator.createConstantInt32 (0)),
                                                            calcFramesBlock);

                // calcFramesBlock
                if (auto wrappedMainFunction = originalProcessor.findMainFunction())
                {
                    auto& wrappedIO = AST::createLocalVariable (calcFramesBlock, "ioCopy", EventHandlerUtilities::getOrCreateIoStructType (originalProcessor), {});

                    for (auto& interpolator : interpolators)
                        if (interpolator->endpoint.isInput)
                            interpolator->getInterpolatedOutputValue (calcFramesBlock, stateParam, AST::createGetStructMember (mainBlock.context,
                                                                                                                               AST::createVariableReference (calcFramesBlock.context, wrappedIO),
                                                                                                                               interpolator->endpoint.getName()));

                    calcFramesBlock.addStatement (AST::createFunctionCall (calcFramesBlock.context,
                                                                           *wrappedMainFunction,
                                                                           AST::createGetStructMember (calcFramesBlock.context, stateParam, "_state"),
                                                                           AST::createVariableReference (calcFramesBlock.context, wrappedIO)));

                    for (auto& interpolator : interpolators)
                        if (interpolator->endpoint.isOutput())
                            interpolator->addInputValue (calcFramesBlock,
                                                         stateParam,
                                                         AST::createGetStructMember (calcFramesBlock.context,
                                                                                     AST::createVariableReference (calcFramesBlock.context, wrappedIO),
                                                                                     interpolator->endpoint.getName()));
                }

                mainBlock.addStatement (ifStatement);

                for (auto& interpolator : interpolators)
                    if (interpolator->endpoint.isOutput())
                        interpolator->getInterpolatedOutputValue (mainBlock,
                                                                  stateParam,
                                                                  AST::createGetStructMember (mainBlock.context, ioParam, interpolator->endpoint.getName()));

                mainBlock.addStatement (AST::createPreInc (mainBlock.context,
                                                           AST::createGetStructMember (mainBlock.context, stateParam, "_frame")));

                mainBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                                AST::createBinaryOp (mainBlock.context,
                                                                                     AST::BinaryOpTypeEnum::Enum::equals,
                                                                                     AST::createGetStructMember (mainBlock.context, stateParam, "_frame"),
                                                                                     mainBlock.context.allocator.createConstantInt32 (undersampleFactor)),
                                                                AST::createAssignment (mainBlock.context,
                                                                                       AST::createGetStructMember (mainBlock.context, stateParam, "_frame"),
                                                                                       mainBlock.context.allocator.createConstantInt32(0))));
            }

            // Copy any output values
            for (auto& endpoint : processor.endpoints.iterateAs<AST::EndpointDeclaration>())
            {
                if (endpoint.isOutput() && endpoint.isValue())
                {
                    auto memberName = cmaj::StreamUtilities::getEndpointStateMemberName (endpoint);

                    mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                                   AST::createGetStructMember (mainBlock.context, stateParam, memberName),
                                                                   AST::createGetStructMember (mainBlock.context, AST::createGetStructMember (mainBlock.context, stateParam, "_state"), memberName)));
                }
            }
        }

        return processor;
    }

    //==============================================================================
    static AST::InterpolationTypeEnum::Enum getStrategyForEndpoint (const EndpointInterpolationStrategy& strategy,
                                                                    AST::EndpointDeclaration& endpoint,
                                                                    bool isOversampling)
    {
        CMAJ_ASSERT (endpoint.isStream());

        for (auto& v : strategy)
        {
            if (v.first->getName() == endpoint.getName())
            {
                if (! endpoint.getSingleDataType().isFloatOrVectorOfFloat() &&
                      v.second != AST::InterpolationTypeEnum::Enum::latch)
                    throwError (endpoint, Errors::invalidInterpolationType());

                return v.second;
            }
        }

        if (! endpoint.getSingleDataType().isFloatOrVectorOfFloat())
            return AST::InterpolationTypeEnum::Enum::latch;

        if (isOversampling)
            return AST::InterpolationTypeEnum::Enum::sinc;

        if (endpoint.isInput)
            return AST::InterpolationTypeEnum::Enum::latch;

        return AST::InterpolationTypeEnum::Enum::linear;
    }
};

}
