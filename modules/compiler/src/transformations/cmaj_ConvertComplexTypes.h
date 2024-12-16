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
inline void convertComplexTypes (AST::Program& program)
{
    enum class OperatorFunction
    {
        none,
        negate,
        add,
        subtract,
        multiply,
        divide,
        equal,
        notequal
    };

    struct VectorSizeAndDepth
    {
        VectorSizeAndDepth (AST::ArraySize size, bool is64) : vectorSize (size), is64Bit (is64) {}

        VectorSizeAndDepth (const AST::TypeBase& type)
        {
            if (type.isVector())
            {
                vectorSize = type.getArrayOrVectorSize (0);
                is64Bit = type.getArrayOrVectorElementType()->isPrimitiveComplex64();
            }
            else
            {
                is64Bit = type.isPrimitiveComplex64();
            }
        }

        uint32_t getHash() const        { return (is64Bit ? (1u << 27) : 0) | (uint32_t) vectorSize; }

        AST::ArraySize vectorSize = 0;
        bool is64Bit = false;
    };

    //==============================================================================
    struct ComplexSupportLibrary
    {
        ComplexSupportLibrary (AST::Namespace& rootNamespace)
           : intrinsicsNamespace (*findIntrinsicsNamespace (rootNamespace))
        {
        }

        AST::StructType& getStructTypeFor (const AST::TypeBase& type)
        {
            return getStructTypeFor (VectorSizeAndDepth (type));
        }

        AST::StructType& getStructTypeFor (VectorSizeAndDepth sizeAndDepth)
        {
            auto& type = types[sizeAndDepth.getHash()];

            if (type == nullptr)
            {
                auto& newType = intrinsicsNamespace.allocateChild<AST::StructType>();
                auto name = "Complex" + std::string (sizeAndDepth.is64Bit ? "64" : "32");

                ref<const AST::TypeBase> memberType = sizeAndDepth.is64Bit ? intrinsicsNamespace.context.allocator.float64Type
                                                                           : intrinsicsNamespace.context.allocator.float32Type;

                if (sizeAndDepth.vectorSize > 0)
                {
                    name += "Vector" + std::to_string (sizeAndDepth.vectorSize);

                    auto& vecType = intrinsicsNamespace.allocateChild<AST::VectorType>();
                    vecType.elementType.referTo (memberType);
                    vecType.numElements.referTo (intrinsicsNamespace.context.allocator.createConstant (static_cast<int32_t> (sizeAndDepth.vectorSize)));
                    memberType = vecType;

                    newType.tupleType.referTo (getStructTypeFor ({ 0, sizeAndDepth.is64Bit }));
                }

                auto pooledName = intrinsicsNamespace.getStringPool().get (name);
                CMAJ_ASSERT (intrinsicsNamespace.findStruct (pooledName) == nullptr);

                newType.name.set (pooledName);
                newType.memberNames.addString (intrinsicsNamespace.getStrings().real);
                newType.memberTypes.addReference (memberType);
                newType.memberNames.addString (intrinsicsNamespace.getStrings().imag);
                newType.memberTypes.addReference (memberType);
                intrinsicsNamespace.structures.addReference (newType);

                type = std::addressof (newType);
            }

            return *type;
        }

        bool contains (ptr<const AST::TypeBase> type) const
        {
            for (auto& t : types)
                if (t.second == type.get())
                    return true;

            return false;
        }

        static std::string_view getFunctionName (OperatorFunction op)
        {
            switch (op)
            {
                case OperatorFunction::negate:      return "negate";
                case OperatorFunction::add:         return "add";
                case OperatorFunction::subtract:    return "subtract";
                case OperatorFunction::multiply:    return "multiply";
                case OperatorFunction::divide:      return "divide";
                case OperatorFunction::equal:       return "equal";
                case OperatorFunction::notequal:    return "notequal";
                case OperatorFunction::none:
                default:                            CMAJ_ASSERT_FALSE; return {};
            }
        }

        AST::Function& getOperatorFunction (OperatorFunction op,
                                            const AST::TypeBase& returnType,
                                            const AST::TypeBase& argType)
        {
            VectorSizeAndDepth sizeAndDepth (argType);

            auto hash = sizeAndDepth.getHash() * 100u + static_cast<uint32_t> (op);
            auto& function = functions[hash];

            if (function != nullptr)
                return *function;

            (void) getStructTypeFor (argType); // get these added early on
            (void) getStructTypeFor (returnType);

            auto functionName = (sizeAndDepth.is64Bit ? "complex64_" : "complex32_")
                                   + std::string (getFunctionName (op));

            if (sizeAndDepth.vectorSize > 0)
                functionName += "_vec" + std::to_string (sizeAndDepth.vectorSize);

            CMAJ_ASSERT (intrinsicsNamespace.findFunction (functionName, 2) == nullptr);

            auto& f = AST::createFunctionInModule (intrinsicsNamespace, returnType, functionName);
            function = std::addressof (f);
            auto& block = *f.getMainBlock();
            auto arg1 = AST::addFunctionParameter (f, argType, "a");

            auto& real1 = AST::createGetStructMember (block, arg1, "real");
            auto& imag1 = AST::createGetStructMember (block, arg1, "imag");

            if (op == OperatorFunction::equal || op == OperatorFunction::notequal)
            {
                auto arg2 = AST::addFunctionParameter (f, argType, "b");
                auto& real2 = AST::createGetStructMember (block, arg2, "real");
                auto& imag2 = AST::createGetStructMember (block, arg2, "imag");

                auto comparisonOp = op == OperatorFunction::equal ? AST::BinaryOpTypeEnum::Enum::equals
                                                                  : AST::BinaryOpTypeEnum::Enum::notEquals;
                auto combinerOp = op == OperatorFunction::equal ? AST::BinaryOpTypeEnum::Enum::logicalAnd
                                                                : AST::BinaryOpTypeEnum::Enum::logicalOr;

                auto& realResult = AST::createBinaryOp (block, comparisonOp, real1, real2);
                auto& imagResult = AST::createBinaryOp (block, comparisonOp, imag1, imag2);

                if (sizeAndDepth.vectorSize <= 1)
                {
                    AST::addReturnStatement (block, AST::createBinaryOp (block, combinerOp, realResult, imagResult));
                }
                else
                {
                    auto& resultCast = block.allocateChild<AST::Cast>();
                    resultCast.targetType.createReferenceTo (returnType);

                    for (int32_t i = 0; i < static_cast<int32_t> (sizeAndDepth.vectorSize); ++i)
                        resultCast.arguments.addReference (AST::createBinaryOp (block, combinerOp,
                                                                                AST::createGetElement (block, realResult, i),
                                                                                AST::createGetElement (block, imagResult, i)));
                    AST::addReturnStatement (block, resultCast);
                }
            }
            else
            {
                auto& resultCast = block.allocateChild<AST::Cast>();
                resultCast.targetType.createReferenceTo (returnType);
                auto& resultArgs = resultCast.arguments;

                if (op == OperatorFunction::negate)
                {
                    resultArgs.addReference (AST::createUnaryOp (block, AST::UnaryOpTypeEnum::Enum::negate, real1));
                    resultArgs.addReference (AST::createUnaryOp (block, AST::UnaryOpTypeEnum::Enum::negate, imag1));
                    AST::addReturnStatement (block, resultCast);
                    return f;
                }

                auto arg2 = AST::addFunctionParameter (f, argType, "b");
                auto& real2 = AST::createGetStructMember (block, arg2, "real");
                auto& imag2 = AST::createGetStructMember (block, arg2, "imag");

                switch (op)
                {
                case OperatorFunction::add:
                    resultArgs.addReference (AST::createAdd (block, real1, real2));
                    resultArgs.addReference (AST::createAdd (block, imag1, imag2));
                    break;

                case OperatorFunction::subtract:
                    resultArgs.addReference (AST::createSubtract (block, real1, real2));
                    resultArgs.addReference (AST::createSubtract (block, imag1, imag2));
                    break;

                case OperatorFunction::multiply:
                    resultArgs.addReference (AST::createSubtract (block, AST::createMultiply (block, real1, real2),
                                                                         AST::createMultiply (block, imag1, imag2)));
                    resultArgs.addReference (AST::createAdd (block, AST::createMultiply (block, real1, imag2),
                                                                    AST::createMultiply (block, imag1, real2)));
                    break;

                case OperatorFunction::divide:
                    {
                        auto& scale = AST::createAdd (block, AST::createMultiply (block, real2, real2),
                                                             AST::createMultiply (block, imag2, imag2));
                        auto& r = AST::createAdd (block, AST::createMultiply (block, real1, real2),
                                                         AST::createMultiply (block, imag1, imag2));
                        auto& i = AST::createSubtract (block, AST::createMultiply (block, imag1, real2),
                                                              AST::createMultiply (block, real1, imag2));
                        resultArgs.addReference (AST::createDivide (block, r, scale));
                        resultArgs.addReference (AST::createDivide (block, i, scale));
                    }
                    break;

                case OperatorFunction::equal:
                    resultArgs.addReference (AST::createBinaryOp (block, AST::BinaryOpTypeEnum::Enum::equals, real1, real2));
                    resultArgs.addReference (AST::createBinaryOp (block, AST::BinaryOpTypeEnum::Enum::equals, imag1, imag2));
                    break;

                case OperatorFunction::notequal:
                    resultArgs.addReference (AST::createBinaryOp (block, AST::BinaryOpTypeEnum::Enum::notEquals, real1, real2));
                    resultArgs.addReference (AST::createBinaryOp (block, AST::BinaryOpTypeEnum::Enum::notEquals, imag1, imag2));
                    break;

                case OperatorFunction::negate:
                case OperatorFunction::none:
                default:
                    CMAJ_ASSERT_FALSE;
                    break;
                }

                AST::addReturnStatement (block, resultCast);
            }
            return f;
        }

        AST::Namespace& intrinsicsNamespace;
        std::unordered_map<uint32_t, AST::StructType*> types;
        std::unordered_map<uint32_t, AST::Function*> functions;
    };

    //==============================================================================
    struct ConvertOperatorsToFunctions  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ConvertOperatorsToFunctions (ComplexSupportLibrary& l)
            : super (l.intrinsicsNamespace.context.allocator), library (l) {}

        ComplexSupportLibrary& library;

        void visit (AST::UnaryOperator& u) override
        {
            super::visit (u);

            if (u.op == AST::UnaryOpTypeEnum::Enum::negate)
            {
                auto& input = AST::castToValueRef (u.input);
                auto& type = *input.getResultType();

                if (type.isComplexOrVectorOfComplex())
                    replaceWithFunctionCall (u, OperatorFunction::negate, type, type, input, {});
            }
        }

        void visit (AST::BinaryOperator& b) override
        {
            super::visit (b);

            auto op = getFunctionForOp (b.op.get());

            if (op != OperatorFunction::none)
            {
                auto types = b.getOperatorTypes();

                if (types.operandType.isComplexOrVectorOfComplex())
                    replaceWithFunctionCall (b, op, types.resultType, types.operandType,
                                             AST::castToValueRef (b.lhs),
                                             AST::castToValueRef (b.rhs));
            }
        }

        void visit (AST::InPlaceOperator& op) override
        {
            super::visit (op);

            auto& target = AST::castToValueRef (op.target);

            if (target.getResultType()->isComplexOrVectorOfComplex())
            {
                auto& resultValue = AST::createBinaryOp (op, op.op.get(), target, AST::castToValueRef (op.source));
                auto& assignment = AST::createAssignment (op.context, target, resultValue);
                op.replaceWith (assignment);

                visitObject (assignment);
            }
        }

        void visit (AST::ConstantAggregate& c) override
        {
            if (! c.values.empty())
            {
                auto& targetType = AST::castToTypeBaseRef (c.type);

                if (auto elementType = targetType.getArrayOrVectorElementType())
                    if (elementType->isPrimitive() && ! elementType->isComplexOrVectorOfComplex())
                        return; // avoid recursing into large aggregates if there's no need

                super::visit (c);

                if (targetType.isComplexOrVectorOfComplex())
                {
                    auto& cast = c.allocateChild<AST::Cast>();
                    cast.targetType.createReferenceTo (targetType);
                    cast.arguments.moveListItems (c.values);
                    c.replaceWith (cast);
                }
            }
        }

        void visit (AST::GetArrayOrVectorSlice& s) override
        {
            super::visit (s);

            auto& parent = AST::castToValueRef (s.parent);

            if (parent.getResultType()->isComplexOrVectorOfComplex())
                throwError (parent, Errors::unimplementedFeature ("slices of complex vectors"));
        }

        static OperatorFunction getFunctionForOp (AST::BinaryOpTypeEnum::Enum op)
        {
            if (op == AST::BinaryOpTypeEnum::Enum::add)        return OperatorFunction::add;
            if (op == AST::BinaryOpTypeEnum::Enum::subtract)   return OperatorFunction::subtract;
            if (op == AST::BinaryOpTypeEnum::Enum::multiply)   return OperatorFunction::multiply;
            if (op == AST::BinaryOpTypeEnum::Enum::divide)     return OperatorFunction::divide;
            if (op == AST::BinaryOpTypeEnum::Enum::equals)     return OperatorFunction::equal;
            if (op == AST::BinaryOpTypeEnum::Enum::notEquals)  return OperatorFunction::notequal;
            return OperatorFunction::none;
        }

        void replaceWithFunctionCall (AST::ValueBase& o, OperatorFunction op,
                                      const AST::TypeBase& returnType, const AST::TypeBase& operandType,
                                      AST::ValueBase& arg1, ptr<AST::ValueBase> arg2)
        {
            auto& call = AST::createFunctionCall (o, library.getOperatorFunction (op, returnType, operandType),
                                                  AST::createCastIfNeeded (operandType, arg1));

            if (arg2 != nullptr)
                call.arguments.addReference (AST::createCastIfNeeded (operandType, *arg2));

            o.replaceWith (call);
        }
    };

    //==============================================================================
    struct ConvertCasts  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ConvertCasts (AST::Allocator& a) : super (a) {}

        CMAJ_DO_NOT_VISIT_CONSTANTS

        void visit (AST::Cast& cast) override
        {
            super::visit (cast);

            if (! cast.arguments.empty())
            {
                auto& targetType = AST::castToTypeBaseRef (cast.targetType);

                if (targetType.isComplexOrVectorOfComplex())
                {
                    convertCast (cast, targetType);
                }
                else if (targetType.isArray() && targetType.getArrayOrVectorElementType()->isComplexOrVectorOfComplex())
                {
                    auto firstArg = AST::castToValue (cast.arguments[0]);
                    auto firstArgType = firstArg->getResultType();

                    if (cast.arguments.size() == 1 && firstArgType->isArray())
                    {
                        CMAJ_ASSERT (firstArgType->isArray() && firstArgType->getNumDimensions() == 1 && targetType.getNumDimensions() == 1);
                        CMAJ_ASSERT (firstArgType->getArrayOrVectorSize(0) == targetType.getArrayOrVectorSize(0));

                        size_t indexOffset = 0;

                        if (auto arraySlice = AST::castTo<AST::GetArrayOrVectorSlice> (firstArg))
                        {
                            if (auto startPos = AST::castTo<AST::ConstantValueBase> (arraySlice->start))
                            {
                                indexOffset = static_cast<size_t> (*startPos->getAsInt32());
                                firstArg = AST::castToValue (arraySlice->parent);
                            }
                        }

                        cast.arguments.reset();

                        // Source must be an array of the same size
                        for (size_t n = 0; n < targetType.getArrayOrVectorSize(0); n++)
                        {
                            auto& sourceElement = AST::createGetElement (cast.context, firstArg, static_cast<int32_t> (indexOffset + n));
                            auto& c = cast.allocateChild<AST::Cast>();

                            c.targetType.createReferenceTo (targetType.getArrayOrVectorElementType());
                            c.arguments.addReference (sourceElement);

                            convertCast (c, *targetType.getArrayOrVectorElementType());
                            cast.arguments.addReference (c);
                        }
                    }
                }
            }
        }

        void convertCast (AST::Cast& cast, const AST::TypeBase& targetType)
        {
            VectorSizeAndDepth destSizeAndDepth (targetType);

            if (destSizeAndDepth.vectorSize != 0)
            {
                bool is64Bit = targetType.getArrayOrVectorElementType()->isPrimitiveComplex64();
                auto& elementType = is64Bit ? cast.context.allocator.float64Type
                                            : cast.context.allocator.float32Type;
                auto& targetComponentType = cast.context.allocator.createVectorType (elementType, destSizeAndDepth.vectorSize);
                auto& firstArg = AST::castToValueRef (cast.arguments[0]);
                auto& firstArgType = *firstArg.getResultType();

                if (cast.arguments.size() == 2
                     && firstArgType.isVector()
                     && firstArgType.getNumDimensions() == 1
                     && firstArgType.getArrayOrVectorSize(0) == destSizeAndDepth.vectorSize
                     && firstArgType.getArrayOrVectorElementType()->isSameType (elementType, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                {
                    // leave a pair of float vectors to be used as the real + imag component vectors
                    return;
                }

                if (cast.arguments.size() == 1 && firstArgType.isComplexOrVectorOfComplex())
                {
                    VectorSizeAndDepth sourceSizeAndDepth (firstArgType);

                    if (sourceSizeAndDepth.vectorSize == destSizeAndDepth.vectorSize)
                    {
                        if (sourceSizeAndDepth.is64Bit != destSizeAndDepth.is64Bit)
                        {
                            // numeric cast
                            auto& real = AST::createCast (targetComponentType, AST::createGetStructMember (cast, firstArg, "real"));
                            auto& imag = AST::createCast (targetComponentType, AST::createGetStructMember (cast, firstArg, "imag"));

                            cast.arguments.reset();
                            cast.arguments.addReference (real);
                            cast.arguments.addReference (imag);
                        }

                        return;
                    }
                }

                auto& reals = cast.allocateChild<AST::Cast>();
                reals.targetType.createReferenceTo (targetComponentType);

                auto& imags = cast.allocateChild<AST::Cast>();
                imags.targetType.createReferenceTo (targetComponentType);

                for (auto& arg : cast.arguments)
                {
                    auto& argValue = AST::castToValueRef (arg);

                    if (argValue.getResultType()->isPrimitiveComplex())
                    {
                        reals.arguments.addReference (AST::createGetStructMember (cast, argValue, "real"));
                        imags.arguments.addReference (AST::createGetStructMember (cast, argValue, "imag"));
                    }
                    else
                    {
                        reals.arguments.addReference (argValue);
                        imags.arguments.addReference (elementType.allocateConstantValue (cast.context));
                    }
                }

                cast.arguments.reset();
                cast.arguments.addReference (reals);
                cast.arguments.addReference (imags);
            }
            else
            {
                if (cast.arguments.size() == 1)
                {
                    auto& arg = AST::castToValueRef (cast.arguments[0]);
                    auto& argType = *arg.getResultType();

                    if (targetType.isSameType(argType, AST::TypeBase::ComparisonFlags::failOnAllDifferences))
                    {
                        cast.replaceWith (arg);
                        return;
                    }

                    auto& elementType = targetType.isPrimitiveComplex64() ? cast.context.allocator.float64Type
                                                                          : cast.context.allocator.float32Type;

                    if (argType.isComplexOrVectorOfComplex())
                    {
                        VectorSizeAndDepth sourceSizeAndDepth (argType);

                        if (sourceSizeAndDepth.vectorSize == destSizeAndDepth.vectorSize)
                        {
                            if (sourceSizeAndDepth.is64Bit != destSizeAndDepth.is64Bit)
                            {
                                // numeric cast
                                auto& real = AST::createCast (elementType, AST::createGetStructMember (cast, arg, "real"));
                                auto& imag = AST::createCast (elementType, AST::createGetStructMember (cast, arg, "imag"));

                                cast.arguments.reset();
                                cast.arguments.addReference (real);
                                cast.arguments.addReference (imag);
                                return;
                            }
                        }
                    }

                    cast.arguments.addReference (elementType.allocateConstantValue (cast.context));
                }
            }
        }
    };

    //==============================================================================
    struct ConvertVectorsToStructs  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ConvertVectorsToStructs (ComplexSupportLibrary& l)
            : super (l.intrinsicsNamespace.context.allocator), library (l) {}

        ComplexSupportLibrary& library;

        // NB: must do all the vectors before going back and doing the primitives, because
        // there are situations where the wrong visit order ends up replacing the element type
        // of a vector with a struct
        void visit (AST::VectorType& t) override
        {
            if (t.isComplexOrVectorOfComplex())
                t.replaceWith (AST::createReference (t, library.getStructTypeFor (t)));
        }
    };

    //==============================================================================
    struct ConvertPrimitivesAndConstantsToStructs  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ConvertPrimitivesAndConstantsToStructs (ComplexSupportLibrary& l)
            : super (l.intrinsicsNamespace.context.allocator), library (l) {}

        ComplexSupportLibrary& library;

        void visit (AST::PrimitiveType& t) override
        {
            super::visit (t);

            if (t.isPrimitiveComplex())
                t.replaceWith (AST::createReference (t, library.getStructTypeFor (t)));
        }

        void visit (AST::ConstantComplex32& c) override
        {
            auto value = c.getComplexValue();
            auto& agg = c.allocateChild<AST::ConstantAggregate>();
            agg.type.createReferenceTo (library.getStructTypeFor ({ 0, false }));
            agg.values.addReference (c.context.allocator.createConstantFloat32 (value.real()));
            agg.values.addReference (c.context.allocator.createConstantFloat32 (value.imag()));
            c.replaceWith (agg);
        }

        void visit (AST::ConstantComplex64& c) override
        {
            auto value = c.getComplexValue();
            auto& agg = c.allocateChild<AST::ConstantAggregate>();
            agg.type.createReferenceTo (library.getStructTypeFor ({ 0, true }));
            agg.values.addReference (c.context.allocator.createConstantFloat64 (value.real()));
            agg.values.addReference (c.context.allocator.createConstantFloat64 (value.imag()));
            c.replaceWith (agg);
        }
    };

    // need to do this in a set of passes as they can interfere with each other..
    ComplexSupportLibrary library (program.rootNamespace);
    ConvertOperatorsToFunctions (library).visitObject (program.rootNamespace);
    ConvertCasts (program.allocator).visitObject (program.rootNamespace);
    ConvertVectorsToStructs (library).visitObject (program.rootNamespace);
    ConvertPrimitivesAndConstantsToStructs (library).visitObject (program.rootNamespace);
}

}
