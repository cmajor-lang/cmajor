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
static inline void replaceMultidimensionalArrays (AST::Program& program)
{
    struct ReplaceMultidimensionalAccesses  : public AST::NonParameterisedObjectVisitor
    {
        using super = AST::NonParameterisedObjectVisitor;
        using super::visit;

        ReplaceMultidimensionalAccesses (AST::Namespace& root) : super (root.context.allocator)
        {}

        // need to save all the changes until the end: otherwise it screws up the types while we're iterating
        std::vector<std::function<void()>> deferredChanges;

        void visit (AST::ArrayType& a) override
        {
            super::visit (a);

            if (a.getNumDimensions() > 1)
            {
                ptr<AST::ValueBase> combinedSize;

                for (auto& d : a.dimensionList)
                {
                    auto& dim = AST::castToValueRef (d);

                    if (combinedSize == nullptr)
                        combinedSize = dim;
                    else
                        combinedSize = AST::foldToConstantIfPossible (AST::createMultiply (*combinedSize, *combinedSize, dim));
                }

                deferredChanges.push_back ([&a, combinedSize]
                {
                    a.setArraySize (*combinedSize);
                });
            }
        }

        void visit (AST::GetElement& g) override
        {
            super::visit (g);

            if (auto parent = AST::castToValue (g.parent))
            {
                if (auto arrayType = AST::castTo<AST::ArrayType> (parent->getResultType()->skipConstAndRefModifiers()))
                {
                    auto numDimensions = static_cast<uint32_t> (arrayType->getNumDimensions());

                    if (numDimensions > 1)
                    {
                        auto numIndexes = static_cast<uint32_t> (g.indexes.size());
                        CMAJ_ASSERT (numDimensions >= numIndexes);

                        auto& context = AST::getContext (g);

                        auto getIndex     = [&] (uint32_t i) -> AST::ValueBase& { return AST::castToValueRef (g.indexes[i]); };
                        auto getDimension = [&] (uint32_t i) -> AST::ValueBase& { return AST::castToValueRef (arrayType->dimensionList[i]); };

                        ref<AST::ValueBase> flattenedOffset = getIndex(0);

                        for (uint32_t i = 1; i < numIndexes; ++i)
                        {
                            auto& nextTerm = AST::foldToConstantIfPossible (AST::createMultiply (context, flattenedOffset, getDimension(i)));
                            flattenedOffset = AST::foldToConstantIfPossible (AST::createAdd (context, nextTerm, getIndex(i)));
                        }

                        if (numDimensions == numIndexes)
                        {
                            deferredChanges.push_back ([&g, flattenedOffset]
                            {
                                g.indexes.reset();
                                g.indexes.addChildObject (flattenedOffset);
                            });
                        }
                        else
                        {
                            ptr<AST::ValueBase> size;

                            for (uint32_t i = numIndexes; i < numDimensions; ++i)
                            {
                                auto& dim = getDimension(i);
                                flattenedOffset = AST::foldToConstantIfPossible (AST::createMultiply (context, flattenedOffset, dim));

                                if (size == nullptr)
                                    size = dim;
                                else
                                    size = AST::foldToConstantIfPossible (AST::createMultiply (context, *size, dim));
                            }

                            auto& end = AST::foldToConstantIfPossible (AST::createAdd (context, flattenedOffset, *size));

                            auto& slice = g.context.allocate<AST::GetArrayOrVectorSlice>();
                            slice.parent.referTo (g.parent.getObjectRef());
                            slice.start.setChildObject (flattenedOffset);
                            slice.end.setChildObject (end);

                            deferredChanges.push_back ([&g, &slice]
                            {
                                g.replaceWith (slice);
                            });
                        }
                    }
                }
            }
        }

        void visit (AST::ConstantAggregate& c) override
        {
            auto& type = c.getType();

            if (type.isVectorOrArray())
            {
                auto numDimensions = type.getNumDimensions();

                if (numDimensions > 1)
                {
                    auto& flattened = c.context.allocate<AST::ConstantAggregate>();
                    flattened.type.referTo (type);
                    addFlattenedSubItems (type, flattened.values, c, numDimensions);

                    deferredChanges.push_back ([&c, &flattened]
                    {
                        c.replaceWith (flattened);
                    });
                }
            }

            super::visit (c);
        }

        void visit (AST::Cast& c) override
        {
            super::visit (c);

            auto& type = *c.getResultType();

            if (type.isVectorOrArray())
            {
                auto numDimensions = type.getNumDimensions();

                if (numDimensions > 1)
                {
                    if (c.arguments.size() == 1)
                        if (auto sourceArray = AST::castTo<AST::ArrayType> (AST::castToValueRef (c.arguments[0]).getResultType()->skipConstAndRefModifiers()))
                            if (sourceArray->resolveFlattenedSize() == AST::castToRef<AST::ArrayType> (type).resolveFlattenedSize())
                                return;

                    auto& flattened = c.context.allocate<AST::Cast>();
                    flattened.targetType.referTo (type);
                    flattened.onlySilentCastsAllowed = c.onlySilentCastsAllowed.get();
                    addFlattenedSubItems (type, flattened.arguments, c, numDimensions);

                    deferredChanges.push_back ([&c, &flattened]
                    {
                        c.replaceWith (flattened);
                    });
                }
            }
        }

        void addFlattenedSubItems (const AST::TypeBase& multidimensionalType, AST::ListProperty& newList,
                                   AST::Object& source, uint32_t numLevelsToFlatten)
        {
            if (numLevelsToFlatten != 0 && multidimensionalType.isVectorOrArray())
            {
                if (auto agg = AST::castTo<AST::ConstantAggregate> (source))
                {
                    auto numItems = multidimensionalType.getArrayOrVectorSize (0);
                    auto& innerType = *multidimensionalType.getArrayOrVectorElementType();

                    for (uint32_t i = 0; i < numItems; ++i)
                        addFlattenedSubItems (innerType, newList, *agg->getOrCreateAggregateElementValue (i), numLevelsToFlatten - 1);

                    return;
                }

                auto numItems = multidimensionalType.getArrayOrVectorSize (0);

                if (auto cast = AST::castTo<AST::Cast> (source))
                {
                    auto& innerType = *multidimensionalType.getArrayOrVectorElementType();
                    auto numArgs = cast->arguments.size();

                    if (numArgs == 0)
                    {
                        auto& zero = innerType.allocateConstantValue (innerType.context);

                        for (uint32_t i = 0; i < numItems; ++i)
                            addFlattenedSubItems (innerType, newList, zero, numLevelsToFlatten - 1);
                    }
                    else if (numArgs == 1)
                    {
                        auto& value = AST::castToValueRef (cast->arguments[0]);

                        for (uint32_t i = 0; i < numItems; ++i)
                            addFlattenedSubItems (innerType, newList, value, numLevelsToFlatten - 1);
                    }
                    else
                    {
                        CMAJ_ASSERT (numArgs == numItems);

                        for (uint32_t i = 0; i < numItems; ++i)
                            addFlattenedSubItems (innerType, newList, cast->arguments[i].getObjectRef(), numLevelsToFlatten - 1);
                    }

                    return;
                }

                auto& sourceType = *AST::castToValueRef (source).getResultType();

                if (sourceType.isFixedSizeAggregate())
                {
                    CMAJ_ASSERT (sourceType.getFixedSizeAggregateNumElements() == numItems);
                    auto& innerType = *multidimensionalType.getArrayOrVectorElementType();

                    for (uint32_t i = 0; i < numItems; ++i)
                        addFlattenedSubItems (innerType, newList, AST::createGetElement (source.context, source, static_cast<int32_t> (i)), numLevelsToFlatten - 1);

                    return;
                }

                for (uint32_t i = 0; i < numItems; ++i)
                    newList.addReference (source);

                return;
            }

            newList.addReference (source);
        }
    };

    ReplaceMultidimensionalAccesses ra (program.rootNamespace);
    ra.visitObject (program.rootNamespace);

    for (auto& f : ra.deferredChanges)
        f();
}

}
