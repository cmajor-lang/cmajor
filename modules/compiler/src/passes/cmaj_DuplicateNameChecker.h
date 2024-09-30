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

namespace cmaj::passes
{

//==============================================================================
struct DuplicateNameCheckPass  : public AST::Visitor
{
    using super = AST::Visitor;
    using super::visit;

    DuplicateNameCheckPass (AST::Allocator& a) : super (a) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    static void check (AST::Program& program)
    {
        DuplicateNameCheckPass (program.allocator).visitObject (program.rootNamespace);
    }

    //==============================================================================
    void visit (AST::Namespace& n) override
    {
        super::visit (n);

        validation::DuplicateFunctionChecker().checkList (n.functions);

        validation::DuplicateNameChecker nameChecker;
        nameChecker.checkList (n.specialisationParams);
        nameChecker.checkList (n.subModules);
        nameChecker.checkList (n.constants);
        nameChecker.checkList (n.structures);
        nameChecker.checkList (n.enums);
        nameChecker.checkList (n.aliases);

        // functions are scanned last because they can be duplicates of other fns but not the other names
        nameChecker.checkFunctions (n.functions);
    }

    void visit (AST::Graph& g) override
    {
        super::visit (g);

        validation::DuplicateFunctionChecker().checkList (g.functions);

        validation::DuplicateNameChecker nameChecker;
        nameChecker.checkList (g.specialisationParams);
        nameChecker.checkList (g.endpoints);
        nameChecker.checkList (g.stateVariables);
        nameChecker.checkList (g.nodes);
        nameChecker.checkList (g.structures);
        nameChecker.checkList (g.enums);
        nameChecker.checkList (g.aliases);

        // functions are scanned last because they can be duplicates of other fns but not the other names
        nameChecker.checkFunctions (g.functions);
    }

    void visit (AST::Processor& p) override
    {
        super::visit (p);

        validation::DuplicateFunctionChecker().checkList (p.functions);

        validation::DuplicateNameChecker nameChecker;
        nameChecker.checkList (p.specialisationParams);
        nameChecker.checkList (p.endpoints);
        nameChecker.checkList (p.stateVariables);
        nameChecker.checkList (p.structures);
        nameChecker.checkList (p.enums);
        nameChecker.checkList (p.aliases);

        // functions are scanned last because they can be duplicates of other fns but not the other names
        nameChecker.checkFunctions (p.functions);
    }

    void visit (AST::GraphNode& n) override
    {
        super::visit (n);

        if (! n.isImplicitlyCreated())
        {
            AST::NameSearch search;
            search.nameToFind                   = n.getName();
            search.stopAtFirstScopeWithResults  = false;
            search.findVariables                = false;
            search.findTypes                    = false;
            search.findFunctions                = false;
            search.findNamespaces               = false;
            search.findProcessors               = false;
            search.findNodes                    = true;
            search.findEndpoints                = false;
            search.onlyFindLocalVariables       = false;

            auto& parent = n.getParentProcessor();

            search.performSearch (parent, {});

            for (auto& found : search.itemsFound)
                if (found != n)
                    validation::throwErrorWithPreviousDeclaration (n, found, Errors::processorNameAlreadyUsed (n.getName()));
        }
    }
};

} // namespace cmaj
