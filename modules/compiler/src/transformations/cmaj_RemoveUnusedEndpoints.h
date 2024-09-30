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

namespace cmaj::transformations
{

//==============================================================================
/// Strips out any internal endpoints that aren't connected to anything
static inline void removeUnusedEndpoints (AST::Program& program,
                                          const std::function<bool(const EndpointID&)>& isEndpointActive)
{
    struct ActiveEndpointList
    {
        std::vector<const AST::EndpointDeclaration*> allEndpoints, usedEndpoints;

        std::vector<const AST::EndpointDeclaration*> getUnusedEndpoints()
        {
            std::vector<const AST::EndpointDeclaration*> unused;

            std::sort (usedEndpoints.begin(), usedEndpoints.end());
            usedEndpoints.erase (std::unique (usedEndpoints.begin(), usedEndpoints.end()), usedEndpoints.end());

            std::sort (allEndpoints.begin(), allEndpoints.end());
            allEndpoints.erase (std::unique (allEndpoints.begin(), allEndpoints.end()), allEndpoints.end());

            std::set_difference (allEndpoints.begin(), allEndpoints.end(),
                                 usedEndpoints.begin(), usedEndpoints.end(),
                                 std::back_inserter (unused));

            return unused;
        }

        void scan (AST::ProcessorBase& mainProcessor,
                   const std::function<bool(const EndpointID&)>& isEndpointActive)
        {
            allEndpoints.reserve (256);
            usedEndpoints.reserve (256);

            mainProcessor.getRootNamespace().visitAllModules (true, [this] (AST::ModuleBase& m)
            {
                visit (m);
            });

            // make sure that active endpoints in the main processor are preserved
            for (auto e : mainProcessor.getAllEndpoints())
                if (isEndpointActive (e->getEndpointID()))
                    usedEndpoints.push_back (e.getPointer());
        }

        void visit (AST::ModuleBase& module)
        {
            if (auto p = module.getAsProcessorBase())
                for (auto& e : p->getAllEndpoints())
                    allEndpoints.push_back (e.getPointer());

            if (auto g = module.getAsGraph())
            {
                g->visitConnections ([&] (AST::Connection& c)
                {
                    visit (c);
                });
            }

            if (auto p = module.getAsProcessorBase())
            {
                for (auto& f : p->functions)
                    visit (AST::castToRef<AST::Function> (f));
            }
        }

        void visit (AST::Function& f)
        {
            f.visitObjectsInScope ([this] (AST::Object& s)
            {
                if (auto e = s.getAsWriteToEndpoint())
                {
                    if (auto endpointDeclaration = e->getEndpoint())
                        usedEndpoints.push_back (endpointDeclaration.get());
                }

                if (auto e = s.getAsReadFromEndpoint())
                {
                    if (auto endpointDeclaration = e->getEndpointDeclaration())
                        usedEndpoints.push_back (endpointDeclaration.get());
                    else
                        CMAJ_ASSERT_FALSE;
                }
            });
        }

        void visit (AST::Connection& c)
        {
            for (auto& source : c.sources)
                visitConnectionEnd (source->getObjectRef(), true);

            for (auto& dest : c.dests)
                visitConnectionEnd (dest->getObjectRef(), false);
        }

        void visitConnectionEnd (AST::Object& endpoint, bool isSource)
        {
            if (auto e = AST::castToSkippingReferences<AST::EndpointDeclaration> (endpoint))
            {
                usedEndpoints.push_back (e.get());
                return;
            }

            if (auto ei = AST::castToSkippingReferences<AST::EndpointInstance> (endpoint))
            {
                auto e = ei->getEndpoint (isSource);
                CMAJ_ASSERT (e != nullptr);
                usedEndpoints.push_back (e.get());
                return;
            }

            if (auto ge = AST::castToSkippingReferences<AST::GetElement> (endpoint))
            {
                visitConnectionEnd (ge->parent.getObjectRef(), isSource);
                return;
            }

            if (auto value = AST::castToSkippingReferences<AST::ValueBase> (endpoint))
            {
                auto instances = GraphConnectivityModel::getUsedEndpointInstances (*value);

                for (auto i : instances)
                    usedEndpoints.push_back (i->getEndpoint (isSource).get());

                return;
            }

            CMAJ_ASSERT_FALSE;
        }
    };

    //==============================================================================
    ActiveEndpointList list;
    list.scan (program.getMainProcessor(), isEndpointActive);

    for (auto e : list.getUnusedEndpoints())
    {
        auto& parent = AST::castToRef<AST::ProcessorBase> (e->getParentModule());

        parent.visitObjectsInScope ([e] (AST::Object& s)
        {
            if (auto w = s.getAsWriteToEndpoint())
            {
                auto* targetEndpoint = w->getEndpoint().get();
                CMAJ_ASSERT (targetEndpoint != nullptr);

                if (e == targetEndpoint)
                {
                    auto& noopStatement = w->context.allocate<AST::NoopStatement>();
                    w->replaceWith (noopStatement);
                }
            }
            else if (auto r = s.getAsReadFromEndpoint())
            {
                if (auto endpoint = r->getEndpointDeclaration())
                {
                    CMAJ_ASSERT (endpoint.get() != nullptr);

                    if (e == endpoint.get())
                    {
                        auto& zero = r->getResultType()->allocateConstantValue (r->context);
                        r->replaceWith (zero);
                    }
                }
            }
        });

        if (e->isInput && e->isEvent())
            for (auto fn : EventHandlerUtilities::getEventHandlerFunctionsForEndpoint (parent, *e))
                parent.functions.removeObject (*fn);

        parent.endpoints.removeObject (*e);
    }
}

}
