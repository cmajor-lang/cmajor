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


struct ObjectContext
{
    Allocator& allocator;
    CodeLocation location;
    ptr<Object> parentScope;

    SourceFile& getSourceFile() const               { return allocator.sourceFileList.getSourceFileContaining (location); }
    SourceFile* findSourceFile() const              { return allocator.sourceFileList.findSourceFileContaining (location); }
    FullCodeLocation getFullLocation() const        { return FullCodeLocation::from (allocator.sourceFileList, location); }

    bool isInSystemModule() const
    {
        if (parentScope != nullptr && parentScope->findSelfOrParentOfType<AST::ModuleBase>()->isSystemModule())
            return true;

        if (auto f = findSourceFile())
            return f->isSystem;

        return true;
    }

    [[noreturn]] void throwError (DiagnosticMessage message) const
    {
        DiagnosticMessageList messages;
        messages.add (message.withLocation (getFullLocation()));
        cmaj::throwError (messages);
    }

    void emitMessage (DiagnosticMessage message) const
    {
        cmaj::emitMessage (message.withLocation (getFullLocation()));
    }

    template <typename Type>
    Type& allocate() const           { return allocator.allocate<Type> (*this); }
};

//==============================================================================
#define CMAJ_AST_DECLARE_STANDARD_METHODS_NO_PROPS(Class, UID) \
    using ThisClass = Class; \
    static constexpr uint8_t classID = UID; \
    uint8_t getObjectClassID() const override               { return UID; } \
    std::string_view getObjectType() const override         { return std::string_view (#Class); } \
    Object& allocateClone (ObjectContext c) const override  { return c.allocator.template allocate<Class> (c); } \
    void invokeVisitorCallback (Visitor& v) override        { return v.visit (*this); } \
    Class* getAs ## Class() override                        { return this; } \
    const Class* getAs ## Class() const override            { return this; } \
    bool is ## Class() const override                       { return true; } \

#define CMAJ_AST_DECLARE_STANDARD_METHODS(Class, UID) \
    CMAJ_AST_DECLARE_STANDARD_METHODS_NO_PROPS(Class, UID) \
    uint32_t getNumProperties() const override                        { return numProperties; } \
    std::string_view getPropertyName (uint32_t i) const override      { CMAJ_ASSERT (i < numProperties); return propertyNames[i]; } \
    uint8_t getPropertyID (uint32_t i) const override                 { CMAJ_ASSERT (i < numProperties); return propertyIDs[i]; }

#define CMAJ_OBJ_DECLARE_PROPERTY_MEMBER(propID, Type, name)    Type name { *this };
#define CMAJ_OBJ_FILL_PROP_LIST(propID, Type, name)             props.properties[i++] = std::addressof (name);
#define CMAJ_OBJ_DECLARE_PROPERTY_NAMES(propID, Type, name)     #name,
#define CMAJ_OBJ_DECLARE_PROPERTY_IDS(propID, Type, name)       propID,
#define CMAJ_OBJ_VISIT_OBJECT_PROPERTY(propID, Type, name)      if constexpr (Type::isObjectProperty) name.visitObjects (v);
#define CMAJ_OBJ_COMPARE_PROPERTY(propID, Type, name)           && name.isIdentical (static_cast<const ThisClass&> (other).name)
#define CMAJ_OBJ_FIND_PROP_FOR_ID(propID, Type, name)           if (propID == targetID) return std::addressof (name);

#define CMAJ_DECLARE_PROPERTY_ACCESSORS(LIST) \
    static constexpr uint8_t propertyIDs[] = { LIST(CMAJ_OBJ_DECLARE_PROPERTY_IDS) }; \
    static constexpr uint32_t numProperties = sizeof (propertyIDs); \
    static_assert (numProperties <= PropertyList::maxNumProperties); \
    static constexpr const char* propertyNames[numProperties] = { LIST(CMAJ_OBJ_DECLARE_PROPERTY_NAMES) }; \
    PropertyList getPropertyList() override                             { PropertyList props (numProperties); uint32_t i = 0; LIST(CMAJ_OBJ_FILL_PROP_LIST) return props; } \
    Property* findPropertyForID (uint32_t targetID) override            { LIST(CMAJ_OBJ_FIND_PROP_FOR_ID) return nullptr; } \
    template <typename VisitorType> void visitObjects (VisitorType& v)  { LIST(CMAJ_OBJ_VISIT_OBJECT_PROPERTY); } \
    bool isIdentical (const Object& other) const override               { return classID == other.getObjectClassID() LIST(CMAJ_OBJ_COMPARE_PROPERTY); } \

#define CMAJ_DECLARE_PROPERTIES(LIST) \
    LIST(CMAJ_OBJ_DECLARE_PROPERTY_MEMBER) \
    CMAJ_DECLARE_PROPERTY_ACCESSORS (LIST)

//==============================================================================
/// The base class for all AST objects
struct Object
{
    Object (const ObjectContext& c) : context (c)  {}
    virtual ~Object() = default;

    Object (Object&&) = delete;
    Object (const Object&) = delete;
    Object& operator= (Object&&) = delete;
    Object& operator= (const Object&) = delete;

    //==============================================================================
    virtual uint8_t getObjectClassID() const = 0;
    virtual std::string_view getObjectType() const = 0;
    virtual Object& allocateClone (ObjectContext) const = 0;

    template <typename Type>
    Type& allocateChild()               { return context.allocator.getContextWithoutLocation (*this).allocate<Type>(); }

    const Strings& getStrings() const   { return context.allocator.strings; }
    StringPool& getStringPool() const   { return context.allocator.strings.stringPool; }

    //==============================================================================
    virtual PropertyList getPropertyList()                              { return PropertyList (0); }
    virtual uint32_t getNumProperties() const                           { return 0; }
    virtual std::string_view getPropertyName (uint32_t) const           { CMAJ_ASSERT_FALSE; return {}; }
    virtual uint8_t getPropertyID (uint32_t) const                      { CMAJ_ASSERT_FALSE; return 0; }
    virtual Property* findPropertyForID (uint32_t)                      { CMAJ_ASSERT_FALSE; return {}; }
    virtual bool canConstantFoldProperty (const Property&)              { return true; }
    virtual bool isDummyStatement() const                               { return false; }
    virtual ptr<const Comment> getComment() const                       { return {}; }
    virtual bool isIdentical (const Object&) const = 0;

    using ObjectVisitor = const std::function<void(Object&)>&;

    virtual void visitObjectsInScope (ObjectVisitor visit)
    {
        visit (*this);
    }

    static void visitObjectIfPossible (const Property& p, ObjectVisitor visitor)
    {
        if (auto op = p.getAsObjectProperty())
        {
            if (auto o = op->getPointer())
                o->visitObjectsInScope (visitor);
        }
        else if (auto list = p.getAsListProperty())
        {
            for (auto& item : *list)
                visitObjectIfPossible (item, visitor);
        }
    }

    //==============================================================================
    ptr<Object> getParentScope() const              { return context.parentScope; }
    Object& getParentScopeRef() const               { return *context.parentScope; }
    void setParentScope (Object& newParent)         { context.parentScope = newParent; }

    template <typename ObjectType>
    ptr<ObjectType> findSelfOrParentOfType()
    {
        for (Object* o = this; o != nullptr; o = o->getParentScope().get())
            if (auto result = castTo<ObjectType> (*o))
                return result;

        return {};
    }

    template <typename ObjectType>
    ptr<const ObjectType> findSelfOrParentOfType() const
    {
        for (const Object* o = this; o != nullptr; o = o->getParentScope().get())
            if (auto result = castTo<const ObjectType> (*o))
                return result;

        return {};
    }

    template <typename ObjectType>
    ptr<ObjectType> findParentOfType() const
    {
        for (auto* o = getParentScope().get(); o != nullptr; o = o->getParentScope().get())
            if (auto result = castTo<ObjectType> (*o))
                return result;

        return {};
    }

    bool isChildOf (const Object& possibleParent) const
    {
        for (auto o = getParentScope(); o != nullptr; o = o->getParentScope())
            if (o == possibleParent)
                return true;

        return false;
    }

    ptr<ModuleBase> findParentModule() const                                    { return findParentOfType<ModuleBase>(); }
    ModuleBase& getParentModule() const                                         { return *findParentModule(); }

    ptr<Namespace> findParentNamespace() const                                  { return findParentOfType<Namespace>(); }
    Namespace& getParentNamespace() const                                       { return *findParentNamespace(); }

    ptr<Function> findParentFunction() const                                    { return findParentOfType<Function>(); }
    Function& getParentFunction() const                                         { return *findParentFunction(); }

    ptr<ProcessorBase> findParentProcessor() const                              { return findParentOfType<ProcessorBase>(); }
    ProcessorBase& getParentProcessor() const                                   { return *findParentProcessor(); }

    virtual Namespace& getRootNamespace()                                       { return getParentNamespace().getRootNamespace(); }
    virtual const Namespace& getRootNamespace() const                           { return getParentNamespace().getRootNamespace(); }

    virtual void performLocalNameSearch (NameSearch&, ptr<const Statement>)     { CMAJ_ASSERT_FALSE; }
    virtual bool hasName (PooledString) const                                   { return false; }
    virtual PooledString getName() const                                        { return {}; }
    virtual void setName (PooledString)                                         { CMAJ_ASSERT_FALSE; }
    virtual PooledString getOriginalName() const                                { return getName(); }
    virtual const ObjectContext& getLocationOfStartOfExpression() const         { return context; }
    virtual bool isSyntacticObject() const                                      { return false; }
    virtual bool isGenericOrParameterised() const                               { return false; }
    virtual void writeSignature (SignatureBuilder&) const                       { CMAJ_ASSERT_FALSE; }
    virtual ptr<Object> getTargetSkippingReferences() const                     { return {}; }

    IdentifierPath getFullyQualifiedName() const
    {
        if (auto r = getAsNamedReference())
            return r->getTarget().getFullyQualifiedName();

        auto name = getName();

        if (auto v = getAsVariableDeclaration())
            if (v->isLocal() || v->isParameter())
                return IdentifierPath (name);

        if (getAsPrimitiveType() || getAsArrayType())
            return IdentifierPath (name);

        for (auto o = getParentScope(); o != nullptr; o = o->getParentScope())
            if (auto m = o->getAsModuleBase())
                if (! m->isRootNamespace())
                    return m->getFullyQualifiedName().getChildPath (name);

        return IdentifierPath (name);
    }

    std::string getFullyQualifiedNameWithoutRoot() const
    {
        return getFullyQualifiedName().withoutTopLevelNameIfPresent (getStrings().rootNamespaceName).fullPath;
    }

    std::string getFullyQualifiedReadableName() const
    {
        if (auto r = getAsNamedReference())
            return r->getTarget().getFullyQualifiedReadableName();

        auto name = getOriginalName();

        if (name.empty())
            return {};

        if (auto v = getAsVariableDeclaration())
            if (v->isLocal() || v->isParameter())
                return std::string (name);

        if (getAsPrimitiveType() || getAsArrayType())
            return std::string (name);

        for (auto o = getParentScope(); o != nullptr; o = o->getParentScope())
            if (auto m = o->getAsModuleBase())
                if (! m->isRootNamespace())
                    return IdentifierPath::join (m->getFullyQualifiedReadableName(), name);

        return std::string (name);
    }

    ObjectRefVector<Object> getAsObjectList()
    {
        if (auto list = getAsExpressionList())
            return list->items.getAsObjectList();

        ObjectRefVector<Object> result;
        result.push_back (*this);
        return result;
    }

    bool replaceWith (Object& replacement)
    {
        CMAJ_ASSERT (firstReferrer != nullptr);

        if (firstReferrer->next == nullptr)
            return firstReferrer->referrer.replaceWith (replacement);

        bool anyReplaced = false;

        for (auto r = firstReferrer; r != nullptr; r = r->next)
            if (r->referrer.replaceWith (replacement))
                anyReplaced = true;

        return anyReplaced;
    }

    bool replaceWith (std::function<Object&()> f)
    {
        CMAJ_ASSERT (firstReferrer != nullptr);

        if (firstReferrer->next == nullptr)
            return firstReferrer->referrer.replaceWith (f());

        auto referrersCopy = getReferrers();
        auto& replacement = f();
        bool anyReplaced = false;

        for (auto& r : referrersCopy)
            if (r->replaceWith (replacement))
                anyReplaced = true;

        return anyReplaced;
    }

    //==============================================================================
    Object& createDeepClone (Allocator& newContext) const
    {
        RemappedObjects objectMap;
        auto& result = createDeepClone (newContext, objectMap);
        result.updateObjectMappings (objectMap);
        return result;
    }

    virtual bool shouldPropertyAppearInSyntaxTree (const SyntaxTreeOptions&, uint8_t)    { return true; }

    choc::value::Value toSyntaxTree (const SyntaxTreeOptions& options)
    {
        auto o = choc::json::create ("OBJECT", getObjectType(),
                                     "ID", options.getObjectID (*this));

        if (options.options.includeSourceLocations && ! context.location.empty())
            o.addMember ("location", choc::text::trim (context.getFullLocation().getLocationDescription()));

        auto props = getPropertyList();

        for (uint32_t i = 0; i < props.size(); ++i)
            if (shouldPropertyAppearInSyntaxTree (options, getPropertyID (i)))
                o.addMember (getPropertyName (i), props[i].toSyntaxTree (options));

        return o;
    }

    //==============================================================================
    // These methods are only to be called by the property classes when
    // the hierarchy is modified.
    void addReferrer (ObjectProperty& newReferrer)
    {
        auto& r = context.allocator.allocate<ReferrerLinkedListItem> (ReferrerLinkedListItem { newReferrer, firstReferrer });
        firstReferrer = std::addressof (r);
    }

    void removeReferrer (ObjectProperty& oldReferrer)
    {
        if (firstReferrer == nullptr)
            return;

        if (std::addressof (firstReferrer->referrer) == std::addressof (oldReferrer))
        {
            firstReferrer = firstReferrer->next;
            return;
        }

        for (auto r = firstReferrer; r->next != nullptr; r = r->next)
        {
            if (std::addressof (r->next->referrer) == std::addressof (oldReferrer))
            {
                r->next = r->next->next;
                return;
            }
        }
    }

    bool hasAnyReferrers() const
    {
        return firstReferrer != nullptr;
    }

    choc::SmallVector<ObjectProperty*, 4> getReferrers() const
    {
        choc::SmallVector<ObjectProperty*, 4> result;

        size_t count = 0;

        for (auto r = firstReferrer; r != nullptr; r = r->next)
            ++count;

        result.reserve (count);

        for (auto r = firstReferrer; r != nullptr; r = r->next)
            result.push_back (std::addressof (r->referrer));

        return result;
    }

    virtual void invokeVisitorCallback (Visitor&) = 0;

    //==============================================================================
    ObjectContext context;

    struct ReferrerLinkedListItem
    {
        ObjectProperty& referrer;
        ReferrerLinkedListItem* next = nullptr;
    };

    ReferrerLinkedListItem* firstReferrer = nullptr;

    //==============================================================================
    #define CMAJ_DECLARE_CASTS(Class) \
        virtual Class*             getAs ## Class()            { return nullptr; } \
        virtual const Class*       getAs ## Class() const      { return nullptr; } \
        virtual bool               is ## Class() const         { return false; } \

    CMAJ_CLASSES_NEEDING_CAST(CMAJ_DECLARE_CASTS)
    #undef CMAJ_DECLARE_CASTS

private:
    //==============================================================================
    friend struct Visitor;

    static constexpr uint16_t maxActiveVisitorStackDepth = 4;
    uint32_t activeVisitors[maxActiveVisitorStackDepth] = {};

    bool checkAndUpdateVisitorStatus (uint32_t visitorDepth, uint16_t visitorID)
    {
        if (activeVisitors[visitorDepth] == visitorID)
            return false;

        activeVisitors[visitorDepth] = visitorID;
        return true;
    }

    //==============================================================================
    friend struct ObjectProperty;
    friend struct ChildObject;

    Object& createDeepClone (Allocator& newContext, RemappedObjects& objectMap) const
    {
        auto& dest = allocateClone (newContext.getContext (context.location, context.parentScope));
        objectMap[this] = std::addressof (dest);

        auto sourceProps = const_cast<Object&> (*this).getPropertyList();
        auto destProps = dest.getPropertyList();
        CMAJ_ASSERT (destProps.size() == sourceProps.size());

        for (size_t i = 0; i < destProps.size(); ++i)
        {
            auto& sourceProp = sourceProps[i];

            if (! sourceProp.hasDefaultValue())
                destProps[i].deepCopy (sourceProp, objectMap);
        }

        return dest;
    }

    void updateObjectMappings (RemappedObjects& objectMap)
    {
        auto newScope = objectMap.find (context.parentScope.get());

        if (newScope != objectMap.end())
            setParentScope (*(newScope->second));

        for (auto& p : getPropertyList())
            p->updateObjectMappings (objectMap);
    }
};
