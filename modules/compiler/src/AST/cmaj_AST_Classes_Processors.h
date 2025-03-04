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


struct ModuleBase  : public Object
{
    ModuleBase (const ObjectContext& c) : Object (c) {}

    ModuleBase* getAsModuleBase() override                      { return this; }
    const ModuleBase* getAsModuleBase() const override          { return this; }
    bool isModuleBase() const override                          { return true; }
    ptr<const Comment> getComment() const override              { return castTo<const Comment> (comment); }
    void writeSignature (SignatureBuilder& sig) const override  { sig << getFullyQualifiedNameWithoutRoot(); }
    bool isRootNamespace() const                                { return name == getStrings().rootNamespaceName; }

    bool isSystemModule() const
    {
        if (auto p = findParentNamespace())
            return p->isRootNamespace() ? isSystem
                                        : p->isSystemModule();

        return false;
    }

    virtual std::string_view getModuleType() const = 0;

    bool hasName (PooledString nametoMatch) const override        { return name.hasName (nametoMatch); }
    PooledString getName() const override                         { return name; }
    void setName (PooledString newName) override                  { name.set (newName); }

    bool isGenericOrParameterised() const override                { return ! specialisationParams.empty(); }

    bool isAnyParentParameterised() const
    {
        if (isGenericOrParameterised())
            return true;

        auto p = findParentNamespace();
        return p != nullptr && p->isAnyParentParameterised();
    }

    bool isSpecialised() const                                    { return ! originalName.get().empty(); }

    PooledString getOriginalName() const override
    {
        if (isRootNamespace())
            return {};

        if (! originalName.hasDefaultValue())
            return originalName;

        return name;
    }

    virtual ptr<ModuleBase> findChildModule (PooledString moduleName)
    {
        if (auto a = aliases.findObjectWithName (moduleName))
            if (auto t = a->getAsAlias())
                return castToSkippingReferences<ModuleBase> (t->target);

        return {};
    }

    virtual ptr<VariableDeclaration> findVariable (PooledString variableName) = 0;

    ptr<Function> findFunction (PooledString functionName, size_t numParameters) const
    {
        for (auto& f : functions)
            if (auto o = f->getObject().get())
                if (o->hasName (functionName))
                    if (auto fn = o->getAsFunction())
                        if (fn->parameters.size() == numParameters)
                            return *fn;

        return {};
    }

    ptr<Function> findFunction (std::string_view functionName, size_t numParameters) const
    {
        for (auto& f : functions.iterateAs<AST::Function>())
            if (f.name == functionName && f.parameters.size() == numParameters)
                return f;

        return {};
    }

    ptr<Function> findFunction (std::string_view functionName, choc::span<ref<const TypeBase>> parameterTypes) const
    {
        for (auto& f : functions.iterateAs<AST::Function>())
            if (f.name == functionName && f.hasParameterTypes (parameterTypes))
                return f;

        return {};
    }

    template <typename Predicate>
    ptr<Function> findFunction (Predicate&& pred) const
    {
        for (auto& f : functions)
        {
            auto& fn = castToFunctionRef (f);

            if (pred (fn))
                return fn;
        }

        return {};
    }

    ptr<Function> findQualifiedFunction (const IdentifierPath& qualifiedFunctionName, choc::span<ref<const TypeBase>> parameterTypes)
    {
        if (qualifiedFunctionName.isUnqualified())
            return findFunction (qualifiedFunctionName.fullPath, parameterTypes);

        if (auto m = findChildModule (getStringPool().get (qualifiedFunctionName.getSection (0))))
            return m->findQualifiedFunction (qualifiedFunctionName.withoutTopLevelName(), parameterTypes);

        return {};
    }

    template <typename... ParamTypes>
    ptr<Function> findQualifiedFunction (std::string_view qualifiedFunctionName, ParamTypes&&... parameterTypes)
    {
        ref<const TypeBase> paramTypes[] = { ref<const TypeBase> (parameterTypes)... };
        return findQualifiedFunction (IdentifierPath (qualifiedFunctionName), paramTypes);
    }

    ptr<StructType> findStruct (PooledString structName) const
    {
        if (auto o = structures.findObjectWithName (structName))
            return ptr<StructType> (o->getAsStructType());

        return {};
    }

    bool isSameOriginalModule (const ModuleBase& other) const
    {
        if (getOriginalName() == other.getOriginalName())
        {
            bool isRoot1 = isRootNamespace();
            bool isRoot2 = other.isRootNamespace();

            if (isRoot1 || isRoot2)
                return isRoot1 && isRoot2;

            return getParentModule().isSameOriginalModule (other.getParentModule());
        }

        return false;
    }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement>) override
    {
        auto targetName = search.nameToFind;

        if (search.findTypes)
        {
            search.addFirstMatching (structures);
            search.addFirstMatching (enums);
        }

        if (search.findNamespaces || search.findProcessors || search.findTypes)
        {
            for (auto& a : aliases)
            {
                if (a->hasName (targetName))
                {
                    auto& alias = castToRef<Alias> (a);
                    auto aliasType = alias.aliasType.get();

                    if ((search.findNamespaces     && aliasType == AliasTypeEnum::Enum::namespaceAlias)
                         || (search.findProcessors && aliasType == AliasTypeEnum::Enum::processorAlias)
                         || (search.findTypes      && aliasType == AliasTypeEnum::Enum::typeAlias))
                    {
                        search.addResult (alias);
                    }
                }
            }
        }

        if (search.findVariables)
            if (auto v = findVariable (targetName))
                search.addResult (*v);

        if (search.findFunctions)
            for (auto& f : functions) // don't call findFunction here, as we want to find multiple fns
                if (auto o = f->getObject())
                    if (o->hasName (targetName))
                        if (auto fn = o->getAsFunction())
                            if (search.requiredNumFunctionParams < 0
                                 || fn->parameters.size() == static_cast<uint32_t> (search.requiredNumFunctionParams))
                                search.addResult (*fn);
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (annotation, visit);
        visitObjectIfPossible (specialisationParams, visit);
        visitObjectIfPossible (aliases, visit);
        visitObjectIfPossible (functions, visit);
        visitObjectIfPossible (structures, visit);
        visitObjectIfPossible (enums, visit);
        visitObjectIfPossible (staticAssertions, visit);
    }

    StringProperty name                 { *this },
                   originalName         { *this };
    ChildObject    annotation           { *this };
    ChildObject    comment              { *this };
    BoolProperty   isSystem             { *this };
    ListProperty   specialisationParams { *this },
                   aliases              { *this },
                   functions            { *this },
                   structures           { *this },
                   enums                { *this },
                   staticAssertions     { *this };
};

//==============================================================================
struct ProcessorBase  : public ModuleBase
{
    ProcessorBase (const ObjectContext& c) : ModuleBase (c) {}

    ProcessorBase* getAsProcessorBase() override              { return this; }
    const ProcessorBase* getAsProcessorBase() const override  { return this; }
    bool isProcessorBase() const override                     { return true; }

    ptr<Function> findMainFunction() const                    { return findFunction ([] (const Function& f) { return f.isMainFunction(); }); }
    ptr<Function> findUserInitFunction() const                { return findFunction ([] (const Function& f) { return f.isUserInitFunction(); }); }
    ptr<Function> findSystemInitFunction() const              { return findFunction ([] (const Function& f) { return f.isSystemInitFunction(); }); }
    ptr<Function> findSystemAdvanceFunction() const           { return findFunction ([] (const Function& f) { return f.isSystemAdvanceFunction(); }); }
    ptr<Function> findResetFunction() const                   { return findFunction ([] (const Function& f) { return f.isResetFunction(); }); }

    ptr<VariableDeclaration> findVariable (PooledString variableName) override
    {
        if (auto o = stateVariables.findObjectWithName (variableName))
            return ptr<VariableDeclaration> (o->getAsVariableDeclaration());

        return {};
    }

    ptr<ModuleBase> findChildModule (PooledString moduleName) override
    {
        if (auto o = nodes.findObjectWithName (moduleName))
            return ptr<ModuleBase> (o->getAsModuleBase());

        return ProcessorBase::findChildModule (moduleName);
    }

    ptr<GraphNode> findNode (PooledString nodeName)
    {
        if (auto o = nodes.findObjectWithName (nodeName))
            return ptr<GraphNode> (o->getAsGraphNode());

        return {};
    }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement> statementToSearchUpTo) override
    {
        ModuleBase::performLocalNameSearch (search, statementToSearchUpTo);

        if (search.findNodes)
            if (auto i = findNode (search.nameToFind))
                search.addResult (*i);

        if (search.findVariables)
            search.addFirstMatching (stateVariables);

        if (search.findEndpoints)
            search.addFirstMatching (endpoints);
    }

    template <typename Predicate>
    ObjectRefVector<const EndpointDeclaration> findEndpoints (Predicate&& pred) const
    {
        ObjectRefVector<const EndpointDeclaration> result;

        for (auto& e : endpoints)
        {
            auto& endpoint = castToRef<EndpointDeclaration> (e);

            if (pred (endpoint))
                result.push_back (endpoint);
        }

        return result;
    }

    ptr<EndpointDeclaration> findEndpointWithName (PooledString endpointName) const
    {
        for (auto& e : endpoints)
        {
            auto& endpoint = castToRef<EndpointDeclaration> (e);

            if (endpoint.hasName (endpointName))
                return endpoint;
        }

        return {};
    }

    auto getAllEndpoints() const                         { return findEndpoints ([] (const EndpointDeclaration&) { return true; }); }
    auto getOutputEndpoints (bool includeConsole) const  { return findEndpoints ([includeConsole] (const EndpointDeclaration& e) { return (includeConsole || e.name != getConsoleEndpointID()) && e.isOutput(); }); }
    auto getInputEndpoints  (bool includeConsole) const  { return findEndpoints ([includeConsole] (const EndpointDeclaration& e) { return (includeConsole || e.name != getConsoleEndpointID()) && e.isInput.get(); }); }
    auto getEventEndpoints  (bool includeConsole) const  { return findEndpoints ([includeConsole] (const EndpointDeclaration& e) { return (includeConsole || e.name != getConsoleEndpointID()) && e.isEvent(); }); }

    size_t getNumEventEndpoints (bool includeConsole) const         { return getEventEndpoints (includeConsole).size(); }

    bool hasInputValueEventEndpoints() const
    {
        for (auto endpoint : getInputEndpoints (true))
            if (endpoint->isValue())
                return true;

        return false;
    }

    bool hasOutputEventEndpoints() const
    {
        for (auto endpoint : getEventEndpoints (true))
            if (endpoint->isOutput())
                return true;

        return false;
    }

    virtual double getLatency() const = 0;

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        ModuleBase::visitObjectsInScope (visit);
        visitObjectIfPossible (nodes, visit);
        visitObjectIfPossible (stateVariables, visit);
        visitObjectIfPossible (endpoints, visit);
    }

    ListProperty nodes          { *this },
                 stateVariables { *this },
                 endpoints      { *this };
};

//==============================================================================
struct Graph  : public ProcessorBase
{
    Graph (const ObjectContext& c) : ProcessorBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Graph, 40)
    std::string_view getModuleType() const override     { return "graph"; }
    static constexpr bool isGraphClass()                { return true; }
    static constexpr AliasTypeEnum::Enum getAliasType() { return AliasTypeEnum::Enum::processorAlias; }

    double getLatency() const override;

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        ProcessorBase::visitObjectsInScope (visit);
        visitObjectIfPossible (connections, visit);
    }

    void visitConnections (const std::function<void(const AST::Connection&)>& visit) const
    {
        visitConnectionList (connections, visit);
    }

    void visitConnections (const std::function<void(AST::Connection&)>& visit)
    {
        visitConnectionList (connections, visit);
    }

    template <class T>
    static void visitConnectionList (const ListProperty& connections, T& visit)
    {
        for (auto& c : connections)
        {
            if (auto connection = AST::castTo<AST::Connection> (c))
            {
                visit (*connection);
            }
            else if (auto connectionList = AST::castTo<AST::ConnectionList> (c))
            {
                visitConnectionList (connectionList->connections, visit);
            }
            else if (auto connectionIf = AST::castTo<AST::ConnectionIf> (c))
            {
                if (auto v = AST::castTo<AST::ConnectionList> (connectionIf->trueConnections))
                    visitConnectionList (v->connections, visit);

                if (auto v = AST::castTo<AST::ConnectionList> (connectionIf->falseConnections))
                    visitConnectionList (v->connections, visit);
            }
        }
    }

    void removeConnections (const std::unordered_set<const AST::Connection*>& connectionsToRemove)
    {
        visitConnectionPropertyLists (connections, [&] (ListProperty& connectionList)
        {
            connectionList.removeIf ([&] (const AST::Property& conn)
            {
                if (auto connection = AST::castTo<AST::Connection> (conn))
                    return connectionsToRemove.find (connection.get()) != connectionsToRemove.end();

                return false;
            });
        });
    }

    static void visitConnectionPropertyLists (ListProperty& connections, std::function<void(ListProperty&)> visit)
    {
        visit (connections);

        for (auto& c : connections)
        {
            if (auto connectionList = AST::castTo<AST::ConnectionList> (c))
            {
                visitConnectionPropertyLists (connectionList->connections, visit);
            }
            else if (auto connectionIf = AST::castTo<AST::ConnectionIf> (c))
            {
                if (auto v = AST::castTo<AST::ConnectionList> (connectionIf->trueConnections))
                    visitConnectionPropertyLists (v->connections, visit);

                if (auto v = AST::castTo<AST::ConnectionList> (connectionIf->falseConnections))
                    visitConnectionPropertyLists (v->connections, visit);
            }
        }
    }


    ListProperty connections { *this };

    #define CMAJ_PROPERTIES(X) \
        X (1,  StringProperty, name) \
        X (2,  StringProperty, originalName) \
        X (3,  ChildObject,    annotation) \
        X (4,  BoolProperty,   isSystem) \
        X (5,  ListProperty,   specialisationParams) \
        X (6,  ListProperty,   functions) \
        X (7,  ListProperty,   structures) \
        X (8,  ListProperty,   enums) \
        X (9,  ListProperty,   aliases) \
        X (10, ListProperty,   staticAssertions) \
        X (11, ChildObject,    comment) \
        X (20, ListProperty,   stateVariables) \
        X (21, ListProperty,   endpoints) \
        X (40, ListProperty,   nodes) \
        X (41, ListProperty,   connections) \

    CMAJ_DECLARE_PROPERTY_ACCESSORS(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Processor  : public ProcessorBase
{
    Processor (const ObjectContext& c) : ProcessorBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Processor, 41)
    std::string_view getModuleType() const override     { return "processor"; }
    static constexpr bool isGraphClass()                { return false; }
    static constexpr AliasTypeEnum::Enum getAliasType() { return AliasTypeEnum::Enum::processorAlias; }

    double getLatency() const override
    {
        if (auto latencyValue = getAsFoldedConstant (latency))
            if (auto numFrames = latencyValue->getAsFloat64())
                if (*numFrames >= 0 && *numFrames <= AST::maxInternalLatency)
                    return *numFrames;

        return 0;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        ProcessorBase::visitObjectsInScope (visit);
        visitObjectIfPossible (latency, visit);
    }

    ChildObject latency  { *this };

    #define CMAJ_PROPERTIES(X) \
        X (1,  StringProperty,  name) \
        X (2,  StringProperty,  originalName) \
        X (3,  ChildObject,     annotation) \
        X (4,  BoolProperty,    isSystem) \
        X (5,  ListProperty,    specialisationParams) \
        X (6,  ListProperty,    functions) \
        X (7,  ListProperty,    structures) \
        X (8,  ListProperty,    enums) \
        X (9,  ListProperty,    aliases) \
        X (10, ListProperty,    staticAssertions) \
        X (11, ChildObject,     comment) \
        X (20, ListProperty,    stateVariables) \
        X (21, ListProperty,    endpoints) \
        X (40, ChildObject,     latency) \
        X (41, ListProperty,    nodes) \

    CMAJ_DECLARE_PROPERTY_ACCESSORS(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Namespace  : public ModuleBase
{
    Namespace (const ObjectContext& c) : ModuleBase (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Namespace, 42)
    std::string_view getModuleType() const override     { return "namespace"; }
    static constexpr bool isGraphClass()                { return false; }
    static constexpr AliasTypeEnum::Enum getAliasType() { return AliasTypeEnum::Enum::namespaceAlias; }

    const Namespace& getRootNamespace() const override      { return isRootNamespace() ? *this : ModuleBase::getRootNamespace(); }
    Namespace& getRootNamespace()  override                 { return isRootNamespace() ? *this : ModuleBase::getRootNamespace(); }

    auto getSubModules() const
    {
        return subModules.getAsObjectTypeList<ModuleBase>();
    }

    ptr<ModuleBase> findChildModule (PooledString moduleName) override
    {
        if (auto o = subModules.findObjectWithName (moduleName))
            return ptr<ModuleBase> (o->getAsModuleBase());

        return ModuleBase::findChildModule (moduleName);
    }

    ptr<Namespace> findSystemChildNamespace (PooledString moduleName)
    {
        for (auto& m : subModules.iterateAs<ModuleBase>())
            if (m.hasName (moduleName) && m.isSystemModule())
                return AST::castTo<AST::Namespace> (m);

        return {};
    }

    ptr<VariableDeclaration> findVariable (PooledString variableName) override
    {
        if (auto o = constants.findObjectWithName (variableName))
            return ptr<VariableDeclaration> (o->getAsVariableDeclaration());

        return {};
    }

    void performLocalNameSearch (NameSearch& search, ptr<const Statement> statementToSearchUpTo) override
    {
        ModuleBase::performLocalNameSearch (search, statementToSearchUpTo);

        if (search.findVariables)
            search.addFirstMatching (constants);

        if (search.findProcessors || search.findNamespaces)
            search.addFirstMatching (subModules);
    }

    template <typename Visitor>
    void visitAllModules (bool avoidGenericsAndParameterised, Visitor&& visitor)
    {
        if (! (avoidGenericsAndParameterised && isGenericOrParameterised()))
        {
            for (auto& sub : subModules.iterateAs<ModuleBase>())
            {
                if (! (avoidGenericsAndParameterised && sub.isGenericOrParameterised()))
                    visitor (sub);

                if (auto ns = sub.getAsNamespace())
                    ns->visitAllModules (avoidGenericsAndParameterised, visitor);
            }
        }
    }

    template <typename Visitor>
    void visitAllFunctions (bool avoidGenericsAndParameterised, Visitor&& visitor) const
    {
        if (! (avoidGenericsAndParameterised && isGenericOrParameterised()))
        {
            for (auto& f : functions)
            {
                auto& fn = castToFunctionRef (f);

                if (! (avoidGenericsAndParameterised && fn.isGenericOrParameterised()))
                    visitor (fn);
            }

            for (auto& sub : subModules.iterateAs<ModuleBase>())
            {
                if (auto p = sub.getAsProcessorBase())
                    if (! (avoidGenericsAndParameterised && p->isGenericOrParameterised()))
                        for (auto& fn : p->functions.iterateAs<AST::Function>())
                            if (! (avoidGenericsAndParameterised && fn.isGenericOrParameterised()))
                                visitor (fn);

                if (auto ns = sub.getAsNamespace())
                    ns->visitAllFunctions (avoidGenericsAndParameterised, visitor);
            }
        }
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        ModuleBase::visitObjectsInScope (visit);
        visitObjectIfPossible (subModules, visit);
        visitObjectIfPossible (constants, visit);
        visitObjectIfPossible (imports, visit);
    }

    void clear()
    {
        subModules.reset();
        constants.reset();
        imports.reset();
        annotation.reset();
        isSystem.reset();
        specialisationParams.reset();
        aliases.reset();
        functions.reset();
        structures.reset();
        enums.reset();
        staticAssertions.reset();
        intrinsicsNamespace = {};
    }

    ListProperty subModules { *this },
                 constants  { *this },
                 imports    { *this };

    #define CMAJ_PROPERTIES(X) \
        X(1,  StringProperty, name) \
        X(2,  StringProperty, originalName) \
        X(3,  ChildObject,    annotation) \
        X(4,  BoolProperty,   isSystem) \
        X(5,  ListProperty,   specialisationParams) \
        X(6,  ListProperty,   functions) \
        X(7,  ListProperty,   structures) \
        X(8,  ListProperty,   enums) \
        X(9,  ListProperty,   aliases) \
        X(10, ListProperty,   staticAssertions) \
        X(11, ChildObject,    comment) \
        X(21, ListProperty,   subModules) \
        X(22, ListProperty,   constants) \
        X(23, ListProperty,   imports) \

    CMAJ_DECLARE_PROPERTY_ACCESSORS(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES

    // the root namespace caches this pointer to the intrinsics namespace
    ptr<Namespace> intrinsicsNamespace;
};

//==============================================================================
struct EndpointDeclaration  : public Object
{
    EndpointDeclaration (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(EndpointDeclaration, 43)
    bool hasName (PooledString nametoMatch) const override        { return name.hasName (nametoMatch); }
    PooledString getName() const override                         { return name; }
    void setName (PooledString newName) override                  { name.set (newName); }
    ptr<const Comment> getComment() const override                { return castTo<const Comment> (comment); }

    EndpointID getEndpointID() const    { return EndpointID::create (getName()); }
    bool isOutput() const               { return ! isInput; }
    bool isEvent() const                { return endpointType == EndpointTypeEnum::Enum::event; }
    bool isStream() const               { return endpointType == EndpointTypeEnum::Enum::stream; }
    bool isValue() const                { return endpointType == EndpointTypeEnum::Enum::value; }
    bool isHoistedEndpoint() const      { return childPath != nullptr; }
    bool isArray() const                { return arraySize != nullptr; }

    cmaj::EndpointType getEndpointType() const
    {
        switch (endpointType)
        {
            case EndpointTypeEnum::Enum::stream:       return EndpointType::stream;
            case EndpointTypeEnum::Enum::value:        return EndpointType::value;
            case EndpointTypeEnum::Enum::event:        return EndpointType::event;
            default:                                   CMAJ_ASSERT_FALSE;
        }
    }

    ObjectRefVector<const TypeBase> getDataTypes (bool includeArraySize = false) const
    {
        auto types = dataTypes.findAllObjectsOfType<const TypeBase>();

        if (! includeArraySize || ! isArray())
            return types;

        for (auto& type : types)
            type = createArrayOfType (context, type, arraySize);

        return types;
    }

    std::optional<size_t> getDataTypeIndex (const AST::TypeBase& valueDataType) const
    {
        auto types = dataTypes.findAllObjectsOfType<const TypeBase>();

        int index = 0;

        for (auto t : types)
        {
            if (AST::TypeRules::areTypesIdentical (t, valueDataType))
                return index;

            index++;
        }

        return {};
    }

    ptr<const TypeBase> getEndpointTypeForValue (bool includeArraySize, const AST::ValueBase& value)
    {
        auto valueType = value.getResultType();
        auto typesToConsider = getDataTypes (includeArraySize);

        if (typesToConsider.size() == 1)
            return typesToConsider.front().get();

        AST::ObjectRefVector<const AST::TypeBase> possibleTypes;

        for (auto& type : typesToConsider)
        {
            if (valueType->skipConstAndRefModifiers().isSameType (type, AST::TypeBase::ComparisonFlags::ignoreVectorSize1))
                return type.get();

            if (AST::TypeRules::canSilentlyCastTo (type, value))
                possibleTypes.push_back (type);
        }

        if (possibleTypes.size() == 1)
            return possibleTypes.front().get();

        return {};
    }

    const TypeBase& getSingleDataType() const    { CMAJ_ASSERT (dataTypes.size() == 1); return AST::castToTypeBaseRef (dataTypes.front()); }

    std::optional<int> getArraySize() const
    {
        if (auto value = arraySize.getAsObjectType<ValueBase>())
            if (value->isCompileTimeConstant())
                if (auto size = value->constantFold())
                    return size->getAsInt32();

        return {};
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (annotation, visit);
        visitObjectIfPossible (dataTypes, visit);
        visitObjectIfPossible (arraySize, visit);
        visitObjectIfPossible (childPath, visit);
    }

    StringProperty      name { *this };
    ChildObject         annotation { *this };
    ChildObject         comment { *this };
    BoolProperty        isInput { *this };
    EndpointTypeEnum    endpointType { *this };
    ListProperty        dataTypes { *this };
    ChildObject         arraySize { *this };
    ChildObject         childPath { *this };
    StringProperty      nameTemplate { *this };

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty, name) \
        X (2, ChildObject, annotation) \
        X (3, BoolProperty, isInput) \
        X (4, EndpointTypeEnum, endpointType) \
        X (5, ListProperty, dataTypes) \
        X (6, ChildObject, arraySize) \
        X (7, ChildObject, childPath) \
        X (8, ChildObject, comment) \
        X (9, StringProperty, nameTemplate) \

    CMAJ_DECLARE_PROPERTY_ACCESSORS(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct EndpointInstance  : public Expression
{
    EndpointInstance (const ObjectContext& c) : Expression (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(EndpointInstance, 44)

    const GraphNode& getNode() const
    {
        if (auto getElement = castToSkippingReferences<GetElement> (node))
            return castToRefSkippingReferences<GraphNode> (getElement->parent);

        return castToRefSkippingReferences<GraphNode> (node);
    }

    bool hasNode (GraphNode& n) const
    {
        if (isParentEndpoint())
            return false;

        return std::addressof (getNode()) == std::addressof (n);
    }

    ptr<Object> getNodeIndex() const
    {
        if (auto getElement = castToSkippingReferences<GetElement> (node))
            if (! getElement->indexes.empty())
                return getElement->getSingleIndex();

        return {};
    }

    std::optional<int> getNodeArraySize() const
    {
        if (auto getElement = castToSkippingReferences<GetElement> (node))
            return {};

        if (auto graphNode = castToSkippingReferences<GraphNode> (node))
            return graphNode->getArraySize();

        return {};
    }

    const ProcessorBase& getProcessor() const           { return *getNode().getProcessorType(); }
    EndpointDeclaration& getResolvedEndpoint() const    { return castToRefSkippingReferences<EndpointDeclaration> (endpoint); }

    ptr<const EndpointDeclaration> getEndpoint (bool isActingAsSource) const
    {
        if (auto e = castToSkippingReferences<EndpointDeclaration> (endpoint))
            return e;

        if (isActingAsSource)
        {
            auto sources = getProcessor().getOutputEndpoints (false);

            if (sources.size() == 1)
                return sources.front().get();
        }
        else
        {
            auto dests = getProcessor().getInputEndpoints (false);

            if (dests.size() == 1)
                return dests.front().get();
        }

        return {};
    }

    void addSideEffects (SideEffects&) const override   {}
    bool isChained() const                              { return endpoint == nullptr; }
    bool isParentEndpoint() const                       { return node == nullptr; }

    bool isSource() const
    {
        if (isChained())
            return true;

        bool endpointIsInput = castToRefSkippingReferences<EndpointDeclaration> (endpoint).isInput;
        return isParentEndpoint() ? endpointIsInput : ! endpointIsInput;
    }

    bool isDestination() const
    {
        if (isChained())
            return true;

        bool endpointIsOutput = ! castToRefSkippingReferences<EndpointDeclaration> (endpoint).isInput;
        return isParentEndpoint() ? endpointIsOutput : ! endpointIsOutput;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (node, visit);
        visitObjectIfPossible (endpoint, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, node) \
        X (2, ChildObject, endpoint) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct Connection  : public Object
{
    Connection (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(Connection, 45)

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (sources, visit);
        visitObjectIfPossible (dests, visit);
        visitObjectIfPossible (delayLength, visit);
    }

    /// NB: sources can be either some EndpointInstances or a connection (when chained)
    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, sources) \
        X (2, ListProperty, dests) \
        X (3, InterpolationTypeEnum, interpolation) \
        X (4, ChildObject, delayLength) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConnectionIf  : public Object
{
    ConnectionIf (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConnectionIf, 72)

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (condition, visit);
        visitObjectIfPossible (trueConnections, visit);
        visitObjectIfPossible (falseConnections, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ChildObject, condition) \
        X (2, ChildObject, trueConnections) \
        X (3, ChildObject, falseConnections) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct ConnectionList : public Object
{
    ConnectionList (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(ConnectionList, 73)

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (connections, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, connections) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct GraphNode  : public Object
{
    GraphNode (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(GraphNode, 46)
    bool hasName (PooledString nametoMatch) const override        { return nodeName.hasName (nametoMatch); }
    PooledString getName() const override                         { return nodeName; }
    void setName (PooledString newName) override                  { nodeName.set (newName); }

    ptr<ProcessorBase> getProcessorType() const                   { return castToSkippingReferences<ProcessorBase> (processorType); }
    Graph& getParentGraph() const                                 { return castToRef<Graph> (getParentProcessor()); }

    static std::string getNameForImplicitNode (AST::ProcessorBase& processorType)
    {
        return "_implicit_" +  makeSafeIdentifierName (processorType.getFullyQualifiedNameWithoutRoot());
    }

    PooledString getOriginalName() const override
    {
        if (! originalName.hasDefaultValue())
            return originalName;

        return nodeName;
    }

    bool isImplicitlyCreated() const        { return ! originalName.hasDefaultValue(); }
    bool isArray() const                    { return arraySize != nullptr; }

    std::optional<int> getArraySize() const
    {
        if (auto size = getAsFoldedConstant (arraySize))
            return size->getAsInt32();

        return {};
    }

    double getClockMultiplier() const
    {
        if (auto v = getAsFoldedConstant (clockMultiplierRatio))
            if (auto f = v->getAsFloat64())
                return *f;

        if (auto v = getAsFoldedConstant (clockDividerRatio))
            if (auto f = v->getAsFloat64())
                return 1.0 / *f;

        return 1.0;
    }

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (processorType, visit);
        visitObjectIfPossible (clockMultiplierRatio, visit);
        visitObjectIfPossible (clockDividerRatio, visit);
        visitObjectIfPossible (arraySize, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, StringProperty,  nodeName) \
        X (2, StringProperty,  originalName) \
        X (3, ChildObject,     processorType) \
        X (4, ChildObject,     clockMultiplierRatio) \
        X (5, ChildObject,     clockDividerRatio) \
        X (6, ChildObject,     arraySize) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};

//==============================================================================
struct HoistedEndpointPath  : public Object
{
    HoistedEndpointPath (const ObjectContext& c) : Object (c) {}

    CMAJ_AST_DECLARE_STANDARD_METHODS(HoistedEndpointPath, 47)

    void visitObjectsInScope (ObjectVisitor visit) override
    {
        visit (*this);
        visitObjectIfPossible (pathSections, visit);
    }

    #define CMAJ_PROPERTIES(X) \
        X (1, ListProperty, pathSections) \
        X (2, StringProperty, wildcardPattern) \

    CMAJ_DECLARE_PROPERTIES(CMAJ_PROPERTIES)
    #undef CMAJ_PROPERTIES
};
