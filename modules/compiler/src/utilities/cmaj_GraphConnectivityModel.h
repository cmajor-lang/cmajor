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
struct GraphConnectivityModel
{
    GraphConnectivityModel() = default;

    GraphConnectivityModel (const AST::Graph& graph)
    {
        for (auto& node : graph.nodes)
            addNode (AST::castToRefSkippingReferences<AST::GraphNode> (node));

        graph.visitConnections ([&] (const AST::Connection& connection)
        {
            addConnection (connection);
        });

        for (auto& n : nodes)
            if (n.isDirectlyConnectedToOutput)
                n.setIndirectConnectionFlag();
    }

    static AST::ObjectRefVector<const AST::EndpointInstance> getUsedEndpointInstances (const AST::ValueBase& source)
    {
        struct FindEndpointInstances  : public AST::NonParameterisedObjectVisitor
        {
            using super = AST::NonParameterisedObjectVisitor;
            using super::visit;

            FindEndpointInstances (AST::Allocator& a) : super (a) {}

            void visit (AST::ReadFromEndpoint& r) override
            {
                endpointInstances.push_back (*AST::castToSkippingReferences<AST::EndpointInstance> (r.endpointInstance));
            }

            CMAJ_DO_NOT_VISIT_CONSTANTS

            AST::ObjectRefVector<const AST::EndpointInstance> endpointInstances;
        };

        auto visitor = FindEndpointInstances (source.context.allocator);
        visitor.visitObject (const_cast<AST::ValueBase&> (source));
        return visitor.endpointInstances;
    }

    //==============================================================================
    struct Node
    {
        struct Source
        {
            ptr<Node> node;
            const AST::Connection& connection;
            const AST::EndpointInstance& sourceEndpointInstance;
            const AST::EndpointInstance& destEndpointInstance;
        };

        const AST::GraphNode& node;
        choc::SmallVector<Source, 4> sources;
        bool isDirectlyConnectedToOutput = false, isIndirectlyConnnectedToOutput = false;

        AST::InterpolationTypeEnum::Enum inputInterpolationMode  = AST::InterpolationTypeEnum::Enum::none,
                                         outputInterpolationMode = AST::InterpolationTypeEnum::Enum::none;

        AST::InterpolationTypeEnum::Enum getInputInterpolationMode (bool isUpsampling) const
        {
            if (inputInterpolationMode == AST::InterpolationTypeEnum::Enum::none)
                return isUpsampling ? AST::InterpolationTypeEnum::Enum::sinc
                                    : AST::InterpolationTypeEnum::Enum::latch;

            return inputInterpolationMode;
        }

        AST::InterpolationTypeEnum::Enum getOutputInterpolationMode (bool isUpsampling) const
        {
            if (outputInterpolationMode == AST::InterpolationTypeEnum::Enum::none)
                return isUpsampling ? AST::InterpolationTypeEnum::Enum::sinc
                                    : AST::InterpolationTypeEnum::Enum::linear;

            return outputInterpolationMode;
        }

        double getLongestDelayFromSource (std::vector<const Node*>& visited) const
        {
            if (std::find (visited.begin(), visited.end(), this) != visited.end())
                return 0;

            double longest = 0;
            visited.push_back (this);

            for (auto& source : sources)
                if (source.node)
                    longest = std::max (longest, source.node->getLongestDelayFromSource (visited));

            visited.pop_back();

            return longest + node.getProcessorType()->getLatency() / node.getClockMultiplier();
        }

        void setIndirectConnectionFlag()
        {
            if (! isIndirectlyConnnectedToOutput)
            {
                isIndirectlyConnnectedToOutput = true;

                for (auto& s : sources)
                    if (s.node)
                        s.node->setIndirectConnectionFlag();
            }
        }
    };

    std::vector<Node> nodes;
    std::vector<Node::Source> outputs;

    //==============================================================================
    void addNode (const AST::GraphNode& node)
    {
        nodes.push_back ({ node });
    }

    void addConnection (const AST::Connection& c)
    {
        if (c.delayLength != nullptr)
            return;

        for (auto& source : c.sources)
        {
            if (auto conn = AST::castToSkippingReferences<AST::Connection> (source))
            {
                addConnection (*conn);
                continue;
            }

            AST::ObjectRefVector<const AST::EndpointInstance> sourceEndpoints;

            if (auto element = AST::castToSkippingReferences<AST::GetElement> (source))
            {
                if (auto value = AST::castToSkippingReferences<AST::ValueBase> (element->parent))
                    sourceEndpoints = getUsedEndpointInstances (*value);
                else
                    sourceEndpoints.push_back (*AST::castToSkippingReferences<AST::EndpointInstance> (element->parent));
            }
            else if (auto instance = AST::castToSkippingReferences<AST::EndpointInstance> (source))
            {
                sourceEndpoints.push_back (*instance);
            }
            else if (auto value = AST::castToSkippingReferences<AST::ValueBase> (source))
            {
                sourceEndpoints = getUsedEndpointInstances (*value);
            }

            for (auto sourceEndpoint : sourceEndpoints)
            {
                auto sourceNode = AST::castToSkippingReferences<AST::GraphNode> (sourceEndpoint->node);

                if (sourceNode == nullptr)
                    if (auto element = AST::castToSkippingReferences<AST::GetElement> (sourceEndpoint->node))
                        sourceNode = AST::castToSkippingReferences<AST::GraphNode> (element->parent);

                for (auto& dest : c.dests)
                {
                    ptr<AST::EndpointInstance>  destEndpoint;

                    if (auto element = AST::castToSkippingReferences<AST::GetElement> (dest))
                        destEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (element->parent);
                    else
                        destEndpoint = AST::castToSkippingReferences<AST::EndpointInstance> (dest);

                    auto destNode = AST::castToSkippingReferences<AST::GraphNode> (destEndpoint->node);
                    addConnection (c, sourceNode, destNode, sourceEndpoint, *destEndpoint);
                }
            }
        }
    }

    void addConnection (const AST::Connection& c,
                        ptr<const AST::GraphNode> source,
                        ptr<const AST::GraphNode> dest,
                        const AST::EndpointInstance& sourceEndpointInstance,
                        const AST::EndpointInstance& destEndpointInstance)
    {
        if (dest != nullptr)
            getNode (dest).sources.push_back ({ findNode (source), c, sourceEndpointInstance, destEndpointInstance });
        else
            outputs.push_back ({ findNode (source), c, sourceEndpointInstance, destEndpointInstance });

        if (dest != nullptr)
            applyInterpolationMode (getNode (dest), c, true);

        if (source != nullptr)
        {
            auto& sourceNode = getNode (source);
            applyInterpolationMode (sourceNode, c, false);

            if (dest == nullptr)
                sourceNode.isDirectlyConnectedToOutput = true;
        }
    }

    void checkAndThrowErrorIfCycleFound() const
    {
        std::vector<const Node*> visited;
        visited.reserve (nodes.size());

        for (auto& node : nodes)
        {
            followConnections ({}, node, visited);
            CMAJ_ASSERT (visited.empty());
        }
    }

    double calculateTotalDelay()
    {
        double longest = 0;
        std::vector<const Node*> visited;

        for (auto& n : nodes)
        {
            if (n.isDirectlyConnectedToOutput)
            {
                visited.clear();
                longest = std::max (longest, n.getLongestDelayFromSource (visited));
            }
        }

        return longest;
    }

private:
    //==============================================================================
    ptr<Node> findNode (ptr<const AST::GraphNode> node)
    {
        if (node)
            for (auto& n : nodes)
                if (std::addressof (n.node) == std::addressof (*node))
                    return n;

        return {};
    }

    Node& getNode (ptr<const AST::GraphNode> node)
    {
        auto n = findNode (node);
        CMAJ_ASSERT (n != nullptr);
        return *n;
    }

    static std::string getCycleNameList (choc::span<const Node*> visited)
    {
        std::vector<std::string> names;

        for (auto& node : visited)
            names.push_back (std::string (node->node.getOriginalName()));

        names.push_back (names.front());
        std::reverse (names.begin(), names.end());
        return choc::text::joinStrings (names, " -> ");
    }

    static void followConnections (ptr<const AST::Connection> connection, const Node& node, std::vector<const Node*>& visited)
    {
        if (std::find (visited.begin(), visited.end(), std::addressof (node)) != visited.end())
            throwError (*connection, Errors::feedbackInGraph (getCycleNameList (visited)));

        visited.push_back (std::addressof (node));

        for (auto& source : node.sources)
            if (source.node)
                followConnections (source.connection, *source.node, visited);

        visited.pop_back();
    }

    static void applyInterpolationMode (Node& node, const AST::Connection& connection, bool isInput)
    {
        auto& currentMode = isInput ? node.inputInterpolationMode : node.outputInterpolationMode;
        auto newMode = connection.interpolation.get();

        if (newMode == AST::InterpolationTypeEnum::Enum::none)
            return;

        if (currentMode == AST::InterpolationTypeEnum::Enum::none)
        {
            currentMode = newMode;
            return;
        }

        auto isSpecificInterpolationMode = [] (auto mode)
        {
            return mode == AST::InterpolationTypeEnum::Enum::latch
                || mode == AST::InterpolationTypeEnum::Enum::linear
                || mode == AST::InterpolationTypeEnum::Enum::sinc;
        };

        if (isSpecificInterpolationMode (currentMode) || isSpecificInterpolationMode (newMode))
        {
            if (currentMode != newMode)
                throwError (connection, isInput ? Errors::incompatibleInputInterpolationTypes (node.node.getName())
                                                : Errors::incompatibleOutputInterpolationTypes (node.node.getName()));
        }
        else
        {
            if (newMode == AST::InterpolationTypeEnum::Enum::best)
                currentMode = newMode;
        }
    }
};

inline double AST::Graph::getLatency() const
{
    return GraphConnectivityModel (*this).calculateTotalDelay();
}


}
