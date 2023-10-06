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
static inline void removeGenericAndParameterisedObjects (AST::Program& program)
{
    struct RemoveGenericsPass  : public AST::Visitor
    {
        using super = AST::Visitor;
        using super::visit;

        RemoveGenericsPass (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::Namespace& ns) override
        {
            super::visit (ns);

            if (ns.isGenericOrParameterised())
                return;

            ns.subModules.removeIf ([] (const AST::Property& prop)
            {
                return prop.getObjectRef().isGenericOrParameterised();
            });

            removeGenericFunctions (ns);
        }

        void visit (AST::Processor& p) override
        {
            super::visit (p);
            removeGenericFunctions (p);
        }

        void visit (AST::Graph& g) override
        {
            super::visit (g);
            removeGenericFunctions (g);
        }

        void removeGenericFunctions (AST::ModuleBase& module)
        {
            if (module.isGenericOrParameterised())
                return;

            module.functions.removeIf ([] (const AST::Property& prop)
            {
                return prop.getObjectRef().isGenericOrParameterised();
            });
        }
    };

    RemoveGenericsPass (program.allocator).visitObject (program.rootNamespace);
}

}
