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
/// Looks for variables that are read but not written, and marks them const
/// so that they can get constant-folded
static inline void convertUnwrittenVariablesToConst (AST::Program& program)
{
    struct ConvertUnwrittenVariables  : public AST::Visitor
    {
        using super = AST::Visitor;
        using super::visit;

        ConvertUnwrittenVariables (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::Assignment& a) override
        {
            super::visit (a);

            if (auto target = AST::castToValue (a.target))
                if (auto v = target->getSourceVariable())
                    if (v->canBeMadeConst())
                        variablesWritten.insert (v.get());
        }

        void visit (AST::FunctionCall& c) override
        {
            super::visit (c);

            if (auto f = c.getTargetFunction())
            {
                auto paramTypes = f->getParameterTypes();

                for (size_t i = 0; i < c.arguments.size(); ++i)
                    if (paramTypes[i]->isNonConstReference())
                        markAnyVariablesAsModifiedWithin (c.arguments[i].getObjectRef());
            }
        }

        void visit (AST::InPlaceOperator& op) override
        {
            super::visit (op);
            markAnyVariablesAsModifiedWithin (op.target);
        }

        void visit (AST::PreOrPostIncOrDec& p) override
        {
            super::visit (p);
            markAnyVariablesAsModifiedWithin (p.target);
        }

        void visit (AST::VariableDeclaration& v) override
        {
            super::visit (v);

            if (v.canBeMadeConst())
                allVariables.insert (std::addressof (v));
        }

        void markAnyVariablesAsModifiedWithin (AST::Object& o)
        {
            if (auto val = AST::castToValue (o))
            {
                val->visitObjectsInScope ([&] (const AST::Object& s)
                {
                    if (auto v = s.getAsValueBase())
                        if (auto sourceVar = v->getSourceVariable())
                            if (sourceVar->canBeMadeConst())
                                variablesWritten.insert (sourceVar.get());
                });
            }
        }

        void convertUnwrittenVariables()
        {
            for (auto& v : allVariables)
                if (variablesWritten.find (v) == variablesWritten.end())
                    v->isConstant = true;
        }

        std::unordered_set<AST::VariableDeclaration*> allVariables;
        std::unordered_set<const AST::VariableDeclaration*> variablesWritten;
    };

    ConvertUnwrittenVariables cv (program.allocator);
    cv.visitObject (program.rootNamespace);
    cv.convertUnwrittenVariables();
}

}
