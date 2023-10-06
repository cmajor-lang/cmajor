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

namespace cmaj::passes
{

//==============================================================================
struct ExternalResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    ExternalResolver (AST::Program& p) : super (p)
    {
        if (auto mainProcessor = p.findMainProcessor())
        {
            if (mainProcessor->isGenericOrParameterised())
            {
                auto params = mainProcessor->specialisationParams.iterateAs<AST::Object>();

                for (auto& param : params)
                {
                    if (auto v = param.getAsVariableDeclaration())
                    {
                        // Convert to external within the processor, converting initialValue to a default value annotation
                        v->isExternal = true;

                        if (auto value = v->initialValue.getAsObjectType<AST::ValueBase>())
                        {
                            auto& annotations = v->allocateChild<AST::Annotation>();

                            annotations.setValue (annotations.getStringPool().get ("default"), *value);
                            v->annotation.referTo (annotations);
                            v->initialValue.reset();
                        }

                        mainProcessor->stateVariables.addReference (*v);
                        mainProcessor->specialisationParams.removeObject (*v);

                        registerChange();
                    }
                    else
                    {
                        throwError (param, Errors::onlyValueSpecialisationsSupported());
                    }
                }
            }
        }
    }

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::VariableDeclaration& v) override
    {
        super::visit (v);

        if (v.isExternal && ! v.isInternalVariable())
        {
            if (auto t = v.getType())
            {
                if (auto mc = t->getAsMakeConstOrRef())
                    if (mc->makeConst)
                        throwError (mc, Errors::noConstOnExternals());

                if (t->isResolved())
                    if (program.externalVariableManager.addExternalIfNotPresent (v))
                        registerChange();
            }
        }
    }

    void visit (AST::Function& f) override
    {
        super::visit (f);

        if (f.isExternal)
            program.externalFunctionManager.addFunctionIfNotPresent (f);
    }
};


}
