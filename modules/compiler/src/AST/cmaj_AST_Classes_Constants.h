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


struct ConstantValueBase  : public ValueBase
{
    ConstantValueBase (const ObjectContext& c) : ValueBase (c) {}
    ~ConstantValueBase() override = default;

    ConstantValueBase* getAsConstantValueBase() override                   { return this; }
    const ConstantValueBase* getAsConstantValueBase() const override       { return this; }
    bool isConstantValueBase() const override                              { return true; }

    bool isCompileTimeConstant() const override                            { return true; }
    void addSideEffects (SideEffects&) const override                      {}
    ptr<ConstantValueBase> constantFold() const override                   { return const_cast<ConstantValueBase&> (*this); }

    virtual std::optional<int32_t> getAsInt32() const                      { return {}; }
    virtual std::optional<int64_t> getAsInt64() const                      { return {}; }
    virtual std::optional<bool> getAsBool() const                          { return {}; }
    virtual std::optional<float> getAsFloat32() const                      { return {}; }
    virtual std::optional<double> getAsFloat64() const                     { return {}; }
    virtual std::optional<std::complex<float>> getAsComplex32() const      { return {}; }
    virtual std::optional<std::complex<double>> getAsComplex64() const     { return {}; }
    virtual std::optional<std::string_view> getAsString() const            { return {}; }
    virtual std::optional<int32_t> getAsEnumIndex() const                  { return {}; }

    template <typename TargetType> std::optional<TargetType> castToPrimitive() const;

    virtual ptr<ConstantValueBase> performUnaryNegate (Allocator&) const            { return {}; }
    virtual ptr<ConstantValueBase> performUnaryLogicalNot (Allocator&) const        { return {}; }
    virtual ptr<ConstantValueBase> performUnaryBitwiseNot (Allocator&) const        { return {}; }

    virtual ptr<const ConstantValueBase> getRealComponent (Allocator&) const        { return {}; }
    virtual ptr<const ConstantValueBase> getImaginaryComponent (Allocator&) const   { return {}; }
    virtual ptr<const ConstantValueBase> getAggregateElementValue (int64_t) const   { return {}; }

    virtual void setRealComponent (const ConstantValueBase*)                        { CMAJ_ASSERT_FALSE; }
    virtual void setImaginaryComponent (const ConstantValueBase*)                   { CMAJ_ASSERT_FALSE; }

    using SliceToValueFn = std::function<choc::value::Value(const choc::value::Value& array)>;
    virtual choc::value::Value toValue (SliceToValueFn*) const = 0;
    virtual bool setFromValue (const choc::value::ValueView&) = 0;
    virtual void setFromConstant (const ConstantValueBase&) = 0;
    virtual void setToZero() = 0;
    virtual bool isZero() const = 0;
    virtual void addInteger (int64_t delta)                     { (void) delta; CMAJ_ASSERT_FALSE; }
    virtual void performWrap (int64_t limit)                    { (void) limit; CMAJ_ASSERT_FALSE; }
    virtual void performClamp (int64_t limit)                   { (void) limit; CMAJ_ASSERT_FALSE; }
};

//==============================================================================
struct ConstantBool  : public ConstantValueBase
{
    ConstantBool (const ObjectContext& c, bool v = false)   : ConstantValueBase (c), value (*this, v) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantBool, 8)
    ptr<const TypeBase> getResultType() const override      { return context.allocator.boolType; }
    std::optional<bool> getAsBool() const override          { return value.get(); }
    std::optional<int32_t> getAsInt32() const override      { return value.get() ? 1 : 0; }
    std::optional<int64_t> getAsInt64() const override      { return value.get() ? 1 : 0; }
    std::optional<float> getAsFloat32() const override      { return value.get() ? 1.0f : 0.0f; }
    std::optional<double> getAsFloat64() const override     { return value.get() ? 1.0 : 0.0; }

    ptr<ConstantValueBase> performUnaryLogicalNot (Allocator& a) const override   { return a.createConstantBool (! value.get()); }

    choc::value::Value toValue (SliceToValueFn*) const override     { return choc::value::createBool (value.get()); }
    void writeSignature (SignatureBuilder& sig) const override      { sig << value; }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (! v.isBool())
            return false;

        value = v.getBool();
        return true;
    }

    void setFromConstant (const ConstantValueBase& v) override  { value.set (AST::getAsBool (v)); }
    void setToZero() override                                   { value.set (false); }
    bool isZero() const override                                { return ! value.get(); }

    #define CMAJ_PROPERTIES(X) \
        X (1, BoolProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantInt32  : public ConstantValueBase
{
    ConstantInt32 (const ObjectContext& c, int32_t v = 0)   : ConstantValueBase (c), value (*this, v) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantInt32, 9)
    ptr<const TypeBase> getResultType() const override                  { return context.allocator.int32Type; }
    std::optional<int32_t> getAsInt32() const override                  { return static_cast<int32_t> (value.get()); }
    std::optional<int64_t> getAsInt64() const override                  { return static_cast<int64_t> (value.get()); }
    std::optional<bool> getAsBool() const override                      { return value.get() != 0; }
    std::optional<float> getAsFloat32() const override                  { return static_cast<float> (value.get()); }
    std::optional<double> getAsFloat64() const override                 { return static_cast<double> (value.get()); }
    std::optional<std::complex<float>> getAsComplex32() const override  { return std::complex<float> (static_cast<float> (value.get()), 0.0f); }
    std::optional<std::complex<double>> getAsComplex64() const override { return std::complex<double> (static_cast<double> (value.get()), 0.0); }
    choc::value::Value toValue (SliceToValueFn*) const override         { return choc::value::createInt32 (static_cast<int32_t> (value.get())); }
    void writeSignature (SignatureBuilder& sig) const override          { sig << value; }

    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override       { return a.createConstantInt32 (-static_cast<int32_t> (value.get())); }
    ptr<ConstantValueBase> performUnaryBitwiseNot (Allocator& a) const override   { return a.createConstantInt32 (~static_cast<int32_t> (value.get())); }
    ptr<ConstantValueBase> performUnaryLogicalNot (Allocator& a) const override   { return a.createConstantInt32 (value.get() != 0 ? 0 : 1); }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isInt() || v.isFloat())
        {
            value = v.get<int32_t>();
            return true;
        }

        if (v.getType().isVectorSize1())
            return setFromValue (v[0]);

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override   { value.set (getAsPrimitive<int32_t> (v)); }
    void setToZero() override                                    { value.set (0); }
    bool isZero() const override                                 { return value.get() == 0; }
    void addInteger (int64_t delta) override                     { value.set (value.get() + static_cast<int32_t> (delta)); }
    void performWrap (int64_t limit) override                    { value.set (wrap (value.get(), limit)); }
    void performClamp (int64_t limit) override                   { value.set (clamp (value.get(), limit)); }
    IntegerRange getKnownIntegerRange() const override           { return { value.get(), value.get() + 1 }; }

    #define CMAJ_PROPERTIES(X) \
        X (1, IntegerProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantInt64  : public ConstantValueBase
{
    ConstantInt64 (const ObjectContext& c, int64_t v = 0)   : ConstantValueBase (c), value (*this, v) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantInt64, 10)
    ptr<const TypeBase> getResultType() const override                  { return context.allocator.int64Type; }
    std::optional<int32_t> getAsInt32() const override                  { return static_cast<int32_t> (value.get()); }
    std::optional<int64_t> getAsInt64() const override                  { return static_cast<int64_t> (value.get()); }
    std::optional<bool> getAsBool() const override                      { return value.get() != 0; }
    std::optional<float> getAsFloat32() const override                  { return static_cast<float> (value.get()); }
    std::optional<double> getAsFloat64() const override                 { return static_cast<double> (value.get()); }
    std::optional<std::complex<float>> getAsComplex32() const override  { return std::complex<float> (static_cast<float> (value.get()), 0.0f); }
    std::optional<std::complex<double>> getAsComplex64() const override { return std::complex<double> (static_cast<double> (value.get()), 0.0); }
    choc::value::Value toValue (SliceToValueFn*) const override         { return choc::value::createInt64 (value.get()); }
    void writeSignature (SignatureBuilder& sig) const override          { sig << value; }

    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override       { return a.createConstantInt64 (-value.get()); }
    ptr<ConstantValueBase> performUnaryBitwiseNot (Allocator& a) const override   { return a.createConstantInt64 (~value.get()); }
    ptr<ConstantValueBase> performUnaryLogicalNot (Allocator& a) const override   { return a.createConstantInt64 (value.get() != 0 ? 0 : 1); }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isInt() || v.isFloat())
        {
            value = v.get<int64_t>();
            return true;
        }

        if (v.getType().isVectorSize1())
            return setFromValue (v[0]);

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override   { value.set (AST::getAsInt64 (v)); }
    void setToZero() override                                    { value.set (0); }
    bool isZero() const override                                 { return value.get() == 0; }
    void addInteger (int64_t delta) override                     { value.set (value.get() + delta); }
    void performWrap (int64_t limit) override                    { value.set (wrap (value.get(), limit)); }
    void performClamp (int64_t limit) override                   { value.set (clamp (value.get(), limit)); }
    IntegerRange getKnownIntegerRange() const override           { return { value.get(), value.get() + 1 }; }

    #define CMAJ_PROPERTIES(X) \
        X (1, IntegerProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantFloat32  : public ConstantValueBase
{
    ConstantFloat32 (const ObjectContext& c, float v = 0)   : ConstantValueBase (c), value (*this, v) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantFloat32, 11)
    ptr<const TypeBase> getResultType() const override                             { return context.allocator.float32Type; }
    std::optional<int32_t> getAsInt32() const override                             { return static_cast<int32_t> (value.get()); }
    std::optional<int64_t> getAsInt64() const override                             { return static_cast<int64_t> (value.get()); }
    std::optional<bool> getAsBool() const override                                 { return value.get() != 0; }
    std::optional<float> getAsFloat32() const override                             { return static_cast<float> (value.get()); }
    std::optional<double> getAsFloat64() const override                            { return static_cast<double> (value.get()); }
    std::optional<std::complex<float>> getAsComplex32() const override             { return std::complex<float> (static_cast<float> (value.get()), 0.0f); }
    std::optional<std::complex<double>> getAsComplex64() const override            { return std::complex<double> (value.get(), 0.0); }
    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override        { return a.createConstantFloat32 (-static_cast<float> (value.get())); }

    choc::value::Value toValue (SliceToValueFn*) const override         { return choc::value::createFloat32 (static_cast<float> (value.get())); }
    void writeSignature (SignatureBuilder& sig) const override          { sig << value; }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isFloat() || v.isInt())
        {
            value = v.get<float>();
            return true;
        }

        if (v.getType().isVectorSize1())
            return setFromValue (v[0]);

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override   { value.set (getAsPrimitive<float> (v)); }
    void setToZero() override                                    { value.set (0); }
    bool isZero() const override                                 { return value.get() == 0; }
    void addInteger (int64_t delta) override                     { value.set (value.get() + static_cast<float> (delta)); }

    #define CMAJ_PROPERTIES(X) \
        X (1, FloatProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantFloat64  : public ConstantValueBase
{
    ConstantFloat64 (const ObjectContext& c, double v = 0)   : ConstantValueBase (c), value (*this, v) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantFloat64, 12)
    ptr<const TypeBase> getResultType() const override                             { return context.allocator.float64Type; }
    std::optional<int32_t> getAsInt32() const override                             { return static_cast<int32_t> (value.get()); }
    std::optional<int64_t> getAsInt64() const override                             { return static_cast<int64_t> (value.get()); }
    std::optional<bool> getAsBool() const override                                 { return value.get() != 0; }
    std::optional<float> getAsFloat32() const override                             { return static_cast<float> (value.get()); }
    std::optional<double> getAsFloat64() const override                            { return static_cast<double> (value.get()); }
    std::optional<std::complex<float>> getAsComplex32() const override             { return std::complex<float> (static_cast<float> (value.get()), 0.0f); }
    std::optional<std::complex<double>> getAsComplex64() const override            { return std::complex<double> (value.get(), 0.0); }
    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override        { return a.createConstantFloat64 (-value.get()); }

    choc::value::Value toValue (SliceToValueFn*) const override         { return choc::value::createFloat64 (value.get()); }
    void writeSignature (SignatureBuilder& sig) const override          { sig << value; }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isFloat() || v.isInt())
        {
            value = v.get<double>();
            return true;
        }

        if (v.getType().isVectorSize1())
            return setFromValue (v[0]);

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override   { value.set (getAsPrimitive<double> (v)); }
    void setToZero() override                                    { value.set (0); }
    bool isZero() const override                                 { return value.get() == 0; }
    void addInteger (int64_t delta) override                     { value.set (value.get() + static_cast<double> (delta)); }

    #define CMAJ_PROPERTIES(X) \
        X (1, FloatProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantComplex32  : public ConstantValueBase
{
    ConstantComplex32 (const ObjectContext& c, std::complex<float> value = {})
        : ConstantValueBase (c) { real.set (value.real()); imag.set (value.imag()); }

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantComplex32, 13)
    ptr<const TypeBase> getResultType() const override                               { return context.allocator.complex32Type; }
    std::complex<float> getComplexValue() const                                      { return std::complex<float> (static_cast<float> (real.get()), static_cast<float> (imag.get())); }
    std::optional<std::complex<float>> getAsComplex32() const override               { return getComplexValue(); }
    std::optional<std::complex<double>> getAsComplex64() const override              { return getComplexValue(); }
    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override          { return a.createConstantComplex32 (-getComplexValue()); }
    ptr<const ConstantValueBase> getRealComponent (Allocator& a) const override      { return a.createConstantFloat32 (static_cast<float> (real.get())); }
    ptr<const ConstantValueBase> getImaginaryComponent (Allocator& a) const override { return a.createConstantFloat32 (static_cast<float> (imag.get())); }
    void setRealComponent (const ConstantValueBase* v) override                      { real.set (v != nullptr ? getAsPrimitive<float> (*v) : 0.0f); }
    void setImaginaryComponent (const ConstantValueBase* v) override                 { imag.set (v != nullptr ? getAsPrimitive<float> (*v) : 0.0f); }
    void writeSignature (SignatureBuilder& sig) const override                       { sig << "cmplx" << real << imag; }

    choc::value::Value toValue (SliceToValueFn*) const override
    {
        return choc::value::createObject ("complex32",
                                          "real", choc::value::createFloat32 (static_cast<float> (real.get())),
                                          "imag", choc::value::createFloat32 (static_cast<float> (imag.get())));
    }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isObject())
        {
            auto r = v["real"];
            auto i = v["imag"];

            if (r.isFloat32() && i.isFloat32())
            {
                real = r.getFloat32();
                imag = i.getFloat32();
                return true;
            }
        }

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override
    {
        auto other = v.getAsComplex32();
        CMAJ_ASSERT (other.has_value());
        real.set (other->real());
        imag.set (other->imag());
    }

    void setToZero() override
    {
        real.set (0);
        imag.set (0);
    }

    bool isZero() const override
    {
        return real.get() == 0 && imag.get() == 0;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, FloatProperty, real) \
        X (2, FloatProperty, imag)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantComplex64  : public ConstantValueBase
{
    ConstantComplex64 (const ObjectContext& c, std::complex<double> value = {})
        : ConstantValueBase (c) { real.set (value.real()); imag.set (value.imag()); }

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantComplex64, 14)
    ptr<const TypeBase> getResultType() const override                                { return context.allocator.complex64Type; }
    std::complex<double> getComplexValue() const                                      { return std::complex<double> (real.get(), imag.get()); }
    std::optional<std::complex<float>> getAsComplex32() const override                { return std::complex<float> (getComplexValue()); }
    std::optional<std::complex<double>> getAsComplex64() const override               { return getComplexValue(); }
    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override           { return a.createConstantComplex64 (-getComplexValue()); }
    ptr<const ConstantValueBase> getRealComponent (Allocator& a) const override       { return a.createConstantFloat64 (real.get()); }
    ptr<const ConstantValueBase> getImaginaryComponent (Allocator& a) const override  { return a.createConstantFloat64 (imag.get()); }
    void setRealComponent (const ConstantValueBase* v) override                       { real.set (v != nullptr ? getAsPrimitive<double> (*v) : 0.0); }
    void setImaginaryComponent (const ConstantValueBase* v) override                  { imag.set (v != nullptr ? getAsPrimitive<double> (*v) : 0.0); }
    void writeSignature (SignatureBuilder& sig) const override                        { sig << "cmplx" << real << imag; }

    choc::value::Value toValue (SliceToValueFn*) const override
    {
        return choc::value::createObject ("complex64",
                                          "real", choc::value::createFloat64 (real.get()),
                                          "imag", choc::value::createFloat64 (imag.get()));
    }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isObject())
        {
            auto r = v["real"];
            auto i = v["imag"];

            if (r.isFloat64() && i.isFloat64())
            {
                real = r.getFloat64();
                imag = i.getFloat64();
                return true;
            }
        }

        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override
    {
        auto other = v.getAsComplex64();
        CMAJ_ASSERT (other.has_value());
        real.set (other->real());
        imag.set (other->imag());
    }

    void setToZero() override
    {
        real.set (0);
        imag.set (0);
    }

    bool isZero() const override
    {
        return real.get() == 0 && imag.get() == 0;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, FloatProperty, real) \
        X (2, FloatProperty, imag)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantEnum  : public ConstantValueBase
{
    ConstantEnum (const ObjectContext& c)  : ConstantValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantEnum, 15)
    ptr<const TypeBase> getResultType() const override                  { return castToTypeBase (type); }
    choc::value::Value toValue (SliceToValueFn*) const override         { return choc::value::createInt32 (static_cast<int32_t> (index.get())); }
    void writeSignature (SignatureBuilder& sig) const override          { sig << type << index; }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (v.isInt32())
        {
            index = v.get<int32_t>();
            return true;
        }

        return v.getType().isVectorSize1() && setFromValue (v[0]);
    }

    std::optional<int32_t> getAsEnumIndex() const override
    {
        return static_cast<int32_t> (index.get());
    }

    std::string_view getEnumItemName() const
    {
        return castToRefSkippingReferences<EnumType> (type).items[static_cast<uint32_t> (index.get())].toString();
    }

    void setFromConstant (const ConstantValueBase& v) override   { index.set (getAsPrimitive<int32_t> (v)); }
    void setToZero() override                                    { index.set (0); }
    bool isZero() const override                                 { return index.get() == 0; }
    IntegerRange getKnownIntegerRange() const override           { return { index.get(), index.get() + 1 }; }

    #define CMAJ_PROPERTIES(X) \
        X (1, ObjectReference, type) \
        X (2, IntegerProperty, index)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantString  : public ConstantValueBase
{
    ConstantString (const ObjectContext& c, std::string v = {})
       : ConstantValueBase (c), value (*this, std::move (v)) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantString, 16)

    ptr<const TypeBase> getResultType() const override              { return context.allocator.stringType; }
    choc::value::Value toValue (SliceToValueFn*) const override     { return choc::value::createString (value.get()); }
    void writeSignature (SignatureBuilder& sig) const override      { sig << value; }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        if (! v.isString())
            return false;

        value = getStringPool().get (v.getString());
        return true;
    }

    std::optional<std::string_view> getAsString() const override    { return value.get(); }
    void setFromConstant (const ConstantValueBase& v) override      { value.set (v.getAsConstantString()->value.get()); }
    void setToZero() override                                       { value.set ({}); }
    bool isZero() const override                                    { return value.get().empty(); }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, value)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConstantAggregate  : public ConstantValueBase
{
    ConstantAggregate (const ObjectContext& c)   : ConstantValueBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConstantAggregate, 17)

    ptr<const TypeBase> getResultType() const override          { return castToTypeBase (type); }
    TypeBase& getType() const                                   { return castToTypeBaseRef (type); }
    void writeSignature (SignatureBuilder& sig) const override  { sig << type << values; }

    template <typename Type, typename GetterFn>
    std::optional<Type> getIfVectorSize1 (GetterFn&& getter) const
    {
        if (values.size() == 1 && getType().isVector())
            if (auto element = castToConstant (values.front()))
                return getter (*element);

        return {};
    }

    std::optional<int32_t>              getAsInt32() const override     { return getIfVectorSize1<int32_t>              ([] (const ConstantValueBase& c) { return c.getAsInt32(); }); }
    std::optional<int64_t>              getAsInt64() const override     { return getIfVectorSize1<int64_t>              ([] (const ConstantValueBase& c) { return c.getAsInt64(); }); }
    std::optional<bool>                 getAsBool() const override      { return getIfVectorSize1<bool>                 ([] (const ConstantValueBase& c) { return c.getAsBool(); }); }
    std::optional<float>                getAsFloat32() const override   { return getIfVectorSize1<float>                ([] (const ConstantValueBase& c) { return c.getAsFloat32(); }); }
    std::optional<double>               getAsFloat64() const override   { return getIfVectorSize1<double>               ([] (const ConstantValueBase& c) { return c.getAsFloat64(); }); }
    std::optional<std::complex<float>>  getAsComplex32() const override { return getIfVectorSize1<std::complex<float>>  ([] (const ConstantValueBase& c) { return c.getAsComplex32(); }); }
    std::optional<std::complex<double>> getAsComplex64() const override { return getIfVectorSize1<std::complex<double>> ([] (const ConstantValueBase& c) { return c.getAsComplex64(); }); }

    template <typename Op>
    ptr<ConstantValueBase> performUnaryOp (Allocator& a, Op&& op) const
    {
        if (auto t = getResultType())
        {
            if (t->isVector())
            {
                auto& newVersion = a.createObjectWithoutLocation<ConstantAggregate>();
                newVersion.type.createReferenceTo (*t);

                for (auto& v : values)
                {
                    if (auto newV = op (castToConstantRef (v)))
                        newVersion.values.addReference (*newV);
                    else
                        return {};
                }

                return newVersion;
            }
        }

        return {};
    }

    ptr<ConstantValueBase> performUnaryNegate (Allocator& a) const override       { return performUnaryOp (a, [&a] (ConstantValueBase& v) { return v.performUnaryNegate (a); }); }
    ptr<ConstantValueBase> performUnaryBitwiseNot (Allocator& a) const override   { return performUnaryOp (a, [&a] (ConstantValueBase& v) { return v.performUnaryBitwiseNot (a); }); }
    ptr<ConstantValueBase> performUnaryLogicalNot (Allocator& a) const override   { return performUnaryOp (a, [&a] (ConstantValueBase& v) { return v.performUnaryLogicalNot (a); }); }

    ptr<ConstantValueBase> getRealOrImagComponents (Allocator& a, bool isReal) const
    {
        if (auto t = getResultType())
        {
            if (t->isVector())
            {
                auto elementType = t->getArrayOrVectorElementType();
                CMAJ_ASSERT (elementType->isPrimitiveComplex());

                auto& newVersion = a.createObjectWithoutLocation<ConstantAggregate>();
                auto& newElementType = elementType->isPrimitiveComplex32() ? a.float32Type : a.float64Type;
                newVersion.type.setChildObject (a.createVectorType (newElementType, t->getArrayOrVectorSize (0)));

                for (auto& v : values)
                {
                    auto& complex = castToRef<ConstantValueBase> (v);

                    if (auto newV = isReal ? complex.getRealComponent (a)
                                           : complex.getImaginaryComponent (a))
                        newVersion.values.addReference (*newV);
                    else
                        return {};
                }

                return newVersion;
            }
        }

        return {};
    }

    void setRealOrImagComponents (bool isReal, const ConstantValueBase* newValue)
    {
        if (auto t = getResultType())
        {
            if (t->isVector())
            {
                auto elementType = t->getArrayOrVectorElementType();
                CMAJ_ASSERT (elementType->isPrimitiveComplex());

                if (newValue->isConstantAggregate())
                {
                    int32_t index = 0;

                    for (auto& v : values)
                    {
                        auto& complex = castToRef<ConstantValueBase> (v);
                        auto element = newValue->getAggregateElementValue (index++);

                        if (isReal)
                            complex.setRealComponent (element.get());
                        else
                            complex.setImaginaryComponent (element.get());
                    }
                }
                else
                {
                    for (auto& v : values)
                    {
                        auto& complex = castToRef<ConstantValueBase> (v);

                        if (isReal)
                            complex.setRealComponent (newValue);
                        else
                            complex.setImaginaryComponent (newValue);
                    }
                }
            }
        }
    }

    ptr<const ConstantValueBase> getRealComponent (Allocator& a) const override       { return getRealOrImagComponents (a, true); }
    ptr<const ConstantValueBase> getImaginaryComponent (Allocator& a) const override  { return getRealOrImagComponents (a, false); }

    void setRealComponent (const ConstantValueBase* v) override                       { setRealOrImagComponents (true, v); }
    void setImaginaryComponent (const ConstantValueBase* v) override                  { setRealOrImagComponents (false, v); }

    void setNumberOfAllocatedElements (size_t num)
    {
        if (num == 0)
        {
            values.reset();
        }
        else if (values.size() > num)
        {
            while (values.size() > num)
                values.remove (values.size() - 1);
        }
        else
        {
            auto& resultType = getType().skipConstAndRefModifiers();
            values.reserve (num);

            for (size_t i = values.size(); i < num; ++i)
                values.addReference (resultType.getAggregateElementType(i)->allocateConstantValue (context));
        }
    }

    ArraySize getNumElements() const
    {
        if (type == nullptr)
            return static_cast<ArraySize> (values.size());

        auto& t = getType().skipConstAndRefModifiers();

        if (t.isSlice())
            return static_cast<ArraySize> (values.size());

        return t.getFixedSizeAggregateNumElements();
    }

    ptr<const ConstantValueBase> getAggregateElementValue (int64_t index) const override
    {
        return getElementValueRefWrapped (index);
    }

    ptr<ConstantValueBase> getOrCreateAggregateElementValue (uint32_t index)
    {
        if (index < values.size())
            return getElement (index);

        auto& resultType = getType().skipConstAndRefModifiers();

        if (isZero())
            return resultType.getAggregateElementType (index)->allocateConstantValue (context);

        return getElement(0);
    }

    template <typename IndexType>
    ConstantValueBase& getElement (IndexType index) const
    {
        return castToConstantRef (values[static_cast<size_t> (index)]);
    }

    ConstantValueBase& getElementValueRef (size_t index) const
    {
        return getElement (index >= values.size() ? 0 : index);
    }

    ConstantValueBase& getElementValueRefWrapped (int64_t index) const
    {
        auto wrappedIndex = TypeRules::convertArrayOrVectorIndexToWrappedIndex (getNumElements(), index);
        return getElementValueRef (wrappedIndex);
    }

    void setElementValue (size_t index, const ConstantValueBase& value)
    {
        getElement (index).setFromConstant (value);
    }

    ptr<ConstantValueBase> getElementSlice (IntegerRange range) const
    {
        if (! range.isValid())
            return {};

        auto& typeRef = castToTypeBaseRef (type);
        auto aggregateSize = typeRef.getFixedSizeAggregateNumElements();
        range = TypeRules::normaliseArrayOrVectorIndexRange (aggregateSize, range);

        if (! TypeRules::canBeSafelyCastToArraySize (range.size()))
            return {};

        auto& newSlice = context.allocate<ConstantAggregate>();
        auto& typeCopy = context.allocator.createDeepClone (typeRef);
        auto sliceSize = static_cast<int32_t> (TypeRules::castToArraySize (range.size()));
        auto& nonConstType = typeCopy.skipConstAndRefModifiers();

        if (auto arr = nonConstType.getAsArrayType())
            arr->setArraySize ({ sliceSize });
        else if (auto vec = nonConstType.getAsVectorType())
            vec->numElements.createConstant (sliceSize);
        else
            CMAJ_ASSERT_FALSE;

        newSlice.type.setChildObject (typeCopy);

        for (auto i = range.start; i < range.end; ++i)
            newSlice.values.addClone (values[static_cast<size_t> (i)]);

        return newSlice;
    }

    choc::value::Value toValue (SliceToValueFn* sliceToValue) const override
    {
        auto& resultType = getType().skipConstAndRefModifiers();
        auto numResultElements = resultType.getFixedSizeAggregateNumElements();

        if (resultType.isArrayType())
        {
            if (values.empty() && ! resultType.isSlice())
            {
                auto zero = choc::value::Value (resultType.getAggregateElementType(0)->toChocType());
                return choc::value::createArray (numResultElements, [&] (uint32_t) { return zero; });
            }

            auto result = choc::value::createEmptyArray();

            for (auto& v : values)
                result.addArrayElement (castToConstantRef(v).toValue (sliceToValue));

            if (values.size() < numResultElements)
            {
                CMAJ_ASSERT (values.size() == 1);
                auto filler = choc::value::Value (result[0]);

                for (size_t i = 1; i < numResultElements; ++i)
                    result.addArrayElement (filler);
            }

            if (resultType.isSlice())
            {
                CMAJ_ASSERT (sliceToValue != nullptr);
                return (*sliceToValue) (result);
            }

            return result;
        }

        if (auto vec = resultType.getAsVectorType())
        {
            auto& elementType = vec->getElementType();

            if (elementType.isPrimitiveInt32())    return toVectorValue<int32_t> (numResultElements);
            if (elementType.isPrimitiveInt64())    return toVectorValue<int64_t> (numResultElements);
            if (elementType.isPrimitiveFloat32())  return toVectorValue<float>   (numResultElements);
            if (elementType.isPrimitiveFloat64())  return toVectorValue<double>  (numResultElements);
            if (elementType.isPrimitiveBool())     return toVectorValue<bool>    (numResultElements);

            CMAJ_ASSERT_FALSE;
        }

        if (auto structure = resultType.getAsStructType())
        {
            auto result = choc::value::createObject (structure->getName());

            if (values.empty())
            {
                for (size_t i = 0; i < numResultElements; ++i)
                    result.addMember (structure->getMemberName (i),
                                      choc::value::Value (castToTypeBaseRef (structure->memberTypes[i]).toChocType()));
            }
            else
            {
                for (size_t i = 0; i < numResultElements; ++i)
                    result.addMember (structure->getMemberName (i),
                                      getElement (i).toValue (sliceToValue));
            }

            return result;
        }

        CMAJ_ASSERT_FALSE;
        return {};
    }

    template <typename ElementType>
    choc::value::Value toVectorValue (uint32_t numElements) const
    {
        if (values.empty())
            return choc::value::createVector (numElements, [] (uint32_t) { return ElementType(); });

        return choc::value::createVector (numElements, [this] (uint32_t i)
        {
            return static_cast<ElementType> (getElement (i < values.size() ? i : 0).toValue (nullptr));
        });
    }

    bool setFromValue (const choc::value::ValueView& v) override
    {
        auto& resultType = getResultType()->skipConstAndRefModifiers();
        auto numResultElements = resultType.getFixedSizeAggregateNumElements();

        if (resultType.getAsArrayType() != nullptr)
        {
            if (numResultElements == 0)
                numResultElements = v.size();

            if (! (v.isArray() && v.size() == numResultElements))
                return false;

            setNumberOfAllocatedElements (numResultElements);

            for (uint32_t i = 0; i < numResultElements; ++i)
                if (! getElement (i).setFromValue (v[i]))
                    return false;

            return true;
        }

        if (resultType.getAsVectorType() != nullptr)
        {
            if (! ((v.isArray() || v.isVector()) && v.size() == numResultElements))
                return false;

            setNumberOfAllocatedElements (numResultElements);

            for (uint32_t i = 0; i < numResultElements; ++i)
                if (! getElement (i).setFromValue (v[i]))
                    return false;

            return true;
        }

        if (auto destStruct = resultType.getAsStructType())
        {
            if (! (v.isObject() && v.size() == numResultElements))
                return false;

            setNumberOfAllocatedElements (numResultElements);

            for (uint32_t i = 0; i < numResultElements; ++i)
                if (! getElement (i).setFromValue (v[destStruct->getMemberName (i)]))
                    return false;

            return true;
        }

        CMAJ_ASSERT_FALSE;
        return false;
    }

    void setFromConstant (const ConstantValueBase& v) override
    {
        if (auto agg = v.getAsConstantAggregate())
        {
            if (agg->values.empty())
                return setToZero();

            if (agg->values.size() == 1)
                return setToSingleValue (agg->getElement (0));

            setNumberOfAllocatedElements (agg->values.size());

            for (size_t i = 0; i < values.size(); ++i)
                setElementValue (i, agg->getElement (i));
        }
        else
        {
            setToSingleValue (v);
        }
    }

    void setToSingleValue (const ConstantValueBase& v)
    {
        if (values.size() <= 1)
        {
            setNumberOfAllocatedElements (1);
            setElementValue (0, v);
        }
        else
        {
            for (size_t i = 0; i < values.size(); ++i)
                setElementValue (i, v);
        }
    }

    void setToZero() override
    {
        if (! isZero())
            for (auto& v : values)
                castToConstantRef (v).setToZero();
    }

    bool isZero() const override
    {
        for (size_t i = 0; i < values.size(); ++i)
            if (! getElement (i).isZero())
                return false;

        return true;
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, type) \
        X (2, ListProperty, values)

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};
