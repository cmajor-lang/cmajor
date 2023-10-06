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
struct RemoveResetCalls  : public AST::NonParameterisedObjectVisitor
{
    using super = AST::NonParameterisedObjectVisitor;
    using super::visit;

    RemoveResetCalls (AST::Allocator& a) : super (a) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::Reset& reset) override
    {
        if (auto fn = reset.getNode()->getProcessorType()->findResetFunction())
        {
            auto nodeName    = reset.getNode()->getName();
            auto& parentFn   = reset.getParentFunction();
            auto& stateParam = AST::createVariableReference (reset.context, parentFn.getParameter (reset.getStrings()._state));

            auto& functionCall = AST::createFunctionCall (reset.context,
                                                          *fn,
                                                          AST::createGetStructMember (reset.context, stateParam, nodeName));
            reset.replaceWith (functionCall);
        }
        else
        {
            reset.replaceWith (reset.context.allocate<AST::NoopStatement>());
        }
    }
};

inline void removeResetCalls (AST::ProcessorBase& processor)
{
    RemoveResetCalls remover (processor.context.allocator);
    remover.visitObject (processor);
}

}
