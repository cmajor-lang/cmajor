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


template <typename ContextType, typename VariableType>
static VariableReference& createVariableReference (const ContextType& context, const VariableType& targetVariable)
{
    auto& c = getContext (context);
    auto& ref = c.allocator.template allocate<VariableReference> (c);
    ref.variable.referTo (castToRef<Object> (targetVariable));
    return ref;
}

template <typename ContextType, typename ObjectType>
static Expression& createReference (const ContextType& context, const ObjectType& target)
{
    auto& c = getContext (context);
    auto& t = castToRef<Object> (target);

    CMAJ_ASSERT (! t.isSyntacticObject());

    if (auto v = castTo<VariableDeclaration> (target))
    {
        auto& ref = c.allocator.template allocate<VariableReference> (c);
        ref.variable.referTo (*v);
        return ref;
    }

    if (t.isVariableReference() || t.isNamedReference())
        return const_cast<Expression&> (castToRef<Expression> (t));

    auto& ref = c.allocator.template allocate<NamedReference> (c);
    ref.target.referTo (t);
    return ref;
}

template <typename ContextType, typename VariableType>
static GetElement& createGetElement (const ContextType& context, const VariableType& parent, Object& index,
                                     bool useIndexReference = false, bool isAtFunction = false)
{
    auto& c = getContext (context);
    auto& ref = c.allocator.template allocate<GetElement> (c);
    ref.parent.referTo (castToRef<Object> (parent));

    if (useIndexReference)
        ref.indexes.addReference (index);
    else
        ref.indexes.addChildObject (index);

    if (isAtFunction)
        ref.isAtFunction = true;

    return ref;
}

template <typename ContextType, typename VariableType>
static GetElement& createGetElement (const ContextType& context, const VariableType& parent, int32_t index)
{
    return createGetElement (context, parent, getContext (context).allocator.createConstantInt32 (index));
}

static TypeBase& createBoundedType (Object& parent, Object& limit, bool isClamp = false)
{
    auto& boundedType = parent.allocateChild<BoundedType>();
    boundedType.isClamp = isClamp;
    boundedType.limit.createReferenceTo (limit);
    return boundedType;
}

template <typename TypeType, typename SizeType>
static TypeBase& createArrayOfType (Object& parent, const TypeType& type, const SizeType& arraySize)
{
    auto& arrayType = parent.allocateChild<ArrayType>();
    arrayType.setInnermostElementType (type);
    arrayType.setArraySize (arraySize);
    return arrayType;
}

template <typename TypeType, typename SizeType>
static TypeBase& createArrayOfType (const ObjectContext& context, const TypeType& type, const SizeType& arraySize)
{
    auto& arrayType = context.allocate<ArrayType>();
    arrayType.setInnermostElementType (type);
    arrayType.setArraySize (arraySize);
    return arrayType;
}

static TypeBase& createSliceOfType (const ObjectContext& context, const TypeBase& type)
{
    auto& arrayType = context.allocate<ArrayType>();
    arrayType.setInnermostElementType (type);
    return arrayType;
}

template <typename ContextType, typename ObjectType>
static GetStructMember& createGetStructMember (const ContextType& context, const ObjectType& object, std::string_view memberName)
{
    auto& c = getContext (context);
    auto& m = c.allocator.template allocate<GetStructMember> (c);
    m.object.referTo (object);
    m.member = m.getStringPool().get (memberName);
    return m;
}

static inline Function& createFunctionInModule (ModuleBase& ns, const TypeBase& returnType, PooledString name)
{
    auto& f = ns.allocateChild<Function>();
    f.name = name;
    f.returnType.referTo (returnType);
    f.mainBlock.setChildObject (f.allocateChild<ScopeBlock>());

    ns.functions.addReference (f);
    return f;
}

static inline Function& createFunctionInModule (ModuleBase& ns, const TypeBase& returnType, std::string_view name)
{
    return createFunctionInModule (ns, returnType, ns.getStringPool().get (name));
}

static inline Function& createExportedFunction (ModuleBase& ns, const TypeBase& returnType, PooledString name)
{
    auto& f = createFunctionInModule (ns, returnType, name);
    f.isExported = true;
    return f;
}

static inline Processor& createProcessorInNamespace (Namespace& ns, PooledString name)
{
    auto& p = ns.allocateChild<Processor>();
    p.name = name;
    ns.subModules.addReference (p);
    return p;
}

static inline EndpointDeclaration& createEndpointDeclaration (ProcessorBase& processor, PooledString name, bool isInput,
                                                              EndpointTypeEnum::Enum endpointType, ObjectRefVector<const TypeBase> types)
{
    auto& endpoint = processor.allocateChild<EndpointDeclaration>();
    endpoint.name = name;
    endpoint.isInput = isInput;
    endpoint.endpointType.set (endpointType);

    for (auto& type : types)
        endpoint.dataTypes.addReference (type);

    processor.endpoints.addReference (endpoint);
    return endpoint;
}

static inline EndpointInstance& createEndpointInstance (ScopeBlock& scope, EndpointDeclaration& endpoint)
{
    auto& endpointInstance = scope.allocateChild<EndpointInstance>();
    endpointInstance.endpoint.createReferenceTo (endpoint);

    return endpointInstance;
}

static inline void addConnection (AST::Graph& graph,
                                  ptr<AST::GraphNode> sourceNode, AST::EndpointDeclaration& sourceEndpoint,
                                  ptr<AST::GraphNode> destNode, AST::EndpointDeclaration& destEndpoint)
{
    auto& context = destEndpoint.context;

    auto& srcEndpointInstance = context.allocate<AST::EndpointInstance>();
    auto& dstEndpointInstance = context.allocate<AST::EndpointInstance>();

    srcEndpointInstance.endpoint.createReferenceTo (sourceEndpoint);
    dstEndpointInstance.endpoint.createReferenceTo (destEndpoint);

    if (sourceNode != nullptr) srcEndpointInstance.node.createReferenceTo (*sourceNode);
    if (destNode   != nullptr) dstEndpointInstance.node.createReferenceTo (*destNode);

    auto& connection = context.allocate<AST::Connection>();
    connection.sources.addReference (srcEndpointInstance);
    connection.dests.addReference (dstEndpointInstance);

    graph.connections.addReference (connection);
}

struct VariableRefGenerator
{
    ptr<const ObjectContext> context;
    ptr<const VariableDeclaration> variable;

    operator VariableReference&() const { return createVariableReference (context, variable); }
};

static inline VariableRefGenerator addFunctionParameter (Function& f, const TypeBase& type, PooledString name,
                                                         bool asRef = false, bool asConst = false, int index = -1)
{
    auto& param = f.allocateChild<VariableDeclaration>();
    param.variableType = VariableTypeEnum::Enum::parameter;
    param.name = name;

    if (asRef || asConst)
    {
        auto& constOrRef = f.allocateChild<MakeConstOrRef>();
        constOrRef.source.createReferenceTo (type);
        constOrRef.makeRef = asRef;
        constOrRef.makeConst = asConst;

        param.declaredType.setChildObject (constOrRef);
    }
    else
    {
        param.declaredType.createReferenceTo (type);
    }

    f.parameters.addReference (param, index);
    return { getContext (f), param };
}

static inline VariableRefGenerator addFunctionParameter (Function& f, const TypeBase& type, std::string_view name,
                                                         bool asRef = false, bool asConst = false, int index = -1)
{
    return addFunctionParameter (f, type, f.getStringPool().get (name), asRef, asConst, index);
}

static inline Assignment& createAssignment (const ObjectContext& context, Expression& target, Expression& source)
{
    auto& a = context.allocate<Assignment>();
    a.target.setChildObject (target);
    a.source.setChildObject (source);
    return a;
}

static inline void addAssignment (ScopeBlock& scope, Expression& target, Expression& source, int insertIndex = -1)
{
    auto& a = scope.allocateChild<Assignment>();
    a.target.setChildObject (target);
    a.source.setChildObject (source);
    scope.addStatement (a, insertIndex);
}

template <typename ContextType>
static UnaryOperator& createUnaryOp (const ContextType& context, UnaryOpTypeEnum::Enum op, Expression& input)
{
    auto& u = getContext (context).template allocate<UnaryOperator>();
    u.op = op;
    u.input.setChildObject (input);
    return u;
}

static inline UnaryOperator& createLogicalNot (const ObjectContext& context, Expression& input)
{
    return createUnaryOp (context, UnaryOpTypeEnum::Enum::logicalNot, input);
}

template <typename ContextType>
static BinaryOperator& createBinaryOp (const ContextType& context, BinaryOpTypeEnum::Enum op,
                                       Expression& lhs, Expression& rhs)
{
    auto& b = getContext (context).template allocate<BinaryOperator>();
    b.op = op;
    b.lhs.setChildObject (lhs);
    b.rhs.setChildObject (rhs);
    return b;
}

template <typename ContextType>
static BinaryOperator& createAdd (const ContextType& context, Expression& lhs, Expression& rhs)
{
    return createBinaryOp (context, BinaryOpTypeEnum::Enum::add, lhs, rhs);
}

template <typename ContextType>
static BinaryOperator& createSubtract (const ContextType& context, Expression& lhs, Expression& rhs)
{
    return createBinaryOp (context, BinaryOpTypeEnum::Enum::subtract, lhs, rhs);
}

template <typename ContextType>
static BinaryOperator& createMultiply (const ContextType& context, Expression& lhs, Expression& rhs)
{
    return createBinaryOp (context, BinaryOpTypeEnum::Enum::multiply, lhs, rhs);
}

template <typename ContextType>
static BinaryOperator& createDivide (const ContextType& context, Expression& lhs, Expression& rhs)
{
    return createBinaryOp (context, BinaryOpTypeEnum::Enum::divide, lhs, rhs);
}

template <typename ContextType, typename... Parameters>
static FunctionCall& createFunctionCall (const ContextType& context, Function& fn, Parameters&... params)
{
    auto& call = getContext (context).template allocate<FunctionCall>();
    call.targetFunction.referTo (fn);

    if constexpr (sizeof...(params) > 0)
    {
        const ptr<Object> paramArray[] { static_cast<Object&> (params)... };

        for (size_t i = 0; i < sizeof...(params); ++i)
            call.arguments.addChildObject (*paramArray[i]);
    }

    return call;
}

static inline VariableDeclaration& createLocalVariable (ScopeBlock& scope, std::string_view name,
                                                        ptr<const TypeBase> declaredType,
                                                        ptr<ValueBase> initialValue, int insertIndex = -1)
{
    auto& v = scope.allocateChild<VariableDeclaration>();
    v.variableType = VariableTypeEnum::Enum::local;
    v.name = v.getStringPool().get (name);

    if (initialValue != nullptr)
        v.initialValue.setChildObject (*initialValue);

    if (declaredType != nullptr)
        v.declaredType.createReferenceTo (*declaredType);

    scope.addStatement (v, insertIndex);
    return v;
}

static inline VariableDeclaration& createLocalVariable (ScopeBlock& scope, std::string_view name,
                                                        ValueBase& initialValue, int insertIndex = -1)
{
    auto& v = scope.allocateChild<VariableDeclaration>();
    v.variableType = VariableTypeEnum::Enum::local;
    v.name = v.getStringPool().get (name);
    v.initialValue.setChildObject (initialValue);
    scope.addStatement (v, insertIndex);
    return v;
}

static inline VariableReference& createLocalVariableRef (ScopeBlock& scope, std::string_view name,
                                                         ptr<const TypeBase> declaredType,
                                                         ptr<ValueBase> initialValue, int insertIndex = -1)
{
    return createVariableReference (scope.context, createLocalVariable (scope, name, declaredType, initialValue, insertIndex));
}

static inline VariableReference& createLocalVariableRef (ScopeBlock& scope, std::string_view name,
                                                         ValueBase& initialValue, int insertIndex = -1)
{
    return createVariableReference (scope.context, createLocalVariable (scope, name, initialValue, insertIndex));
}

static inline VariableDeclaration& createStateVariable (ProcessorBase& parent, std::string_view name,
                                                        ptr<const TypeBase> declaredType, ptr<ValueBase> initialValue)
{
    auto& v = parent.allocateChild<VariableDeclaration>();
    v.variableType = VariableTypeEnum::Enum::state;
    v.name = v.getStringPool().get (name);

    if (initialValue != nullptr)
        v.initialValue.referTo (*initialValue);

    if (declaredType != nullptr)
        v.declaredType.referTo (*declaredType);

    parent.stateVariables.addReference (v);
    return v;
}

static inline VariableDeclaration& getOrCreateStateVariable (ProcessorBase& parent, std::string_view name,
                                                             ptr<const TypeBase> declaredType, ptr<ValueBase> initialValue)
{
    auto pooledName = parent.getStringPool().get (name);

    if (auto v = parent.stateVariables.findObjectWithName (pooledName))
        return castToRef<VariableDeclaration> (*v);

    return createStateVariable (parent, pooledName, declaredType, initialValue);
}

static inline VariableDeclaration& createProcessorSpecialisationVariable (ProcessorBase& processor, std::string_view name,
                                                                          ref<TypeBase> declaredType, ptr<ValueBase> defaultValue)
{
    auto& param = processor.allocateChild<VariableDeclaration>();
    param.variableType = VariableTypeEnum::Enum::state;
    param.name = param.getStringPool().get (name);
    param.declaredType.referTo (declaredType);

    if (defaultValue)
        param.initialValue.referTo (*defaultValue);

    processor.specialisationParams.addReference (param);
    processor.stateVariables.addReference (param);

    return param;
}

static inline StructType& createStruct (ModuleBase& module, PooledString name)
{
    auto& s = module.allocateChild<StructType>();
    s.name = name;
    module.structures.addReference (s);
    return s;
}

static inline StructType& createStruct (ModuleBase& module, std::string_view name)
{
    return createStruct (module, module.getStringPool().get (name));
}

static inline void ensureStatementIsBlock (ChildObject& statement)
{
    if (statement != nullptr && castTo<ScopeBlock> (statement) == nullptr)
    {
        auto& s = castToRef<Statement> (statement);
        auto& block = s.context.allocate<ScopeBlock>();
        block.addStatement (s);
        statement.setChildObject (block);
    }
}

static inline Statement& createWriteToEndpoint (const ObjectContext& context, EndpointInstance& dest,
                                                ptr<ValueBase> destIndex, ptr<ValueBase> value)
{
    auto& streamWrite = context.allocate<WriteToEndpoint>();
    streamWrite.target.setChildObject (castToRef<NamedReference> (dest.endpoint));

    if (value != nullptr)
        streamWrite.value.referTo (value);

    if (destIndex != nullptr)
        streamWrite.targetIndex.referTo (destIndex);

    return streamWrite;
}

static inline IfStatement& createIfStatement (const ObjectContext& context,
                                              Expression& condition,
                                              Statement& trueBranch)
{
    auto& i = context.allocate<IfStatement>();
    i.condition.setChildObject (condition);
    i.trueBranch.setChildObject (trueBranch);
    ensureStatementIsBlock (i.trueBranch);
    return i;
}

static inline IfStatement& createIfStatement (const ObjectContext& context, Expression& condition,
                                              Statement& trueBranch, Statement& falseBranch)
{
    auto& i = context.allocate<IfStatement>();
    i.condition.setChildObject (condition);
    i.trueBranch.setChildObject (trueBranch);
    i.falseBranch.setChildObject (falseBranch);
    ensureStatementIsBlock (i.trueBranch);
    ensureStatementIsBlock (i.falseBranch);
    return i;
}

static inline TernaryOperator& createTernary (const ObjectContext& context, ValueBase& condition,
                                              ValueBase& trueValue, ValueBase& falseValue)
{
    auto& ternary = context.allocate<TernaryOperator>();
    ternary.condition.referTo (condition);
    ternary.trueValue.referTo (trueValue);
    ternary.falseValue.referTo (falseValue);
    return ternary;
}

static inline PreOrPostIncOrDec& createPreIncOrDec (const ObjectContext& context, ValueBase& v, bool isIncrement)
{
    auto& p = context.allocate<PreOrPostIncOrDec>();
    p.isPost = false;
    p.isIncrement = isIncrement;
    p.target.createReferenceTo (v);
    return p;
}

static inline PreOrPostIncOrDec& createPreInc (const ObjectContext& context, ValueBase& v)
{
    return createPreIncOrDec (context, v, true);
}

static inline PreOrPostIncOrDec& createPreDec (const ObjectContext& context, ValueBase& v)
{
    return createPreIncOrDec (context, v, false);
}

static inline void addReturnStatement (ScopeBlock& scope, ValueBase& value, int insertIndex = -1)
{
    auto& ret = scope.allocateChild<ReturnStatement>();
    ret.value.setChildObject (value);
    scope.addStatement (ret, insertIndex);
}

static inline void addReturnStatement (ScopeBlock& scope, int insertIndex = -1)
{
    scope.addStatement (scope.allocateChild<ReturnStatement>(), insertIndex);
}

static inline ValueBase& createCast (const TypeBase& destType, ValueBase& value)
{
    auto& cast = value.allocateChild<Cast>();
    cast.targetType.createReferenceTo (destType);
    cast.arguments.addReference (value);
    return cast;
}

static inline ValueBase& createCastIfNeeded (const TypeBase& destType, ValueBase& value)
{
    if (destType.isReference() || destType.isSameType (*value.getResultType(), TypeBase::ComparisonFlags::failOnAllDifferences))
        return value;

    return createCast (destType, value);
}

/// Creates a new object from an AST object ID number
static inline ptr<AST::Object> createObjectOfClassType (AST::ObjectContext& context, uint8_t classID)
{
    switch (classID)
    {
        #define CMAJ_CREATE_OBJECT_OF_TYPE(name)   case name::classID:  return context.allocate<name>();
        CMAJ_AST_CLASSES(CMAJ_CREATE_OBJECT_OF_TYPE)
        #undef CMAJ_CREATE_OBJECT_OF_TYPE

        default: return {};
    }
}

static inline AST::NamespaceSeparator& createNamespaceSeparator (const ObjectContext& context, AST::Expression& lhs, AST::Expression& rhs)
{
    auto& separator = context.allocate<AST::NamespaceSeparator>();
    separator.lhs.referTo (lhs);
    separator.rhs.referTo (rhs);
    return separator;
}

static AST::Expression& createIdentifierPath (const ObjectContext& context, const std::vector<std::string_view>& path)
{
    ptr<AST::Expression> result;

    for (auto name : path)
    {
        auto& identifier = context.allocate<AST::Identifier>();
        identifier.name = context.allocator.strings.stringPool.get (name);

        if (result != nullptr)
            result = createNamespaceSeparator (context, *result, identifier);
        else
            result = identifier;
    }

    return *result;
}

//==============================================================================
/// Inserts a statement into the parent ScopeBlock of the referenceStatement.
struct ParentBlockInsertionPoint
{
    ParentBlockInsertionPoint (Statement& referenceStatement)
        : parentBlock (getOrCreateParentBlock (referenceStatement)),
          insertionIndex (parentBlock.findIndexOfStatementContaining (referenceStatement))
    {
        CMAJ_ASSERT (insertionIndex >= 0);
    }

    void insert (Statement& newStatement)
    {
        parentBlock.addStatement (newStatement, insertionIndex++);
    }

    void replace (Statement& newStatement)
    {
        parentBlock.setStatement (static_cast<size_t> (insertionIndex), newStatement);
    }

    static ScopeBlock& getOrCreateParentBlock (ref<Object> statement)
    {
        for (;;)
        {
            auto& parent = *statement->getParentScope();

            if (auto block = castTo<ScopeBlock> (parent).get())
                return *block;

            CMAJ_ASSERT (castTo<IfStatement> (parent) == nullptr
                          && castTo<LoopStatement> (parent) == nullptr);

            statement = parent;
        }

        CMAJ_ASSERT_FALSE;
    }

    ScopeBlock& parentBlock;
    int insertionIndex;
};
