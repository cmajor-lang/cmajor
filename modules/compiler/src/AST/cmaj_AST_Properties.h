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


struct IntegerProperty;
struct FloatProperty;
struct BoolProperty;
struct StringProperty;
struct EnumProperty;
struct ObjectProperty;
struct ChildObject;
struct ObjectReference;
struct ListProperty;

//==============================================================================
struct Property
{
    Property() = delete;
    Property (Property&&) = delete;
    Property (const Property&) = delete;
    Property& operator= (Property&&) = delete;
    Property& operator= (const Property&) = delete;

    Property (Object& o) : owner (o) {}
    virtual ~Property() = default;

    virtual void reset() = 0;
    virtual bool hasDefaultValue() const = 0;
    virtual bool isPrimitive() const = 0;
    virtual std::string_view getPropertyType() const = 0;
    virtual uint8_t getPropertyTypeID() const = 0;
    virtual void writeSignature (SignatureBuilder&) const = 0;
    virtual void visitObjects (Visitor&) = 0;

    virtual ptr<IntegerProperty>      getAsIntegerProperty()       { return {}; }
    virtual ptr<FloatProperty>        getAsFloatProperty()         { return {}; }
    virtual ptr<BoolProperty>         getAsBoolProperty()          { return {}; }
    virtual ptr<StringProperty>       getAsStringProperty()        { return {}; }
    virtual ptr<EnumProperty>         getAsEnumProperty()          { return {}; }
    virtual ptr<ObjectProperty>       getAsObjectProperty()        { return {}; }
    virtual ptr<ChildObject>          getAsChildObject()           { return {}; }
    virtual ptr<ObjectReference>      getAsObjectReference()       { return {}; }
    virtual ptr<ListProperty>         getAsListProperty()          { return {}; }

    virtual ptr<const IntegerProperty>      getAsIntegerProperty() const   { return {}; }
    virtual ptr<const FloatProperty>        getAsFloatProperty() const     { return {}; }
    virtual ptr<const BoolProperty>         getAsBoolProperty() const      { return {}; }
    virtual ptr<const StringProperty>       getAsStringProperty() const    { return {}; }
    virtual ptr<const EnumProperty>         getAsEnumProperty() const      { return {}; }
    virtual ptr<const ObjectProperty>       getAsObjectProperty() const    { return {}; }
    virtual ptr<const ChildObject>          getAsChildObject() const       { return {}; }
    virtual ptr<const ObjectReference>      getAsObjectReference() const   { return {}; }
    virtual ptr<const ListProperty>         getAsListProperty() const      { return {}; }

    virtual PooledString toString() const                       { CMAJ_ASSERT_FALSE; return {}; }
    std::string_view toStdString() const                        { return toString().get(); }
    virtual bool hasName (PooledString nameToMatch) const       { (void) nameToMatch; return false; }

    virtual Property& allocateEmptyCopy (Object&) const = 0;
    virtual Property& createClone (Object&) const = 0;
    virtual void deepCopy (const Property&, RemappedObjects&) = 0;
    virtual void updateObjectMappings (RemappedObjects&) {}
    virtual bool containsStatement (const Statement&) const     { return false; }

    virtual bool isIdentical (const Property&) const = 0;

    virtual ptr<Object> getObject() const                    { return {}; }
    virtual Object& getObjectRef() const                     { return *getObject(); }
    virtual ObjectRefVector<Object> getAsObjectList() const  { return {}; }

    virtual ObjectContext& getContext() const                { return owner.context; }

    virtual choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) = 0;

    template <typename Type>
    ptr<Type> getAsObjectType() const
    {
        if (auto o = getObject())
            return castTo<Type> (*o);

        return {};
    }

    Allocator& getAllocator() const     { return AST::getAllocator (owner); }
    StringPool& getStringPool() const   { return getAllocator().strings.stringPool; }

    Object& owner;
};

//==============================================================================
struct PropertyList
{
    PropertyList (uint32_t num) : numProperties (num) {}

    size_t size() const                     { return numProperties; }
    Property& operator[] (size_t i) const   { return *properties[i]; }
    Property* const* begin() const          { return properties; }
    Property* const* end() const            { return properties + numProperties; }

    static constexpr size_t maxNumProperties = 15;
    const uint32_t numProperties = 0;
    Property* properties[maxNumProperties];
};

//==============================================================================
struct StringProperty   : public Property
{
    explicit StringProperty (Object& o) : Property (o) {}
    explicit StringProperty (Object& o, std::string&& v) : Property (o), value (getStringPool().get (v)) {}
    explicit StringProperty (Object& o, const PooledString& v) : Property (o), value (v) {}
    StringProperty (StringProperty&&) = delete;
    StringProperty (const StringProperty&) = delete;

    static constexpr uint8_t typeID = 1;
    static constexpr bool isObjectProperty = false;

    void reset() override                                               { value = {}; }
    bool hasDefaultValue() const override                               { return value.empty(); }
    bool isPrimitive() const override                                   { return true; }
    std::string_view getPropertyType() const override                   { return "string"; }
    uint8_t getPropertyTypeID() const override                          { return typeID; }
    void writeSignature (SignatureBuilder& sig) const override          { sig << value; }
    ptr<StringProperty> getAsStringProperty() override                  { return *this; }
    ptr<const StringProperty> getAsStringProperty() const override      { return *this; }
    void visitObjects (Visitor&) override                               {}

    PooledString toString() const override                              { return value; }
    bool hasName (PooledString nameToMatch) const override              { return value == nameToMatch; }
    bool operator== (PooledString nameToMatch) const                    { return value == nameToMatch; }
    bool operator!= (PooledString nameToMatch) const                    { return value != nameToMatch; }
    bool operator== (std::string_view nameToMatch) const                { return value.get() == nameToMatch; }
    bool operator!= (std::string_view nameToMatch) const                { return value.get() != nameToMatch; }

    PooledString get() const                                            { return value; }
    void set (PooledString newValue)                                    { value = newValue; }

    operator PooledString() const                                       { return get(); }
    StringProperty& operator= (PooledString newValue)                   { set (newValue); return *this; }
    StringProperty& operator= (const StringProperty& newValue)          { set (newValue.value); return *this; }

    Property& allocateEmptyCopy (Object& o) const override              { return AST::getAllocator (o).allocate<StringProperty> (o); }
    Property& createClone (Object& o) const override                    { auto& a = AST::getAllocator (o); return a.allocate<StringProperty> (o, a.strings.stringPool.get (value.get())); }
    void deepCopy (const Property& source, RemappedObjects&) override   { auto s = source.getAsStringProperty(); CMAJ_ASSERT (s != nullptr); value = s->value; }
    choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) override { return choc::value::createString (value); }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsStringProperty())
            return o->value == value;

        return false;
    }

private:
    PooledString value;
};

//==============================================================================
struct IntegerProperty   : public Property
{
    explicit IntegerProperty (Object& o, int64_t v = 0) : Property (o), value (v) {}

    static constexpr uint8_t typeID = 2;
    static constexpr bool isObjectProperty = false;

    void reset() override                                                   { value = {}; }
    bool hasDefaultValue() const override                                   { return value == 0; }
    bool isPrimitive() const override                                       { return true; }
    std::string_view getPropertyType() const override                       { return "int"; }
    uint8_t getPropertyTypeID() const override                              { return typeID; }
    void writeSignature (SignatureBuilder& sig) const override              { sig << std::to_string (value); }
    ptr<IntegerProperty> getAsIntegerProperty() override                    { return *this; }
    ptr<const IntegerProperty> getAsIntegerProperty() const override        { return *this; }
    void visitObjects (Visitor&) override                                   {}

    int64_t get() const                                                     { return value; }
    void set (int64_t newValue)                                             { value = newValue; }

    operator int64_t() const                                                { return get(); }
    IntegerProperty& operator= (int64_t newValue)                           { set (newValue); return *this; }

    Property& allocateEmptyCopy (Object& o) const override                  { return AST::getAllocator (o).allocate<IntegerProperty> (o); }
    Property& createClone (Object& o) const override                        { return AST::getAllocator (o).allocate<IntegerProperty> (o, value); }
    void deepCopy (const Property& source, RemappedObjects&) override       { auto s = source.getAsIntegerProperty(); CMAJ_ASSERT (s != nullptr); value = s->value; }
    choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) override     { return choc::value::createInt64 (value); }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsIntegerProperty())
            return o->value == value;

        return false;
    }

private:
    int64_t value = 0;
};

//==============================================================================
struct FloatProperty   : public Property
{
    explicit FloatProperty (Object& o, double v = 0) : Property (o), value (v) {}

    static constexpr uint8_t typeID = 3;
    static constexpr bool isObjectProperty = false;

    void reset() override                                               { value = {}; }
    bool hasDefaultValue() const override                               { return value == 0; }
    bool isPrimitive() const override                                   { return true; }
    std::string_view getPropertyType() const override                   { return "float"; }
    uint8_t getPropertyTypeID() const override                          { return typeID; }
    void writeSignature (SignatureBuilder& sig) const override          { sig << std::to_string (value); }
    ptr<FloatProperty> getAsFloatProperty() override                    { return *this; }
    ptr<const FloatProperty> getAsFloatProperty() const override        { return *this; }
    void visitObjects (Visitor&) override                               {}

    double get() const                                                  { return value; }
    void set (double newValue)                                          { value = newValue; }

    operator double() const                                             { return get(); }
    FloatProperty& operator= (double newValue)                          { set (newValue); return *this; }

    Property& allocateEmptyCopy (Object& o) const override              { return AST::getAllocator (o).allocate<FloatProperty> (o); }
    Property& createClone (Object& o) const override                    { return AST::getAllocator (o).allocate<FloatProperty> (o, value); }
    void deepCopy (const Property& source, RemappedObjects&) override   { auto s = source.getAsFloatProperty(); CMAJ_ASSERT (s != nullptr); value = s->value; }
    choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) override { return choc::value::createFloat64 (value); }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsFloatProperty())
            return o->value == value;

        return false;
    }

private:
    double value = 0;
};

//==============================================================================
struct BoolProperty   : public Property
{
    explicit BoolProperty (Object& o, bool v = false) : Property (o), value (v) {}

    static constexpr uint8_t typeID = 4;
    static constexpr bool isObjectProperty = false;

    void reset() override                                               { value = {}; }
    bool hasDefaultValue() const override                               { return ! value; }
    bool isPrimitive() const override                                   { return true; }
    std::string_view getPropertyType() const override                   { return "bool"; }
    uint8_t getPropertyTypeID() const override                          { return typeID; }
    void writeSignature (SignatureBuilder& sig) const override          { sig << (value ? "true" : "false"); }
    ptr<BoolProperty> getAsBoolProperty() override                      { return *this; }
    ptr<const BoolProperty> getAsBoolProperty() const override          { return *this; }
    void visitObjects (Visitor&) override                               {}

    bool get() const                                                    { return value; }
    void set (bool newValue)                                            { value = newValue; }

    operator bool() const                                               { return get(); }
    BoolProperty& operator= (bool newValue)                             { set (newValue); return *this; }

    Property& allocateEmptyCopy (Object& o) const override              { return AST::getAllocator (o).allocate<BoolProperty> (o); }
    Property& createClone (Object& o) const override                    { return AST::getAllocator (o).allocate<BoolProperty> (o, value); }
    void deepCopy (const Property& source, RemappedObjects&) override   { auto s = source.getAsBoolProperty(); CMAJ_ASSERT (s != nullptr); value = s->value; }
    choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) override { return choc::value::createBool (value); }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsBoolProperty())
            return o->value == value;

        return false;
    }

private:
    bool value = false;
};

//==============================================================================
struct EnumProperty   : public Property
{
    explicit EnumProperty (Object& o, int v = 0)  : Property (o), value (v)  {}

    static constexpr uint8_t typeID = 5;
    static constexpr bool isObjectProperty = false;

    void reset() override                                               { value = 0; }
    bool hasDefaultValue() const override                               { return false; }
    bool isPrimitive() const override                                   { return true; }
    std::string_view getPropertyType() const override                   { return "enum"; }
    uint8_t getPropertyTypeID() const override                          { return typeID; }
    void writeSignature (SignatureBuilder& sig) const override          { sig << getEnumString(); }
    ptr<EnumProperty> getAsEnumProperty() override                      { return *this; }
    ptr<const EnumProperty> getAsEnumProperty() const override          { return *this; }
    void visitObjects (Visitor&) override                               {}

    int getID() const                                   { return value; }
    void setID (int newValue)                           { CMAJ_ASSERT (getEnumList().isValidID (newValue)); value = newValue; }

    void set (std::string_view name)                    { setID (getEnumList().getID (name)); }
    PooledString getEnumString() const                  { return getStringPool().get (getEnumList().getNameForID (value)); }

    void deepCopy (const Property& source, RemappedObjects&) override
    {
        auto s = source.getAsEnumProperty();
        CMAJ_ASSERT (s != nullptr && getEnumList().items == s->getEnumList().items);
        value = s->value;
    }

    choc::value::Value toSyntaxTree (const SyntaxTreeOptions&) override  { return choc::value::createString (getEnumString()); }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsEnumProperty())
            return o->value == value;

        return false;
    }

    virtual EnumList getEnumList() const = 0;

private:
    int value = 0;
};

//==============================================================================
/// Can't really find a cleaner way to define one of these classes without resorting to a macro
#define CMAJ_DECLARE_ENUM_PROPERTY(EnumName, ...) \
    struct EnumName  : public EnumProperty \
    { \
        enum class Enum { __VA_ARGS__ }; \
        explicit EnumName (Object& o) : EnumProperty (o) {} \
        EnumName (Object& o, Enum v) : EnumProperty (o, static_cast<int> (v)) {} \
        EnumList getEnumList() const override                         { return getEnums(); } \
        static EnumList getEnums()                                    { static const EnumList list (#__VA_ARGS__); return list; } \
        Property& allocateEmptyCopy (Object& o) const override        { return AST::getAllocator (o).allocate<EnumName> (o); } \
        Property& createClone (Object& o) const override              { return AST::getAllocator (o).allocate<EnumName> (o, get()); } \
        operator Enum() const                                         { return get(); } \
        EnumProperty& operator= (Enum newValue)                       { set (newValue); return *this; } \
        Enum get() const                                              { return static_cast<Enum> (getID()); } \
        void set (Enum newValue)                                      { setID (static_cast<int> (newValue)); } \
        std::string_view getString() const                            { return getEnums().getNameForID (getID()); } \
        bool operator== (Enum testValue) const                        { return get() == testValue; } \
        bool operator!= (Enum testValue) const                        { return get() != testValue; } \
    };

//==============================================================================
// TODO: these probably belong in some other file, but not sure where..

// IMPORTANT: The numbers must remain constant if items are added or reordered, as these are part of the binary format spec

CMAJ_DECLARE_ENUM_PROPERTY (PrimitiveTypeEnum, void_ = 0, int32 = 1, int64 = 2, float32 = 3, float64 = 4,
                                               boolean = 5, string = 6, complex32 = 7, complex64 = 8)

CMAJ_DECLARE_ENUM_PROPERTY (UnaryOpTypeEnum, negate = 0, logicalNot = 1, bitwiseNot = 2)

CMAJ_DECLARE_ENUM_PROPERTY (BinaryOpTypeEnum, add = 0, subtract = 1, multiply = 2, divide = 3, modulo = 4, exponent = 5,
                                              bitwiseOr = 6, bitwiseAnd = 7, bitwiseXor = 8, logicalOr = 9, logicalAnd = 10,
                                              equals = 11, notEquals = 12, lessThan = 13, lessThanOrEqual = 14, greaterThan = 15,
                                              greaterThanOrEqual = 16, leftShift = 17, rightShift = 18, rightShiftUnsigned = 19)

CMAJ_DECLARE_ENUM_PROPERTY (EndpointTypeEnum, stream = 0, value = 1, event = 2)

CMAJ_DECLARE_ENUM_PROPERTY (InterpolationTypeEnum, none = 0, latch = 1, linear = 2, sinc = 3, fast = 4, best = 5)

CMAJ_DECLARE_ENUM_PROPERTY (ProcessorPropertyEnum, frequency = 0, period = 1, id = 2, session = 3, latency = 4, maxFrequency = 5)

CMAJ_DECLARE_ENUM_PROPERTY (TypeMetaFunctionTypeEnum, type = 0, makeConst = 1, makeReference = 2, removeConst = 3,
                                                      removeReference = 4, elementType = 5, primitiveType = 6, innermostElementType = 7)

CMAJ_DECLARE_ENUM_PROPERTY (ValueMetaFunctionTypeEnum, size = 0, isStruct = 1, isArray = 2, isSlice = 3,
                                                       isFixedSizeArray = 4, isVector = 5, isPrimitive = 6,
                                                       isFloat = 7, isFloat32 = 8, isFloat64 = 9, isInt = 10, isInt32 = 11, isInt64 = 12,
                                                       isScalar = 13, isString = 14, isBool = 15,
                                                       isComplex = 16, isReference = 17, isConst = 18, isEnum = 20,
                                                       numDimensions = 19, alloc = 21)

CMAJ_DECLARE_ENUM_PROPERTY (VariableTypeEnum, local = 0, parameter = 1, state = 2)

CMAJ_DECLARE_ENUM_PROPERTY (AliasTypeEnum, typeAlias = 0, processorAlias = 1, namespaceAlias = 2)

//==============================================================================
struct ObjectProperty   : public Property
{
    ObjectProperty (Object& o) : Property (o) {}

    std::string_view getPropertyType() const override                   { return "reference"; }
    void writeSignature (SignatureBuilder& sig) const override          { if (referencedObject != nullptr) sig << *referencedObject; else sig << "null"; }
    bool isPrimitive() const override                                   { return false; }
    ptr<ObjectProperty> getAsObjectProperty() override                  { return *this; }
    ptr<const ObjectProperty> getAsObjectProperty() const override      { return *this; }
    ptr<Object> getObject() const override                              { return getPointer(); }
    Object& getObjectRef() const override                               { CMAJ_ASSERT (referencedObject != nullptr); return *referencedObject; }
    ObjectRefVector<Object> getAsObjectList() const override            { return referencedObject != nullptr ? referencedObject->getAsObjectList() : ObjectRefVector<Object>(); }
    bool hasDefaultValue() const override                               { return referencedObject == nullptr; }
    void visitObjects (Visitor& v) override                             { if (referencedObject != nullptr) v.visitObject (*referencedObject); }
    ObjectContext& getContext() const override                          { CMAJ_ASSERT (referencedObject != nullptr); return referencedObject->context; }

    bool containsStatement (const Statement& s) const override
    {
        if (referencedObject != nullptr)
            if (auto statement = referencedObject->getAsStatement())
                return statement->containsStatement (s);

        return false;
    }

    PooledString toString() const override
    {
        if (referencedObject != nullptr)
            if (auto i = referencedObject->getAsIdentifier())
                return i->name;

        return {};
    }

    bool hasName (PooledString nameToMatch) const override
    {
        return referencedObject != nullptr
                && referencedObject->hasName (nameToMatch);
    }

    Object& get() const                     { CMAJ_ASSERT (referencedObject != nullptr); return *referencedObject; }
    Object* operator->() const              { CMAJ_ASSERT (referencedObject != nullptr); return referencedObject; }
    ptr<Object> getPointer() const          { return ptr<Object> (referencedObject); }
    Object* getRawPointer() const           { return referencedObject; }

    virtual bool isParentOfObject() const = 0;

    void referToUnchecked (const Object& newObject)
    {
        referencedObject = const_cast<Object*> (std::addressof (newObject));
        referencedObject->addReferrer (*this);
    }

    bool referTo (ptr<const Object> newChild)
    {
        if (auto c = newChild.get())
            return referTo (*c);

        reset();
        return true;
    }

    bool referTo (const Object& newObject)
    {
        if (referencedObject != std::addressof (newObject))
        {
            if (newObject.isConstantValueBase() && ! owner.canConstantFoldProperty (*this))
                return false;

            if (referencedObject != nullptr)
                referencedObject->removeReferrer (*this);

            referToUnchecked (newObject);
            return true;
        }

        return false;
    }

    bool replaceWith (Object& newObject)
    {
        return referTo (newObject);
    }

    void reset() override
    {
        if (referencedObject != nullptr)
        {
            referencedObject->removeReferrer (*this);
            referencedObject = nullptr;
        }
    }

    operator Object&() const                                 { return get(); }

    bool operator== (decltype(nullptr)) const                { return referencedObject == nullptr; }
    bool operator!= (decltype(nullptr)) const                { return referencedObject != nullptr; }

    choc::value::Value toSyntaxTree (const SyntaxTreeOptions& options) override
    {
        if (auto* ref = referencedObject)
        {
            if (isParentOfObject())
                return ref->toSyntaxTree (options);

            return choc::value::createString ("ID:" + std::to_string (options.getObjectID (*ref)));
        }

        return {};
    }

    void updateObjectMappings (RemappedObjects& objectMap) override
    {
        if (referencedObject != nullptr)
        {
            auto newObject = objectMap.find (referencedObject);

            if (newObject != objectMap.end())
            {
                CMAJ_ASSERT (newObject->second != nullptr);
                referencedObject = newObject->second;
                referencedObject->addReferrer (*this);
            }

            if (isParentOfObject())
                referencedObject->updateObjectMappings (objectMap);
        }
    }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsObjectProperty())
        {
            if (referencedObject == o->referencedObject)
                return true;

            if (referencedObject != nullptr && o->referencedObject != nullptr)
                return referencedObject->isIdentical (*o->referencedObject);
        }

        return false;
    }

private:
    Object* referencedObject = nullptr;
};


//==============================================================================
struct ChildObject  : public ObjectProperty
{
    ChildObject (Object& o) : ObjectProperty (o) {}

    static constexpr uint8_t typeID = 6;
    static constexpr bool isObjectProperty = true;

    uint8_t getPropertyTypeID() const override      { return typeID; }
    bool isParentOfObject() const override          { return true; }

    ptr<ChildObject>       getAsChildObject() override           { return *this; }
    ptr<const ChildObject> getAsChildObject() const override     { return *this; }

    void setChildObject (Object& newObject)
    {
        CMAJ_ASSERT (std::addressof (newObject) != std::addressof (owner));
        newObject.setParentScope (owner);
        referTo (newObject);
    }

    template <typename ObjectType>
    void createReferenceTo (const ObjectType& target)
    {
        setChildObject (createReference (owner, target));
    }

    template <typename ConstantType>
    void createConstant (ConstantType value)
    {
        setChildObject (getAllocator().createConstant (value));
    }

    Property& allocateEmptyCopy (Object& o) const override
    {
        return AST::getAllocator (o).allocate<ChildObject> (o);
    }

    Property& createClone (Object& o) const override
    {
        auto& p = AST::getAllocator (o).allocate<ChildObject> (o);

        if (auto* ref = getRawPointer())
            p.setChildObject (*ref);

        return p;
    }

    void deepCopy (const Property& source, RemappedObjects& objectMap) override
    {
        CMAJ_ASSERT (*this == nullptr); // this method is only designed for use on an empty property
        auto s = source.getAsObjectProperty();
        CMAJ_ASSERT (s != nullptr);

        if (auto* o = s->getRawPointer())
        {
            if (objectMap.find (o) == objectMap.end())
                setChildObject (o->createDeepClone (getAllocator(), objectMap));
            else
                referTo (*o);
        }
    }
};

//==============================================================================
struct ObjectReference  : public ObjectProperty
{
    ObjectReference (Object& o) : ObjectProperty (o) {}

    static constexpr uint8_t typeID = 7;
    static constexpr bool isObjectProperty = true;

    uint8_t getPropertyTypeID() const override      { return typeID; }
    bool isParentOfObject() const override          { return false; }

    ptr<ObjectReference>        getAsObjectReference() override        { return *this; }
    ptr<const ObjectReference>  getAsObjectReference() const override  { return *this; }

    Property& allocateEmptyCopy (Object& o) const override
    {
        return AST::getAllocator (o).allocate<ObjectReference> (o);
    }

    Property& createClone (Object& o) const override
    {
        auto& p = AST::getAllocator (o).allocate<ObjectReference> (o);

        if (auto* ref = getRawPointer())
            p.referTo (*ref);

        return p;
    }

    void deepCopy (const Property& source, RemappedObjects&) override
    {
        CMAJ_ASSERT (*this == nullptr); // this method is only designed for use on an empty property
        auto s = source.getAsObjectProperty();
        CMAJ_ASSERT (s != nullptr);

        if (auto* o = s->getRawPointer())
            referTo (*o);
    }
};

//==============================================================================
struct ListProperty   : public Property
{
    explicit ListProperty (Object& o) : Property (o) {}

    static constexpr uint8_t typeID = 8;
    static constexpr bool isObjectProperty = true;

    std::string_view getPropertyType() const override              { return "list"; }
    uint8_t getPropertyTypeID() const override                     { return typeID; }
    bool isPrimitive() const override                              { return false; }
    ptr<ListProperty> getAsListProperty() override                 { return *this; }
    ptr<const ListProperty> getAsListProperty() const override     { return *this; }
    bool hasDefaultValue() const override                          { return list.empty(); }

    void visitObjects (Visitor& v) override
    {
        for (size_t i = 0; i < list.size(); ++i)
            list[i]->visitObjects (v);
    }

    void reset() override
    {
        for (auto& p : list)
            p->reset();

        list.clear();
    }

    const std::vector<ref<Property>>& get() const         { return list; }

    template <typename ObjectType>
    struct TypedIterator
    {
        TypedIterator (const ListProperty& l) : list (l.list) {}

        struct Iterator
        {
            Iterator (const ref<Property>* i) : item (i) {}

            ObjectType& operator*() const    { return castToRefSkippingReferences<ObjectType> (*item); }
            Iterator& operator++()           { ++item; return *this; }

            bool operator== (const Iterator& i) const     { return item == i.item; }
            bool operator!= (const Iterator& i) const     { return item != i.item; }

            const ref<Property>* item;
        };

        Iterator begin() const                  { return { list.data() }; }
        Iterator end() const                    { return { list.data() + list.size() }; }
        size_t size() const                     { return list.size(); }
        ObjectType& front() const               { return castToRefSkippingReferences<ObjectType> (list.front()); }
        ObjectType& back() const                { return castToRefSkippingReferences<ObjectType> (list.back()); }
        ObjectType& operator[](size_t i) const  { return castToRefSkippingReferences<ObjectType> (list[i]); }

        const std::vector<ref<Property>>& list;
    };

    template <typename ObjectType>
    TypedIterator<ObjectType> iterateAs() const
    {
        return TypedIterator<ObjectType> (*this);
    }

    ObjectRefVector<Object> getAsObjectList() const override
    {
        ObjectRefVector<Object> result;
        result.reserve (list.size());

        for (auto& item : list)
            result.push_back (item->getObjectRef());

        return result;
    }

    template <typename ObjectType>
    ObjectRefVector<ObjectType> getAsObjectTypeList() const
    {
        ObjectRefVector<ObjectType> result;
        result.reserve (size());

        for (auto& item : iterateAs<ObjectType>())
            result.push_back (item);

        return result;
    }

    template <typename ObjectType>
    ObjectRefVector<ObjectType> findAllObjectsOfType() const
    {
        ObjectRefVector<ObjectType> result;

        for (auto& item : list)
            if (auto o = castToSkippingReferences<ObjectType> (item))
                result.push_back (*o);

        return result;
    }

    bool containsStatement (const Statement& s) const override
    {
        for (auto& item : list)
            if (item->containsStatement (s))
                return true;

        return false;
    }

    auto begin()         { return list.begin(); }
    auto end()           { return list.end(); }
    auto begin() const   { return list.begin(); }
    auto end() const     { return list.end(); }

    void set (choc::span<ref<Property>> newList)
    {
        for (auto& p : newList)
            CMAJ_ASSERT (std::addressof (getAllocator()) == std::addressof (p->getAllocator()));

        reset();
        list = std::vector<ref<Property>> (newList.begin(), newList.end());
    }

    bool empty() const                          { return list.empty(); }
    size_t size() const                         { return list.size(); }
    Property& operator[] (size_t index) const   { CMAJ_ASSERT (index < list.size()); return list[index]; }
    Property& front() const                     { CMAJ_ASSERT (! list.empty()); return list.front(); }
    Property& back() const                      { CMAJ_ASSERT (! list.empty()); return list.back(); }
    void reserve (size_t size)                  { list.reserve (size); }

    void add (Property& p, int insertIndex = -1)
    {
        if (insertIndex < 0)
            list.push_back (p);
        else
            list.insert (list.begin() + insertIndex, p);
    }

    void set (Property& p, size_t index)
    {
        CMAJ_ASSERT (index < list.size());
        list[index] = p;
    }

    void addReference (const Object& o, int insertIndex = -1)           { auto& p = getAllocator().allocate<ChildObject> (owner); p.referTo (o); add (p, insertIndex); }
    void addChildObject (Object& o, int insertIndex = -1)               { auto& p = getAllocator().allocate<ChildObject> (owner); p.setChildObject (o); add (p, insertIndex); }
    void addNullObject (int insertIndex = -1)                           { add (getAllocator().allocate<ChildObject> (owner), insertIndex); }
    void setChildObject (Object& o, size_t index)                       { CMAJ_ASSERT (index < list.size()); auto c = list[index]->getAsChildObject(); if (c == nullptr) { c = getAllocator().allocate<ChildObject> (owner); list[index] = *c; } c->setChildObject (o); }
    void addString (PooledString value, int insertIndex = -1)           { add (getAllocator().allocate<StringProperty> (owner, value), insertIndex); }
    void setString (PooledString value, size_t index)                   { set (getAllocator().allocate<StringProperty> (owner, value), index); }
    void addClone (Property& p, int insertIndex = -1)                   { add (p.createClone (owner), insertIndex); }

    void moveListItems (ListProperty& sourceList)
    {
        list.reserve (list.size() + sourceList.list.size());

        for (auto& item : sourceList)
        {
            list.push_back (item);

            if (auto o = item->getObject())
                o->setParentScope (owner);
        }

        sourceList.list.clear(); // must not call reset() on the source, as we have all its items now
    }

    void remove (size_t index)
    {
        CMAJ_ASSERT (index < list.size());
        list[index]->reset();
        list.erase (list.begin() + static_cast<decltype(list)::difference_type> (index));
    }

    void remove (size_t start, size_t end)
    {
        for (auto i = end; i > start; --i)
            remove (i - 1);
    }

    template <typename Predicate>
    void removeIf (Predicate&& predicate)
    {
        for (size_t i = list.size(); i > 0; --i)
            if (predicate (list[i - 1]))
                remove (i - 1);
    }

    int indexOf (const Object& o) const
    {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i]->getObject() == o)
                return static_cast<int> (i);

        return -1;
    }

    ptr<Object> findObjectWithName (PooledString name) const
    {
        for (auto& item : list)
            if (auto o = item->getObject().get())
                if (o->hasName (name))
                    return *o;

        return {};
    }

    bool removeObject (const AST::Object& o)
    {
        auto index = indexOf (o);

        if (index < 0)
            return false;

        remove (static_cast<size_t> (index));
        return true;
    }

    operator const std::vector<ref<Property>>&() const             { return get(); }
    ListProperty& operator= (choc::span<ref<Property>> newList)    { set (newList); return *this; }

    void writeSignature (SignatureBuilder& sig) const override
    {
        sig << list.size();

        for (auto& i : list)
            sig << i;
    }

    Property& allocateEmptyCopy (Object& o) const override      { return AST::getAllocator (o).allocate<ListProperty> (o); }

    Property& createClone (Object& o) const override
    {
        auto& newList = AST::getAllocator (o).allocate<ListProperty> (o);

        for (auto& p : list)
            newList.addClone (p);

        return newList;
    }

    void shallowCopy (const ListProperty& source)
    {
        CMAJ_ASSERT (list.empty()); // this method is only designed for use on an empty property

        for (auto& item : source)
            addReference (item->getObjectRef());
    }

    void deepCopy (const Property& source, RemappedObjects& remappedObjects) override
    {
        CMAJ_ASSERT (list.empty()); // this method is only designed for use on an empty property
        auto s = source.getAsListProperty();
        CMAJ_ASSERT (s != nullptr);
        list.reserve (s->list.size());

        for (auto& p : s->list)
        {
            if (p->isPrimitive())
            {
                list.push_back (p->createClone (owner));
            }
            else
            {
                list.push_back (p->allocateEmptyCopy (owner));
                list.back()->deepCopy (p, remappedObjects);
            }
        }
    }

    void updateObjectMappings (RemappedObjects& objectMap) override
    {
        for (auto& p : list)
            p->updateObjectMappings (objectMap);
    }

    choc::value::Value toSyntaxTree (const SyntaxTreeOptions& options) override
    {
        return choc::value::createArray (static_cast<uint32_t> (size()),
                                         [&] (uint32_t index) { return list[index]->toSyntaxTree (options); });
    }

    bool isIdentical (const Property& other) const override
    {
        if (auto o = other.getAsListProperty())
        {
            if (o->list.size() == list.size())
            {
                for (size_t i = 0; i < list.size(); ++i)
                    if (! list[i]->isIdentical (o->list[i]))
                        return false;

                return true;
            }
        }

        return false;
    }

private:
    std::vector<ref<Property>> list;
};
