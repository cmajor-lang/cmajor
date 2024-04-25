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

namespace cmaj
{

//==============================================================================
/// Parses the structural AST from some source code.
struct Parser   : public Lexer
{
    using NewModuleAddedCallback = std::function<bool(AST::ModuleBase&)>;

    static void parseModuleDeclarations (AST::Allocator& allocator,
                                         const SourceFile& source,
                                         bool isSystemCode,
                                         bool parseComments,
                                         AST::Namespace& parentNamespace,
                                         const NewModuleAddedCallback& moduleAddedCallback)
    {
        if (transformations::isValidBinaryModuleData (source.content.data(), source.content.size()))
        {
            for (auto& module : transformations::parseBinaryModule (allocator, source.content.data(), source.content.size(), false))
            {
                parentNamespace.subModules.addChildObject (module);

                if (moduleAddedCallback != nullptr && ! moduleAddedCallback (module))
                    break;
            }
        }
        else
        {
            Parser parser (allocator, source.getCodeLocation(), isSystemCode, parseComments);
            parser.parseTopLevelDeclarations (parentNamespace, moduleAddedCallback);
        }

        transformations::mergeDuplicateNamespaces (parentNamespace);
    }

private:
    //==============================================================================
    Parser (AST::Allocator& a, const CodeLocation& code, bool isSystem, bool parseComments)
        : allocator (a), isParsingSystemModule (isSystem), parsingComments (parseComments)
    {
        setLexerInput (code);
    }

    ~Parser() override = default;

    //==============================================================================
    AST::Allocator& allocator;
    ptr<AST::ModuleBase> activeModule;
    const bool isParsingSystemModule, parsingComments;

    template <typename Type, typename... Args>
    Type& allocate (Args&&... args) const    { return allocator.allocate<Type> (std::forward<Args> (args)...); }

    template <typename ObjectType>
    ObjectType& create()    { return allocator.allocate<ObjectType> (getContext()); }

    AST::StringPool& getStringPool() const        { return allocator.strings.stringPool; }

    AST::PooledString readPooledIdentifier()      { return getStringPool().get (readIdentifier()); }

    AST::MakeConstOrRef& createConstOrRef (AST::ObjectContext& c, AST::Expression& sourceType, bool makeConst, bool makeRef)
    {
        auto& t = allocate<AST::MakeConstOrRef> (c);
        t.source.setChildObject (sourceType);
        t.makeConst = makeConst;
        t.makeRef = makeRef;
        return t;
    }

    //==============================================================================
    template <typename Object> Object& matchEndOfStatement (Object& o)  { expectSemicolon();  return o; }

    AST::ObjectContext getContext() const override  { return { allocator, location, nullptr }; }
    AST::ObjectContext getContextAndSkip()          { auto c = getContext(); skip(); return c; }

    ptr<AST::Comment> getComment (bool backwards) const
    {
        if (parsingComments)
            if (auto comment = getCurrentComment(); ! comment.empty())
                if (AST::Comment::isReferringBackwards (comment) == backwards)
                    return allocate<AST::Comment> (getContext(), comment);

        return {};
    }

    ptr<AST::Comment> getCommentIfForward() const    { return getComment (false); }
    ptr<AST::Comment> getCommentIfBackwards() const  { return getComment (true); }

    template <typename Type>
    void addBackwardsCommentIfPresent (Type& o)
    {
        if (auto backwardsComment = getCommentIfBackwards())
            o.comment.referTo (*backwardsComment);
    }

    //==============================================================================
    std::string parseIdentifierOrKeywordOrString()
    {
        std::string result;

        if (matches (LexerToken::literalString))        result = escapedStringLiteral;
        else if (matches (LexerToken::identifier))      result = std::string (currentTokenText);
        else if (matchesAnyKeyword())                   result = std::string (currentToken.token);
        else expect (LexerToken::identifier);
        skip();
        return result;
    }

    AST::Expression& parseQualifiedIdentifier()
    {
        ref<AST::Expression> result = parseIdentifier();

        while (matches (LexerToken::operator_doubleColon))
        {
            auto& join = create<AST::NamespaceSeparator>();
            skip();
            join.lhs.setChildObject (result);
            join.rhs.setChildObject (parseIdentifier());
            result = join;
        }

        return result;
    }

    AST::Identifier& parseIdentifier()
    {
        auto& identifier = create<AST::Identifier>();
        identifier.name = readPooledIdentifier();
        return identifier;
    }

    AST::Identifier& parseUnqualifiedIdentifier()
    {
        auto& identifier = parseIdentifier();

        if (matches (LexerToken::operator_doubleColon))
            throwError (Errors::identifierMustBeUnqualified());

        return identifier;
    }

    AST::PooledString parseUnqualifiedName()
    {
        auto name = readPooledIdentifier();

        if (matches (LexerToken::operator_doubleColon))
            throwError (Errors::identifierMustBeUnqualified());

        return name;
    }

    //==============================================================================
    void parseTopLevelDeclarations (AST::Namespace& parentNamespace, const NewModuleAddedCallback& moduleAdded)
    {
        auto comment = getCommentIfForward();

        while (! skipIf (LexerToken::eof))
        {
            if (matches (LexerToken::keyword_namespace))     { if (! parseNamespace (parentNamespace, getContextAndSkip(), comment, moduleAdded)) return; comment = {}; continue; }
            if (matches (LexerToken::keyword_graph))         { if (! parseGraph     (parentNamespace, getContextAndSkip(), comment, moduleAdded)) return; comment = {}; continue; }
            if (matches (LexerToken::keyword_processor))     { if (! parseProcessor (parentNamespace, getContextAndSkip(), comment, moduleAdded)) return; comment = {}; continue; }

            if (matches (LexerToken::keyword_import))
                throwError (Errors::importsMustBeAtStart());

            throwError (Errors::expectedTopLevelDecl());
        }
    }

    bool parseNamespace (AST::ModuleBase& parent, const AST::ObjectContext& c, ptr<AST::Comment> comment, const NewModuleAddedCallback& moduleAdded)   { return parseModule<AST::Namespace> (parent, c, comment, moduleAdded); }
    bool parseGraph     (AST::ModuleBase& parent, const AST::ObjectContext& c, ptr<AST::Comment> comment, const NewModuleAddedCallback& moduleAdded)   { return parseModule<AST::Graph>     (parent, c, comment, moduleAdded); }
    bool parseProcessor (AST::ModuleBase& parent, const AST::ObjectContext& c, ptr<AST::Comment> comment, const NewModuleAddedCallback& moduleAdded)   { return parseModule<AST::Processor> (parent, c, comment, moduleAdded); }

    template <typename ModuleClass>
    bool parseModule (AST::ModuleBase& parentModule, const AST::ObjectContext& declContext,
                      ptr<AST::Comment> comment, const NewModuleAddedCallback& moduleAddedCallback)
    {
        auto originalContext = getContext();
        auto name = readPooledIdentifier();

        if (skipIf (LexerToken::operator_assign))
        {
            if (ModuleClass::isGraphClass())
                throwError (declContext, Errors::useProcessorForGraphAlias());

            parseModuleAlias (ModuleClass::getAliasType(), name, parentModule, originalContext);
            return true;
        }

        auto parentNamespace = parentModule.getAsNamespace();

        if (parentNamespace == nullptr)
            throwError (originalContext, Errors::namespaceMustBeInsideNamespace());

        auto& newModule = allocate<ModuleClass> (originalContext);
        newModule.name = name;
        newModule.isSystem = isParsingSystemModule;
        newModule.comment.referTo (comment);

        parentNamespace->subModules.addChildObject (newModule);

        auto oldModule = activeModule;
        activeModule = newModule;

        if (matches (LexerToken::operator_doubleColon))
        {
            if (auto ns = newModule.getAsNamespace())
            {
                skip();
                newModule.comment.referTo (nullptr);
                parseModule<ModuleClass> (*ns, declContext, comment, {});
            }
            else
            {
                throwError (Errors::expectedProcessorName());
            }
        }
        else
        {
            parseModuleContent();
        }

        activeModule = oldModule;

        return moduleAddedCallback == nullptr || moduleAddedCallback (newModule);
    }

    void parseModuleAlias (AST::AliasTypeEnum::Enum aliasType, AST::PooledString aliasName,
                           AST::ModuleBase& parentModule, const AST::ObjectContext& c)
    {
        for (;;)
        {
            auto& source = parseModuleAliasSource();
            auto& alias = allocate<AST::Alias> (c);
            alias.aliasType = aliasType;
            alias.target.setChildObject (source);
            alias.name = aliasName;
            parentModule.aliases.addChildObject (alias);

            if (! skipIf (LexerToken::operator_comma))
            {
                expectSemicolon();
                break;
            }

            aliasName = readPooledIdentifier();
            expect (LexerToken::operator_assign);
        }
    }

    AST::Expression& parseModuleAliasSource()
    {
        ref<AST::Expression> result = parseIdentifier();

        for (;;)
        {
            if (auto specialisationArgs = parseSpecialisationArgs())
            {
                auto& cc = allocate<AST::CallOrCast> (result->context);
                cc.functionOrType.setChildObject (result);
                cc.arguments.setChildObject (*specialisationArgs);
                result = cc;
            }

            if (matches (LexerToken::operator_doubleColon))
            {
                auto& join = create<AST::NamespaceSeparator>();
                skip();
                join.lhs.setChildObject (result);
                join.rhs.setChildObject (parseIdentifier());
                result = join;
                continue;
            }

            return result;
        }
    }

    ptr<AST::Object> parseSpecialisationArgs()
    {
        auto c = getContext();

        if (skipIf (LexerToken::operator_openParen))
            if (! skipIf (LexerToken::operator_closeParen))
                return parseParenthesisedExpression (c, false);

        return {};
    }

    void parseModuleContent()
    {
        parseModuleSpecialisationParams();
        parseOptionalAnnotation (activeModule->annotation);
        expect (LexerToken::operator_openBrace);

        if (auto processorOrGraph = activeModule->getAsProcessorBase())
        {
            while (parseEndpoint (*processorOrGraph))
            {}
        }

        if (auto ns = activeModule->getAsNamespace())
            parseImportStatements (*ns);

        while (! skipIf (LexerToken::operator_closeBrace))
        {
            if (skipIf (LexerToken::keyword_node))
            {
                if (auto processorOrGraph = activeModule->getAsProcessorBase())
                {
                    parseDeclarationList (true, [this, processorOrGraph] { parseGraphNode (*processorOrGraph); });
                    continue;
                }

                throwError (Errors::nodesCannotBeDeclaredWithinNamespaces());
            }

            if (skipIf (LexerToken::keyword_connection))
            {
                if (auto graph = activeModule->getAsGraph())
                {
                    parseConnections (graph->connections);
                    continue;
                }

                if (matches (LexerToken::keyword_graph))
                    throwError (Errors::graphMustBeInsideNamespace());
            }

            if (matches (LexerToken::keyword_graph))
            {
                auto comment = getCommentIfForward();

                if (auto ns = activeModule->getAsNamespace())
                {
                    parseGraph (*ns, getContextAndSkip(), comment, {});
                    continue;
                }

                throwError (Errors::graphMustBeInsideNamespace());
            }

            auto comment = getCommentIfForward();

            if (matches (LexerToken::keyword_processor))
            {
                auto processorContext = getContextAndSkip();

                if (matches (LexerToken::operator_dot))
                {
                    parseLatencyAssignment();
                    continue;
                }

                if (auto parent = activeModule->getAsNamespace())
                {
                    parseProcessor (*parent, processorContext, comment, {});
                    continue;
                }

                throwError (processorContext, Errors::processorMustBeInsideNamespace());
            }

            if (skipIf (LexerToken::keyword_struct))         { parseStructDeclaration (comment);      continue; }
            if (skipIf (LexerToken::keyword_enum))           { parseEnumDeclaration (comment);        continue; }
            if (skipIf (LexerToken::keyword_var))            { parseStateVarOrLet (false, comment);   continue; }
            if (skipIf (LexerToken::keyword_let))            { parseStateVarOrLet (true, comment);    continue; }
            if (skipIf (LexerToken::keyword_event))          { parseEventHandler();                   continue; }
            if (skipIf (LexerToken::keyword_using))          { parseUsingDeclaration (true, comment); continue; }
            if (matches ("static_assert"))                   { parseStaticAssert();                   continue; }
            if (matches (LexerToken::keyword_namespace))     { parseNamespace (*activeModule, getContextAndSkip(), comment, {}); continue; }

            if (matchesAny (LexerToken::keyword_output, LexerToken::keyword_input))
                throwError (activeModule->isNamespace() ? Errors::namespaceCannotContainEndpoints()
                                                        : Errors::endpointDeclsMustBeFirst());

            if (matches (LexerToken::keyword_import))
                throwError (Errors::importsMustBeAtStart());

            parseVariableOrFunction (comment);
        }

        checkForTrailingSemicolon();
    }

    void parseImportStatements (AST::Namespace& parent)
    {
        while (skipIf (LexerToken::keyword_import))
        {
            bool isString = matches (LexerToken::literalString);

            if (! (isString || matches (LexerToken::identifier) || matchesAnyKeyword()))
                throwError (Errors::expectedImportModule());

            auto name = parseIdentifierOrKeywordOrString();

            if (! isString)
                while (skipIf (LexerToken::operator_dot))
                    name += '.' + parseIdentifierOrKeywordOrString();

            parent.imports.addString (getStringPool().get (name));
            expectSemicolon();
            throwError (Errors::unimplementedFeature ("import statements"));
        }
    }

    void parseVariableOrFunction (ptr<AST::Comment> comment)
    {
        auto originalContext = getContext();
        bool hasExternalKeyword = skipIf (LexerToken::keyword_external);

        if (auto type = tryToParseType (true))
        {
            auto& name = parseUnqualifiedIdentifier();

            if (matchesAny (LexerToken::operator_openParen, LexerToken::operator_lessThan))
            {
                auto& fn = allocate<AST::Function> (name.context);
                activeModule->functions.addChildObject (fn);
                parseFunctionDeclaration (fn, *type, name.name, comment, nullptr, hasExternalKeyword);
            }
            else
            {
                auto& list = getVariableList (*activeModule);
                parseVariableDeclarations (name.context, *type, name.name, hasExternalKeyword,
                                           AST::VariableTypeEnum::Enum::state, comment,
                                           [&] (AST::VariableDeclaration& v)  { list.addChildObject (v); });
            }
        }
        else
        {
            throwError (originalContext, Errors::expectedFunctionOrVariable());
        }
    }

    void parseStructDeclaration (ptr<AST::Comment> mainComment)
    {
        auto& newStruct = create<AST::StructType>();
        newStruct.name = readPooledIdentifier();
        newStruct.comment.referTo (mainComment);

        if (matches (LexerToken::operator_openParen))
            throwError (Errors::unimplementedFeature ("Specialisation parameters for structures"));

        expect (LexerToken::operator_openBrace);

        activeModule->structures.addChildObject (newStruct);

        while (! skipIf (LexerToken::operator_closeBrace))
        {
            auto memberComment = getCommentIfForward();
            auto startPos = getLexerPosition();
            bool hasExternalKeyword = skipIf (LexerToken::keyword_external);
            auto& type = parseType (true);
            auto nameContext = getContext();
            auto name = readPooledIdentifier();

            if (matchesAny (LexerToken::operator_openParen, LexerToken::operator_lessThan))
            {
                auto& fn = allocate<AST::Function> (nameContext);
                activeModule->functions.addChildObject (fn);

                bool hasConstFlag = false;
                parseFunctionDeclaration (fn, type, name, memberComment, std::addressof (hasConstFlag), hasExternalKeyword);

                if (auto mainBlock = fn.mainBlock.getObject())
                {
                    auto& thisParamType = createConstOrRef (mainBlock->context, newStruct, hasConstFlag, true);
                    addFunctionParameter (mainBlock->context, fn, thisParamType, getStringPool().get("this"), 0);
                }

                continue;
            }

            if (hasExternalKeyword)
            {
                setLexerPosition (startPos);
                throwError (Errors::expectedType());
            }

            for (;;)
            {
                newStruct.memberNames.addString (name);
                newStruct.memberTypes.addChildObject (type);

                auto addMemberComment = [&] (AST::Comment& c)
                {
                    while (newStruct.memberComments.size() < newStruct.memberTypes.size() - 1)
                        newStruct.memberComments.addNullObject();

                    newStruct.memberComments.addChildObject (c);
                };

                if (memberComment)
                {
                    addMemberComment (*memberComment);
                    memberComment = {};
                }

                if (! skipIf (LexerToken::operator_comma))
                {
                    expectSemicolon();

                    if (auto backwardsComment = getCommentIfBackwards())
                        addMemberComment (*backwardsComment);

                    break;
                }

                name = readPooledIdentifier();
            }
        }

        checkForTrailingSemicolon();
    }

    void parseEnumDeclaration (ptr<AST::Comment> comment)
    {
        auto& newEnum = create<AST::EnumType>();
        newEnum.name = readPooledIdentifier();
        newEnum.comment.referTo (comment);
        expect (LexerToken::operator_openBrace);

        activeModule->enums.addChildObject (newEnum);

        if (matches (LexerToken::operator_closeBrace))
            throwError (Errors::enumCannotBeEmpty());

        for (;;)
        {
            auto memberComment = getCommentIfForward();
            newEnum.items.addReference (parseUnqualifiedIdentifier());

            if (auto backwardsComment = getCommentIfBackwards())
                memberComment = backwardsComment;

            bool comma = skipIf (LexerToken::operator_comma);

            if (comma)
                if (auto backwardsComment = getCommentIfBackwards())
                    memberComment = backwardsComment;

            if (memberComment)
            {
                while (newEnum.itemComments.size() < newEnum.items.size() - 1)
                    newEnum.itemComments.addNullObject();

                newEnum.itemComments.addChildObject (*memberComment);
            }

            if (comma)
                continue;

            if (skipIf (LexerToken::operator_closeBrace))
                break;

            expect (LexerToken::operator_closeBrace);
        }

        checkForTrailingSemicolon();
    }

    AST::Alias& parseUsingDeclaration (bool addToModule, ptr<AST::Comment> comment)
    {
        auto& decl = create<AST::Alias>();
        decl.aliasType = AST::AliasTypeEnum::Enum::typeAlias;
        decl.name = readPooledIdentifier();
        expect (LexerToken::operator_assign);
        decl.target.setChildObject (parseExpression());
        decl.comment.referTo (comment);
        expectSemicolon();

        if (addToModule)
            activeModule->aliases.addChildObject (decl);

        return decl;
    }

    AST::Alias& parseNamespaceAlias (bool mustHaveInitialiser)
    {
        auto& a = create<AST::Alias>();
        a.aliasType = AST::AliasTypeEnum::Enum::namespaceAlias;
        a.name = readPooledIdentifier();

        if (skipIf (LexerToken::operator_assign))
            a.target.setChildObject (parseType (false));
        else if (mustHaveInitialiser)
            expect (LexerToken::operator_assign);

        return a;
    }

    //==============================================================================
    void parseModuleSpecialisationParams()
    {
        if (skipIf (LexerToken::operator_openParen))
        {
            if (skipIf (LexerToken::operator_closeParen))
                return;

            for (;;)
            {
                if (skipIf (LexerToken::keyword_using))
                {
                    auto& usingDeclaration = create<AST::Alias>();
                    usingDeclaration.aliasType = AST::AliasTypeEnum::Enum::typeAlias;
                    usingDeclaration.name = readPooledIdentifier();

                    if (skipIf (LexerToken::operator_assign))
                        usingDeclaration.target.setChildObject (parseType (true));

                    activeModule->specialisationParams.addChildObject (usingDeclaration);
                    addBackwardsCommentIfPresent (usingDeclaration);
                }
                else if (skipIf (LexerToken::keyword_processor))
                {
                    if (! activeModule->isGraph())
                        throwError (Errors::processorSpecialisationNotAllowed());

                    auto& processorAlias = create<AST::Alias>();
                    processorAlias.aliasType = AST::AliasTypeEnum::Enum::processorAlias;
                    processorAlias.name = readPooledIdentifier();

                    if (skipIf (LexerToken::operator_assign))
                        processorAlias.target.setChildObject (parseType (false));

                    activeModule->specialisationParams.addChildObject (processorAlias);
                    addBackwardsCommentIfPresent (processorAlias);
                }
                else if (skipIf (LexerToken::keyword_namespace))
                {
                    if (! activeModule->isNamespace())
                        throwError (Errors::namespaceSpecialisationNotAllowed());

                    auto& namespaceAlias = parseNamespaceAlias (false);
                    activeModule->specialisationParams.addChildObject (namespaceAlias);
                    addBackwardsCommentIfPresent (namespaceAlias);
                }
                else
                {
                    failOnExternalKeyword();
                    auto& parameterType = parseType (true);
                    auto& parameterVariable = create<AST::VariableDeclaration>();
                    parameterVariable.variableType = AST::VariableTypeEnum::Enum::state;
                    parameterVariable.declaredType.setChildObject (parameterType);
                    parameterVariable.name = readPooledIdentifier();

                    if (skipIf (LexerToken::operator_assign))
                        parameterVariable.initialValue.setChildObject (parseExpression());

                    activeModule->specialisationParams.addChildObject (parameterVariable);
                    addBackwardsCommentIfPresent (parameterVariable);
                }

                if (! skipIf (LexerToken::operator_comma))
                {
                    expectCloseParen();
                    break;
                }
            }
        }
    }

    template <typename HandleItem>
    void parseDeclarationList (bool commasAllowed, HandleItem&& handleItem)
    {
        auto braced = skipIf (LexerToken::operator_openBrace);

        if (braced && skipIf (LexerToken::operator_closeBrace))
            return;

        for (;;)
        {
            handleItem();

            if (! (commasAllowed && skipIf (LexerToken::operator_comma)))
            {
                expectSemicolon();

                if (! braced || skipIf (LexerToken::operator_closeBrace))
                    break;
            }
        }
    }

    //==============================================================================
    void parseConnections (AST::ListProperty& connectionList)
    {
        auto braced = skipIf (LexerToken::operator_openBrace);

        if (braced && skipIf (LexerToken::operator_closeBrace))
            return;

        for (;;)
        {
            if (skipIf (LexerToken::keyword_if))
            {
                auto& connectionIf = create<AST::ConnectionIf>();

                expect (LexerToken::operator_openParen);
                connectionIf.condition.setChildObject (parseExpression());
                expectCloseParen();

                auto& trueConnectionList = create<AST::ConnectionList>();
                connectionIf.trueConnections.setChildObject (trueConnectionList);
                parseConnections (trueConnectionList.connections);

                if (skipIf (LexerToken::keyword_else))
                {
                    auto& falseConnectionList = create<AST::ConnectionList>();
                    connectionIf.falseConnections.setChildObject (falseConnectionList);
                    parseConnections (falseConnectionList.connections);
                }

                connectionList.addChildObject (connectionIf);
            }
            else
            {
                auto interpolationType = parseInterpolationTypeIfPresent();
                ptr<AST::Connection> lastConnection;

                for (;;)
                {
                    auto& connection = create<AST::Connection>();

                    if (lastConnection != nullptr)
                        connection.sources.addChildObject (*lastConnection);
                    else
                        parseConnectionEndpoints (connection.sources);

                    connection.context.location = location;
                    expect (LexerToken::operator_rightArrow);
                    connection.interpolation = interpolationType;

                    if (auto delayLength = parseDelayLength())
                        connection.delayLength.setChildObject (*delayLength);

                    parseConnectionEndpoints (connection.dests);

                    if (connection.sources.size() > 1 && connection.dests.size() > 1)
                        throwError (connection, Errors::unimplementedFeature ("Many-to-many connections are not currently supported"));

                    if (! matches (LexerToken::operator_rightArrow))
                    {
                        connectionList.addChildObject (connection);
                        break;
                    }

                    if (connection.dests.size() > 1)
                        throwError (connection.dests[1], Errors::cannotChainConnectionWithMultiple());

                    if (auto dot = AST::castTo<AST::DotOperator> (connection.dests[0]))
                        throwError (dot->rhs, Errors::cannotNameEndpointInChain());

                    lastConnection = connection;
                }

                expectSemicolon();
            }

            if (! braced || skipIf (LexerToken::operator_closeBrace))
                break;
        }
    }

    void parseConnectionEndpoints (AST::ListProperty& list)
    {
        for (;;)
        {
            if (auto endpoint = tryToParseExpression())
            {
                list.addChildObject (*endpoint);

                if (! skipIf (LexerToken::operator_comma))
                    break;
            }
            else
            {
                throwError (Errors::expectedProcessorOrEndpoint());
            }
        }
    }

    ptr<AST::Object> parseDelayLength()
    {
        if (! skipIf (LexerToken::operator_openBracket))
            return {};

        auto& numFrames = parseExpression();
        expect (LexerToken::operator_closeBracket);
        expect (LexerToken::operator_rightArrow);
        return numFrames;
    }

    AST::InterpolationTypeEnum::Enum parseInterpolationType()
    {
        if (skipIfKeywordOrIdentifier ("none"))   return AST::InterpolationTypeEnum::Enum::none;
        if (skipIfKeywordOrIdentifier ("latch"))  return AST::InterpolationTypeEnum::Enum::latch;
        if (skipIfKeywordOrIdentifier ("linear")) return AST::InterpolationTypeEnum::Enum::linear;
        if (skipIfKeywordOrIdentifier ("sinc"))   return AST::InterpolationTypeEnum::Enum::sinc;
        if (skipIfKeywordOrIdentifier ("fast"))   return AST::InterpolationTypeEnum::Enum::fast;
        if (skipIfKeywordOrIdentifier ("best"))   return AST::InterpolationTypeEnum::Enum::best;

        throwError (Errors::expectedInterpolationType());
    }

    AST::InterpolationTypeEnum::Enum parseInterpolationTypeIfPresent()
    {
        if (! skipIf (LexerToken::operator_openBracket))
            return AST::InterpolationTypeEnum::Enum::none;

        auto type = parseInterpolationType();
        expect (LexerToken::operator_closeBracket);
        return type;
    }

    void parseGraphNode (AST::ProcessorBase& graph)
    {
        auto& node = create<AST::GraphNode>();
        node.nodeName = parseUnqualifiedName();
        graph.nodes.addChildObject (node);

        expect (LexerToken::operator_assign);

        if (! matches (LexerToken::identifier))
            throwError (Errors::expectedProcessorName());

        ref<AST::Expression> processor = parseQualifiedIdentifier();

        for (;;)
        {
            if (auto args = parseSpecialisationArgs())
            {
                auto& parameterised = allocate<AST::CallOrCast> (AST::getContext (processor));
                parameterised.functionOrType.setChildObject (processor);
                parameterised.arguments.setChildObject (*args);
                processor = parameterised;
            }

            if (skipIf (LexerToken::operator_openBracket))
            {
                node.arraySize.setChildObject (parseExpression());

                if (matches (LexerToken::operator_comma))
                    throwError (Errors::unimplementedFeature ("Multi-dimensional graph nodes"));

                expect (LexerToken::operator_closeBracket);
                break;
            }

            if (matches (LexerToken::operator_doubleColon))
            {
                auto& join = create<AST::NamespaceSeparator>();
                skip();
                join.lhs.setChildObject (processor);
                join.rhs.setChildObject (parseIdentifier());
                processor = join;
            }
            else
            {
                break;
            }
        }

        node.processorType.setChildObject (processor);

        if (skipIf (LexerToken::operator_times))
            node.clockMultiplierRatio.setChildObject (parseExpression());
        else if (skipIf (LexerToken::operator_divide))
            node.clockDividerRatio.setChildObject (parseExpression());
    }

    AST::Object& parseSpecialisationValueOrType()
    {
        auto startPos = getLexerPosition();

        if (auto type = tryToParseType (false))
            if (! matches (LexerToken::operator_openParen))
                return *type;

        setLexerPosition (startPos);
        return parseExpression();
    }

    //==============================================================================
    bool isNextTokenEndpointType()  { return matchesAny ("value", "stream", "event"); }

    AST::EndpointTypeEnum::Enum parseEndpointType()
    {
        if (skipIfKeywordOrIdentifier ("value"))   return AST::EndpointTypeEnum::Enum::value;
        if (skipIfKeywordOrIdentifier ("stream"))  return AST::EndpointTypeEnum::Enum::stream;
        if (skipIfKeywordOrIdentifier ("event"))   return AST::EndpointTypeEnum::Enum::event;

        throwError (Errors::expectedStreamType());
    }

    void parseEndpointName (AST::EndpointDeclaration& e)
    {
        auto nameContext = getContext();
        auto name = readPooledIdentifier();
        e.name = name;

        if (skipIf (LexerToken::operator_openBracket))
        {
            e.arraySize.setChildObject (parseExpression());

            if (matches (LexerToken::operator_comma))
                throwError (Errors::unimplementedFeature ("Multi-dimensional endpoint arrays"));

            expect (LexerToken::operator_closeBracket);
        }

        parseOptionalAnnotation (e.annotation);

        if (AST::isSpecialFunctionName (e.getStrings(), name))
            throwError (nameContext, Errors::invalidEndpointName (name));
    }

    bool parseEndpoint (AST::ProcessorBase& owner)
    {
        auto comment = getCommentIfForward();

        if (skipIf (LexerToken::keyword_input))  { parseEndpoint (owner, comment, true,  false); return true; }
        if (skipIf (LexerToken::keyword_output)) { parseEndpoint (owner, comment, false, false); return true; }

        return false;
    }

    void parseEndpoint (AST::ProcessorBase& owner, ptr<AST::Comment> comment, bool isInput, bool insideBracedExpression)
    {
        if (insideBracedExpression || ! skipIf (LexerToken::operator_openBrace))
        {
            comment = getCommentIfForward();

            if (owner.isGraph() && matches (LexerToken::identifier) && ! isNextTokenEndpointType())
                return parseHoistedEndpoint (owner, comment, isInput);

            auto endpointType = parseEndpointType();

            if (skipIf (LexerToken::operator_openBrace))
            {
                while (! skipIf (LexerToken::operator_closeBrace))
                {
                    comment = getCommentIfForward();
                    parseEndpointWithType (owner, comment, isInput, endpointType);
                }
            }
            else
            {
                parseEndpointWithType (owner, comment, isInput, endpointType);
            }
        }
        else
        {
            while (! skipIf (LexerToken::operator_closeBrace))
            {
                parseEndpoint (owner, comment, isInput, true);
                comment = {};
            }
        }
    }

    void parseEndpointWithType (AST::ProcessorBase& owner, ptr<AST::Comment> comment, bool isInput,
                                AST::EndpointTypeEnum::Enum endpointType)
    {
        auto& first = create<AST::EndpointDeclaration>();
        first.isInput = isInput;
        first.endpointType = endpointType;
        first.comment.referTo (comment);
        parseEndpointTypeList (first, endpointType);
        parseEndpointName (first);
        owner.endpoints.addChildObject (first);
        ref<AST::EndpointDeclaration> lastEndpoint = first;

        while (skipIf (LexerToken::operator_comma))
        {
            auto& next = create<AST::EndpointDeclaration>();
            lastEndpoint = next;
            next.isInput = isInput;
            next.endpointType = endpointType;
            parseEndpointName (next);
            owner.endpoints.addChildObject (next);

            for (auto& t : first.dataTypes)
                next.dataTypes.addChildObject (t->getObjectRef());
        }

        expectSemicolon();
        addBackwardsCommentIfPresent (lastEndpoint.get());
    }

    void parseHoistedEndpoint (AST::ProcessorBase& parent, ptr<AST::Comment> comment, bool isInput)
    {
        for (;;)
        {
            auto& endpointDeclaration = create<AST::EndpointDeclaration>();
            endpointDeclaration.isInput = isInput;

            if (comment)
            {
                endpointDeclaration.comment.referTo (comment);
                comment = {};
            }

            parent.endpoints.addChildObject (endpointDeclaration);

            auto& hoistedEndpointPath = create<AST::HoistedEndpointPath>();
            endpointDeclaration.childPath.setChildObject (hoistedEndpointPath);
            bool isFirstIdentifier = true;

            for (;;)
            {
                auto& name = parseUnqualifiedIdentifier();

                if (isFirstIdentifier)
                    isFirstIdentifier = false;
                else
                    endpointDeclaration.name = name.name;

                if (skipIf (LexerToken::operator_openBracket))
                {
                    auto& bracketed = create<AST::GetElement>();
                    bracketed.parent.setChildObject (name);
                    bracketed.indexes.addChildObject (parseExpression());
                    expect (LexerToken::operator_closeBracket);
                    hoistedEndpointPath.pathSections.addChildObject (bracketed);
                }
                else
                {
                    hoistedEndpointPath.pathSections.addChildObject (name);
                }

                if (skipIf (LexerToken::operator_dot))
                {
                    auto startOfName = getLexerPosition();
                    std::string nameOrWildcard;
                    auto pos = location.text;
                    bool isWildcard = false;

                    for (;;)
                    {
                        auto c = *pos;

                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                              || c == '_' || c == '*' || c == '?')
                        {
                            isWildcard = isWildcard || (c == '*' || c == '?');
                            choc::text::appendUTF8 (nameOrWildcard, c);
                            ++pos;
                            continue;
                        }

                        setLexerPosition (isWildcard ? pos : startOfName);
                        break;
                    }

                    if (isWildcard)
                    {
                        hoistedEndpointPath.wildcardPattern = getStringPool().get (nameOrWildcard);
                        endpointDeclaration.name.reset();
                        break;
                    }

                    continue;
                }

                if (matches (LexerToken::identifier))
                    endpointDeclaration.name = readPooledIdentifier();

                break;
            }

            parseOptionalAnnotation (endpointDeclaration.annotation);

            if (skipIf (LexerToken::operator_comma))
                continue;

            expectSemicolon();
            addBackwardsCommentIfPresent (endpointDeclaration);
            break;
        }
    }

    //==============================================================================
    void addFunctionParameter (AST::ObjectContext& c, AST::Function& fn, AST::Expression& type,
                               AST::PooledString name, int insertIndex = -1)
    {
        auto& param = allocate<AST::VariableDeclaration> (c);
        param.variableType = AST::VariableTypeEnum::Enum::parameter;
        param.declaredType.setChildObject (type);
        param.name = name;
        fn.parameters.addChildObject (param, insertIndex);

        if (fn.parameters.size() > 128)
            throwError (type, Errors::tooManyParameters());
    }

    void parseFunctionParameter (AST::Function& fn)
    {
        auto& type = parseType (true);
        auto c = getContext();
        addFunctionParameter (c, fn, type, readPooledIdentifier());
    }

    void parseGenericFunctionWildcards (AST::Function& fn)
    {
        if (skipIf (LexerToken::operator_lessThan))
        {
            if (fn.isExternal)
                throwError (Errors::externalFunctionCannotBeGeneric());

            for (;;)
            {
                if (! matches (LexerToken::identifier))
                    throwError (Errors::expectedGenericWildcardName());

                fn.genericWildcards.addChildObject (parseUnqualifiedIdentifier());

                if (skipIf (LexerToken::operator_greaterThan))
                    break;

                expect (LexerToken::operator_comma);
            }
        }
    }

    void parseFunctionDeclaration (AST::Function& fn, AST::Expression& returnType,
                                   AST::PooledString name, ptr<AST::Comment> comment,
                                   bool* hasConstFlag, bool hasExternalKeyword)
    {
        fn.name = name;
        fn.returnType.setChildObject (returnType);
        fn.comment.referTo (comment);

        parseGenericFunctionWildcards (fn);

        expect (LexerToken::operator_openParen);

        if (! skipIf (LexerToken::operator_closeParen))
        {
            for (;;)
            {
                failOnExternalKeyword();
                parseFunctionParameter (fn);

                if (skipIf (LexerToken::operator_closeParen))
                    break;

                expect (LexerToken::operator_comma);
            }
        }

        if (hasConstFlag != nullptr)
        {
            *hasConstFlag = skipIf (LexerToken::keyword_const);
        }
        else
        {
            if (matches (LexerToken::keyword_const))
                throwError (Errors::onlyMemberFunctionsCanBeConst());
        }

        parseOptionalAnnotation (fn.annotation);

        if (hasExternalKeyword)
        {
            fn.isExternal = true;

            if (! skipIf (LexerToken::operator_semicolon))
                throwError (Errors::externalFunctionCannotHaveBody());
        }
        else
        {
            fn.mainBlock.setChildObject (parseBracedBlock());
        }
    }

    void parseEventHandler()
    {
        if (! activeModule->isProcessorBase())
            throwError (Errors::noEventFunctionsAllowed());

        auto& fn = create<AST::Function>();
        fn.name = parseUnqualifiedName();

        expect (LexerToken::operator_openParen);

        activeModule->getAsProcessorBase()->functions.addChildObject (fn);

        fn.returnType.setChildObject (allocator.voidType.clone());
        fn.isEventHandler = true;

        if (! matches (LexerToken::operator_closeParen))
        {
            // Event function params are either (event type) or (index, event type)
            for (int i = 0; i < 2; ++i)
            {
                parseFunctionParameter (fn);

                if (! (i == 0 && skipIf (LexerToken::operator_comma)))
                    break;
            }
        }

        expectCloseParen();
        fn.mainBlock.setChildObject (parseBracedBlock());
    }

    AST::ScopeBlock& parseBracedBlock()
    {
        expect (LexerToken::operator_openBrace);
        return parseBracedBlock (create<AST::ScopeBlock>());
    }

    AST::ScopeBlock& parseBracedBlock (AST::ScopeBlock& block)
    {
        while (! skipIf (LexerToken::operator_closeBrace))
            parseAndAddStatement (block);

        return block;
    }

    AST::ScopeBlock& parseStatementAsBlock()
    {
        if (matches (LexerToken::operator_openBrace))
            return parseBracedBlock();

        auto& block = create<AST::ScopeBlock>();
        parseAndAddStatement (block);
        return block;
    }

    void parseAndAddStatement (AST::ScopeBlock& parentBlock)
    {
        parentBlock.statements.addChildObject (parseStatement (parentBlock));
    }

    AST::Statement& parseStatement (AST::ScopeBlock& parentBlock)
    {
        if (matchesAny (LexerToken::literalInt32, LexerToken::literalInt64, LexerToken::literalFloat64,
                        LexerToken::literalFloat32, LexerToken::literalString, LexerToken::operator_minus,
                        LexerToken::literalImag32, LexerToken::literalImag64))
        {
            return matchEndOfStatement (parseExpression());
        }

        if (matches (LexerToken::operator_openBrace))     return parseBracedBlock();
        if (skipIf  (LexerToken::operator_semicolon))     return create<AST::ScopeBlock>();
        if (skipIf  (LexerToken::keyword_if))             return parseIf();
        if (skipIf  (LexerToken::keyword_loop))           return parseLoopStatement ({});
        if (skipIf  (LexerToken::keyword_for))            return parseForLoop ({});
        if (skipIf  (LexerToken::keyword_while))          return parseWhileLoop ({});
        if (skipIf  (LexerToken::keyword_return))         return parseReturn();
        if (matches (LexerToken::keyword_break))          return parseBreakOrContinue<AST::BreakStatement>();
        if (matches (LexerToken::keyword_continue))       return parseBreakOrContinue<AST::ContinueStatement>();
        if (skipIf  (LexerToken::keyword_using))          return parseUsingDeclaration (false, {});
        if (skipIf  (LexerToken::keyword_namespace))      return parseNamespaceAlias (true);
        if (skipIf  (LexerToken::operator_plusplus))      return matchEndOfStatement (parsePreIncDec (true));
        if (skipIf  (LexerToken::operator_minusminus))    return matchEndOfStatement (parsePreIncDec (false));
        if (skipIf  (LexerToken::keyword_forward_branch)) return parseForwardBranch();
        if (matches (LexerToken::keyword_external))       throwError (Errors::externalNotAllowedInFunction());
        if (matches (LexerToken::keyword_switch))         throwError (Errors::unimplementedFeature ("switch statements"));

        {
            auto variables = tryParsingVariableDeclaration (AST::VariableTypeEnum::Enum::local);

            if (! variables.empty())
            {
                for (size_t i = 0; i < variables.size() - 1; ++i)
                    parentBlock.statements.addChildObject (variables[i]);

                return variables.back();
            }
        }

        if (matches (LexerToken::identifier))
        {
            auto startPos = getLexerPosition();
            auto labelName = getStringPool().get (currentTokenText);
            skip();

            if (skipIf (LexerToken::operator_colon))
            {
                if (skipIf (LexerToken::operator_openBrace))
                {
                    auto& block = allocate<AST::ScopeBlock> (AST::ObjectContext { allocator, startPos, nullptr });
                    block.label = labelName;
                    return parseBracedBlock (block);
                }

                if (skipIf (LexerToken::keyword_loop))    return parseLoopStatement (labelName);
                if (skipIf (LexerToken::keyword_for))     return parseForLoop (labelName);
                if (skipIf (LexerToken::keyword_while))   return parseWhileLoop (labelName);

                throwError (Errors::expectedBlockAfterLabel());
            }

            setLexerPosition (startPos);
        }
        else
        {
            failOnExternalKeyword();
            failOnAssignmentToProcessorProperty();
        }

        return matchEndOfStatement (parseAssignmentOrEndpointWrite (parentBlock));
    }

    AST::ObjectRefVector<AST::VariableDeclaration> tryParsingVariableDeclaration (AST::VariableTypeEnum::Enum variableType)
    {
        AST::ObjectRefVector<AST::VariableDeclaration> variablesCreated;
        auto comment = getCommentIfForward();

        if (skipIf (LexerToken::keyword_let))
        {
            parseVarOrLetDeclaration (true, variableType, comment, [&] (AST::VariableDeclaration& v)
            {
                variablesCreated.push_back (v);
            });
        }
        else if (skipIf (LexerToken::keyword_var))
        {
            parseVarOrLetDeclaration (false, variableType, comment, [&] (AST::VariableDeclaration& v)
            {
                variablesCreated.push_back (v);
            });
        }
        else
        {
            auto oldPos = getLexerPosition();

            if (auto type = tryToParseType (true))
            {
                if (matches (LexerToken::identifier))
                {
                    auto c = getContext();
                    auto name = readPooledIdentifier();
                    parseVariableDeclarations (c, *type, name, false, variableType, comment,
                                              [&] (AST::VariableDeclaration& v)
                                              {
                                                  variablesCreated.push_back (v);
                                              });
                }
            }

            if (variablesCreated.empty())
                setLexerPosition (oldPos);
        }

        return variablesCreated;
    }

    void parseOptionalAnnotation (AST::ChildObject& targetProperty)
    {
        if (matches (LexerToken::operator_openDoubleBracket))
        {
            auto& annotation = create<AST::Annotation>();
            skip();
            targetProperty.setChildObject (annotation);

            if (skipIf (LexerToken::operator_closeDoubleBracket))
                return;

            for (;;)
            {
                auto keyContext = getContext();
                auto name = parseIdentifierOrKeywordOrString();

                if (annotation.findProperty (name) != nullptr)
                    throwError (keyContext, Errors::nameInUse (name));

                annotation.names.addString (getStringPool().get (name));

                if (skipIf (LexerToken::operator_colon))
                {
                    annotation.values.addChildObject (parseExpression());

                    if (skipIf (LexerToken::operator_comma))
                        continue;

                    expect (LexerToken::operator_closeDoubleBracket);
                    break;
                }

                annotation.values.addChildObject (allocator.createConstantBool (keyContext.location, true));

                if (skipIf (LexerToken::operator_comma))
                    continue;

                if (skipIf (LexerToken::operator_closeDoubleBracket))
                    break;

                expect (LexerToken::operator_colon);
            }
        }
    }

    //==============================================================================
    AST::Expression& parseExpression (bool giveErrorOnTrailingAssignment = true,
                                      bool giveErrorOnTrailingLeftArrow = true)
    {
        auto& lhs = parseTernaryOperator();

        if (matches (LexerToken::operator_plusEquals))                 return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::add);
        if (matches (LexerToken::operator_minusEquals))                return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::subtract);
        if (matches (LexerToken::operator_timesEquals))                return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::multiply);
        if (matches (LexerToken::operator_divideEquals))               return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::divide);
        if (matches (LexerToken::operator_moduloEquals))               return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::modulo);
        if (matches (LexerToken::operator_leftShiftEquals))            return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::leftShift);
        if (matches (LexerToken::operator_rightShiftEquals))           return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::rightShift);
        if (matches (LexerToken::operator_rightShiftUnsignedEquals))   return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned);
        if (matches (LexerToken::operator_xorEquals))                  return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::bitwiseXor);
        if (matches (LexerToken::operator_bitwiseAndEquals))           return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::bitwiseAnd);
        if (matches (LexerToken::operator_bitwiseOrEquals))            return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::bitwiseOr);
        if (matches (LexerToken::operator_logicalAndEquals))           return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::logicalAnd);
        if (matches (LexerToken::operator_logicalOrEquals))            return parseInPlaceOperator (giveErrorOnTrailingAssignment, lhs, AST::BinaryOpTypeEnum::Enum::logicalOr);

        if (giveErrorOnTrailingAssignment && matches (LexerToken::operator_assign))
            throwError (Errors::assignmentInsideExpression());

        if (giveErrorOnTrailingLeftArrow && matches (LexerToken::operator_leftArrow))
            throwError (Errors::endpointWriteInsideExpression());

        return lhs;
    }

    ptr<AST::Expression> tryToParseExpression()
    {
        try
        {
            DiagnosticMessageHandler handler;
            return parseExpression();
        }
        catch (DiagnosticMessageHandler::IgnoredErrorException) {}

        return {};
    }

    bool tryToParseChevronSuffixExpression (AST::ObjectRefVector<AST::Expression>& terms)
    {
        try
        {
            DiagnosticMessageHandler handler;
            terms.push_back (parseShiftOperator());
        }
        catch (DiagnosticMessageHandler::IgnoredErrorException)
        {
            return false;
        }

        for (;;)
        {
            if (! skipIf (LexerToken::operator_comma))
                return true;

            terms.push_back (parseShiftOperator());
        }
    }

    //==============================================================================
    AST::Expression& parseParenthesisedExpression (const AST::ObjectContext& c, bool parseSuffixes)
    {
        if (skipIf (LexerToken::operator_closeParen))
            return allocate<AST::ExpressionList> (c);

        auto& e = parseExpression();

        if (skipIf (LexerToken::operator_closeParen))
            return parseSuffixes ? parseExpressionSuffixes (e) : e;

        if (skipIf (LexerToken::operator_comma))
        {
            auto& list = allocate<AST::ExpressionList> (c);
            list.items.addChildObject (e);

            for (;;)
            {
                list.items.addChildObject (parseExpression());

                if (list.size() > AST::maxInitialiserListLength)
                    throwError (e, Errors::initialiserListTooLong());

                if (skipIf (LexerToken::operator_comma))
                    continue;

                expectCloseParen();
                break;
            }

            return list;
        }

        expectCloseParen();
        return e;
    }

    template <typename ObjectType, typename ValueType>
    AST::Expression& createLiteral (ValueType v)
    {
        auto& lit = allocate<ObjectType> (getContextAndSkip(), v);
        return parseExpressionSuffixes (lit);
    }

    AST::Expression& parseFactor()
    {
        if (matches (LexerToken::operator_openParen))   return parseParenthesisedExpression (getContextAndSkip(), true);
        if (matches (LexerToken::literalInt32))         return createLiteral<AST::ConstantInt32> (static_cast<int32_t> (currentIntLiteral));
        if (matches (LexerToken::literalInt64))         return createLiteral<AST::ConstantInt64> (static_cast<int64_t> (currentIntLiteral));
        if (matches (LexerToken::literalFloat32))       return createLiteral<AST::ConstantFloat32> (static_cast<float> (currentDoubleLiteral));
        if (matches (LexerToken::literalFloat64))       return createLiteral<AST::ConstantFloat64> (currentDoubleLiteral);
        if (matches (LexerToken::literalImag32))        return createLiteral<AST::ConstantComplex32> (std::complex (0.0f, static_cast<float> (currentDoubleLiteral)));
        if (matches (LexerToken::literalImag64))        return createLiteral<AST::ConstantComplex64> (std::complex (0.0, static_cast<double> (currentDoubleLiteral)));
        if (matches (LexerToken::literalString))        return createLiteral<AST::ConstantString> (escapedStringLiteral);
        if (matches (LexerToken::keyword_true))         return createLiteral<AST::ConstantBool> (true);
        if (matches (LexerToken::keyword_false))        return createLiteral<AST::ConstantBool> (false);
        if (skipIf  (LexerToken::keyword_processor))    return parseProcessorProperty();

        if (auto type = tryToParseType (false))
            return parseExpressionSuffixes (*type);

        return parseExpressionSuffixes (parseQualifiedIdentifier());
    }

    AST::Expression& parseExponent()
    {
        for (ref<AST::Expression> e = parseFactor();;)
        {
            if (! matches (LexerToken::operator_exponent))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::exponent, e, parseUnary());
        }
    }

    AST::Expression& parseUnary()
    {
        if (skipIf (LexerToken::operator_plusplus))    return parsePreIncDec (true);
        if (skipIf (LexerToken::operator_minusminus))  return parsePreIncDec (false);

        AST::UnaryOpTypeEnum::Enum op;

        if (matches (LexerToken::operator_minus))            op = AST::UnaryOpTypeEnum::Enum::negate;
        else if (matches (LexerToken::operator_logicalNot))  op = AST::UnaryOpTypeEnum::Enum::logicalNot;
        else if (matches (LexerToken::operator_bitwiseNot))  op = AST::UnaryOpTypeEnum::Enum::bitwiseNot;
        else                                                 return parseExponent();

        auto& unary = create<AST::UnaryOperator>();
        skip();
        unary.op = op;
        unary.input.setChildObject (parseUnary());
        return unary;
    }

    AST::Expression& parseMultiplyDivide()
    {
        for (ref<AST::Expression> e = parseUnary();;)
        {
            AST::BinaryOpTypeEnum::Enum type;

            if (matches (LexerToken::operator_times))          type = AST::BinaryOpTypeEnum::Enum::multiply;
            else if (matches (LexerToken::operator_divide))    type = AST::BinaryOpTypeEnum::Enum::divide;
            else if (matches (LexerToken::operator_modulo))    type = AST::BinaryOpTypeEnum::Enum::modulo;
            else                                               return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, type, e, parseUnary());
        }
    }

    AST::Expression& parseAdditionSubtraction()
    {
        for (ref<AST::Expression> e = parseMultiplyDivide();;)
        {
            AST::BinaryOpTypeEnum::Enum type;
            bool skipToken = true;

            if (matches (LexerToken::operator_plus))                type = AST::BinaryOpTypeEnum::Enum::add;
            else if (matches (LexerToken::operator_minus))          type = AST::BinaryOpTypeEnum::Enum::subtract;
            // Deal with a minus sign that has a space before but not after it, e.g. (a -2)
            else if ((currentIntLiteral < 0 && matchesAny (LexerToken::literalInt32, LexerToken::literalInt64))
                     || (currentDoubleLiteral < 0 && matchesAny (LexerToken::literalFloat32, LexerToken::literalFloat64,
                                                                 LexerToken::literalImag32, LexerToken::literalImag64)))
            {
                skipToken = false;
                type = AST::BinaryOpTypeEnum::Enum::add;
            }
            else
                return e;

            auto c = getContext();

            if (skipToken)
                skip();

            e = AST::createBinaryOp (c, type, e, parseMultiplyDivide());
        }
    }

    AST::Expression& parseShiftOperator()
    {
        for (ref<AST::Expression> e = parseAdditionSubtraction();;)
        {
            AST::BinaryOpTypeEnum::Enum type;

            if (matches (LexerToken::operator_leftShift))                type = AST::BinaryOpTypeEnum::Enum::leftShift;
            else if (matches (LexerToken::operator_rightShift))          type = AST::BinaryOpTypeEnum::Enum::rightShift;
            else if (matches (LexerToken::operator_rightShiftUnsigned))  type = AST::BinaryOpTypeEnum::Enum::rightShiftUnsigned;
            else                                                         return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, type, e, parseAdditionSubtraction());
        }
    }

    AST::Expression& parseComparisonOperator()
    {
        for (ref<AST::Expression> e = parseShiftOperator();;)
        {
            AST::BinaryOpTypeEnum::Enum type;

            if (matches (LexerToken::operator_lessThan))                type = AST::BinaryOpTypeEnum::Enum::lessThan;
            else if (matches (LexerToken::operator_lessThanOrEqual))    type = AST::BinaryOpTypeEnum::Enum::lessThanOrEqual;
            else if (matches (LexerToken::operator_greaterThanOrEqual)) type = AST::BinaryOpTypeEnum::Enum::greaterThanOrEqual;
            else if (matches (LexerToken::operator_greaterThan))        type = AST::BinaryOpTypeEnum::Enum::greaterThan;
            else                                                        return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, type, e, parseShiftOperator());
        }
    }

    AST::Expression& parseEqualityOperator()
    {
        for (ref<AST::Expression> e = parseComparisonOperator();;)
        {
            AST::BinaryOpTypeEnum::Enum type;

            if (matches (LexerToken::operator_equals))           type = AST::BinaryOpTypeEnum::Enum::equals;
            else if (matches (LexerToken::operator_notEquals))   type = AST::BinaryOpTypeEnum::Enum::notEquals;
            else                                                 return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, type, e, parseComparisonOperator());
        }
    }

    AST::Expression& parseBitwiseAnd()
    {
        for (ref<AST::Expression> e = parseEqualityOperator();;)
        {
            if (! matches (LexerToken::operator_bitwiseAnd))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::bitwiseAnd, e, parseEqualityOperator());
        }
    }

    AST::Expression& parseBitwiseXor()
    {
        for (ref<AST::Expression> e = parseBitwiseAnd();;)
        {
            if (! matches (LexerToken::operator_bitwiseXor))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::bitwiseXor, e, parseBitwiseAnd());
        }
    }

    AST::Expression& parseBitwiseOr()
    {
        for (ref<AST::Expression> e = parseBitwiseXor();;)
        {
            if (! matches (LexerToken::operator_bitwiseOr))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::bitwiseOr, e, parseBitwiseXor());
        }
    }

    AST::Expression& parseLogicalAnd()
    {
        for (ref<AST::Expression> e = parseBitwiseOr();;)
        {
            if (! matches (LexerToken::operator_logicalAnd))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::logicalAnd, e, parseBitwiseOr());
        }
    }

    AST::Expression& parseLogicalOr()
    {
        for (ref<AST::Expression> e = parseLogicalAnd();;)
        {
            if (! matches (LexerToken::operator_logicalOr))
                return e;

            auto c = getContextAndSkip();
            e = AST::createBinaryOp (c, AST::BinaryOpTypeEnum::Enum::logicalOr, e, parseLogicalAnd());
        }
    }

    AST::Expression& parseTernaryOperator()
    {
        auto& e = parseLogicalOr();

        if (! matches (LexerToken::operator_question))
            return e;

        auto& ternary = create<AST::TernaryOperator>();
        skip();
        ternary.condition.setChildObject (e);
        ternary.trueValue.setChildObject (parseTernaryOperator());
        expect (LexerToken::operator_colon);
        ternary.falseValue.setChildObject (parseTernaryOperator());
        return ternary;
    }

    AST::Statement& parseAssignmentOrEndpointWrite (ptr<AST::ScopeBlock> parentBlockForEndpointWrites)
    {
        auto& lhs = parseExpression (false, false);

        if (matches (LexerToken::operator_assign))
        {
            auto& assignment = create<AST::Assignment>();
            skip();
            assignment.target.setChildObject (lhs);
            assignment.source.setChildObject (parseExpression());
            return assignment;
        }

        if (matches (LexerToken::operator_leftArrow))
        {
            if (parentBlockForEndpointWrites == nullptr)
                throwError (Errors::endpointWriteInsideExpression());

            for (;;)
            {
                auto& write = create<AST::WriteToEndpoint>();
                skip();
                write.target.setChildObject (lhs);

                if (! skipIf (LexerToken::keyword_void))
                    write.value.setChildObject (parseExpressionAsListIfParenthesised (false));

                if (matches (LexerToken::operator_leftArrow))
                {
                    parentBlockForEndpointWrites->addStatement (write);
                    continue;
                }

                return write;
            }
        }

        if (matches (LexerToken::operator_rightArrow))
            throwError (Errors::rightArrowInStatement());

        return lhs;
    }


    AST::Expression& parsePostIncDec (AST::Object& lhs, bool isIncrement)
    {
        auto& op = create<AST::PreOrPostIncOrDec>();
        op.target.setChildObject (lhs);
        op.isIncrement = isIncrement;
        op.isPost = true;
        return op;
    }

    AST::Expression& parsePreIncDec (bool isIncrement)
    {
        auto& op = create<AST::PreOrPostIncOrDec>();
        op.target.setChildObject (parseFactor());
        op.isIncrement = isIncrement;
        op.isPost = false;
        return op;
    }

    AST::Expression& parseInPlaceOperator (bool giveErrorOnTrailingAssignment, AST::Expression& lhs, AST::BinaryOpTypeEnum::Enum opType)
    {
        if (giveErrorOnTrailingAssignment)
            throwError (Errors::inPlaceOperatorMustBeStatement (currentToken));

        auto& op = create<AST::InPlaceOperator>();
        skip();
        op.op = opType;
        op.target.setChildObject (lhs);
        op.source.setChildObject (parseExpression());
        return op;
    }

    //==============================================================================
    AST::ProcessorProperty& parseProcessorProperty()
    {
        expect (LexerToken::operator_dot);

        if (! (activeModule->isProcessor() || activeModule->isGraph()))
            throwError (Errors::propertiesOutsideProcessor());

        auto& pp = create<AST::ProcessorProperty>();
        auto propertyTypeID = pp.property.getEnumList().getID (readIdentifier());

        if (propertyTypeID < 0)
            throwError (pp, Errors::unknownProcessorProperty());

        pp.property.setID (propertyTypeID);
        return pp;
    }

    AST::Expression& parseExpressionAsListIfParenthesised (bool giveErrorOnTrailingLeftArrow = true)
    {
        if (matches (LexerToken::operator_openParen))
        {
            auto startPos = getLexerPosition();
            skip();
            auto& list = parseExpressionList();

            if (matchesAny (LexerToken::operator_comma, LexerToken::operator_semicolon))
                return list;

            setLexerPosition (startPos);
        }

        return parseExpressionSuffixes (parseExpression (true, giveErrorOnTrailingLeftArrow));
    }

    AST::ExpressionList& parseExpressionList()
    {
        auto& result = create<AST::ExpressionList>();

        if (! skipIf (LexerToken::operator_closeParen))
        {
            for (;;)
            {
                auto& newItem = parseExpression();

                if (result.size() >= AST::maxInitialiserListLength)
                    throwError (newItem, Errors::initialiserListTooLong());

                result.items.addChildObject (newItem);

                if (skipIf (LexerToken::operator_closeParen))
                    break;

                expect (LexerToken::operator_comma);
            }
        }

        return result;
    }

    AST::Expression& parseDotOperator (AST::Object& expression)
    {
        auto dotContext = getContext();
        expect (LexerToken::operator_dot);
        auto& propertyOrMethodName = parseUnqualifiedIdentifier();

        auto& dot = allocate<AST::DotOperator> (dotContext);
        dot.lhs.setChildObject (expression);
        dot.rhs.setChildObject (propertyOrMethodName);

        return parseExpressionSuffixes (dot);
    }

    AST::Statement& parseReturn()
    {
        auto& ret = create<AST::ReturnStatement>();

        if (skipIf (LexerToken::operator_semicolon))
            return ret;

        ret.value.setChildObject (parseExpressionAsListIfParenthesised());
        expectSemicolon();
        return ret;
    }

    AST::Statement& parseIf()
    {
        auto& i = create<AST::IfStatement>();
        i.isConst = skipIf (LexerToken::keyword_const);
        expect (LexerToken::operator_openParen);
        i.condition.setChildObject (parseExpression());
        expectCloseParen();
        i.trueBranch.setChildObject (parseStatementAsBlock());

        if (skipIf (LexerToken::keyword_else))
            i.falseBranch.setChildObject (parseStatementAsBlock());

        return i;
    }

    AST::Statement& parseForwardBranch()
    {
        auto& b = create<AST::ForwardBranch>();
        expect (LexerToken::operator_openParen);
        b.condition.setChildObject (parseExpression());
        expect (LexerToken::operator_closeParen);
        expect (LexerToken::operator_rightArrow);

        expect (LexerToken::operator_openParen);

        while (! matches (LexerToken::operator_closeParen))
        {
            if (! b.targetBlocks.empty())
                expect (LexerToken::operator_comma);

            b.targetBlocks.addChildObject (parseUnqualifiedIdentifier());
        }

        expectCloseParen();
        expectSemicolon();

        return b;
    }

    AST::Expression& parseSubscriptWithBrackets (AST::Expression& lhs)
    {
        auto& suffix = create<AST::BracketedSuffix>();
        suffix.parent.setChildObject (lhs);

        for (;;)
        {
            auto& term = create<AST::BracketedSuffixTerm>();

            if (skipIf (LexerToken::operator_colon))
            {
                term.isRange = true;
                term.startIndex.createConstant (static_cast<int32_t> (0));

                if (! matchesAny (LexerToken::operator_closeBracket, LexerToken::operator_closeDoubleBracket))
                    term.endIndex.setChildObject (parseExpression());
            }
            else if (! matchesAny (LexerToken::operator_closeBracket, LexerToken::operator_closeDoubleBracket))
            {
                term.startIndex.setChildObject (parseExpression());

                if (skipIf (LexerToken::operator_colon))
                {
                    term.isRange = true;

                    if (! matchesAny (LexerToken::operator_closeBracket, LexerToken::operator_closeDoubleBracket))
                        term.endIndex.setChildObject (parseExpression());
                }
            }

            suffix.terms.addChildObject (term);

            if (skipIf (LexerToken::operator_comma))
                continue;

            if (matches (LexerToken::operator_closeDoubleBracket))
            {
                replaceCurrentToken (LexerToken::operator_closeBracket);
                break;
            }

            expect (LexerToken::operator_closeBracket);
            break;
        }

        return parseExpressionSuffixes (suffix);
    }

    AST::Expression& parseExpressionSuffixes (AST::Expression& expression)
    {
        if (matches (LexerToken::operator_dot))
            return parseDotOperator (expression);

        if (skipIf (LexerToken::operator_openParen))
        {
            auto& args = parseExpressionList();
            auto& cc = allocate<AST::CallOrCast> (expression.context);
            cc.functionOrType.setChildObject (expression);
            cc.arguments.setChildObject (args);
            return parseExpressionSuffixes (cc);
        }

        if (matches (LexerToken::operator_lessThan))    return parseVectorOrArrayTypeSuffixes (false, expression);
        if (skipIf (LexerToken::operator_openBracket))  return parseSubscriptWithBrackets (expression);
        if (skipIf (LexerToken::operator_plusplus))     return parsePostIncDec (expression, true);
        if (skipIf (LexerToken::operator_minusminus))   return parsePostIncDec (expression, false);

        if (matches (LexerToken::operator_doubleColon))
        {
            auto& join = create<AST::NamespaceSeparator>();
            skip();
            join.lhs.setChildObject (expression);
            join.rhs.setChildObject (parseIdentifier());
            return parseExpressionSuffixes (join);
        }

        return expression;
    }

    AST::Expression& parseVectorOrArrayTypeSuffixes (bool typesCanBeFollowedByIdentifier, AST::Expression& outerType)
    {
        auto startPos = getLexerPosition();
        auto startContext = getContext();

        if (! skipIf (LexerToken::operator_lessThan))
            return parseArrayTypeSuffixes (typesCanBeFollowedByIdentifier, outerType);

        AST::ObjectRefVector<AST::Expression> terms;

        if (tryToParseChevronSuffixExpression (terms))
        {
            if (skipIf (LexerToken::operator_greaterThan))
            {
                if (matches (LexerToken::operator_lessThan))
                    throwError (Errors::illegalTypeForVectorElement());

                auto& suffix = allocate<AST::ChevronedSuffix> (startContext);
                suffix.parent.setChildObject (outerType);

                for (auto& term : terms)
                    suffix.terms.addChildObject (term);

                return parseArrayTypeSuffixes (typesCanBeFollowedByIdentifier, suffix);
            }
        }

        setLexerPosition (startPos);
        return outerType;
    }

    AST::Expression& parseArrayTypeSuffixes (bool typesCanBeFollowedByIdentifier, AST::Expression& outerType)
    {
        if (skipIf (LexerToken::operator_openBracket))
            return parseArrayTypeSuffixes (typesCanBeFollowedByIdentifier, parseSubscriptWithBrackets (outerType));

        if (matches (LexerToken::operator_bitwiseAnd))
        {
            auto startPos = getLexerPosition();
            skip();

            if ((typesCanBeFollowedByIdentifier && matches (LexerToken::identifier))
                 || matchesAny (LexerToken::operator_dot, LexerToken::operator_closeParen,
                                LexerToken::operator_openBracket, LexerToken::operator_closeBracket,
                                LexerToken::operator_semicolon, LexerToken::operator_question, LexerToken::operator_rightArrow,
                                LexerToken::operator_comma, LexerToken::operator_openBrace, LexerToken::operator_closeBrace, LexerToken::eof))
                return parseArrayTypeSuffixes (typesCanBeFollowedByIdentifier, createConstOrRef (outerType.context, outerType, false, true));

            setLexerPosition (startPos);
            return outerType;
        }

        if (matches (LexerToken::operator_dot))
            return parseDotOperator (outerType);

        return outerType;
    }

    AST::Expression& parseType (bool typesCanBeFollowedByIdentifier)
    {
        auto type = tryToParseType (typesCanBeFollowedByIdentifier);

        if (type == nullptr)
            throwError (Errors::expectedType());

        if (matchesAny (LexerToken::operator_dot, LexerToken::operator_openParen))
            return parseExpressionSuffixes (*type);

        return *type;
    }

    ptr<AST::Expression> tryToParseType (bool typesCanBeFollowedByIdentifier)
    {
        auto c = getContext();

        if (skipIf (LexerToken::keyword_void))       return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::void_));
        if (skipIf (LexerToken::keyword_int))        return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::int32));
        if (skipIf (LexerToken::keyword_int32))      return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::int32));
        if (skipIf (LexerToken::keyword_int64))      return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::int64));
        if (skipIf (LexerToken::keyword_float))      return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::float32));
        if (skipIf (LexerToken::keyword_float32))    return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::float32));
        if (skipIf (LexerToken::keyword_float64))    return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::float64));
        if (skipIf (LexerToken::keyword_bool))       return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::boolean));
        if (skipIf (LexerToken::keyword_complex))    return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::complex32));
        if (skipIf (LexerToken::keyword_complex32))  return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::complex32));
        if (skipIf (LexerToken::keyword_complex64))  return parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::complex64));
        if (skipIf (LexerToken::keyword_string))     return parseArrayTypeSuffixes         (typesCanBeFollowedByIdentifier, allocate<AST::PrimitiveType> (c, AST::PrimitiveTypeEnum::Enum::string));

        if (skipIf (LexerToken::keyword_const))
        {
            auto& type = parseType (typesCanBeFollowedByIdentifier);

            if (auto mc = AST::castTo<AST::MakeConstOrRef> (type))
                if (mc->makeConst)
                    throwError (c, Errors::tooManyConsts());

            return createConstOrRef (c, type, true, false);
        }

        if (matches (LexerToken::identifier))
        {
            auto& type = parseVectorOrArrayTypeSuffixes (typesCanBeFollowedByIdentifier, parseQualifiedIdentifier());

            if (matchesAny (LexerToken::operator_dot, LexerToken::operator_openParen))
                return parseExpressionSuffixes (type);

            return type;
        }

        if (matches (LexerToken::operator_openParen))
            if (auto possibleType = tryToParseExpression())
                return *possibleType;

        if (matches (LexerToken::keyword_fixed))
            throwError (c, Errors::unimplementedFeature ("Fixed point support"));

        if (matches (LexerToken::keyword_double))
            throwError (c, Errors::useFloat64InsteadOfDouble());

        return {};
    }

    void parseEndpointTypeList (AST::EndpointDeclaration& decl, AST::EndpointTypeEnum::Enum endpointType)
    {
        if (skipIf (LexerToken::operator_openParen))
        {
            for (;;)
            {
                decl.dataTypes.addChildObject (parseType (true));

                if (skipIf (LexerToken::operator_closeParen))
                    break;

                expect (LexerToken::operator_comma);
            }
        }
        else
        {
            decl.dataTypes.addChildObject (parseType (true));
        }

        if (endpointType != AST::EndpointTypeEnum::Enum::event && decl.dataTypes.size() > 1)
            throwError (decl, Errors::noMultipleTypesOnEndpoint());
    }

    template <typename VariableCreatedFn>
    void parseVariableDeclarations (const AST::ObjectContext& c, AST::Expression& declaredType,
                                    AST::PooledString name, bool isExternal, AST::VariableTypeEnum::Enum variableType,
                                    ptr<AST::Comment> comment, VariableCreatedFn&& variableCreated)
    {
        ref<AST::VariableDeclaration> variable = allocate<AST::VariableDeclaration> (c);

        for (;;)
        {
            {
                if (name.empty())
                    name = readPooledIdentifier();

                variable->variableType = variableType;
                variable->name = name;
                variable->declaredType.setChildObject (declaredType);
                variable->isExternal = isExternal;
                variable->isConstant = isExternal;

                if (comment)
                {
                    variable->comment.referTo (comment);
                    comment = {};
                }

                if (skipIf (LexerToken::operator_assign))
                {
                    if (isExternal)
                        throwError (Errors::externalCannotHaveInitialiser());

                    auto& initialValue = parseExpressionAsListIfParenthesised();

                    if (AST::castToTypeBase (initialValue) != nullptr)
                        throwError (initialValue, Errors::expectedValue());

                    variable->initialValue.setChildObject (initialValue);
                }

                parseOptionalAnnotation (variable->annotation);

                variableCreated (variable);
            }

            if (skipIf (LexerToken::operator_comma))
            {
                variable = create<AST::VariableDeclaration>();
                name = {};
                continue;
            }

            if (matches (LexerToken::operator_closeParen))
                break;

            expectSemicolon();
            addBackwardsCommentIfPresent (variable.get());
            break;
        }
    }

    template <typename VariableCreatedFn>
    void parseVarOrLetDeclaration (bool isConst, AST::VariableTypeEnum::Enum variableType,
                                   ptr<AST::Comment> comment, VariableCreatedFn&& variableCreated)
    {
        for (;;)
        {
            auto& variable = create<AST::VariableDeclaration>();
            variable.variableType = variableType;
            variable.name = readPooledIdentifier();
            variable.isConstant = isConst;
            expect (LexerToken::operator_assign);
            variable.initialValue.setChildObject (parseExpression());

            if (comment)
            {
                variable.comment.referTo (comment);
                comment = {};
            }

            variableCreated (variable);

            if (! skipIf (LexerToken::operator_comma))
            {
                expectSemicolon();
                addBackwardsCommentIfPresent (variable);
                break;
            }
        }
    }

    void parseStateVarOrLet (bool isLet, ptr<AST::Comment> comment)
    {
        auto& list = getVariableList (*activeModule);
        parseVarOrLetDeclaration (isLet, AST::VariableTypeEnum::Enum::state, comment,
                                  [&] (AST::VariableDeclaration& v) { list.addChildObject (v); });
    }

    void parseLatencyAssignment()
    {
        auto& pp = parseProcessorProperty();

        if (pp.property != AST::ProcessorPropertyEnum::Enum::latency)
            throwError (pp, Errors::expectedFunctionOrVariable());

        expect (LexerToken::operator_assign);
        auto& value = parseExpression();
        expectSemicolon();

        if (auto p = activeModule->getAsProcessor())
        {
            if (p->latency != nullptr)
                throwError (pp, Errors::latencyAlreadyDeclared());

            p->latency.setChildObject (value);
            return;
        }

        throwError (pp, Errors::latencyOnlyForProcessor());
    }

    template <typename ObjectType>
    AST::Statement& parseBreakOrContinue()
    {
        auto& s = create<ObjectType>();
        skip();

        if (matches (LexerToken::identifier))
            s.targetBlock.referTo (parseUnqualifiedIdentifier());

        return matchEndOfStatement (s);
    }

    AST::Statement& parseForLoop (AST::PooledString labelName)
    {
        expect (LexerToken::operator_openParen);
        auto& loop = create<AST::LoopStatement>();
        loop.label = labelName;
        auto& bodyBlock = create<AST::ScopeBlock>();
        loop.body.setChildObject (bodyBlock);

        auto initialiserVariables = tryParsingVariableDeclaration (AST::VariableTypeEnum::Enum::local);

        if (matches (LexerToken::operator_closeParen))
        {
            if (initialiserVariables.empty())
            {
                expectSemicolon();
            }
            else
            {
                if (initialiserVariables.size() > 1)
                    throwError (initialiserVariables[1], Errors::expectedSingleVariableDeclaration());

                loop.numIterations.setChildObject (initialiserVariables.front());
                skip();
            }
        }
        else
        {
            for (auto& v : initialiserVariables)
                loop.initialisers.addChildObject (v);

            if (initialiserVariables.empty())
                if (! skipIf (LexerToken::operator_semicolon))
                    loop.initialisers.addChildObject (matchEndOfStatement (parseAssignmentOrEndpointWrite ({})));

            if (! skipIf (LexerToken::operator_semicolon))
                loop.condition.setChildObject (matchEndOfStatement (parseExpression()));

            if (! skipIf (LexerToken::operator_closeParen))
            {
                loop.iterator.setChildObject (parseAssignmentOrEndpointWrite ({}));
                expectCloseParen();
            }
        }

        if (skipIf (LexerToken::operator_openBrace))
        {
            while (! skipIf (LexerToken::operator_closeBrace))
                parseAndAddStatement (bodyBlock);
        }
        else
        {
            parseAndAddStatement (bodyBlock);
        }

        return loop;
    }

    AST::Statement& parseWhileLoop (AST::PooledString labelName)
    {
        auto& loop = create<AST::LoopStatement>();
        loop.label = labelName;

        expect (LexerToken::operator_openParen);
        loop.condition.setChildObject (parseExpression());
        expectCloseParen();
        loop.body.setChildObject (parseStatementAsBlock());

        return loop;
    }

    AST::Statement& parseLoopStatement (AST::PooledString labelName)
    {
        auto& loop = create<AST::LoopStatement>();
        loop.label = labelName;

        if (skipIf (LexerToken::operator_openParen))
        {
            loop.numIterations.setChildObject (parseExpression());
            expectCloseParen();
        }

        if (matchesAny (LexerToken::literalInt32, LexerToken::literalInt64, LexerToken::literalFloat64,
                        LexerToken::literalFloat32, LexerToken::literalString, LexerToken::operator_minus,
                        LexerToken::literalImag32, LexerToken::literalImag64,
                        LexerToken::operator_semicolon))
            throwError (Errors::expectedStatement());

        loop.body.setChildObject (parseStatementAsBlock());
        return loop;
    }

    void parseStaticAssert()
    {
        auto& assertion = create<AST::StaticAssertion>();
        activeModule->staticAssertions.addChildObject (assertion);
        skip();
        expect (LexerToken::operator_openParen);
        assertion.initialiseFromArgs (parseExpressionList().getAsObjectList());
        expectSemicolon();
    }

    static AST::ListProperty& getVariableList (AST::ModuleBase& m)
    {
        if (auto n = m.getAsNamespace())   return n->constants;

        auto p = m.getAsProcessorBase();
        CMAJ_ASSERT (p != nullptr);
        return p->stateVariables;
    }

    void expectSemicolon()    { expect (LexerToken::operator_semicolon); }
    void expectCloseParen()   { expect (LexerToken::operator_closeParen); }

    void checkForTrailingSemicolon()
    {
        if (matches (LexerToken::operator_semicolon))
            throwError (Errors::semicolonAfterBrace());
    }

    void failOnAssignmentToProcessorProperty()
    {
        if (skipIf (LexerToken::keyword_processor) && matches (LexerToken::operator_dot))
        {
            auto& pp = parseProcessorProperty();

            if (matches (LexerToken::operator_assign))
                throwError (Errors::cannotAssignToProcessorProperties());

            throwError (pp, Errors::expectedStatement());
        }
    }

    void failOnExternalKeyword()
    {
        if (matches (LexerToken::keyword_external))
            throwError (Errors::externalOnlyAllowedOnStateVars());
    }
};

} // namespace cmaj
