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
/// Strips out any nodes whose outputs don't go anywhere
static inline void removeUnusedNodes (AST::Program& program)
{
    struct Helper
    {
        static void removeUnusedNodes (AST::Graph& graph)
        {
            if (graph.isGenericOrParameterised())
                return;

            GraphConnectivityModel model (graph);

            for (auto& node : model.nodes)
            {
                if (auto g = AST::castToSkippingReferences<AST::Graph> (node.node.processorType))
                    removeUnusedNodes (*g);

                if (! node.isIndirectlyConnnectedToOutput)
                {
                    graph.nodes.removeObject (node.node);

                    for (size_t i = graph.connections.size(); i > 0; --i)
                    {
                        auto& conn = AST::castToRefSkippingReferences<AST::Connection> (graph.connections[i - 1]);

                        conn.dests.removeIf ([&] (const AST::Property& dest)
                        {
                            ptr<AST::EndpointInstance> endpoint;

                            if (auto element = AST::castToSkippingReferences<AST::GetElement> (dest))
                                endpoint = AST::castToRefSkippingReferences<AST::EndpointInstance> (element->parent);
                            else if (auto instance = AST::castToSkippingReferences<AST::EndpointInstance> (dest))
                                endpoint = AST::castToRefSkippingReferences<AST::EndpointInstance> (dest);

                            ptr<AST::GraphNode> graphNode;

                            if (auto n = AST::castToSkippingReferences<AST::GraphNode> (endpoint->node))
                                graphNode = n;
                            else if (auto element = AST::castToSkippingReferences<AST::GetElement> (endpoint->node))
                                graphNode = AST::castToSkippingReferences<AST::GraphNode> (element->parent);

                            return graphNode == node.node;
                        });

                        if (conn.dests.empty())
                            graph.connections.remove (i - 1);
                    }

                    auto* nodeBeingRemoved = std::addressof (node.node);

                    graph.visitObjectsInScope ([nodeBeingRemoved] (AST::Object& s)
                    {
                        if (auto w = s.getAsWriteToEndpoint())
                        {
                            if (auto instance = AST::castToSkippingReferences<AST::EndpointInstance> (w->target))
                            {
                                if (std::addressof (instance->getNode()) == nodeBeingRemoved)
                                {
                                    auto& noopStatement = w->context.allocate<AST::NoopStatement>();
                                    w->replaceWith (noopStatement);
                                }
                            }
                        }
                    });
                }
            }
        }
    };

    if (auto g = AST::castTo<AST::Graph> (program.getMainProcessor()))
        Helper::removeUnusedNodes (*g);
}

}
