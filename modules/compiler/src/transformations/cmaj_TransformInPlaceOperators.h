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
static void transformInPlaceOperators (AST::Program& program)
{
    struct TransformInPlaceOperators  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;
    
        TransformInPlaceOperators (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        ptr<AST::InPlaceOperator> inPlaceOperator = nullptr;
        ptr<AST::ScopeBlock> scopeBlock = nullptr;

        void visit (AST::InPlaceOperator& o) override
        {
            inPlaceOperator = o;
            scopeBlock = nullptr;
            visitObject (o.target);

            if (scopeBlock != nullptr)
            {
                o.replaceWith (*scopeBlock);
                scopeBlock->addStatement (o);
            }

            inPlaceOperator = nullptr;
            scopeBlock = nullptr;
        }

        void visit (AST::GetElement& g) override
        {
            super::visit (g);

            if (inPlaceOperator != nullptr)
            {
                bool allConstant = true;

                for (auto& i : g.indexes)
                    allConstant &= AST::isCompileTimeConstant (i);

                if (allConstant)
                    return;

                if (scopeBlock == nullptr)
                    scopeBlock = inPlaceOperator->context.allocate<AST::ScopeBlock>();

                for (size_t i = 0; i < g.indexes.size(); i++)
                {
                    auto& temp = AST::createLocalVariableRef (*scopeBlock, "t", AST::castToValueRef (g.indexes[i]));

                    g.indexes[i].getAsObjectProperty()->referTo (temp);
                }
            }
        }

    };

    TransformInPlaceOperators (program.allocator).visitObject (program.rootNamespace);
}


}
