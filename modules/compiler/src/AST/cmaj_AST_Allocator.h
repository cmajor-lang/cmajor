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


struct Allocator
{
    Allocator()
       : voidType               (createVoidType()),
         int32Type              (createInt32Type()),
         int64Type              (createInt64Type()),
         float32Type            (createFloat32Type()),
         float64Type            (createFloat64Type()),
         complex32Type          (createComplex32Type()),
         complex64Type          (createComplex64Type()),
         boolType               (createBoolType()),
         stringType             (createStringType()),
         arraySizeType          (createInt32Type()),
         processorFrequencyType (createFloat64Type())
    {
    }

    template <typename Type, typename... Args>
    Type& allocate (Args&&... args)   { return pool.allocate<Type> (std::forward<Args> (args)...); }

    ObjectContext getContext (CodeLocation location, ptr<Object> parentScope)      { return { *this, location, parentScope }; }
    ObjectContext getContextWithoutLocation (ptr<Object> parentScope)              { return getContext ({}, parentScope); }
    ObjectContext getContextWithoutLocation()                                      { return getContext ({}, {}); }

    template <typename ObjectType>
    ObjectType& createObjectWithoutLocation()             { return allocate<ObjectType> (getContextWithoutLocation()); }

    ConstantInt32&      createConstantInt32     (CodeLocation location, int32_t v)                { return allocate<ConstantInt32>     (getContext (location, {}), v); }
    ConstantInt64&      createConstantInt64     (CodeLocation location, int64_t v)                { return allocate<ConstantInt64>     (getContext (location, {}), v); }
    ConstantFloat32&    createConstantFloat32   (CodeLocation location, float v)                  { return allocate<ConstantFloat32>   (getContext (location, {}), v); }
    ConstantFloat64&    createConstantFloat64   (CodeLocation location, double v)                 { return allocate<ConstantFloat64>   (getContext (location, {}), v); }
    ConstantComplex32&  createConstantComplex32 (CodeLocation location, std::complex<float> v)    { return allocate<ConstantComplex32> (getContext (location, {}), v); }
    ConstantComplex64&  createConstantComplex64 (CodeLocation location, std::complex<double> v)   { return allocate<ConstantComplex64> (getContext (location, {}), v); }
    ConstantBool&       createConstantBool      (CodeLocation location, bool v)                   { return allocate<ConstantBool>      (getContext (location, {}), v); }
    ConstantString&     createConstantString    (CodeLocation location, std::string v)            { return allocate<ConstantString>    (getContext (location, {}), std::move (v)); }

    ConstantInt32&      createConstantInt32     (int32_t v)                 { return createConstantInt32     ({}, v); }
    ConstantInt64&      createConstantInt64     (int64_t v)                 { return createConstantInt64     ({}, v); }
    ConstantFloat32&    createConstantFloat32   (float v)                   { return createConstantFloat32   ({}, v); }
    ConstantFloat64&    createConstantFloat64   (double v)                  { return createConstantFloat64   ({}, v); }
    ConstantComplex32&  createConstantComplex32 (std::complex<float> v)     { return createConstantComplex32 ({}, v); }
    ConstantComplex64&  createConstantComplex64 (std::complex<double> v)    { return createConstantComplex64 ({}, v); }
    ConstantBool&       createConstantBool      (bool v)                    { return createConstantBool      ({}, v); }
    ConstantString&     createConstantString    (std::string v)             { return createConstantString    ({}, std::move (v)); }

    ConstantInt32&      createConstant (int32_t v)                          { return createConstantInt32     (v); }
    ConstantInt64&      createConstant (int64_t v)                          { return createConstantInt64     (v); }
    ConstantFloat32&    createConstant (float v)                            { return createConstantFloat32   (v); }
    ConstantFloat64&    createConstant (double v)                           { return createConstantFloat64   (v); }
    ConstantComplex32&  createConstant (std::complex<float> v)              { return createConstantComplex32 (v); }
    ConstantComplex64&  createConstant (std::complex<double> v)             { return createConstantComplex64 (v); }
    ConstantBool&       createConstant (bool v)                             { return createConstantBool      (v); }
    ConstantString&     createConstant (std::string v)                      { return createConstantString    (std::move (v)); }

    ptr<ConstantInt32>      createConstant (std::optional<int32_t> v)                          { if (v) return createConstantInt32     (*v); return {}; }
    ptr<ConstantInt64>      createConstant (std::optional<int64_t> v)                          { if (v) return createConstantInt64     (*v); return {}; }
    ptr<ConstantFloat32>    createConstant (std::optional<float> v)                            { if (v) return createConstantFloat32   (*v); return {}; }
    ptr<ConstantFloat64>    createConstant (std::optional<double> v)                           { if (v) return createConstantFloat64   (*v); return {}; }
    ptr<ConstantComplex32>  createConstant (std::optional<std::complex<float>> v)              { if (v) return createConstantComplex32 (*v); return {}; }
    ptr<ConstantComplex64>  createConstant (std::optional<std::complex<double>> v)             { if (v) return createConstantComplex64 (*v); return {}; }
    ptr<ConstantBool>       createConstant (std::optional<bool> v)                             { if (v) return createConstantBool      (*v); return {}; }
    ptr<ConstantString>     createConstant (std::optional<std::string> v)                      { if (v) return createConstantString    (std::move (*v)); return {}; }
    ptr<ConstantString>     createConstant (std::optional<std::string_view> v)                 { if (v) return createConstantString    (std::string (*v)); return {}; }

    PrimitiveType& createPrimitiveType (PrimitiveTypeEnum::Enum type)   { return allocate<PrimitiveType> (getContextWithoutLocation(), type); }
    PrimitiveType& createVoidType()                                     { return createPrimitiveType (PrimitiveTypeEnum::Enum::void_); }
    PrimitiveType& createInt32Type()                                    { return createPrimitiveType (PrimitiveTypeEnum::Enum::int32); }
    PrimitiveType& createInt64Type()                                    { return createPrimitiveType (PrimitiveTypeEnum::Enum::int64); }
    PrimitiveType& createFloat32Type()                                  { return createPrimitiveType (PrimitiveTypeEnum::Enum::float32); }
    PrimitiveType& createFloat64Type()                                  { return createPrimitiveType (PrimitiveTypeEnum::Enum::float64); }
    PrimitiveType& createComplex32Type()                                { return createPrimitiveType (PrimitiveTypeEnum::Enum::complex32); }
    PrimitiveType& createComplex64Type()                                { return createPrimitiveType (PrimitiveTypeEnum::Enum::complex64); }
    PrimitiveType& createBoolType()                                     { return createPrimitiveType (PrimitiveTypeEnum::Enum::boolean); }
    PrimitiveType& createStringType()                                   { return createPrimitiveType (PrimitiveTypeEnum::Enum::string); }

    template <typename ObjectType>
    ObjectType& createDeepClone (const ObjectType& o)
    {
        return castToRef<ObjectType> (o.createDeepClone (*this));
    }

    VectorType& createVectorType (const PrimitiveType& elementType, Expression& numElements)
    {
        auto& v = createObjectWithoutLocation<AST::VectorType>();
        v.elementType.referTo (elementType);
        v.numElements.referTo (numElements);
        return v;
    }

    VectorType& createVectorType (const PrimitiveType& elementType, ArraySize numElements)
    {
        return createVectorType (elementType, createConstantInt32 (static_cast<int32_t> (numElements)));
    }

    AST::Namespace& createNamespace (PooledString name)
    {
        auto& ns = createObjectWithoutLocation<AST::Namespace>();
        ns.name = name;
        return ns;
    }

    choc::memory::Pool pool;
    SourceFileList sourceFileList;

    Strings strings { pool };

    const PrimitiveType& voidType;
    const PrimitiveType& int32Type;
    const PrimitiveType& int64Type;
    const PrimitiveType& float32Type;
    const PrimitiveType& float64Type;
    const PrimitiveType& complex32Type;
    const PrimitiveType& complex64Type;
    const PrimitiveType& boolType;
    const PrimitiveType& stringType;
    const PrimitiveType& arraySizeType;
    const PrimitiveType& processorFrequencyType;

    uint16_t nextVisitorNumber = 0;
    uint32_t visitorStackDepth = 0;
};

static Allocator& getAllocator (Object&);
