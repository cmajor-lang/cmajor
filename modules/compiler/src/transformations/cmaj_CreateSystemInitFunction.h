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
inline void createSystemInitFunctions (AST::Program& program,
                                       ptr<AST::VariableDeclaration> sessionIDStateVariable,
                                       ptr<AST::VariableDeclaration> frequencyStateVariable)
{
    struct CreateSystemInitFunctions  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        CreateSystemInitFunctions (AST::Program& program,
                                   ptr<AST::VariableDeclaration> s,
                                   ptr<AST::VariableDeclaration> f)
            : super (program.allocator), sessionIDVariable (s), frequencyVariable (f)
        {
            // Ensure the main processor always has an init function as it's required by the runtime
            getOrCreateSystemInitFunction (program.getMainProcessor());
        }

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::Processor& p) override
        {
            CMAJ_ASSERT (! p.isGenericOrParameterised());
            super::visit (p);

            if (auto userInit = p.findUserInitFunction())
                addUserInitCall (getOrCreateSystemInitFunction (p), *userInit);
        }

        // Move the initialisers for state variables into the system init function
        void visit (AST::VariableDeclaration& v) override
        {
            super::visit (v);

            if (v.isStateVariable() && ! v.hasStaticConstantValue())
            {
                if (auto initialiser = AST::castToValue (v.initialValue))
                {
                    if (auto parent = v.findParentProcessor())
                    {
                        auto& systemInitBlock = *getOrCreateSystemInitFunction (*parent).getMainBlock();
                        size_t insertIndex = 0;

                        while (insertIndex < systemInitBlock.statements.size())
                        {
                            if (AST::castTo<AST::Assignment> (systemInitBlock.statements[insertIndex]) != nullptr)
                                ++insertIndex;
                            else
                                break;
                        }

                        AST::addAssignment (systemInitBlock,
                                            AST::createVariableReference (systemInitBlock.context, v),
                                            *initialiser,
                                            static_cast<int> (insertIndex));

                        if (v.declaredType == nullptr)
                            v.declaredType.referTo (*v.getType());

                        v.initialValue.reset();
                        v.isInitialisedInInit = true;
                    }
                }
            }
        }

        AST::Function& getOrCreateSystemInitFunction (AST::ProcessorBase& p)
        {
            if (auto f = p.findSystemInitFunction())
                return *f;

            auto& f = AST::createFunctionInModule (p, p.context.allocator.voidType, p.getStrings().systemInitFunctionName);
            auto processorIDParam = AST::addFunctionParameter (f, p.context.allocator.int32Type, p.getStrings().initFnProcessorIDParamName, true);
            auto sessionIDParam   = AST::addFunctionParameter (f, p.context.allocator.int32Type, p.getStrings().initFnSessionIDParamName);
            auto frequencyParam   = AST::addFunctionParameter (f, p.context.allocator.float64Type, p.getStrings().initFnFrequencyParamName);
            auto& mainBlock = *f.getMainBlock();

            (void) processorIDParam;
            
            if (sessionIDVariable != nullptr)
                AST::addAssignment (mainBlock, AST::createVariableReference (mainBlock.context, *sessionIDVariable), sessionIDParam);

            if (frequencyVariable != nullptr)
                AST::addAssignment (mainBlock, AST::createVariableReference (mainBlock.context, *frequencyVariable), frequencyParam);

            return f;
        }

        void addUserInitCall (AST::Function& systemInit, AST::Function& userInit)
        {
            auto& systemInitBlock = *systemInit.getMainBlock();

            // Ensure we are not already calling the user init function
            for (auto& s : systemInitBlock.statements)
                if (auto call = AST::castTo<AST::FunctionCall> (s))
                    if (call->targetFunction.getObject() == userInit)
                        CMAJ_ASSERT_FALSE;

            systemInitBlock.addStatement (AST::createFunctionCall (systemInitBlock, userInit));
        }

        ptr<AST::VariableDeclaration> sessionIDVariable, frequencyVariable;
    };

    CreateSystemInitFunctions (program, sessionIDStateVariable, frequencyStateVariable).visitObject (program.rootNamespace);
}

}
