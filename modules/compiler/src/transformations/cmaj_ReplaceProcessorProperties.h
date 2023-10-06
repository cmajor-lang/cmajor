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

struct ProcessorPropertyReplacementState
{
    size_t propertiesReplaced = 0;
    ptr<AST::VariableDeclaration> frequencyVariable, sessionIDVariable;
};

static inline ProcessorPropertyReplacementState replaceProcessorProperties (AST::Program& program,
                                                                            double maxFrequency,
                                                                            double frequency,
                                                                            bool useDynamicSampleRate)
{
    struct ReplaceProcessorProperties  : public passes::PassAvoidingGenericFunctionsAndModules
    {
        using super = PassAvoidingGenericFunctionsAndModules;
        using super::visit;

        ReplaceProcessorProperties (AST::Program& p, AST::ProcessorBase& mainProcessor, double maxFreq, double freq)
            : super (p), maxFrequency (maxFreq), frequency (freq)
        {
            state.sessionIDVariable = AST::getOrCreateStateVariable (mainProcessor, "_sessionID", allocator.int32Type,
                                                                     allocator.createConstantInt32 (0));
            state.sessionIDVariable->isExternal = true;

            if (frequency == 0)
            {
                state.frequencyVariable = AST::getOrCreateStateVariable (mainProcessor, "_frequency", allocator.float64Type,
                                                                         allocator.createConstantFloat64 (0));
                state.frequencyVariable->isExternal = true;
            }
        }

        CMAJ_DO_NOT_VISIT_CONSTANTS

        const double maxFrequency, frequency;
        double frequencyMultiplier = 1.0;
        ProcessorPropertyReplacementState state;

        void visit (AST::GraphNode& node) override
        {
            auto old = frequencyMultiplier;
            frequencyMultiplier *= node.getClockMultiplier();
            super::visit (node);
            frequencyMultiplier = old;
        }

        void visit (AST::ProcessorProperty& pp) override
        {
            switch (pp.property.get())
            {
                case AST::ProcessorPropertyEnum::Enum::id:
                    return;

                case AST::ProcessorPropertyEnum::Enum::session:
                    pp.replaceWith (AST::createVariableReference (pp.context, *state.sessionIDVariable));
                    state.propertiesReplaced++;
                    return;

                case AST::ProcessorPropertyEnum::Enum::latency:
                    pp.replaceWith (allocator.createConstantInt32 (pp.context.location, static_cast<int32_t> (pp.getParentProcessor().getLatency())));
                    state.propertiesReplaced++;
                    return;

                case AST::ProcessorPropertyEnum::Enum::frequency:
                    if (frequency != 0)
                    {
                        pp.replaceWith (allocator.createConstantFloat64 (pp.context.location, frequencyMultiplier * frequency));
                    }
                    else
                    {
                        auto& freq = AST::createVariableReference (pp.context, *state.frequencyVariable);
                        auto& multiplier = allocator.createConstantFloat64 (frequencyMultiplier);
                        pp.replaceWith (AST::createMultiply (pp.context, multiplier, freq));
                    }

                    state.propertiesReplaced++;
                    return;

                case AST::ProcessorPropertyEnum::Enum::period:
                    if (frequency != 0)
                    {
                        pp.replaceWith (allocator.createConstantFloat64 (pp.context.location, 1.0 / (frequencyMultiplier * frequency)));
                    }
                    else
                    {
                        auto& freq = AST::createVariableReference (pp.context, *state.frequencyVariable);
                        auto& multiplier = allocator.createConstantFloat64 (frequencyMultiplier);
                        pp.replaceWith (AST::createDivide (pp.context, allocator.createConstantFloat64 (1.0),
                                                                       AST::createMultiply(pp.context, multiplier, freq)));
                    }

                    state.propertiesReplaced++;
                    return;

                case AST::ProcessorPropertyEnum::Enum::maxFrequency:
                    if (maxFrequency != 0)
                    {
                        pp.replaceWith (allocator.createConstantFloat64 (pp.context.location, frequencyMultiplier * maxFrequency));
                    }
                    else
                    {
                        auto& freq = AST::createVariableReference (pp.context, *state.frequencyVariable);
                        auto& multiplier = allocator.createConstantFloat64 (frequencyMultiplier);
                        pp.replaceWith (AST::createMultiply (pp.context, multiplier, freq));
                    }

                    state.propertiesReplaced++;
                    return;

                default:
                    CMAJ_ASSERT_FALSE;
                    break;
            }
        }
    };

    if (useDynamicSampleRate)
        frequency = 0;
    else if (frequency == 0)
        throwError (Errors::unsupportedSampleRate());

    auto& mainProcessor = program.getMainProcessor();
    ReplaceProcessorProperties visitor (program, mainProcessor, maxFrequency, frequency);
    visitor.visitObject (mainProcessor);

    return visitor.state;
}

}
