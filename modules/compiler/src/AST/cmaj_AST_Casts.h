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


template <typename TargetType> static ptr<TargetType> castTo (Object& o)              { return ptr<TargetType> (castObject<TargetType> (o)); }
template <typename TargetType> static ptr<const TargetType> castTo (const Object& o)  { return ptr<const TargetType> (castObject<TargetType> (o)); }
template <typename TargetType> static ptr<TargetType> castTo (const Property& p)      { return castTo<TargetType> (p.getObject()); }

template <typename TargetType, typename SourceType>
static auto castTo (SourceType* source)
{
    if (source != nullptr)
        return castTo<TargetType> (*source);

    return decltype (castTo<TargetType> (*source)) {};
}

template <typename TargetType, typename SourceType>
static auto castTo (ref<SourceType>& source)
{
    return castTo<TargetType> (source.get());
}

template <typename TargetType, typename SourceType>
static auto castTo (ptr<SourceType> source)
{
    return castTo<TargetType> (source.get());
}

template <typename TargetType, typename SourceType>
static auto& castToRef (SourceType&& source)
{
    return *castTo<TargetType> (source);
}

template <typename TargetType, typename SourceType>
static auto castToSkippingReferences (SourceType&& source)
{
    auto o = castTo<Object> (source).get();

    for (;;)
    {
        auto result = castTo<TargetType> (o);

        if (result != nullptr)
            return result;

        if (o != nullptr)
        {
            o = o->getTargetSkippingReferences().get();

            if (o != nullptr)
                continue;
        }

        return result;
    }
}

template <typename TargetType, typename SourceType>
static auto& castToRefSkippingReferences (SourceType&& source)
{
    auto* o = castTo<Object> (source).get();
    CMAJ_ASSERT (o != nullptr);

    for (;;)
    {
        if (auto result = castTo<TargetType> (*o))
            return *result;

        o = o->getTargetSkippingReferences().get();
        CMAJ_ASSERT (o != nullptr);
    }
}

//==============================================================================
template <typename SourceType>
static auto castToTypeBase (SourceType&& source)                    { return castToSkippingReferences<TypeBase> (source); }

template <typename SourceType>
static auto& castToTypeBaseRef (SourceType&& source)                { return castToRefSkippingReferences<TypeBase> (source); }

template <typename SourceType>
static auto castToValue (SourceType&& source)                       { return castToSkippingReferences<ValueBase> (source); }

template <typename SourceType>
static auto& castToValueRef (SourceType&& source)                   { return castToRefSkippingReferences<ValueBase> (source); }

template <typename SourceType>
static auto castToConstant (SourceType&& source)                    { return castToSkippingReferences<ConstantValueBase> (source); }

template <typename SourceType>
static auto& castToConstantRef (SourceType&& source)                { return castToRefSkippingReferences<ConstantValueBase> (source); }

template <typename SourceType>
static auto& castToExpressionRef (SourceType&& source)              { return castToRef<Expression> (source); }

template <typename SourceType>
static auto castToVariableDeclaration (SourceType&& source)         { return castToSkippingReferences<VariableDeclaration> (source); }

template <typename SourceType>
static auto castToAssignment (SourceType&& source)                  { return castToSkippingReferences<Assignment> (source); }

template <typename SourceType>
static auto& castToVariableDeclarationRef (SourceType&& source)     { return castToRefSkippingReferences<VariableDeclaration> (source); }

template <typename SourceType>
static auto castToFunction (SourceType&& source)                    { return castToSkippingReferences<Function> (source); }

template <typename SourceType>
static auto& castToFunctionRef (SourceType&& source)                { return castToRefSkippingReferences<Function> (source); }

template <typename ObjectType, typename SourceType>
static ObjectRefVector<ObjectType> convertToList (SourceType&& source)
{
    if (auto o = castToSkippingReferences<ObjectType> (source))
    {
        ObjectRefVector<ObjectType> result;
        result.emplace_back (*o);
        return result;
    }

    return {};
}

template <typename PrimitiveType>
static std::optional<PrimitiveType> getAsOptionalPrimitive (const AST::Object& value)
{
    return castToRef<const AST::ConstantValueBase> (value).castToPrimitive<PrimitiveType>();
}

template <typename PrimitiveType>
static PrimitiveType getAsPrimitive (const AST::Object& value)
{
    auto v = getAsOptionalPrimitive<PrimitiveType> (value);
    CMAJ_ASSERT (v.has_value());
    return *v;
}

static int64_t getAsInt64 (const AST::Object& value)    { return getAsPrimitive<int64_t> (value); }
static bool getAsBool (const AST::Object& value)        { return getAsPrimitive<bool> (value); }

//==============================================================================
template <typename ObjectType>
static inline auto& getContext (ptr<ObjectType> o)              { return getContext (*o); }
static inline auto& getContext (const Object& o)                { return o.context; }
static inline auto& getContext (const Object* o)                { CMAJ_ASSERT (o != nullptr); return o->context; }
static inline auto& getContext (const Property& p)              { return p.getContext(); }
static inline auto& getContext (const ObjectContext& c)         { return c; }

static inline auto& getContextOfStartOfExpression (const Object& o)         { return o.getLocationOfStartOfExpression(); }
static inline auto& getContextOfStartOfExpression (const Property& p)       { return getContextOfStartOfExpression (p.getObjectRef()); }
static inline auto& getContextOfStartOfExpression (const ref<Object>& o)    { return getContextOfStartOfExpression (o.get()); }


//==============================================================================
template <typename PropertyType>
static bool isResolvedAsType (PropertyType& property)
{
    if (auto t = castToTypeBase (property))
        return t->isResolved();

    return false;
}

template <typename ObjectOrProperty>
static bool isCompileTimeConstant (const ObjectOrProperty& o)
{
    if (auto v = castToValue (o))
        return v->isCompileTimeConstant();

    if (auto v = castToVariableDeclaration (o))
        return v->isCompileTimeConstant();

    return false;
}

template <typename ObjectOrProperty>
static ptr<ConstantValueBase> getAsFoldedConstant (const ObjectOrProperty& o)
{
    if (auto v = castToValue (o))
        return v->constantFold();

    if (auto v = castToVariableDeclaration (o))
        if (v->isConstant && v->initialValue != nullptr)
            return getAsFoldedConstant (v->initialValue);

    return {};
}

static ValueBase& foldToConstantIfPossible (ValueBase& v)
{
    if (auto c = getAsFoldedConstant (v))
        return *c;

    return v;
}
