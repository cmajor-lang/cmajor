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

inline void moveProcessorPropertiesToState (AST::ProcessorBase& processor, ProcessorInfo::GetInfo getInfo, bool isTopLevelProcessor)
{
    struct MoveProperties  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        MoveProperties (AST::Allocator& a, ProcessorInfo::GetInfo getInfo, bool topLevelProcessor)
            : super (a), getProcessorInfo (std::move (getInfo)), isTopLevelProcessor (topLevelProcessor)
        {
        }

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::ProcessorProperty& pp) override
        {
            auto property = pp.property.get();

            if (property == AST::ProcessorPropertyEnum::Enum::id)
            {
                if (isTopLevelProcessor)
                {
                    pp.replaceWith (pp.context.allocator.createConstantInt32 (1));
                }
                else
                {
                    auto& processor = *pp.findParentOfType<AST::ProcessorBase>();
                    pp.replaceWith (getOrCreateStateMember (processor, pp.getStringPool().get (EventHandlerUtilities::getIdMemberName()), pp.context.allocator.createInt32Type()));
                    getProcessorInfo (processor).usesProcessorId = true;
                }
            }
        }

    private:
        AST::VariableReference& getOrCreateStateMember (AST::ProcessorBase& processor, AST::PooledString memberName, AST::TypeBase& type)
        {
            if (auto stateVariable = processor.findVariable (memberName))
                return AST::createVariableReference (processor.context, stateVariable);

            return AST::createVariableReference (processor.context, AST::createStateVariable (processor, memberName, type, {}));
        }

        ProcessorInfo::GetInfo getProcessorInfo;
        bool isTopLevelProcessor;

    };

    MoveProperties (processor.context.allocator, std::move (getInfo), isTopLevelProcessor).visitObject (processor);
}

}
