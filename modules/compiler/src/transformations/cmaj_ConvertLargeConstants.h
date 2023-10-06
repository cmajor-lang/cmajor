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
/// Takes large inline aggregrate constants and puts them into global variables
/// because this helps to stop llvm creating some long-winded codegen
static inline void convertLargeConstantsToGlobals (AST::Program& program)
{
    static constexpr uint32_t arraySizeToConvertToGlobal = 32;

    struct ConvertLargeConstants  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ConvertLargeConstants (AST::Namespace& root)
          : super (root.context.allocator), rootNamespace (root)
        {}

        AST::Namespace& rootNamespace;
        int insideFunction = 0;

        std::vector<ref<AST::VariableDeclaration>> constants;

        AST::VariableDeclaration& createGlobal (AST::ValueBase& a, const AST::TypeBase& type)
        {
            for (auto existing : constants)
                if (existing->isIdentical (a))
                    return existing;

            auto& gv = rootNamespace.context.allocate<AST::VariableDeclaration>();
            gv.name = a.getStringPool().get ("__constant_");
            gv.declaredType.referTo (type);
            gv.initialValue.setChildObject (a);
            gv.variableType = AST::VariableTypeEnum::Enum::state;
            gv.isConstant = true;

            constants.push_back (gv);
            return gv;
        }

        void visit (AST::Function& f) override
        {
            if (! f.isSystemInitFunction())
            {
                ++insideFunction;
                super::visit (f);
                --insideFunction;
            }
        }

        void visit (AST::Cast& c) override
        {
            if (AST::castToTypeBaseRef (c.targetType).isSlice())
            {
                if (c.arguments.size() == 1)
                {
                    auto& arg = AST::castToValueRef (c.arguments[0].getObjectRef());
                    auto& type = *arg.getResultType();

                    if (type.isFixedSizeAggregate())
                        if (auto a = AST::castToSkippingReferences<AST::ConstantAggregate> (arg))
                            replaceWithGlobal (arg, type);
                }
            }

            super::visit (c);
        }

        void visit (AST::ConstantAggregate& a) override
        {
            if (insideFunction == 0)
                return;

            auto& type = *a.getResultType();

            if (type.getFixedSizeAggregateNumElements() > arraySizeToConvertToGlobal)
                replaceWithGlobal (a, type);
        }

        void replaceWithGlobal (AST::ValueBase& a, const AST::TypeBase& type)
        {
            auto referrersCopy = a.getReferrers();
            auto& gv = createGlobal (a, type);

            // must be careful not to replace any sub-elements of a constant
            // aggregate with a variable
            for (auto& r : referrersCopy)
                if (r->owner.findSelfOrParentOfType<AST::ConstantValueBase>() == nullptr)
                    r->replaceWith (AST::createVariableReference (r->owner.context, gv));
        }
    };

    ConvertLargeConstants (program.rootNamespace).visitObject (program.rootNamespace);
}

}
