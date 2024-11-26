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

namespace cmaj
{

//==============================================================================
struct EventHandlerUtilities
{
    static std::vector<ptr<AST::Function>> getEventHandlerFunctionsForEndpoint (const AST::ProcessorBase& processor,
                                                                                const AST::EndpointDeclaration& endpoint)
    {
        std::vector<ptr<AST::Function>> fns;

        for (auto& fn : processor.functions.iterateAs<AST::Function>())
            if (fn.getName() == endpoint.getName())
                fns.push_back (fn);

        return fns;
    }

    static ptr<AST::Function> findEventFunctionForType (const AST::ProcessorBase& processor,
                                                        AST::PooledString functionName,
                                                        const AST::TypeBase& type,
                                                        bool isArrayEvent)
    {
        return processor.findFunction ([&] (AST::Function& fn) -> bool
        {
            if (fn.hasName (functionName))
            {
                if (type.isVoid())
                {
                    if (!isArrayEvent && fn.getNumParameters() == 1)
                        return true;

                    if (isArrayEvent && fn.getNumParameters() == 2)
                        return true;

                    return false;
                }

                auto eventValue = fn.parameters.back().getAsObjectType<AST::VariableDeclaration>();

                if (eventValue->getType()->isSameType (type, AST::TypeBase::ComparisonFlags::ignoreReferences +
                                                             AST::TypeBase::ComparisonFlags::ignoreConst +
                                                             AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                    return true;
            }

            return false;
        });
    }

    static ptr<AST::Function> findEventFunctionForType (AST::EndpointInstance& endpointInstance,
                                                        const AST::TypeBase& type,
                                                        bool isSource)
    {
        auto& endpoint = *endpointInstance.getEndpoint (isSource);

        auto expectedName = isSource ? endpoint.getStringPool().get (getWriteEventFunctionName (endpoint))
                                     : endpoint.getName();

        return findEventFunctionForType (endpointInstance.getProcessor(), expectedName, type, endpoint.isArray());
    }

    static AST::StructType& getOrCreateStateStructType (AST::ProcessorBase& processor)
    {
        if (auto result = processor.structures.findObjectWithName (processor.getStrings().stateStructName))
            return *result->getAsStructType();

        return AST::createStruct (processor, processor.getStrings().stateStructName);
    }

    static AST::StructType& getOrCreateIoStructType (AST::ProcessorBase& processor)
    {
        if (auto result = processor.structures.findObjectWithName (processor.getStrings().ioStructName))
            return *result->getAsStructType();

        return AST::createStruct (processor, processor.getStrings().ioStructName);
    }

    static void addUpcastCall (AST::Function& fn, AST::Function& targetFunction, bool isArray)
    {
        auto targetProcessor = targetFunction.findParentOfType<AST::ProcessorBase>();
        auto block = fn.getMainBlock();

        auto& stateArgument = AST::createVariableReference (block->context, fn.parameters.findObjectWithName (fn.getStrings()._state));

        auto& stateUpcast = fn.allocateChild<AST::StateUpcast>();

        stateUpcast.argument.referTo (stateArgument);
        stateUpcast.targetType.referTo (EventHandlerUtilities::getOrCreateStateStructType (*targetProcessor));

        auto& functionCall = AST::createFunctionCall (block, targetFunction, stateUpcast);

        if (isArray)
        {
            // Either there is an index parameter, or we have a state member indicating the arrayIndex
            if (auto parameter = fn.parameters.findObjectWithName (fn.getStrings().index))
            {
                auto& indexArgument = AST::createVariableReference (block->context, *parameter);
                functionCall.arguments.addReference (indexArgument);
            }
            else
            {
                auto& indexArgument = AST::createGetStructMember (block->context, stateArgument, getInstanceIndexMemberName());
                functionCall.arguments.addReference (indexArgument);
            }
        }

        auto valueParam = fn.parameters.findObjectWithName (fn.getStrings().value);

        if (valueParam)
        {
            auto& valueArgument = AST::createVariableReference (block->context, valueParam);
            functionCall.arguments.addReference (valueArgument);
        }

        block->addStatement (functionCall);
    }

    static std::string getWriteEventFunctionName (const AST::EndpointDeclaration& e)    { return "_writeEvent_" + std::string (e.getName()); }
    static std::string getCurrentFrameStateMemberName()                                 { return "_currentFrame"; }
    static std::string getEventCountStateMemberName (std::string_view name)             { return std::string (name) + "_eventCount"; }
    static std::string getEventCountStateMemberName (const AST::EndpointDeclaration& e) { return getEventCountStateMemberName (e.getName()); }
    static std::string getInstanceIndexMemberName()                                     { return "_instanceIndex"; }
    static std::string getIdMemberName()                                                { return "_id"; }
};

struct ProcessorInfo
{
    using GetInfo = std::function<ProcessorInfo&(AST::ModuleBase&)>;

    bool isProcessorArray = false;
    bool usesProcessorId = false;
};

struct ProcessorInfoManager
{
    ProcessorInfo::GetInfo getProcessorInfo()
    {
        return [&] (const AST::ModuleBase& processor) -> ProcessorInfo&
        {
            return processorInfoMap[&processor];
        };
    }

    std::unordered_map<const AST::ModuleBase*, ProcessorInfo> processorInfoMap;
};


}
