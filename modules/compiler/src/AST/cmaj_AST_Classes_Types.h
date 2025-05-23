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


struct TypeBase  : public Expression
{
    TypeBase (const ObjectContext& c) : Expression (c) {}
    ~TypeBase() override = default;

    TypeBase* getAsTypeBase() override                 { return this; }
    const TypeBase* getAsTypeBase() const override     { return this; }
    bool isTypeBase() const override                   { return true; }

    virtual bool isVoid() const                     { return false; }
    virtual bool isPrimitive() const                { return false; }
    virtual bool isPrimitiveBool() const            { return false; }
    virtual bool isPrimitiveInt() const             { return false; }
    virtual bool isPrimitiveInt32() const           { return false; }
    virtual bool isPrimitiveInt64() const           { return false; }
    virtual bool isPrimitiveFloat() const           { return false; }
    virtual bool isPrimitiveFloat32() const         { return false; }
    virtual bool isPrimitiveFloat64() const         { return false; }
    virtual bool isPrimitiveComplex() const         { return false; }
    virtual bool isPrimitiveComplex32() const       { return false; }
    virtual bool isPrimitiveComplex64() const       { return false; }
    virtual bool isPrimitiveString() const          { return false; }
    virtual bool isEnum() const                     { return false; }
    virtual bool isArray() const                    { return false; }
    virtual bool isVector() const                   { return false; }
    virtual bool isVectorSize1() const              { return false; }
    virtual bool isVectorOrArray() const            { return false; }
    virtual bool isSlice() const                    { return false; }
    virtual bool isFixedSizeArray() const           { return false; }
    virtual bool isFixedSizeAggregate() const       { return false; }
    virtual bool isStruct() const                   { return false; }
    virtual bool isScalar() const                   { return false; }
    virtual bool isScalar32() const                 { return false; }
    virtual bool isScalar64() const                 { return false; }
    virtual bool isFloatOrVectorOfFloat() const     { return false; }
    virtual bool isComplexOrVectorOfComplex() const { return isPrimitiveComplex(); }
    virtual bool canBeVectorElementType() const     { return false; }
    virtual bool containsSlice() const              { return false; }

    virtual bool isConst() const                    { return false; }
    virtual bool isReference() const                { return false; }
    bool isNonConstReference() const                { return isReference() && ! isConst(); }
    bool isConstReference() const                   { return isReference() && isConst(); }
    bool isNonConstSlice() const                    { return ! isConst() && isSlice(); }

    virtual TypeBase& skipConstAndRefModifiers()                { return *this; }
    virtual const TypeBase& skipConstAndRefModifiers() const    { return *this; }

    virtual uint32_t getNumDimensions() const                                { CMAJ_ASSERT_FALSE; }
    virtual ArraySize getVectorSize() const                                  { return 0; }
    virtual ArraySize getArrayOrVectorSize (uint32_t dimensionIndex) const   { (void) dimensionIndex; CMAJ_ASSERT_FALSE; }
    virtual ptr<const TypeBase> getArrayOrVectorElementType() const          { return {}; }
    virtual ArraySize getFixedSizeAggregateNumElements() const               { CMAJ_ASSERT_FALSE; }
    virtual ptr<const TypeBase> getAggregateElementType (size_t) const       { CMAJ_ASSERT_FALSE; }
    virtual IntegerRange getAddressableIntegerRange() const                  { return {}; }

    virtual size_t getPackedStorageSize() const     { CMAJ_ASSERT_FALSE; }

    virtual ConstantValueBase& allocateConstantValue (const ObjectContext&) const = 0;

    enum ComparisonFlags : int
    {
        failOnAllDifferences   = 0,
        ignoreReferences       = 1,
        ignoreConst            = 2,
        ignoreVectorSize1      = 4,
        duckTypeStructures     = 8
    };

    virtual bool isSameType (const TypeBase&, int flags) const = 0;

    virtual std::string getLayoutSignature() const = 0;

    virtual choc::value::Type toChocType() const            { CMAJ_ASSERT_FALSE; }

    virtual ObjectRefVector<TypeBase> getResolvedReferencedTypes() const     { return {}; }

    void addSideEffects (SideEffects&) const override   {}

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
    }

    bool isResolved() const
    {
        if (resolved)
            return true;

        resolved = checkResolved();
        return resolved;
    }

protected:
    virtual bool checkResolved() const = 0;
    mutable bool resolved = false;
};

//==============================================================================
struct PrimitiveType  : public TypeBase
{
    PrimitiveType (const ObjectContext& c, PrimitiveTypeEnum::Enum t = PrimitiveTypeEnum::Enum::int32)
        : TypeBase (c)
    {
        resolved = true;
        type.set (t);
    }

    PooledString getName() const override           { return getTypeName(); }
    bool checkResolved() const override             { return true; }

    CMAJ_AST_DECLARE_STANDARD_METHODS(PrimitiveType, 18)

    PooledString getTypeName() const
    {
        switch (type.get())
        {
            case PrimitiveTypeEnum::Enum::void_:        return getStrings().voidTypeName;
            case PrimitiveTypeEnum::Enum::int32:        return getStrings().int32TypeName;
            case PrimitiveTypeEnum::Enum::int64:        return getStrings().int64TypeName;
            case PrimitiveTypeEnum::Enum::float32:      return getStrings().float32TypeName;
            case PrimitiveTypeEnum::Enum::float64:      return getStrings().float64TypeName;
            case PrimitiveTypeEnum::Enum::complex32:    return getStrings().complex32TypeName;
            case PrimitiveTypeEnum::Enum::complex64:    return getStrings().complex64TypeName;
            case PrimitiveTypeEnum::Enum::boolean:      return getStrings().boolTypeName;
            case PrimitiveTypeEnum::Enum::string:       return getStrings().stringTypeName;
            default:                                    CMAJ_ASSERT_FALSE;
        }
    }

    bool isPrimitive() const override            { return true; }
    bool isScalar() const override               { return isPrimitiveInt() || isPrimitiveFloat() || isPrimitiveComplex(); }
    bool isScalar32() const override             { return type == PrimitiveTypeEnum::Enum::int32 || type == PrimitiveTypeEnum::Enum::float32 || type == PrimitiveTypeEnum::Enum::complex32; }
    bool isScalar64() const override             { return type == PrimitiveTypeEnum::Enum::int64 || type == PrimitiveTypeEnum::Enum::float64 || type == PrimitiveTypeEnum::Enum::complex64; }
    bool isVoid() const override                 { return type == PrimitiveTypeEnum::Enum::void_; }
    bool isPrimitiveBool() const override        { return type == PrimitiveTypeEnum::Enum::boolean; }
    bool isPrimitiveInt() const override         { return type == PrimitiveTypeEnum::Enum::int32 || type == PrimitiveTypeEnum::Enum::int64; }
    bool isPrimitiveInt32() const override       { return type == PrimitiveTypeEnum::Enum::int32; }
    bool isPrimitiveInt64() const override       { return type == PrimitiveTypeEnum::Enum::int64; }
    bool isPrimitiveFloat() const override       { return type == PrimitiveTypeEnum::Enum::float32 || type == PrimitiveTypeEnum::Enum::float64; }
    bool isPrimitiveFloat32() const override     { return type == PrimitiveTypeEnum::Enum::float32; }
    bool isPrimitiveFloat64() const override     { return type == PrimitiveTypeEnum::Enum::float64; }
    bool isFloatOrVectorOfFloat() const override { return isPrimitiveFloat(); }
    bool isPrimitiveString() const override      { return type == PrimitiveTypeEnum::Enum::string; }
    bool isPrimitiveComplex() const override     { return type == PrimitiveTypeEnum::Enum::complex32 || type == PrimitiveTypeEnum::Enum::complex64; }
    bool isPrimitiveComplex32() const override   { return type == PrimitiveTypeEnum::Enum::complex32; }
    bool isPrimitiveComplex64() const override   { return type == PrimitiveTypeEnum::Enum::complex64; }
    bool canBeVectorElementType() const override { return ! (isVoid() || isPrimitiveString()); }
    ArraySize getVectorSize() const override     { return 1; }

    IntegerRange getAddressableIntegerRange() const override
    {
        if (isPrimitiveInt32()) return IntegerRange::forType<int32_t>();
        if (isPrimitiveInt64()) return IntegerRange::forType<int64_t>();
        return {};
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto prim = other.getAsPrimitiveType())
            return type.get() == prim->type;

        return other.isSameType (*this, flags);
    }

    std::string getLayoutSignature() const override
    {
        switch (type.get())
        {
            case PrimitiveTypeEnum::Enum::void_:        return "void";
            case PrimitiveTypeEnum::Enum::int32:        return "i32";
            case PrimitiveTypeEnum::Enum::int64:        return "i64";
            case PrimitiveTypeEnum::Enum::float32:      return "f32";
            case PrimitiveTypeEnum::Enum::float64:      return "f64";
            case PrimitiveTypeEnum::Enum::complex32:    return "c32";
            case PrimitiveTypeEnum::Enum::complex64:    return "c64";
            case PrimitiveTypeEnum::Enum::boolean:      return "bool";
            case PrimitiveTypeEnum::Enum::string:       return "string";
            default:                                    CMAJ_ASSERT_FALSE; return {};
        }
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << getLayoutSignature();
    }

    size_t getPackedStorageSize() const override
    {
        switch (type.get())
        {
            case PrimitiveTypeEnum::Enum::void_:        return 0;
            case PrimitiveTypeEnum::Enum::int32:        return 4;
            case PrimitiveTypeEnum::Enum::int64:        return 8;
            case PrimitiveTypeEnum::Enum::float32:      return 4;
            case PrimitiveTypeEnum::Enum::float64:      return 8;
            case PrimitiveTypeEnum::Enum::complex32:    return 8;
            case PrimitiveTypeEnum::Enum::complex64:    return 16;
            case PrimitiveTypeEnum::Enum::boolean:      return 1;
            case PrimitiveTypeEnum::Enum::string:       return 4;
            default:                                    CMAJ_ASSERT_FALSE;
        }
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        switch (type.get())
        {
            case PrimitiveTypeEnum::Enum::void_:        CMAJ_ASSERT_FALSE;
            case PrimitiveTypeEnum::Enum::int32:        return c.allocate<ConstantInt32>();
            case PrimitiveTypeEnum::Enum::int64:        return c.allocate<ConstantInt64>();
            case PrimitiveTypeEnum::Enum::float32:      return c.allocate<ConstantFloat32>();
            case PrimitiveTypeEnum::Enum::float64:      return c.allocate<ConstantFloat64>();
            case PrimitiveTypeEnum::Enum::complex32:    return c.allocate<ConstantComplex32>();
            case PrimitiveTypeEnum::Enum::complex64:    return c.allocate<ConstantComplex64>();
            case PrimitiveTypeEnum::Enum::boolean:      return c.allocate<ConstantBool>();
            case PrimitiveTypeEnum::Enum::string:       return c.allocate<ConstantString>();
            default:                                    CMAJ_ASSERT_FALSE;
        }
    }

    PrimitiveType& clone() const    { return context.allocator.allocate<PrimitiveType> (context, type.get()); }

    choc::value::Type toChocType() const override
    {
        switch (type.get())
        {
            case PrimitiveTypeEnum::Enum::void_:        return choc::value::Type::createVoid();
            case PrimitiveTypeEnum::Enum::int32:        return choc::value::Type::createInt32();
            case PrimitiveTypeEnum::Enum::int64:        return choc::value::Type::createInt64();
            case PrimitiveTypeEnum::Enum::float32:      return choc::value::Type::createFloat32();
            case PrimitiveTypeEnum::Enum::float64:      return choc::value::Type::createFloat64();
            case PrimitiveTypeEnum::Enum::complex32:    return createComplexType ("complex32", choc::value::Type::createFloat32());
            case PrimitiveTypeEnum::Enum::complex64:    return createComplexType ("complex64", choc::value::Type::createFloat64());
            case PrimitiveTypeEnum::Enum::boolean:      return choc::value::Type::createBool();
            case PrimitiveTypeEnum::Enum::string:       return choc::value::Type::createString();
            default:                                    CMAJ_ASSERT_FALSE;
        }
    }

    static choc::value::Type createComplexType (std::string_view name, choc::value::Type&& elementType)
    {
        auto t = choc::value::Type::createObject (name);
        t.addObjectMember ("real", elementType);
        t.addObjectMember ("imag", elementType);
        return t;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, PrimitiveTypeEnum, type) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ArrayType  : public TypeBase
{
    ArrayType (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ArrayType, 66)

    bool checkResolved() const override
    {
         if (! isResolvedAsType (elementType))
            return false;

        if (! isSlice())
            for (auto& d : dimensionList)
                if (getAsFoldedConstant (d) == nullptr)
                    return false;

        return true;
    }

    bool isArray() const override                                               { return true; }
    bool isVectorOrArray() const override                                       { return true; }
    bool isSlice() const override                                               { return dimensionList.empty(); }
    bool containsSlice() const override                                         { return isSlice() || getInnermostElementTypeRef().containsSlice(); }
    bool isFixedSizeArray() const override                                      { return ! isSlice(); }
    bool isFixedSizeAggregate() const override                                  { return isFixedSizeArray(); }
    ArraySize getArrayOrVectorSize (uint32_t dimensionIndex) const override     { return resolveSize (dimensionIndex); }
    ArraySize getFixedSizeAggregateNumElements() const override                 { return resolveSize (0); }
    ptr<const TypeBase> getArrayOrVectorElementType() const override            { return getElementType (0); }
    ptr<const TypeBase> getAggregateElementType (size_t) const override         { return getArrayOrVectorElementType(); }
    ObjectRefVector<TypeBase> getResolvedReferencedTypes() const override       { return convertToList<TypeBase> (elementType); }

    uint32_t getNumDimensions() const override                                  { return isSlice() ? 1 : static_cast<uint32_t> (dimensionList.size()); }

    Object& getInnermostElementTypeObject() const                               { return elementType; }
    TypeBase& getInnermostElementTypeRef() const                                { return castToTypeBaseRef (elementType); }
    ptr<TypeBase> getInnermostElementType() const                               { return castToTypeBase (elementType); }

    void setInnermostElementType (const Object& newType)
    {
        elementType.referTo (newType);
    }

    const TypeBase& getElementType (uint32_t dimensionIndex) const
    {
        auto numDimensions = dimensionList.size();

        if (dimensionIndex + 1 >= numDimensions)
            return getInnermostElementTypeRef();

        if (cachedElementType == nullptr)
        {
            auto& t = context.allocate<AST::ArrayType>();
            t.elementType.referTo (elementType);

            for (uint32_t i = 1; i < numDimensions; ++i)
                t.dimensionList.addReference (dimensionList[i].getObjectRef());

            cachedElementType = t;
        }

        if (dimensionIndex > 0)
            return cachedElementType->getElementType (dimensionIndex - 1);

        return *cachedElementType;
    }

    void resetCachedElementType()
    {
        cachedElementType.reset();
    }

    ArraySize resolveSize() const
    {
        return resolveSize (0);
    }

    ArraySize resolveSize (uint32_t dimensionIndex) const
    {
        if (isSlice())
            return 0;

        if (auto sizeValue = getConstantSize (dimensionIndex))
            if (auto sizeConst = sizeValue->getAsInt64())
                return static_cast<ArraySize> (*sizeConst);

        CMAJ_ASSERT_FALSE;
        return 0;
    }

    ArraySize resolveFlattenedSize() const
    {
        ArraySize total = 1;

        for (uint32_t i = 0; i < dimensionList.size(); ++i)
            total *= resolveSize (i);

        return total;
    }

    ptr<ConstantValueBase> getConstantSize (uint32_t dimensionIndex) const
    {
        return getAsFoldedConstant (dimensionList[dimensionIndex]);
    }

    void setArraySize (std::initializer_list<int32_t> sizes)
    {
        dimensionList.reset();

        for (auto i : sizes)
        {
            CMAJ_ASSERT (i > 0);
            dimensionList.addChildObject (context.allocator.createConstant (i));
        }

        resetCachedElementType();
    }

    void setArraySize (int32_t size)
    {
        dimensionList.reset();
        dimensionList.addChildObject (context.allocator.createConstant (size));
        resetCachedElementType();
    }

    void setArraySize (const Object& size)
    {
        dimensionList.reset();
        dimensionList.addReference (size);
        resetCachedElementType();
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto a = other.getAsArrayType())
        {
            if (! dimensionList.isIdentical (a->dimensionList))
            {
                if (dimensionList.size() != a->dimensionList.size())
                    return false;

                for (uint32_t i = 0; i < dimensionList.size(); ++i)
                    if (resolveSize (i) != a->resolveSize (i))
                        return false;
            }

            return getInnermostElementTypeRef().isSameType (a->getInnermostElementTypeRef(), flags & ~ComparisonFlags::ignoreReferences);
        }

        if (auto ref = other.getAsMakeConstOrRef())
            return ref->isSameType (*this, flags);

        return false;
    }

    std::string getLayoutSignature() const override
    {
        auto elementTypeSig = getInnermostElementTypeRef().getLayoutSignature();

        if (isSlice())
            return "slice_" + elementTypeSig;

        std::string sig = "arr";

        for (uint32_t i = 0; i < dimensionList.size(); ++i)
            sig += std::to_string (resolveSize (i)) + "_";

        return sig + elementTypeSig;
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        if (isSlice())
            sig << "slice" << elementType;
        else
            sig << "arr" << dimensionList << elementType;
    }

    size_t getPackedStorageSize() const override
    {
        if (isSlice())
            return 16; // this is only really needed for stack size checking, so as a rough fat-pointer size, this'll probably do..

        auto totalSize = getInnermostElementTypeRef().getPackedStorageSize();

        for (uint32_t i = 0; i < dimensionList.size(); ++i)
            totalSize *= resolveSize (i);

        return totalSize;
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        auto& agg = c.allocate<ConstantAggregate>();
        agg.type.createReferenceTo (*this);
        return agg;
    }

    choc::value::Type toChocType() const override
    {
        choc::value::Type type = castToTypeBaseRef (elementType).toChocType();

        if (isSlice())
            return choc::value::Type::createArray (type, 0);

        for (size_t i = dimensionList.size(); i > 0; i--)
            type = choc::value::Type::createArray (type, resolveSize (static_cast<uint32_t> (i - 1)));

        return type;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (elementType, visit);
        visitObjectIfPossible (dimensionList, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,   elementType) \
        X (2, ListProperty,  dimensionList) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

    mutable ptr<ArrayType> cachedElementType;
};

//==============================================================================
struct VectorType  : public TypeBase
{
    VectorType (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(VectorType, 20)

    bool checkResolved() const override                                         { return isResolvedAsType (elementType) && getAsFoldedConstant (numElements) != nullptr; }
    bool isVector() const override                                              { return true; }
    bool isVectorOrArray() const override                                       { return true; }
    bool isVectorSize1() const override                                         { return isSize1(); }
    bool isScalar() const override                                              { return getElementType().isScalar(); }
    bool isScalar32() const override                                            { return getElementType().isScalar32(); }
    bool isScalar64() const override                                            { return getElementType().isScalar64(); }
    bool isFloatOrVectorOfFloat() const override                                { return getElementType().isPrimitiveFloat(); }
    bool isComplexOrVectorOfComplex() const override                            { return getElementType().isPrimitiveComplex(); }
    TypeBase& getElementType() const                                            { return castToTypeBaseRef (elementType); }
    ValueBase& getSize() const                                                  { return castToValueRef (numElements); }
    bool isSize1() const                                                        { return resolveSize() == 1; }
    ArraySize getVectorSize() const override                                    { return resolveSize(); }
    bool isFixedSizeAggregate() const override                                  { return true; }
    ArraySize getArrayOrVectorSize (uint32_t dimensionIndex) const override     { (void) dimensionIndex; return resolveSize(); }
    ArraySize getFixedSizeAggregateNumElements() const override                 { return resolveSize(); }
    ptr<const TypeBase> getArrayOrVectorElementType() const override            { return castToTypeBase (elementType); }
    ptr<const TypeBase> getAggregateElementType (size_t) const override         { return getArrayOrVectorElementType(); }
    ObjectRefVector<TypeBase> getResolvedReferencedTypes() const override       { return convertToList<TypeBase> (elementType); }
    uint32_t getNumDimensions() const override                                  { return 1; }

    TypeBase& getInnermostElementTypeRef() const                                { return castToTypeBaseRef (elementType); }
    ptr<TypeBase> getInnermostElementType() const                               { return castToTypeBase (elementType); }

    ArraySize resolveSize() const
    {
        if (auto sizeValue = getAsFoldedConstant (numElements))
            if (auto sizeConst = sizeValue->getAsInt64())
                return static_cast<ArraySize> (*sizeConst);

        CMAJ_ASSERT_FALSE;
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto a = other.getAsVectorType())
            return numElements.isIdentical (a->numElements)
                    && getElementType().isSameType (a->getElementType(), flags);

        if ((flags & ComparisonFlags::ignoreVectorSize1) != 0)
            if (auto prim = other.getAsPrimitiveType())
                if (auto sizeValue = getAsFoldedConstant (numElements))
                    if (auto sizeConst = sizeValue->getAsInt64())
                        if (*sizeConst == 1)
                            return prim->isSameType (getElementType(), ComparisonFlags::failOnAllDifferences);

        if (auto ref = other.getAsMakeConstOrRef())
            return ref->isSameType (*this, flags);

        return false;
    }

    std::string getLayoutSignature() const override
    {
        return "vec" + std::to_string (resolveSize()) + "_" + getElementType().getLayoutSignature();
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "vec" << numElements << elementType;
    }

    size_t getPackedStorageSize() const override
    {
        return resolveSize() * getElementType().getPackedStorageSize();
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        auto& agg = c.allocate<ConstantAggregate>();
        agg.type.createReferenceTo (*this);
        auto& element = castToTypeBaseRef (elementType);

        for (ArraySize i = 0; i < resolveSize(); ++i)
            agg.values.addReference (element.allocateConstantValue (c));

        return agg;
    }

    choc::value::Type toChocType() const override
    {
        auto& element = castToTypeBaseRef (elementType);
        auto size = resolveSize();

        if (element.isPrimitiveInt32())     return choc::value::Type::createVector<int32_t> (size);
        if (element.isPrimitiveInt64())     return choc::value::Type::createVector<int64_t> (size);
        if (element.isPrimitiveFloat32())   return choc::value::Type::createVector<float> (size);
        if (element.isPrimitiveFloat64())   return choc::value::Type::createVector<double> (size);
        if (element.isPrimitiveBool())      return choc::value::Type::createVector<bool> (size);
        if (element.isPrimitiveComplex32()) return PrimitiveType::createComplexType ("complex32", choc::value::Type::createVector<float> (size));
        if (element.isPrimitiveComplex64()) return PrimitiveType::createComplexType ("complex64", choc::value::Type::createVector<double> (size));

        CMAJ_ASSERT_FALSE;
        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (elementType, visit);
        visitObjectIfPossible (numElements, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  elementType) \
        X (2, ChildObject,  numElements) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct StructType  : public TypeBase
{
    StructType (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(StructType, 21)
    ptr<const Comment> getComment() const override                              { return castTo<const Comment> (comment); }
    bool hasName (PooledString nametoMatch) const override                      { return name.hasName (nametoMatch); }
    PooledString getName() const override                                       { return name; }
    void setName (PooledString newName) override                                { name.set (newName); }
    bool isStruct() const override                                              { return true; }
    bool isFixedSizeAggregate() const override                                  { return true; }
    ArraySize getFixedSizeAggregateNumElements() const override                 { return static_cast<ArraySize> (memberNames.size()); }
    ptr<const TypeBase> getAggregateElementType (size_t i) const override       { return castToTypeBase (memberTypes[i]); }
    ObjectRefVector<TypeBase> getResolvedReferencedTypes() const override       { return memberTypes.findAllObjectsOfType<TypeBase>(); }
    bool hasMember (PooledString memberName) const                              { return indexOfMember (memberName) != -1; }
    bool hasMember (std::string_view memberName) const                          { return indexOfMember (memberName) != -1; }

    void addMember (PooledString n, const AST::TypeBase& t, int insertIndex = -1)
    {
        CMAJ_ASSERT (! hasMember (n));

        memberNames.addString (n, insertIndex);
        memberTypes.addReference (t, insertIndex);
    }

    void addMember (std::string_view n, const AST::TypeBase& t, int insertIndex = -1)
    {
        addMember (getStringPool().get (n), t, insertIndex);
    }

    bool checkResolved() const override
    {
        for (auto& m : memberTypes)
            if (! isResolvedAsType (m.get()))
                return false;

        return true;
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto otherStruct = other.getAsStructType())
        {
            if (otherStruct == this)
                return true;

            if (memberNames.size() != otherStruct->memberNames.size())
                return false;

            bool duckType = (flags & ComparisonFlags::duckTypeStructures) != 0;

            if (! duckType && getFullyQualifiedReadableName() != otherStruct->getFullyQualifiedReadableName())
                return false;

            for (size_t i = 0; i < memberNames.size(); ++i)
            {
                if (! duckType)
                    if (getMemberName (i) != otherStruct->getMemberName (i))
                        return false;

                auto& type1 = castToTypeBaseRef (memberTypes[i]);
                auto& type2 = castToTypeBaseRef (otherStruct->memberTypes[i]);

                if (! type1.isSameType (type2, flags & ~ComparisonFlags::ignoreReferences))
                    return false;
            }

            return true;
        }

        if (auto ref = other.getAsMakeConstOrRef())
            return ref->isSameType (*this, flags);

        return false;
    }

    bool containsSlice() const override
    {
        for (auto& m : memberTypes)
            if (castToTypeBaseRef (m).containsSlice())
                return true;

        return false;
    }

    template <typename NameType>
    int64_t indexOfMember (NameType memberName) const
    {
        for (size_t i = 0; i < memberNames.size(); ++i)
            if (getMemberName (i) == memberName)
                return static_cast<int64_t> (i);

        return -1;
    }

    PooledString getMemberName (size_t index) const
    {
        return memberNames[index].toString();
    }

    const TypeBase& getMemberType (size_t index) const
    {
        return castToTypeBaseRef (memberTypes[index]);
    }

    template <typename NameType>
    ptr<const TypeBase> getTypeForMember (NameType memberName) const
    {
        for (size_t i = 0; i < memberNames.size(); ++i)
            if (getMemberName (i) == memberName)
                return castToTypeBase (memberTypes[i]);

        return {};
    }

    std::string getLayoutSignature() const override
    {
        auto s = "struct" + std::to_string (memberTypes.size());

        for (auto& m : memberTypes)
            s += "_" + AST::castToTypeBaseRef (m).getLayoutSignature();

        return s;
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "struct" << getFullyQualifiedNameWithoutRoot();
    }

    size_t getPackedStorageSize() const override
    {
        size_t total = 0;

        for (size_t i = 0; i < memberNames.size(); ++i)
            total += getMemberType(i).getPackedStorageSize();

        return total;
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        auto& agg = c.allocate<ConstantAggregate>();
        agg.type.createReferenceTo (*this);
        return agg;
    }

    choc::value::Type toChocType() const override
    {
        auto t = choc::value::Type::createObject (name.get());

        for (size_t i = 0; i < memberNames.size(); ++i)
            t.addObjectMember (getMemberName (i), getMemberType (i).toChocType());

        return t;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (memberTypes, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, name) \
        X (2, ListProperty, memberNames) \
        X (3, ListProperty, memberTypes) \
        X (4, ChildObject, tupleType) \
        X (5, ChildObject, comment) \
        X (6, ListProperty, memberComments) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct BoundedType  : public TypeBase
{
    BoundedType (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(BoundedType, 22)

    bool checkResolved() const override
    {
        if (auto v = castToValue (limit))
            return v->constantFold() != nullptr;

        return false;
    }

    bool isPrimitive() const override            { return false; }
    bool isScalar() const override               { return true; }
    bool isPrimitiveInt() const override         { return false; }

    ArraySize getBoundedIntLimit() const
    {
        if (auto constSize = castToValueRef (limit).constantFold())
            if (auto size = constSize->getAsInt64())
                return static_cast<ArraySize> (*size);

        CMAJ_ASSERT_FALSE;
    }

    template <typename IntType>
    bool isValidBoundedIntIndex (IntType value) const
    {
        return value >= 0 && (uint64_t) value < (uint64_t) getBoundedIntLimit();
    }

    IntegerRange getAddressableIntegerRange() const override
    {
        return { 0, static_cast<int64_t> (getBoundedIntLimit()) };
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto b = other.getAsBoundedType())
            return isClamp.get() == b->isClamp.get()
                    && limit.isIdentical (b->limit);

        if (auto ref = other.getAsMakeConstOrRef())
            return ref->isSameType (*this, flags);

        return false;
    }

    std::string getLayoutSignature() const override
    {
        return "i32";
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << (isClamp ? "clamp" : "wrap") << limit;
    }

    size_t getPackedStorageSize() const override
    {
        return 4;
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        return c.allocate<AST::ConstantInt32>();
    }

    choc::value::Type toChocType() const override
    {
        return choc::value::Type::createInt32();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (limit, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,  limit) \
        X (2, BoolProperty, isClamp) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct EnumType  : public TypeBase
{
    EnumType (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(EnumType, 23)

    PooledString getName() const override                           { return name; }
    ptr<const Comment> getComment() const override                  { return castTo<const Comment> (comment); }
    bool hasName (PooledString nametoMatch) const override          { return name.hasName (nametoMatch); }
    bool checkResolved() const override                             { return true; }
    bool isEnum() const override                                    { return true; }

    IntegerRange getAddressableIntegerRange() const override
    {
        return { 0, static_cast<int64_t> (items.size()) };
    }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if (auto e = other.getAsEnumType())
            if (e->hasName (name))
               return getParentModule().isSameOriginalModule (other.getParentModule());

        if (auto ref = other.getAsMakeConstOrRef())
            return ref->isSameType (*this, flags);

        return false;
    }

    std::string getLayoutSignature() const override
    {
        return "i32";
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "enum" << getFullyQualifiedNameWithoutRoot();
    }

    size_t getPackedStorageSize() const override
    {
        return 4;
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        return c.allocate<AST::ConstantInt32>();
    }

    choc::value::Type toChocType() const override
    {
        return choc::value::Type::createInt32();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, name) \
        X (2, ListProperty,   items) \
        X (3, ChildObject,    comment) \
        X (4, ListProperty,   itemComments) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct MakeConstOrRef  : public TypeBase
{
    MakeConstOrRef (const ObjectContext& c)  : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(MakeConstOrRef, 24)

    ptr<TypeBase> getSource() const
    {
        if (auto t = castToTypeBase (source))
            if (t->isResolved())
                return t;

        return {};
    }

    TypeBase& getSourceRef() const                                          { return castToTypeBaseRef (source); }

    TypeBase& skipConstAndRefModifiers() override                           { return getSourceRef(); }
    const TypeBase& skipConstAndRefModifiers() const override               { return getSourceRef(); }

    bool checkResolved() const override                                     { return isResolvedAsType (source); }
    bool isConst() const override                                           { return makeConst; }
    bool isReference() const override                                       { return makeRef; }
    bool isVoid() const override                                            { return getSourceRef().isVoid(); }
    bool isPrimitive() const override                                       { return getSourceRef().isPrimitive(); }
    bool isPrimitiveBool() const override                                   { return getSourceRef().isPrimitiveBool(); }
    bool isPrimitiveInt() const override                                    { return getSourceRef().isPrimitiveInt(); }
    bool isPrimitiveInt32() const override                                  { return getSourceRef().isPrimitiveInt32(); }
    bool isPrimitiveInt64() const override                                  { return getSourceRef().isPrimitiveInt64(); }
    bool isPrimitiveFloat() const override                                  { return getSourceRef().isPrimitiveFloat(); }
    bool isPrimitiveFloat32() const override                                { return getSourceRef().isPrimitiveFloat32(); }
    bool isPrimitiveFloat64() const override                                { return getSourceRef().isPrimitiveFloat64(); }
    bool isPrimitiveComplex() const override                                { return getSourceRef().isPrimitiveComplex(); }
    bool isPrimitiveComplex32() const override                              { return getSourceRef().isPrimitiveComplex32(); }
    bool isPrimitiveComplex64() const override                              { return getSourceRef().isPrimitiveComplex64(); }
    bool isPrimitiveString() const override                                 { return getSourceRef().isPrimitiveString(); }
    bool isEnum() const override                                            { return getSourceRef().isEnum(); }
    bool isArray() const override                                           { return getSourceRef().isArray(); }
    bool isVector() const override                                          { return getSourceRef().isVector(); }
    bool isVectorOrArray() const override                                   { return getSourceRef().isVectorOrArray(); }
    bool isFloatOrVectorOfFloat() const override                            { return getSourceRef().isFloatOrVectorOfFloat(); }
    bool isComplexOrVectorOfComplex() const override                        { return getSourceRef().isComplexOrVectorOfComplex(); }
    bool isSlice() const override                                           { return getSourceRef().isSlice(); }
    bool containsSlice() const override                                     { return getSourceRef().containsSlice(); }
    bool isFixedSizeArray() const override                                  { return getSourceRef().isFixedSizeArray(); }
    bool isFixedSizeAggregate() const override                              { return getSourceRef().isFixedSizeAggregate(); }
    bool isStruct() const override                                          { return getSourceRef().isStruct(); }
    bool isBoundedType() const override                                     { return getSourceRef().isBoundedType(); }
    bool isScalar() const override                                          { return getSourceRef().isScalar(); }

    uint32_t getNumDimensions() const override                              { return getSourceRef().getNumDimensions(); }
    ArraySize getArrayOrVectorSize (uint32_t dimensionIndex) const override { return getSourceRef().getArrayOrVectorSize (dimensionIndex); }
    ArraySize getFixedSizeAggregateNumElements() const override             { return getSourceRef().getFixedSizeAggregateNumElements(); }
    ptr<const TypeBase> getAggregateElementType (size_t i) const override   { return getSourceRef().getAggregateElementType (i); }
    ptr<const TypeBase> getArrayOrVectorElementType() const override        { return getSourceRef().getArrayOrVectorElementType(); }
    IntegerRange getAddressableIntegerRange() const override                { return getSourceRef().getAddressableIntegerRange(); }
    ObjectRefVector<TypeBase> getResolvedReferencedTypes() const override   { return convertToList<TypeBase> (source); }

    bool isSameType (const TypeBase& other, int flags) const override
    {
        if ((flags & ComparisonFlags::ignoreReferences) == 0 && isReference() != other.isReference())
            return false;

        if ((flags & ComparisonFlags::ignoreConst) == 0 && isConst() != other.isConst())
            return false;

        if (auto s = getSource())
            return s->isSameType (other.skipConstAndRefModifiers(), flags);

        return false;
    }

    std::string getLayoutSignature() const override
    {
        std::string s;
        if (makeConst) s = "const_";
        if (makeRef)   s += "ref_";

        return s + getSourceRef().getLayoutSignature();
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        if (makeConst) sig << "const";
        if (makeRef)   sig << "ref";
        sig << source;
    }

    size_t getPackedStorageSize() const override
    {
        return makeRef ? 8 : skipConstAndRefModifiers().getPackedStorageSize();
    }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        CMAJ_ASSERT (! makeRef);
        return getSourceRef().allocateConstantValue (c);
    }

    choc::value::Type toChocType() const override
    {
        return getSource()->toChocType();
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (source, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject,   source) \
        X (2, BoolProperty,  makeConst) \
        X (3, BoolProperty,  makeRef) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
// Converts a type into a different type
struct TypeMetaFunction  : public TypeBase
{
    TypeMetaFunction (const ObjectContext& c) : TypeBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(TypeMetaFunction, 25)

    bool checkResolved() const override    { return false; }

    ptr<TypeBase> getSourceType() const
    {
        if (auto t = castToTypeBase (source))
            if (t->isResolved())
                return t;

        return {};
    }

    std::string getLayoutSignature() const override
    {
        CMAJ_ASSERT_FALSE; return {};
    }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << "meta" << op.getEnumString() << source;
    }

    bool isSameType (const TypeBase&, int) const override   { CMAJ_ASSERT_FALSE; }

    ConstantValueBase& allocateConstantValue (const ObjectContext& c) const override
    {
        return getSourceType()->allocateConstantValue (c);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (source, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, source) \
        X (2, TypeMetaFunctionTypeEnum, op) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};
