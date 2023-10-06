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
static inline void determineFunctionAliasStatus (AST::Program& program)
{
    struct DetermineFunctionAliasStatus  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        DetermineFunctionAliasStatus (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        bool contains (const AST::TypeBase& type, const AST::TypeBase& t)
        {
            if (type.isSameType (t, AST::TypeBase::ComparisonFlags::ignoreConst))
                return true;

            if (auto arrayType = type.getAsArrayType())
                return contains (arrayType->getInnermostElementTypeRef(), t);

            if (auto structType = type.getAsStructType())
            {
                for (auto memberType : structType->memberTypes.getAsObjectTypeList<AST::TypeBase>())
                    if (contains (memberType, t))
                        return true;
            }

            return false;
        }

        bool contains (std::vector<ptr<const AST::TypeBase>>& types, const AST::TypeBase& t)
        {
            for (auto type : types)
                if (contains (*type, t))
                    return true;

            return false;
        }

        void visit (AST::Function& fn) override
        {
            super::visit (fn);

            std::vector<ptr<const AST::TypeBase>> typesInScope;
            std::vector<ptr<const AST::TypeBase>> nonConstRefTypes;

            if (auto processor = fn.getParentScope()->getAsProcessorBase())
                for (auto stateVariable : processor->stateVariables.getAsObjectTypeList<AST::VariableDeclaration>())
                    typesInScope.push_back (stateVariable->getType());

            for (auto& param : fn.iterateParameters())
            {
                auto t = param.getType();

                if (t->isNonConstReference())
                    nonConstRefTypes.push_back (t);
                else if (t->isReference())
                    typesInScope.push_back (t);
            }

            for (auto t : nonConstRefTypes)
                if (contains (typesInScope, *t))
                    for (auto& param : fn.iterateParameters())
                        if (contains (*param.getType(), *t))
                            param.isAliasFree = false;
        }
    };

    DetermineFunctionAliasStatus (program.allocator).visitObject (program.rootNamespace);
}

}
