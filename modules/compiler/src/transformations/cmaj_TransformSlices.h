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
static inline void transformSlices (AST::Program& program)
{
    struct TransformSlices  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        TransformSlices (AST::Namespace& root)
          : super (root.context.allocator), rootNamespace (root),
            intrinsicsNamespace (*findIntrinsicsNamespace (root))
        {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::GetArrayOrVectorSlice& g) override
        {
            super::visit (g);

            if (auto parentValue = AST::castToValue (g.parent))
            {
                auto& parentType = *parentValue->getResultType();

                if (parentType.isSlice())
                {
                    if (choc::text::startsWith (g.findParentFunction()->getName(), getSliceOfSliceFunctionName()))
                        return; // need to avoid modifying our generated functions

                    auto& readFn = getOrCreateSliceOfSliceFunction (parentType);

                    auto& start = (g.start != nullptr) ? g.start.get() : allocator.createConstantInt32 (0);
                    auto& end   = (g.end   != nullptr) ? g.end.get() : allocator.createConstantInt32 (0);

                    g.replaceWith (AST::createFunctionCall (g, readFn, g.parent, start, end));
                }
            }
        }

        void visit (AST::PreOrPostIncOrDec& p) override
        {
            auto& target = AST::castToValueRef (p.target);

            if (auto g = target.getAsGetElement())
            {
                if (auto parentValue = AST::castToValue (g->parent))
                {
                    auto& parentType = *parentValue->getResultType();

                    if (parentType.isSlice())
                    {
                        auto& function = getOrCreatePreOrPostIncFunction (parentType, p.isIncrement, p.isPost);
                        p.replaceWith (AST::createFunctionCall (p, function, *parentValue, g->getSingleIndex()));
                        return;
                    }
                }
            }
        }

        void visit (AST::GetElement& g) override
        {
            super::visit (g);

            if (auto parentValue = AST::castToValue (g.parent))
            {
                auto& parentType = *parentValue->getResultType();

                if (parentType.isSlice())
                {
                    if (choc::text::startsWith (g.findParentFunction()->getName(), "_slice"))
                        return; // need to avoid modifying our generated functions

                    if (! g.findParentOfType<AST::Assignment>())
                    {
                        auto& readFn = getOrCreateReadSliceElementFunction (parentType);
                        g.replaceWith (AST::createFunctionCall (g, readFn, *parentValue, g.getSingleIndex()));
                    }
                }
            }
        }

        void visit (AST::Assignment& a) override
        {
            super::visit (a);

            if (auto g = AST::castTo<AST::GetElement> (a.target))
            {
                if (auto parentValue = AST::castToValue (g->parent))
                {
                    auto& parentType = *parentValue->getResultType();

                    if (parentType.isSlice())
                    {
                        if (choc::text::startsWith (a.findParentFunction()->getName(), getWriteSliceFunctionName()))
                            return; // need to avoid modifying our generated functions

                        auto& writeFn = getOrCreateWriteSliceElementFunction (parentType);
                        a.replaceWith (AST::createFunctionCall (*g, writeFn, *parentValue, g->getSingleIndex(), a.source));
                    }
                }
            }
        }


        AST::Function& getOrCreateSliceOfSliceFunction (const AST::TypeBase& sliceType)
        {
            CMAJ_ASSERT (sliceType.isSlice());
            auto& elementType = *sliceType.getArrayOrVectorElementType();

            AST::SignatureBuilder sig;
            sig << getSliceOfSliceFunctionName() << elementType;
            auto name = intrinsicsNamespace.getStringPool().get (sig.toString (30));

            if (auto f = intrinsicsNamespace.findFunction (name, 3))
                return *f;

            auto& f = AST::createFunctionInModule (intrinsicsNamespace, sliceType, name);
            auto parentSliceParam = AST::addFunctionParameter (f, sliceType, f.getStrings().array);
            auto startIndexParam  = AST::addFunctionParameter (f, allocator.int32Type, f.getStrings().start);
            auto endIndexParam    = AST::addFunctionParameter (f, allocator.int32Type, f.getStrings().end);

            auto& mainBlock = *f.getMainBlock();

            auto& sliceSize = mainBlock.allocateChild<AST::ValueMetaFunction>();
            sliceSize.op = AST::ValueMetaFunctionTypeEnum::Enum::size;
            sliceSize.arguments.addReference (parentSliceParam);

            auto& setSizeToZero = mainBlock.allocateChild<AST::ScopeBlock>();
            setSizeToZero.addStatement (AST::createAssignment (mainBlock.context, startIndexParam, allocator.createConstantInt32 (0)));
            setSizeToZero.addStatement (AST::createAssignment (mainBlock.context, endIndexParam, allocator.createConstantInt32 (0)));

            mainBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                            AST::createBinaryOp (mainBlock,
                                                                                 AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual,
                                                                                 startIndexParam,
                                                                                 sliceSize),
                                                            setSizeToZero));

            auto& setEndToSize = mainBlock.allocateChild<AST::ScopeBlock>();
            setEndToSize.addStatement (AST::createAssignment (mainBlock.context, endIndexParam, sliceSize));

            mainBlock.addStatement (AST::createIfStatement (mainBlock.context,
                                                            AST::createBinaryOp (mainBlock,
                                                                                 AST::BinaryOpTypeEnum::Enum::logicalOr,
                                                                                 AST::createBinaryOp (mainBlock,
                                                                                                      AST::BinaryOpTypeEnum::Enum::equals,
                                                                                                      endIndexParam,
                                                                                                      allocator.createConstantInt32 (0)),
                                                                                 AST::createBinaryOp (mainBlock,
                                                                                                      AST::BinaryOpTypeEnum::Enum::greaterThan,
                                                                                                      endIndexParam,
                                                                                                      sliceSize)),
                                                            setEndToSize));

            auto& resultSlice = mainBlock.allocateChild<AST::GetArrayOrVectorSlice>();
            resultSlice.parent.referTo (parentSliceParam);
            resultSlice.start.referTo (startIndexParam);
            resultSlice.end.referTo (endIndexParam);

            AST::addReturnStatement (mainBlock, resultSlice);

            CMAJ_ASSERT (intrinsicsNamespace.findFunction (name, 3) == f);
            return f;
        }

        static constexpr std::string_view getSliceOfSliceFunctionName()  { return "_createSliceOfSlice"; }

        
        AST::Function& getOrCreateReadSliceElementFunction (const AST::TypeBase& sliceType)
        {
            CMAJ_ASSERT (sliceType.isSlice());
            auto& elementType = *sliceType.getArrayOrVectorElementType();

            AST::SignatureBuilder sig;
            sig << getReadSliceFunctionName() << elementType;
            auto name = intrinsicsNamespace.getStringPool().get (sig.toString (30));

            if (auto f = intrinsicsNamespace.findFunction (name, 2))
                return *f;

            auto& f = AST::createFunctionInModule (intrinsicsNamespace, elementType, name);
            auto arrayParam = AST::addFunctionParameter (f, sliceType, f.getStrings().array);
            auto indexParam = AST::addFunctionParameter (f, allocator.int32Type, f.getStrings().index);

            auto& mainBlock = *f.getMainBlock();

            auto& sliceSize = mainBlock.allocateChild<AST::ValueMetaFunction>();
            sliceSize.op = AST::ValueMetaFunctionTypeEnum::Enum::size;
            sliceSize.arguments.addReference (arrayParam);

            auto& zero = allocator.createConstantInt32 (0);
            auto& sizeIsZero = AST::createBinaryOp (mainBlock, AST::BinaryOpTypeEnum::Enum::equals, sliceSize, zero);

            auto& returnNull = mainBlock.allocateChild<AST::ReturnStatement>();
            returnNull.value.referTo (elementType.allocateConstantValue (mainBlock.context));

            mainBlock.addStatement (AST::createIfStatement (mainBlock.context, sizeIsZero, returnNull));

            auto& wrapFn = *intrinsicsNamespace.findFunction ("wrap", 2);
            auto& wrappedIndex = AST::createFunctionCall (mainBlock, wrapFn, indexParam, sliceSize);
            AST::addReturnStatement (mainBlock, AST::createGetElement (mainBlock, arrayParam, wrappedIndex));

            return f;
        }

        static constexpr std::string_view getReadSliceFunctionName()  { return "_sliceReadElement"; }

        AST::Function& getOrCreateWriteSliceElementFunction (const AST::TypeBase& sliceType)
        {
            CMAJ_ASSERT (sliceType.isSlice());
            auto& elementType = *sliceType.getArrayOrVectorElementType();

            AST::SignatureBuilder sig;
            sig << getWriteSliceFunctionName() << elementType;
            auto name = intrinsicsNamespace.getStringPool().get (sig.toString (30));

            if (auto f = intrinsicsNamespace.findFunction (name, 3))
                return *f;

            auto& f = AST::createFunctionInModule (intrinsicsNamespace, sliceType.context.allocator.createVoidType(), name);
            auto arrayParam = AST::addFunctionParameter (f, sliceType, f.getStrings().array);
            auto indexParam = AST::addFunctionParameter (f, allocator.int32Type, f.getStrings().index);
            auto valueParam = AST::addFunctionParameter (f, elementType, f.getStrings().value);

            auto& mainBlock = *f.getMainBlock();

            auto& sliceSize = mainBlock.allocateChild<AST::ValueMetaFunction>();
            sliceSize.op = AST::ValueMetaFunctionTypeEnum::Enum::size;
            sliceSize.arguments.addReference (arrayParam);

            auto& zero = allocator.createConstantInt32 (0);
            auto& sizeIsZero = AST::createBinaryOp (mainBlock, AST::BinaryOpTypeEnum::Enum::equals, sliceSize, zero);

            auto& returnNull = mainBlock.allocateChild<AST::ReturnStatement>();

            mainBlock.addStatement (AST::createIfStatement (mainBlock.context, sizeIsZero, returnNull));

            auto& wrapFn = *intrinsicsNamespace.findFunction ("wrap", 2);
            auto& wrappedIndex = AST::createFunctionCall (mainBlock, wrapFn, indexParam, sliceSize);

            AST::addAssignment (mainBlock, AST::createGetElement (mainBlock, arrayParam, wrappedIndex), valueParam);

            return f;
        }

        static constexpr std::string_view getWriteSliceFunctionName()  { return "_sliceWriteElement"; }

        AST::Function& getOrCreatePreOrPostIncFunction (const AST::TypeBase& sliceType, bool isIncrement, bool isPost)
        {
            CMAJ_ASSERT (sliceType.isSlice());
            auto& elementType = *sliceType.getArrayOrVectorElementType();

            AST::SignatureBuilder sig;
            sig << getPreOrPistIncSliceFunctionName (isIncrement, isPost) << elementType;
            auto name = intrinsicsNamespace.getStringPool().get (sig.toString (30));

            if (auto f = intrinsicsNamespace.findFunction (name, 2))
                return *f;

            auto& f = AST::createFunctionInModule (intrinsicsNamespace, elementType, name);
            auto arrayParam = AST::addFunctionParameter (f, sliceType, f.getStrings().array);
            auto indexParam = AST::addFunctionParameter (f, allocator.int32Type, f.getStrings().index);

            auto& mainBlock = *f.getMainBlock();

            auto& readFn = getOrCreateReadSliceElementFunction (sliceType);
            auto& writeFn = getOrCreateWriteSliceElementFunction (sliceType);

            auto& currentValue = AST::createVariableReference (mainBlock,
                                                               AST::createLocalVariable (mainBlock,
                                                                                         "v",
                                                                                         elementType,
                                                                                         AST::createFunctionCall (mainBlock, readFn, arrayParam, indexParam)));

            auto& newValue = AST::createBinaryOp (mainBlock,
                                                  isIncrement ? AST::BinaryOpTypeEnum::Enum::add : AST::BinaryOpTypeEnum::Enum::subtract,
                                                  currentValue,
                                                  allocator.createConstantInt32 (1));

            mainBlock.addStatement (AST::createFunctionCall (mainBlock, writeFn, arrayParam, indexParam, newValue));

            if (isPost)
                AST::addReturnStatement (mainBlock, currentValue);
            else
                AST::addReturnStatement (mainBlock, newValue);

            return f;
        }

        static std::string getPreOrPistIncSliceFunctionName (bool isIncrement, bool isPost)
        {
            return std::string ("_sliceElement") + (isIncrement ? "Inc" : "Dec") + (isPost ? "Post" : "Pre");
        }


        AST::Namespace& rootNamespace;
        AST::Namespace& intrinsicsNamespace;
    };

    TransformSlices (program.rootNamespace).visitObject (program.rootNamespace);
}

}
