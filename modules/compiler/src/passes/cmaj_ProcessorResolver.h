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

namespace cmaj::passes
{

//==============================================================================
struct ProcessorResolver  : public PassAvoidingGenericFunctionsAndModules
{
    using super = PassAvoidingGenericFunctionsAndModules;
    using super::visit;

    ProcessorResolver (AST::Program& p) : super (p) {}

    CMAJ_DO_NOT_VISIT_CONSTANTS

    void visit (AST::DotOperator& dot) override
    {
        super::visit (dot);

        if (auto p = resolveGraphNode (dot.lhs, dot.lhs))
            dot.lhs.referTo (*p);
    }

    ptr<AST::GraphNode> resolveGraphNode (AST::Object& mainObject, AST::ObjectProperty& node)
    {
        if (auto n = AST::castToSkippingReferences<AST::GraphNode> (node))
            return n;

        if (auto p = AST::castToSkippingReferences<AST::ProcessorBase> (node))
        {
            if (auto conn = mainObject.findParentOfType<AST::Connection>())
            {
                auto& parentModule = conn->getParentModule();

                if (auto parentGraph = parentModule.getAsGraph())
                    return getOrCreateGraphNode (AST::getContext (node), *parentGraph, *p);
            }

            if (auto writeToEndpoint = mainObject.findParentOfType<AST::WriteToEndpoint>())
            {
                auto& parentModule = writeToEndpoint->getParentModule();

                if (auto parentGraph = parentModule.getAsGraph())
                    return getOrCreateGraphNode (AST::getContext (node), *parentGraph, *p);
            }

            if (mainObject.findParentOfType<AST::EndpointDeclaration>() != nullptr)
                return {};

            if (registerFailure())
            {
                if (mainObject.getAsCallOrCast() != nullptr)
                    throwError (mainObject, Errors::cannotUseProcessorAsFunction());

                throwError (mainObject, Errors::noSuchOperationOnProcessor());
            }
        }

        return {};
    }

    ptr<AST::GraphNode> getOrCreateGraphNode (const AST::ObjectContext& declarationContext,
                                              AST::Graph& parentGraph,
                                              AST::ProcessorBase& processorType)
    {
        for (auto& n : parentGraph.nodes)
        {
            auto& node = AST::castToRefSkippingReferences<AST::GraphNode> (n);
            auto nodeType = node.getProcessorType();

            if (nodeType == nullptr)
                return {};

            if (nodeType.get() == std::addressof (processorType))
            {
                if (node.isImplicitlyCreated())
                {
                    if (processorType.isSpecialised())
                        throwError (declarationContext, Errors::cannotReuseImplicitNode());
                }
                else
                {
                    throwError (declarationContext, Errors::cannotUseProcessorInLet (processorType.getOriginalName()));
                }
            }
        }

        auto implicitNodeName = parentGraph.getStringPool().get (AST::GraphNode::getNameForImplicitNode (processorType));

        if (auto existing = parentGraph.findNode (implicitNodeName))
            return existing;

        auto& newNode = declarationContext.allocate<AST::GraphNode>();
        newNode.nodeName = implicitNodeName;
        newNode.originalName = processorType.getOriginalName();
        newNode.processorType.createReferenceTo (processorType);
        parentGraph.nodes.addReference (newNode);
        return newNode;
    }

    //==============================================================================
    void visitConnectionEnd (AST::Connection& connection, AST::ObjectProperty& endpoint, bool isSource)
    {
        auto& endpointContext = AST::getContext (endpoint);

        if (auto node = AST::castToSkippingReferences<AST::GraphNode> (endpoint))
        {
            if (auto processorType = node->getProcessorType())
            {
                auto possibleTargets = isSource ? processorType->getOutputEndpoints (true)
                                                : processorType->getInputEndpoints (true);

                if (possibleTargets.size() == 1)
                {
                    auto& ei = endpointContext.allocate<AST::EndpointInstance>();
                    ei.node.referTo (endpoint);
                    endpoint.referTo (ei);
                }
                else
                {
                    if (registerFailure())
                        throwError (endpoint, possibleTargets.empty() ? (isSource ? Errors::processorHasNoSuitableOutputs()
                                                                                  : Errors::processorHasNoSuitableInputs())
                                                                      : Errors::invalidEndpointSpecifier());
                }

                return;
            }
        }

        if (AST::castToSkippingReferences<AST::EndpointDeclaration> (endpoint) != nullptr)
        {
            auto& ei = endpointContext.allocate<AST::EndpointInstance>();
            ei.endpoint.referTo (endpoint);
            endpoint.referTo (ei);
            return;
        }

        if (auto processor = AST::castToSkippingReferences<AST::ProcessorBase> (endpoint))
        {
            auto instance = getOrCreateGraphNode (endpointContext, *connection.getParentModule().getAsGraph(), *processor);

            if (instance != nullptr)
            {
                auto& ei = endpointContext.allocate<AST::EndpointInstance>();
                ei.node.createReferenceTo (*instance);
                endpoint.referTo (ei);
                return;
            }
        }

        if (auto value = AST::castToSkippingReferences<AST::Expression> (endpoint))
        {
            if (isSource)
                return;
        }

        registerFailure();
    }

    void visit (AST::Connection& c) override
    {
        super::visit (c);

        for (size_t i = 0; i < c.sources.size(); ++i)
            visitConnectionEnd (c, *c.sources[i].getAsObjectProperty(), true);

        for (size_t i = 0; i < c.dests.size(); ++i)
            visitConnectionEnd (c, *c.dests[i].getAsObjectProperty(), false);
    }

    //==============================================================================
    void visit (AST::HoistedEndpointPath& hoistedEndpointPath) override
    {
        CMAJ_ASSERT (! hoistedEndpointPath.pathSections.empty());
        ptr<AST::ProcessorBase> processorToSearch = hoistedEndpointPath.getParentProcessor();

        for (size_t i = 0; i < hoistedEndpointPath.pathSections.size(); ++i)
        {
            ref<AST::Property> pathSection = hoistedEndpointPath.pathSections[i];

            if (auto element = AST::castToSkippingReferences<AST::GetElement> (pathSection))
                pathSection = element->parent;

            if (auto node = AST::castToSkippingReferences<AST::GraphNode> (pathSection))
            {
                processorToSearch = node->getProcessorType();
                continue;
            }

            if (AST::castToSkippingReferences<AST::EndpointDeclaration> (pathSection))
                break;

            auto name = AST::castTo<AST::Identifier> (pathSection);

            if (name == nullptr || processorToSearch == nullptr
                 || processorToSearch->isGenericOrParameterised())
            {
                registerFailure();
                return;
            }

            AST::NameSearch search;
            search.nameToFind                   = name->name;
            search.stopAtFirstScopeWithResults  = true;
            search.findVariables                = false;
            search.findTypes                    = false;
            search.findFunctions                = false;
            search.findNamespaces               = false;
            search.findProcessors               = i == 0;
            search.findNodes                    = true;
            search.findEndpoints                = true;
            search.onlyFindLocalVariables       = false;

            search.performSearch (*processorToSearch, {});

            if (search.itemsFound.empty())
            {
                registerFailure();
                return;
            }

            if (search.itemsFound.size() > 1)
            {
                if (registerFailure())
                    validation::throwAmbiguousNameError (AST::getContext (name), search.nameToFind, search.itemsFound);

                return;
            }

            auto& firstItem = search.itemsFound.front();

            auto node = AST::castTo<AST::GraphNode> (firstItem);

            // Allow the first item in a hoisted endpoint path to be a Processor which gets implicitly promoted to a node
            if (i == 0 && node == nullptr)
                if (auto processor = AST::castTo<AST::ProcessorBase> (firstItem))
                    node = getOrCreateGraphNode (hoistedEndpointPath.context,
                                                 AST::castToRef<AST::Graph> (hoistedEndpointPath.getParentProcessor()),
                                                 *processor);

            if (node != nullptr)
            {
                replaceObject (pathSection->getObjectRef(), *node);
                processorToSearch = node->getProcessorType();
                continue;
            }

            if (auto endpoint = AST::castTo<AST::EndpointDeclaration> (firstItem))
            {
                if (! hoistedEndpointPath.wildcardPattern.hasDefaultValue())
                    throwError (pathSection, Errors::expectedNode());

                replaceObject (pathSection->getObjectRef(), *endpoint);
                break;
            }

            registerFailure();
            return;
        }

        if (! hoistedEndpointPath.wildcardPattern.hasDefaultValue())
            resolveHoistedWildcard (hoistedEndpointPath, { hoistedEndpointPath.wildcardPattern.get(), true });
    }

    void resolveHoistedWildcard (AST::HoistedEndpointPath& hoistedEndpointPath,
                                 const choc::text::WildcardPattern& wildcard)
    {
        auto& hoistedEndpoint = *hoistedEndpointPath.findParentOfType<AST::EndpointDeclaration>();
        ref<AST::Property> lastPathSection = hoistedEndpointPath.pathSections.back();

        if (auto element = AST::castToSkippingReferences<AST::GetElement> (lastPathSection))
            lastPathSection = element->parent;

        if (auto node = AST::castToSkippingReferences<AST::GraphNode> (lastPathSection))
        {
            if (auto processorType = node->getProcessorType())
            {
                const auto dependsOnAnyWildcard = [&]
                {
                    for (auto& e : processorType->endpoints.iterateAs<AST::EndpointDeclaration>())
                    {
                        if (e.isHoistedEndpoint())
                        {
                            auto& path = AST::castToRef<AST::HoistedEndpointPath> (e.childPath);

                            if (! path.wildcardPattern.hasDefaultValue())
                                return true;
                        }
                    }
                    return false;
                };

                if (dependsOnAnyWildcard())
                {
                    registerFailure();
                    return;
                }

                auto& graph = *hoistedEndpoint.findParentOfType<AST::Graph>();
                bool isInput = hoistedEndpoint.isInput;
                auto oldEndpointIndex = graph.endpoints.indexOf (hoistedEndpoint);
                CMAJ_ASSERT (oldEndpointIndex >= 0);
                graph.endpoints.remove (static_cast<size_t> (oldEndpointIndex));
                size_t numMatches = 0;

                for (auto& e : processorType->endpoints.iterateAs<AST::EndpointDeclaration>())
                {
                    if (e.isInput == isInput && wildcard.matches (std::string (e.name.toString())))
                    {
                        auto& newEndpoint = hoistedEndpoint.context.allocate<AST::EndpointDeclaration>();

                        newEndpoint.name = makeEndpointName (hoistedEndpoint, e);
                        newEndpoint.isInput = isInput;

                        if (hoistedEndpoint.annotation != nullptr)
                            newEndpoint.annotation.referTo (hoistedEndpoint.annotation.getObject());

                        auto& childPath = newEndpoint.allocateChild<AST::HoistedEndpointPath>();
                        newEndpoint.childPath.referTo (childPath);

                        for (auto& section : hoistedEndpointPath.pathSections.iterateAs<AST::Object>())
                            childPath.pathSections.addReference (section);

                        childPath.pathSections.addReference (e);
                        graph.endpoints.addChildObject (newEndpoint, oldEndpointIndex++);
                        registerChange();
                        ++numMatches;
                    }
                }

                if (numMatches == 0)
                    throwError (hoistedEndpointPath, isInput ? Errors::noMatchForWildcardInput (hoistedEndpointPath.wildcardPattern.get())
                                                             : Errors::noMatchForWildcardOutput (hoistedEndpointPath.wildcardPattern.get()));

                return;
            }
        }

        registerFailure();
    }

    static AST::PooledString makeEndpointName (AST::EndpointDeclaration& hoistedEndpoint, const AST::EndpointDeclaration& matchingEndpoint)
    {
        if (hoistedEndpoint.nameTemplate.toString().empty())
            return matchingEndpoint.name.toString();

        // Substitute all * in the nameTemplate with the endpoint name
        auto newName = choc::text::replace (hoistedEndpoint.nameTemplate.toString().get(), "*", matchingEndpoint.name.toString().get());

        return hoistedEndpoint.getStringPool().get (newName);
    }};

}
