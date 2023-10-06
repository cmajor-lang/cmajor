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

#include "cmaj_EventHandlerUtilities.h"

namespace cmaj
{

//==============================================================================
struct StreamUtilities
{
    static std::string getValueEndpointStructMemberName (std::string_view endpointName)
    {
        return "_v_" + std::string (endpointName);
    }

    static std::string getEndpointStateMemberName (const AST::EndpointDeclaration& endpointDeclaration)
    {
        if (endpointDeclaration.isValue())
            return getValueEndpointStructMemberName (endpointDeclaration.getName());

        return std::string (endpointDeclaration.getName());
    }

    static AST::Statement& createAccumulateOrAssign (AST::Object& parent, AST::ValueBase& target, AST::Object& source, bool accumulate)
    {
        if (accumulate)
        {
            // Complex streams have been converted into struct types by this point, so we must accumulate the real and imaginary
            // components
            if (target.getResultType()->isStructType())
            {
                auto& block = parent.allocateChild<AST::ScopeBlock>();

                auto& targetReal = parent.allocateChild<AST::GetStructMember>();
                targetReal.object.referTo (target);
                targetReal.member.set (parent.getStrings().real);

                auto& targetImag = parent.allocateChild<AST::GetStructMember>();
                targetImag.object.referTo (target);
                targetImag.member.set (parent.getStrings().imag);

                auto& sourceReal = parent.allocateChild<AST::GetStructMember>();
                sourceReal.object.referTo (source);
                sourceReal.member.set (parent.getStrings().real);

                auto& sourceImag = parent.allocateChild<AST::GetStructMember>();
                sourceImag.object.referTo (source);
                sourceImag.member.set (parent.getStrings().imag);

                auto& plusEqualsReal = parent.allocateChild<AST::InPlaceOperator>();
                plusEqualsReal.target.referTo (targetReal);
                plusEqualsReal.source.referTo (sourceReal);
                plusEqualsReal.op = AST::BinaryOpTypeEnum::Enum::add;

                auto& plusEqualsImag = parent.allocateChild<AST::InPlaceOperator>();
                plusEqualsImag.target.referTo (targetImag);
                plusEqualsImag.source.referTo (sourceImag);
                plusEqualsImag.op = AST::BinaryOpTypeEnum::Enum::add;

                block.addStatement (plusEqualsReal);
                block.addStatement (plusEqualsImag);

                return block;
            }
            else
            {
                auto& plusEquals = parent.allocateChild<AST::InPlaceOperator>();
                plusEquals.target.referTo (target);
                plusEquals.source.referTo (source);
                plusEquals.op = AST::BinaryOpTypeEnum::Enum::add;

                return plusEquals;
            }
        }
        else
        {
            auto& assignment = parent.allocateChild<AST::Assignment>();
            assignment.target.referTo (target);
            assignment.source.referTo (source);

            return assignment;
        }
    }
};

//==============================================================================
struct ValueStreamUtilities
{
    static bool dataTypeCanBeInterpolated (const AST::EndpointDeclaration& endpointDeclaration)
    {
        return endpointDeclaration.isInput
                && endpointDeclaration.isValue()
                && AST::castToTypeBaseRef (endpointDeclaration.dataTypes[0]).isPrimitiveFloat();
    }

    static AST::Function* getSetValueFunction (AST::ProcessorBase& processor, const AST::EndpointDeclaration& e)
    {
        return processor.findFunction (AST::getSetValueFunctionName (e), 3).get();
    }

    static AST::TypeBase& getTypeForValueEndpoint (AST::ProcessorBase& processor,
                                                   const AST::EndpointDeclaration& endpointDeclaration,
                                                   bool isTopLevelProcessor)
    {
        CMAJ_ASSERT (endpointDeclaration.isValue());

        auto& type = AST::castToTypeBaseRef (endpointDeclaration.dataTypes[0]);

        if (isTopLevelProcessor && dataTypeCanBeInterpolated (endpointDeclaration))
        {
            auto& valueStruct = AST::createStruct (processor, "_value_" + std::string (endpointDeclaration.getName()));

            valueStruct.memberNames.addString (processor.getStrings().value);
            valueStruct.memberTypes.addReference (type);

            valueStruct.memberNames.addString (processor.getStrings().increment);
            valueStruct.memberTypes.addReference (type);

            valueStruct.memberNames.addString (processor.getStrings().frames);
            valueStruct.memberTypes.addReference (processor.context.allocator.createInt32Type());

            if (endpointDeclaration.isArray())
                return AST::createArrayOfType (processor, valueStruct, *endpointDeclaration.getArraySize());

            return valueStruct;
        }

        if (endpointDeclaration.isArray())
            return AST::createArrayOfType (processor, type, *endpointDeclaration.getArraySize());

        return type;
    }

    static void addStateStructMember (AST::ProcessorBase& processor,
                                      AST::StructType& stateType,
                                      const AST::EndpointDeclaration& endpointDeclaration,
                                      bool isTopLevelProcessor)
    {
        CMAJ_ASSERT (endpointDeclaration.isValue());

        stateType.addMember (StreamUtilities::getEndpointStateMemberName (endpointDeclaration),
                             ValueStreamUtilities::getTypeForValueEndpoint (processor,
                                                                            endpointDeclaration, isTopLevelProcessor));
    }

    static AST::ValueBase& getStateStructMember (AST::ObjectContext& context,
                                                 const AST::EndpointDeclaration& endpointDeclaration,
                                                 const AST::ValueBase& stateParam,
                                                 bool isTopLevelProcessor)
    {
        if (endpointDeclaration.isInput && isTopLevelProcessor && dataTypeCanBeInterpolated (endpointDeclaration))
            return AST::createGetStructMember (context, AST::createGetStructMember (context, stateParam, StreamUtilities::getEndpointStateMemberName (endpointDeclaration)), "value");

        return AST::createGetStructMember (context, stateParam, StreamUtilities::getEndpointStateMemberName (endpointDeclaration));
    }

    static AST::ValueBase& getStateEndpointMember (AST::ScopeBlock& block,
                                                   const AST::EndpointDeclaration& endpointDeclaration,
                                                   const AST::ValueBase& stateParam,
                                                   std::string_view param)
    {
        return AST::createGetStructMember (block, AST::createGetStructMember (block.context, stateParam, StreamUtilities::getEndpointStateMemberName (endpointDeclaration)), param);
    }

    static ptr<AST::ScopeBlock> addUpdateRampsCall (AST::ProcessorBase& processor, AST::ScopeBlock& block, AST::ValueBase& stateParam)
    {
        if (! processor.hasInputValueEventEndpoints())
            return {};

        auto& updateRampsBlock = block.allocateChild<AST::ScopeBlock>();

        block.addStatement (AST::createIfStatement (block.context,
                                                    AST::createBinaryOp (block.context,
                                                                         AST::BinaryOpTypeEnum::Enum::notEquals,
                                                                         AST::createGetStructMember (block.context, stateParam, "_activeRamps"),
                                                                         block.context.allocator.createConstantInt32 (0)),
                                                    updateRampsBlock));

        updateRampsBlock.addStatement (AST::createFunctionCall (updateRampsBlock.context,
                                                                *processor.findFunction ("_updateRamps", 1),
                                                                stateParam));
        return updateRampsBlock;
    }

    static void addEventStreamSupport (AST::ProcessorBase& processor)
    {
        if (! processor.hasInputValueEventEndpoints())
            return;

        auto& stateType = EventHandlerUtilities::getOrCreateStateStructType (processor);

        if (stateType.hasMember (processor.getStrings()._activeRamps))
            return;

        // For now a simple implementation that short circuits if there are no ramping values, otherwise checks them in turn
        stateType.addMember (processor.getStrings()._activeRamps,
                             processor.context.allocator.createInt32Type(),
                             0);

        auto& updateRamps = AST::createFunctionInModule (processor, processor.context.allocator.createVoidType(), processor.getStrings()._updateRamps);
        auto stateParam = AST::addFunctionParameter (updateRamps, stateType, processor.getStrings()._state, true, false);

        for (auto endpointDeclaration : processor.getInputEndpoints (true))
        {
            if (endpointDeclaration->isValue())
            {
                {
                    auto& setValueFn = AST::createExportedFunction (processor, processor.context.allocator.createVoidType(),
                                                                    processor.getStringPool().get (AST::getSetValueFunctionName (endpointDeclaration)));
                    auto setValueStateParam = AST::addFunctionParameter (setValueFn, stateType, processor.getStrings()._state, true, false);
                    auto valueParam = AST::addFunctionParameter (setValueFn, AST::castToTypeBaseRef (endpointDeclaration->dataTypes[0]), processor.getStrings().value, true, false);
                    auto framesParam = AST::addFunctionParameter (setValueFn, processor.context.allocator.createInt32Type(), processor.getStrings().frames, false, false);

                    auto& mainBlock = *setValueFn.getMainBlock();

                    if (dataTypeCanBeInterpolated (endpointDeclaration))
                    {
                        // Model an instantaneous update of the value as a ramp in 1 frame
                        mainBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                                        AST::createBinaryOp (mainBlock.context,
                                                                                             AST::BinaryOpTypeEnum::Enum::equals,
                                                                                             framesParam,
                                                                                             mainBlock.context.allocator.createConstantInt32 (0)),
                                                                        AST::createAssignment (mainBlock.context,
                                                                                               framesParam,
                                                                                               mainBlock.context.allocator.createConstantInt32 (1))));

                        mainBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                                        AST::createBinaryOp (mainBlock.context,
                                                                                             AST::BinaryOpTypeEnum::Enum::equals,
                                                                                             getStateEndpointMember (mainBlock, endpointDeclaration, setValueStateParam, "frames"),
                                                                                             mainBlock.context.allocator.createConstantInt32 (0)),
                                                                        AST::createPreInc (mainBlock.context,
                                                                                           AST::createGetStructMember (mainBlock.context, setValueStateParam, "_activeRamps"))));

                        mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                                       getStateEndpointMember (mainBlock, endpointDeclaration, setValueStateParam, "increment"),
                                                                       AST::createDivide (mainBlock.context,
                                                                                          AST::createSubtract (mainBlock.context, valueParam,
                                                                                                               getStateEndpointMember (mainBlock, endpointDeclaration, setValueStateParam, "value")),
                                                                                          framesParam)));

                        mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                                       getStateEndpointMember (mainBlock, endpointDeclaration, setValueStateParam, "frames"),
                                                                       framesParam));
                    }
                    else
                    {
                        mainBlock.addStatement (AST::createAssignment (mainBlock.context,
                                                                       getStateStructMember (mainBlock.context,
                                                                                             endpointDeclaration,
                                                                                             setValueStateParam,
                                                                                             true),
                                                                       valueParam));
                    }
                }

                if (dataTypeCanBeInterpolated (endpointDeclaration))
                {
                    auto& mainBlock = *updateRamps.getMainBlock();

                    auto& incrementBlock = mainBlock.allocateChild<AST::ScopeBlock>();

                    auto& ifStatement = AST::createIfStatement (mainBlock.context,
                                                                AST::createBinaryOp (mainBlock.context,
                                                                                     AST::BinaryOpTypeEnum::Enum::notEquals,
                                                                                     getStateEndpointMember (mainBlock, endpointDeclaration, stateParam, "frames"),
                                                                                     mainBlock.context.allocator.createConstantInt32 (0)),
                                                                incrementBlock);

                    incrementBlock.addStatement (AST::createAssignment (incrementBlock.context,
                                                                        getStateEndpointMember (incrementBlock, endpointDeclaration, stateParam, "value"),
                                                                        AST::createAdd (incrementBlock.context,
                                                                                        getStateEndpointMember (incrementBlock, endpointDeclaration, stateParam, "value"),
                                                                                        getStateEndpointMember (incrementBlock, endpointDeclaration, stateParam, "increment"))));

                    incrementBlock.addStatement (AST::createPreDec (incrementBlock.context,
                                                                    getStateEndpointMember (incrementBlock, endpointDeclaration, stateParam, "frames")));

                    incrementBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                                         AST::createBinaryOp (mainBlock.context,
                                                                                              AST::BinaryOpTypeEnum::Enum::equals,
                                                                                              getStateEndpointMember (mainBlock, endpointDeclaration, stateParam, "frames"),
                                                                                              mainBlock.context.allocator.createConstantInt32 (0)),
                                                                         AST::createPreDec (incrementBlock.context,
                                                                                            AST::createGetStructMember (mainBlock.context, stateParam, "_activeRamps"))));

                    mainBlock.addStatement (ifStatement);
                }

            }
        }

    }
};

}
