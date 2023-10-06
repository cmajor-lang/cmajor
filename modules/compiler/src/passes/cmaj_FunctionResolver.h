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

#pragma once

#include "../standard_library/cmaj_StandardLibrary.h"


namespace cmaj::passes
{

//==============================================================================
struct FunctionResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    FunctionResolver (AST::Program& p)
      : super (p), intrinsicsNamespace (findIntrinsicsNamespaceFromRoot (p.rootNamespace))
    {
    }

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::CallOrCast& cc) override
    {
        super::visit (cc);

        if (auto fn = AST::castToFunction (cc.functionOrType))
        {
            auto& call = replaceWithNewObject<AST::FunctionCall> (cc);
            call.targetFunction.referTo (*fn);

            if (auto list = AST::castTo<AST::ExpressionList> (cc.arguments))
                call.arguments.moveListItems (list->items);
            else
                call.arguments.addReference (cc.arguments.get());

            visit (call);
            return;
        }

        if (auto type = AST::castToTypeBase (cc.functionOrType))
        {
            auto& cast = cc.context.allocate<AST::Cast>();
            replaceObject (cc, cast);
            cast.targetType.createReferenceTo (type);

            if (auto list = AST::castTo<AST::ExpressionList> (cc.arguments))
                cast.arguments.moveListItems (list->items);
            else
                cast.arguments.addReference (cc.arguments.get());

            cast.onlySilentCastsAllowed = cast.arguments.size() > 1;

            AST::updateCastTypeSizeIfPossible (cast);
            return;
        }

        if (auto name = AST::castTo<AST::Identifier> (cc.functionOrType))
        {
            if (name->name.get() == "static_assert")
                return convertToStaticAssertion (cc);

            return performUnqualifiedNameSearch (cc, cc.functionOrType, name->name,
                                                 cc.arguments.getAsObjectList(), false);
        }

        if (auto separator = AST::castTo<AST::NamespaceSeparator> (cc.functionOrType))
            return performSearchInSpecificNamespace (cc, cc.functionOrType, cc.arguments.getAsObjectList(), *separator);

        // for an expression of the form a.b(c), see if we should convert it into
        // a method call of the form b(a, c)
        if (auto dot = AST::castTo<AST::DotOperator> (cc.functionOrType))
        {
            if (auto possibleFunctionName = AST::castTo<AST::Identifier> (dot->rhs))
            {
                auto& possibleTarget = dot->lhs.getObjectRef();

                auto shouldConvertToMethodCall = [&]
                {
                    if (auto leftValue = AST::castToSkippingReferences<AST::ValueBase> (possibleTarget))
                    {
                        auto args = cc.arguments.getAsObjectList();
                        args.insert (args.begin(), possibleTarget);

                        performUnqualifiedNameSearch (cc, dot->rhs, possibleFunctionName->name, args, true);

                        // return true if the search has resolved the dot's RHS into something else
                        return dot->rhs != nullptr && AST::castTo<AST::Identifier> (dot->rhs) == nullptr;
                    }

                    if (auto leftNode = AST::castToSkippingReferences<AST::GraphNode> (possibleTarget))
                    {
                        auto args = cc.arguments.getAsObjectList();
                        args.insert (args.begin(), possibleTarget);

                        performUnqualifiedNameSearch (cc, dot->rhs, possibleFunctionName->name, args, true);

                        // return true if the search has resolved the dot's RHS into something else
                        return dot->rhs != nullptr && AST::castTo<AST::Identifier> (dot->rhs) == nullptr;
                    }

                    if (possibleFunctionName->name.get() == "at")
                        if (AST::castToSkippingReferences<AST::ProcessorBase> (possibleTarget) != nullptr
                             || AST::castToSkippingReferences<AST::GraphNode> (possibleTarget) != nullptr
                             || AST::castToSkippingReferences<AST::EndpointDeclaration> (possibleTarget) != nullptr)
                            return true;

                    if (AST::ValueMetaFunctionTypeEnum::getEnums().getID (possibleFunctionName->name.get()) >= 0)
                        return true;

                    return false;
                };

                if (shouldConvertToMethodCall())
                {
                    cc.functionOrType.referTo (dot->rhs);

                    auto list = AST::castTo<AST::ExpressionList> (cc.arguments);
                    list->items.addChildObject (possibleTarget, 0);
                    visit (cc);
                    return;
                }
            }
        }

        registerFailure();
    }

    void visit (AST::FunctionCall& fc) override
    {
        super::visit (fc);

        if (fc.getTargetFunction() != nullptr)
            return;

        if (auto name = AST::castTo<AST::Identifier> (fc.targetFunction))
        {
            if (name->name.get() == "static_assert")
                return convertToStaticAssertion (fc);

            return performUnqualifiedNameSearch (fc, fc.targetFunction, name->name,
                                                 fc.arguments.getAsObjectList(), false);
        }

        if (auto separator = AST::castTo<AST::NamespaceSeparator> (fc.targetFunction))
            return performSearchInSpecificNamespace (fc, fc.targetFunction, fc.arguments.getAsObjectList(), *separator);

        registerFailure();
    }

    void visit (AST::BinaryOperator& op) override
    {
        super::visit (op);

        bool isModulo = op.op == AST::BinaryOpTypeEnum::Enum::modulo;

        if (isModulo || op.op == AST::BinaryOpTypeEnum::Enum::exponent)
        {
            if (auto types = op.getOperatorTypes())
            {
                if (types.operandType.isPrimitiveFloat() && intrinsicsNamespace != nullptr)
                {
                    if (auto lhs = AST::castToValue (op.lhs))
                    {
                        if (auto rhs = AST::castToValue (op.rhs))
                        {
                            auto& call = replaceWithNewObject<AST::FunctionCall> (op);
                            auto path = std::vector<std::string_view> { "std", "intrinsics", isModulo ? "fmod" : "pow" };
                            call.targetFunction.referTo (AST::createIdentifierPath (call.context, path));
                            call.arguments.addChildObject (AST::createCastIfNeeded (types.operandType, *lhs));
                            call.arguments.addChildObject (AST::createCastIfNeeded (types.operandType, *rhs));
                        }
                    }
                }
            }
        }
    }

    //==============================================================================
    struct MatchingFunctionList
    {
        struct Match
        {
            Match() = delete;
            Match (Match&&) = default;
            Match& operator= (Match&&) = default;

            Match (AST::Function& f, const AST::FunctionArgumentListInfo& args)  : function (f)
            {
                for (auto& param : f.iterateParameters())
                    paramTypes.push_back (param.getType());

                for (size_t i = 0; i < args.argInfo.size(); ++i)
                {
                    auto paramType = paramTypes[i];
                    auto& arg = args.argInfo[i];

                    if (paramType == nullptr || ! paramType->isResolved() || ! arg.type.isResolved())
                    {
                        if (f.isGenericOrParameterised())
                            requiresGeneric = true;
                        else
                            functionIsNotResolved = true;

                        continue;
                    }

                    bool sourceIsReferenceable = arg.value.getSourceVariable() != nullptr;

                    auto suitability = AST::TypeRules::getArgumentSuitability (*paramType, arg.type, sourceIsReferenceable);

                    if (suitability == AST::TypeRules::ArgumentSuitability::perfect)
                        continue;

                    if (suitability == AST::TypeRules::ArgumentSuitability::constnessDifference)
                    {
                        requiresConstCast = true;
                        continue;
                    }

                    if (suitability == AST::TypeRules::ArgumentSuitability::impossible)
                        if (arg.constant == nullptr || ! paramType->isBoundedType() || ! AST::TypeRules::canSilentlyCastTo (*paramType, *arg.constant))
                            isImpossible = true;

                    requiresCast = true;
                }

                isExactMatch = ! (isImpossible || requiresCast || requiresGeneric);
            }

            ref<AST::Function> function;
            choc::SmallVector<ptr<const AST::TypeBase>, 8> paramTypes;

            bool isImpossible = false,
                 isExactMatch = false,
                 requiresCast = false,
                 requiresConstCast = false,
                 requiresGeneric = false,
                 functionIsNotResolved = false;
        };

        AST::FunctionArgumentListInfo argInfo;
        choc::SmallVector<Match, 4> matches;

        template <typename Predicate>
        size_t countMatches (Predicate&& pred) const    { return static_cast<size_t> (std::count_if (matches.begin(), matches.end(), pred)); }

        size_t getTotalNumMatches() const               { return matches.size(); }
        size_t getNumberOfPossibleMatches() const       { return countMatches ([=] (const auto& f) { return ! f.isImpossible; }); }
        size_t getNumberOfExactMatches() const          { return countMatches ([=] (const auto& f) { return f.isExactMatch; }); }
        size_t getNumberOfMatchesWithCast() const       { return countMatches ([=] (const auto& f) { return f.requiresCast && ! f.isImpossible; }); }

        AST::ObjectRefVector<AST::Function> getFunctions() const
        {
            AST::ObjectRefVector<AST::Function> list;

            for (auto& m : matches)
                list.push_back (m.function);

            return list;
        }

        bool areAnyFunctionsUnresolved() const
        {
            for (auto& m : matches)
                if (m.functionIsNotResolved)
                    return true;

            return false;
        }

        ptr<const Match> getSingleExactMatch() const
        {
            // Slightly complicated logic in here so that if there are several exact matches, but
            // only one of them has no const ref cast needed, then we'll allow that to be the result
            ptr<const Match> found;

            for (auto& m : matches)
            {
                if (m.isExactMatch && ! m.requiresConstCast)
                {
                    if (found != nullptr)
                        return {};

                    found = m;
                }
            }

            if (found == nullptr)
            {
                for (auto& m : matches)
                {
                    if (m.isExactMatch)
                    {
                        if (found != nullptr)
                            return {};

                        found = m;
                    }
                }
            }

            return found;
        }

        ptr<const Match> getSinglePossibleMatch() const
        {
            ptr<const Match> found;

            for (auto& m : matches)
            {
                if (! m.isImpossible)
                {
                    if (found != nullptr)
                        return {}; // more than one match is a fail

                    found = m;
                }
            }

            return found;
        }

        bool populate (AST::Expression& call, AST::PooledString functionName, choc::span<ref<AST::Object>> args,
                       ptr<AST::Namespace> intrinsics, bool isMethodCall, bool couldBeIntrinsic)
        {
            auto parentObject = call.getParentScope();

            if (parentObject == nullptr)
                return false;

            if (auto statement = parentObject->findSelfOrParentOfType<AST::Statement>())
                return populate (functionName, statement->getParentScopeRef(), statement,
                                 args, intrinsics, isMethodCall, couldBeIntrinsic, false);

            return populate (functionName, call, {}, args, intrinsics,
                             isMethodCall, couldBeIntrinsic, false);
        }

        bool populate (AST::PooledString functionName, AST::Object& searchScope, ptr<AST::Statement> searchUpTo,
                       choc::span<ref<AST::Object>> args, ptr<AST::Namespace> intrinsics,
                       bool isMethodCall, bool couldBeIntrinsic, bool searchOnlySpecifiedScope)
        {
            AST::NameSearch search;
            search.nameToFind                   = functionName;
            search.stopAtFirstScopeWithResults  = true;
            search.findVariables                = false;
            search.findTypes                    = false;
            search.findFunctions                = true;
            search.findNamespaces               = false;
            search.findProcessors               = false;
            search.findNodes                    = false;
            search.findEndpoints                = false;
            search.onlyFindLocalVariables       = false;
            search.searchOnlySpecifiedScope     = searchOnlySpecifiedScope;
            search.requiredNumFunctionParams    = static_cast<int> (args.size());

            search.performSearch (searchScope, searchUpTo);

            if (couldBeIntrinsic)
            {
                if (intrinsics == nullptr)
                    return false;

                // If it's an unqualified name, also search for intrinsics
                search.searchOnlySpecifiedScope = true;
                search.performSearch (*intrinsics, nullptr);

                // "Koenig" lookup
                if (isMethodCall)
                {
                    CMAJ_ASSERT (! args.empty());

                    auto firstArg = AST::castToValue (args.front());

                    if (firstArg == nullptr)
                        return false;

                    auto firstArgType = firstArg->getResultType();

                    if (firstArgType == nullptr)
                        return false;

                    if (auto firstArgStructType = firstArgType->skipConstAndRefModifiers().getAsStructType())
                    {
                        search.nameToFind = functionName;
                        search.performSearch (const_cast<AST::StructType&> (*firstArgStructType), nullptr);
                    }
                }
            }

            if (! argInfo.populate (args))
                return false;

            for (auto& i : search.itemsFound)
                if (auto f = i->getAsFunction())
                    if (! f->isSpecialisedGeneric())
                        matches.push_back (Match (*f, argInfo));

            return true;
        }
    };

    //==============================================================================
    void performUnqualifiedNameSearch (AST::Expression& call, AST::Object& nameToResolve,
                                       AST::PooledString functionName, choc::span<ref<AST::Object>> args,
                                       bool isMethodCall)
    {
        MatchingFunctionList matches;

        if (intrinsicsNamespace == nullptr
            || ! matches.populate (call, functionName, args, *intrinsicsNamespace, isMethodCall, true))
        {
            if (functionName == "at")
                if (resolveAtCallForProcessorForProcessorOrEndpoint (call, args))
                    return;

            if (resolveMetaFunction<AST::ValueMetaFunction, AST::ValueMetaFunctionTypeEnum, false> (call, functionName, args, false))
                return;

            if (resolveMetaFunction<AST::TypeMetaFunction, AST::TypeMetaFunctionTypeEnum, true> (call, functionName, args, false))
                return;

            if (functionName != "advance" && functionName != "reset")
            {
                registerFailure();
                return;
            }
        }

        handleSearchResults (matches, call, nameToResolve, functionName, args, isMethodCall, true);
    }

    void performSearchInSpecificNamespace (AST::Expression& call, AST::Object& nameToResolve,
                                           choc::span<ref<AST::Object>> args, AST::NamespaceSeparator& separator)
    {
        if (auto module = AST::castToSkippingReferences<AST::ModuleBase> (separator.lhs))
        {
            if (auto name = AST::castTo<AST::Identifier> (separator.rhs))
            {
                MatchingFunctionList matches;

                if (matches.populate (name->name, *module, {}, args, {}, false, false, true))
                    return handleSearchResults (matches, call, nameToResolve, name->name, args, false, false);
           }
        }

        registerFailure();
    }

    void handleSearchResults (MatchingFunctionList& matches,
                              AST::Expression& call, AST::Object& nameToResolve,
                              AST::PooledString functionName, choc::span<ref<AST::Object>> args,
                              bool isMethodCall, bool couldBeIntrinsic)
    {
        // If there's only one that could possibly work, then try it
        if (auto onlyPossibleMatch = matches.getSinglePossibleMatch())
            return resolveFunctionCall (call, nameToResolve, *onlyPossibleMatch, matches.argInfo, ! throwOnErrors);

        // If we get an exact match, then ignore any others
        if (auto exactMatch = matches.getSingleExactMatch())
            return resolveFunctionCall (call, nameToResolve, *exactMatch, matches.argInfo, ! throwOnErrors);

        // Try to specialise all the generic functions to reach a final list of possibilities
        for (auto& m : matches.matches)
        {
            if (m.requiresGeneric && ! m.isImpossible)
            {
                if (auto resolved = getResolvedGenericForCall (call, nameToResolve, m, matches.argInfo, true))
                    m = MatchingFunctionList::Match (*resolved, matches.argInfo);
                else
                    m.isImpossible = true;
            }
        }

        // If there's only one that could possibly work, then try it
        if (auto onlyPossibleMatch = matches.getSinglePossibleMatch())
            return resolveFunctionCall (call, nameToResolve, *onlyPossibleMatch, matches.argInfo, ! throwOnErrors);

        // Now check again to see if there's now just one possible match
        if (auto exactMatch = matches.getSingleExactMatch())
            return resolveFunctionCall (call, nameToResolve, *exactMatch, matches.argInfo, ! throwOnErrors);

        if (couldBeIntrinsic)
        {
            if (functionName == "at")
                return resolveAtCall (call, args);

            if (functionName == "advance")
            {
                if (args.size() == 1)
                {
                    auto& advance = call.context.allocate<AST::Advance>();

                    advance.node.referTo (args.front());
                    replaceObject (call, advance);
                    return;
                }

                if (auto cc = call.getAsCallOrCast())    return resolveAdvanceCall (*cc, cc->arguments);
                if (auto fc = call.getAsFunctionCall())  return resolveAdvanceCall (*fc, fc->arguments);
            }

            if (functionName == "reset")
            {
                if (args.size() >= 1)
                {
                    if (auto graphNode = AST::castTo<AST::GraphNode> (args.front()))
                    {
                        if (args.size() > 1)
                        {
                            if (registerFailure())
                                throwError (call, Errors::resetWrongArguments());

                            return;
                        }

                        auto& reset = call.context.allocate<AST::Reset>();

                        reset.node.referTo (*graphNode);
                        replaceObject (call, reset);
                        return;
                    }
                }
            }

            if (resolveMetaFunction<AST::ValueMetaFunction, AST::ValueMetaFunctionTypeEnum, false> (call, functionName, args, isMethodCall))
                return;

            if (resolveMetaFunction<AST::TypeMetaFunction, AST::TypeMetaFunctionTypeEnum, true> (call, functionName, args, isMethodCall))
                return;
        }

        if (registerFailure())
            throwOnFailure (matches, call, functionName, nameToResolve, args.size());
    }

    void resolveFunctionCall (AST::Expression& call, AST::Object& nameToResolve, const MatchingFunctionList::Match& functionToUse,
                              const AST::FunctionArgumentListInfo& argInfo, bool ignoreErrorsInGenerics)
    {
        auto fn = functionToUse.function;

        if (fn->isMainFunction() || fn->isUserInitFunction() || fn->isEventHandler)
            throwError (AST::getContextOfStartOfExpression (nameToResolve), Errors::cannotCallFunction (fn->name.get()));

        fn = ModuleSpecialiser::getFunctionWithSpecialisedParentModules (program, call.context, fn, throwOnErrors);

        if (fn->getParentModule().isAnyParentParameterised())
        {
            registerFailure();
            return;
        }

        if (fn->isGenericOrParameterised())
        {
            if (auto resolvedFn = getResolvedGenericForCall (call, nameToResolve, functionToUse, argInfo, ignoreErrorsInGenerics))
                replaceObject (nameToResolve, *resolvedFn);
            else
                registerFailure();

            return;
        }

        replaceObject (nameToResolve, fn);
    }

    ptr<AST::Function> getResolvedGenericForCall (AST::Expression& call, AST::Object& nameToResolve,
                                                  const MatchingFunctionList::Match& functionToUse,
                                                  const AST::FunctionArgumentListInfo& argInfo, bool ignoreErrors)
    {
        AST::ObjectRefVector<const AST::Expression> resolvedWildcards;

        if (resolveArgTypesForGenericFunction (call, nameToResolve, functionToUse, argInfo, resolvedWildcards, ignoreErrors))
            return findOrCreateSpecialisedFunction (call, functionToUse, resolvedWildcards);

        return {};
    }

    static AST::Function& findOrCreateSpecialisedFunction (AST::Expression& call, const MatchingFunctionList::Match& genericFunctionInfo,
                                                           choc::span<ref<const AST::Expression>> resolvedWildcards)
    {
        auto& genericFn = genericFunctionInfo.function.get();

        AST::SignatureBuilder sig;
        sig << genericFn << AST::getSpecialisedFunctionSuffix();

        for (auto& w : resolvedWildcards)
            sig << w;

        auto specialisedFunctionName = sig.toString (50);

        auto& parentModule = genericFn.getParentModule();

        if (auto existing = parentModule.findFunction (specialisedFunctionName, genericFn.parameters.size()))
            return *existing;

        auto& specialisedFn = parentModule.context.allocator.createDeepClone (genericFn);
        specialisedFn.name = specialisedFn.getStringPool().get (specialisedFunctionName);
        specialisedFn.originalGenericFunction.referTo (genericFn);
        specialisedFn.originalCallLeadingToSpecialisation.referTo (call);

        CMAJ_ASSERT (resolvedWildcards.size() == specialisedFn.genericWildcards.size());

        for (size_t i = 0; i < resolvedWildcards.size(); ++i)
        {
            auto& resolvedWildcard = resolvedWildcards[i];
            auto& wildcardToResolve = AST::castToRef<AST::Identifier> (specialisedFn.genericWildcards[i]);

            if (resolvedWildcard->getAsTypeBase())
            {
                auto& alias = wildcardToResolve.context.allocate<AST::Alias>();
                alias.name = wildcardToResolve.name;
                alias.target.createReferenceTo (resolvedWildcard.get());
                specialisedFn.getMainBlock()->addStatement (alias, 0);
            }
            else if (resolvedWildcard->getAsValueBase())
            {
                auto& constant = wildcardToResolve.context.allocate<AST::VariableDeclaration>();
                constant.name = wildcardToResolve.name;
                constant.initialValue.createReferenceTo (resolvedWildcard.get());
                constant.variableType = AST::VariableTypeEnum::Enum::local;
                constant.isConstant = true;
                specialisedFn.getMainBlock()->addStatement (constant, 0);
            }
        }

        specialisedFn.genericWildcards.reset();

        if (auto returnType = specialisedFn.returnType.getObject())
        {
            if (AST::castToTypeBase (returnType) == nullptr)
            {
                auto& removeConst = returnType->context.allocate<AST::TypeMetaFunction>();
                removeConst.op = AST::TypeMetaFunctionTypeEnum::Enum::removeConst;
                removeConst.source.setChildObject (*returnType);
                specialisedFn.returnType.setChildObject (removeConst);
            }
        }

        parentModule.functions.addChildObject (specialisedFn);
        return specialisedFn;
    }

    bool resolveArgTypesForGenericFunction (AST::Expression& call, const AST::Object& nameToResolve,
                                            const MatchingFunctionList::Match& functionToUse,
                                            const AST::FunctionArgumentListInfo& argInfo,
                                            AST::ObjectRefVector<const AST::Expression>& resolvedWildcards,
                                            bool shouldIgnoreErrors)
    {
        auto& genericFn = functionToUse.function.get();
        choc::SmallVector<AST::PooledString, 8> wildcardNamesUsed;

        for (auto& wildcard : genericFn.genericWildcards)
        {
            auto& wildcardIdentifier = AST::castToRef<AST::Identifier> (wildcard);
            auto wildcardName = wildcardIdentifier.name.toString();

            if (wildcardNamesUsed.contains (wildcardName))
                throwError (wildcard, Errors::nameInUse (wildcardName));

            wildcardNamesUsed.push_back (wildcardName);

            AST::ObjectRefVector<const AST::Expression> matchesFound;

            for (size_t i = 0; i < genericFn.parameters.size(); ++i)
            {
                auto& paramVariable = AST::castToVariableDeclarationRef (genericFn.parameters[i]);
                auto& paramType = AST::castToExpressionRef (paramVariable.declaredType);
                findWildcardMatches (matchesFound, paramType, argInfo.argInfo[i].type, wildcardName);
            }

            ptr<const AST::Expression> resolvedWildcard;

            for (auto& w : matchesFound)
            {
                if (auto type = w->getAsTypeBase())
                    if (! type->isReference())
                        w = type->skipConstAndRefModifiers();

                if (resolvedWildcard != nullptr)
                {
                    bool isSame = false;

                    if (auto type = w->getAsTypeBase())
                    {
                        if (auto otherType = resolvedWildcard->getAsTypeBase())
                            isSame = type->isSameType (*otherType, AST::TypeBase::ComparisonFlags::failOnAllDifferences);
                    }
                    else
                    {
                        if (auto value = w->getAsValueBase())
                            isSame = value->isIdentical (*resolvedWildcard);
                    }

                    if (! isSame)
                    {
                        if (! shouldIgnoreErrors && registerFailure())
                            throwGenericFunctionResolutionError (call, nameToResolve, functionToUse,
                                                                 Errors::cannotResolveGenericWildcard (wildcardName)
                                                                    .withContext (wildcardIdentifier));

                        return false;
                    }
                }
                else
                {
                    resolvedWildcard = w.get();
                }
            }

            if (resolvedWildcard != nullptr)
            {
                if (auto type = resolvedWildcard->getAsTypeBase())
                {
                    if (type->isResolved())
                    {
                        resolvedWildcards.push_back (type->skipConstAndRefModifiers());
                        continue;
                    }
                }

                if (auto value = resolvedWildcard->getAsValueBase())
                {
                    resolvedWildcards.push_back (*value);
                    continue;
                }
            }

            if (! shouldIgnoreErrors && registerFailure())
                throwGenericFunctionResolutionError (call, nameToResolve, functionToUse,
                                                        Errors::cannotResolveGenericParameter (wildcardName)
                                                        .withLocation (wildcardIdentifier.context.getFullLocation()));
            return false;
        }

        return true;
    }

    void findWildcardMatches (AST::ObjectRefVector<const AST::Expression>& results,
                              const AST::Expression& paramType,
                              const AST::TypeBase& callerArgumentType,
                              AST::PooledString wildcardToFind)
    {
        if (auto unresolvedIdentifier = paramType.getAsIdentifier())
        {
            if (unresolvedIdentifier->hasName (wildcardToFind))
                results.push_back (callerArgumentType.skipConstAndRefModifiers());

            return;
        }

        if (auto makeConstOrRef = paramType.getAsMakeConstOrRef())
        {
            ptr<const AST::TypeBase> argType = callerArgumentType;

            if (auto argConstRef = callerArgumentType.getAsMakeConstOrRef())
            {
                auto& argWithoutConstRef = *argConstRef->getSource();

                if (makeConstOrRef->makeConst != argConstRef->makeConst
                     || makeConstOrRef->makeRef != argConstRef->makeRef)
                {
                    if (makeConstOrRef->makeConst == argConstRef->makeConst
                          && makeConstOrRef->makeRef == argConstRef->makeRef)
                    {
                        argType = argWithoutConstRef;
                    }
                    else
                    {
                        auto& newModifier = callerArgumentType.context.allocate<AST::MakeConstOrRef>();
                        newModifier.source.referTo (argWithoutConstRef);
                        newModifier.makeConst = argConstRef->makeConst && ! makeConstOrRef->makeConst;
                        newModifier.makeRef   = argConstRef->makeRef   && ! makeConstOrRef->makeRef;
                        argType = newModifier;
                    }
                }
            }

            return findWildcardMatches (results, AST::castToExpressionRef (makeConstOrRef->source), *argType, wildcardToFind);
        }

        if (auto brackets = paramType.getAsBracketedSuffix())
        {
            if (auto arrayArg = callerArgumentType.getAsArrayType())
            {
                if (auto elementType = arrayArg->getInnermostElementType())
                {
                    if (arrayArg->getNumDimensions() == brackets->terms.size()
                        || (brackets->terms.empty() && arrayArg->getNumDimensions() == 1))
                    {
                        uint32_t termIndex = 0;

                        for (auto& term : brackets->terms.iterateAs<AST::BracketedSuffixTerm>())
                        {
                            if (term.endIndex == nullptr && term.startIndex != nullptr)
                                if (term.startIndex.hasName (wildcardToFind) && term.startIndex.getObjectRef().isIdentifier())
                                    if (arrayArg->isFixedSizeArray())
                                        if (auto argSize = arrayArg->getConstantSize (termIndex))
                                            results.push_back (*argSize);

                            ++termIndex;
                        }

                        findWildcardMatches (results, AST::castToExpressionRef (brackets->parent), *elementType, wildcardToFind);
                    }
                }
            }

            return;
        }

        if (auto chevrons = paramType.getAsChevronedSuffix())
        {
            if (auto vectorArg = callerArgumentType.getAsVectorType())
            {
                if (vectorArg->getNumDimensions() == chevrons->terms.size())
                {
                    auto& term = chevrons->terms[0].getObjectRef();

                    if (term.hasName (wildcardToFind) && term.isIdentifier())
                        if (auto argSize = AST::getAsFoldedConstant (vectorArg->numElements))
                            results.push_back (*argSize);

                    findWildcardMatches (results, AST::castToExpressionRef (chevrons->parent),
                                        vectorArg->getElementType(), wildcardToFind);
                }
            }
        }
    }

    template <typename MetaFunction, typename MetaFunctionEnum, bool isType>
    bool resolveMetaFunction (AST::Expression& call, std::string_view name, choc::span<ref<AST::Object>> args, bool isMethodCall)
    {
        (void) isMethodCall;

        auto opID = MetaFunctionEnum::getEnums().getID (name);

        if (opID < 0 || args.size() == 0)
            return false;

        auto& mf = call.context.allocate<MetaFunction>();
        mf.op.setID (opID);

        if constexpr (isType)
        {
            mf.source.setChildObject (args.front());

            if (args.size() > 1 || isMethodCall)
            {
                // if we end up with multiple args, it probably means we've got a constructor like "something.type(123)"
                auto& cast = replaceWithNewObject<AST::Cast> (call);
                cast.onlySilentCastsAllowed = false;
                cast.targetType.createReferenceTo (mf);

                for (size_t i = 1; i < args.size(); ++i)
                    cast.arguments.addChildObject (args[i]);

                return true;
            }
        }
        else
        {
            for (auto& arg : args)
                mf.arguments.addChildObject (arg);
        }

        replaceObject (call, mf);
        return true;
    }

    void throwOnFailure (const MatchingFunctionList& matches,
                         AST::Expression& call, AST::PooledString name,
                         const AST::Object& nameObject, size_t numArgs)
    {
        if (matches.getTotalNumMatches() == 0)
            throwForUnknownName (call, name, nameObject, numArgs);

        if (matches.getNumberOfExactMatches() + matches.getNumberOfMatchesWithCast() == 0)
        {
            if (matches.getTotalNumMatches() == 1 && ! matches.matches.front().requiresGeneric)
            {
                auto& paramTypes = matches.matches.front().paramTypes;
                CMAJ_ASSERT (paramTypes.size() == numArgs);

                for (size_t i = 0; i < paramTypes.size(); ++i)
                {
                    auto& arg = matches.argInfo.argInfo[i];

                    if (paramTypes[i]->isNonConstReference() && arg.value.getSourceVariable() == nullptr)
                        throwError (arg.object.context, Errors::cannotPassConstAsNonConstRef());

                    validation::expectCastPossible (arg.object.context, *paramTypes[i], arg.type, true);
                }
            }
        }

        if (matches.getNumberOfPossibleMatches() > 1)
            throwError (call, Errors::ambiguousFunctionCall (AST::getFunctionCallDescription (name, call)));

        throwError (call, Errors::noMatchForFunctionCall (AST::getFunctionCallDescription (name, call)));
    }

    void throwForUnknownName (AST::Expression& call, AST::PooledString functionName,
                              const AST::Object& nameObject, size_t numArgs)
    {
        AST::NameSearch search;
        search.nameToFind                   = functionName;
        search.stopAtFirstScopeWithResults  = true;
        search.findVariables                = true;
        search.findTypes                    = true;
        search.findFunctions                = true;
        search.findNamespaces               = true;
        search.findProcessors               = true;
        search.findNodes                    = false;
        search.findEndpoints                = true;

        search.performSearch (call.getParentScopeRef(), nullptr);

        search.searchOnlySpecifiedScope = true;

        if (intrinsicsNamespace != nullptr)
            search.performSearch (*intrinsicsNamespace, nullptr);

        size_t numFunctions = 0, numWithCorrectNumArgs = 0;

        for (auto& i : search.itemsFound)
        {
            if (auto f = i->getAsFunction())
            {
                ++numFunctions;

                if (f->getNumParameters() == numArgs)
                    ++numWithCorrectNumArgs;
            }
        }

        auto& nameLocation = AST::getContextOfStartOfExpression (nameObject);
        auto name = AST::print (nameObject);

        if (numFunctions > 0 && numWithCorrectNumArgs == 0)
            throwError (nameLocation, Errors::noFunctionWithNumberOfArgs (name, std::to_string (numArgs)));

        if (! search.itemsFound.empty())
        {
            if (search.itemsFound.front()->getAsProcessorBase() != nullptr)
                throwError (nameLocation, Errors::cannotUseProcessorAsFunction());

            if (auto e = search.itemsFound.front()->getAsEndpointDeclaration())
                throwError (nameLocation, e->isInput ? Errors::cannotUseInputAsFunction()
                                                     : Errors::cannotUseOutputAsFunction());
        }

        auto possibleFunction = findMisspeltFunctionSuggestion (call, functionName).fullPath;

        if (! possibleFunction.empty())
            throwError (nameLocation, Errors::unknownFunctionWithSuggestion (name, possibleFunction));

        throwError (nameLocation, Errors::unknownFunction (name));
    }

    IdentifierPath findMisspeltFunctionSuggestion (AST::Expression& call, std::string_view functionName)
    {
        IdentifierPath nearest;
        size_t lowestDistance = 5;

        auto topLevelScope = call.findParentModule();

        for (;;)
        {
            if (auto parent = topLevelScope->findParentModule())
                topLevelScope = parent;
            else
                break;
        }

        searchForSimilarNames (*topLevelScope, functionName, nearest, lowestDistance);

        nearest = nearest.withoutTopLevelNameIfPresent (call.getStrings().rootNamespaceName);
        nearest = nearest.withoutTopLevelNameIfPresent (getIntrinsicsNamespaceFullName());

        return nearest;
    }

    static void searchForSimilarNames (AST::ModuleBase& module, std::string_view targetName, IdentifierPath& nearest, size_t& lowestDistance)
    {
        for (auto& f : module.functions.iterateAs<AST::Function>())
        {
            auto functionName = f.name.toString();
            auto distance = choc::text::getLevenshteinDistance (targetName, functionName.get());

            if (distance < lowestDistance)
            {
                lowestDistance = distance;
                nearest = module.getFullyQualifiedName() + IdentifierPath (functionName);
            }
        }

        if (auto ns = module.getAsNamespace())
            for (auto& sub : ns->subModules.iterateAs<AST::ModuleBase>())
                searchForSimilarNames (sub, targetName, nearest, lowestDistance);
    }

    void throwGenericFunctionResolutionError (const AST::Expression& call,
                                              const AST::Object& nameToResolve,
                                              const MatchingFunctionList::Match& functionToUse,
                                              DiagnosticMessage failureMessage)
    {
        auto callDescription = AST::getFunctionCallDescription (functionToUse.function->getName(), call);
        auto& errorContext = AST::getContextOfStartOfExpression (nameToResolve);

        if (functionToUse.function->getParentModule().isSystemModule())
            throwError (errorContext, Errors::cannotResolveGenericArgs (callDescription));

        errorContext.emitMessage (Errors::cannotResolveGenericFunction (callDescription));
        throwError (failureMessage);
    }

    void resolveAdvanceCall (AST::Expression& call, AST::Property& arguments)
    {
        if (! arguments.getAsObjectList().empty())
            throwError (call, Errors::advanceHasNoArgs());

        replaceObject (call, call.context.allocate<AST::Advance>());
    }

    void resolveAtCall (AST::Expression& call, choc::span<ref<AST::Object>> args)
    {
        auto& at = replaceWithNewObject<AST::GetElement> (call);
        at.parent.referTo (args[0].get());

        for (size_t i = 1; i < args.size(); ++i)
            at.indexes.addReference (args[i].get());

        at.isAtFunction = true;
    }

    bool resolveAtCallForProcessorForProcessorOrEndpoint (AST::Expression& call, choc::span<ref<AST::Object>> args)
    {
        if (args.size() == 2)
        {
            auto& target = args.front();

            if (AST::castToSkippingReferences<AST::Processor> (target) != nullptr
                 || AST::castToSkippingReferences<AST::GraphNode> (target) != nullptr
                 || AST::castToSkippingReferences<AST::EndpointDeclaration> (target) != nullptr
                 || AST::castToSkippingReferences<AST::EndpointInstance> (target) != nullptr)
            {
                auto& b = replaceWithNewObject<AST::BracketedSuffix> (call);
                b.parent.referTo (args[0].get());
                auto& term = b.allocateChild<AST::BracketedSuffixTerm>();
                term.startIndex.referTo (args[1].get());
                b.terms.addChildObject (term);
                return true;
            }
        }

        return false;
    }

    template <typename CallType>
    void convertToStaticAssertion (CallType& cc)
    {
        auto& assertion = replaceWithNewObject<AST::StaticAssertion> (cc);
        assertion.initialiseFromArgs (cc.arguments.getAsObjectList());
    }

    ptr<AST::Namespace> intrinsicsNamespace;
};


}
