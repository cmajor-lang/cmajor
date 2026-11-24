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

#include "choc/containers/choc_Value.h"
#include "choc/text/choc_JSON.h"
#include "../AST/cmaj_AST.h"
#include "../utilities/cmaj_GraphConnectivityModel.h"
#include "../utilities/cmaj_GraphPaths.h"
#include "cmaj_ProgramPrinter.h"

namespace cmaj
{
    //==============================================================================
    /// Creates a plain description of the structure of a program's main processor, as
    /// a choc::value that can be serialised as JSON. It contains no layout or styling:
    /// that's left entirely to whatever renders it.
    ///
    /// The description is hierarchical. Each node may itself be a graph with its own
    /// "nodes", and each graph owns the "connections" between the things inside it. A
    /// connection end whose "node" is null refers to an endpoint of the owning graph.
    ///
    /// The program should have been prepared with transformations::prepareForGraphGen,
    /// passing keepDelayedConnections = true so that delayed connections are reported as
    /// connections with a "delay" property, rather than as synthesised delay nodes.
    ///
    /// Format (optional properties are omitted when they don't apply):
    ///
    ///   { "format": "cmajor-patch-graph", "version": 1, "root": <node> }
    ///
    ///   node:       { id, path, name, processor, processorFullName, isGraph, source?,
    ///                 nodeSource?, isImplicit?, arraySize?, clockMultiplier?,
    ///                 clockDivider?, inputs: [endpoint], outputs: [endpoint],
    ///                 nodes?: [node], connections?: [connection] }
    ///   endpoint:   { path, name, endpointType ("stream" | "value" | "event"),
    ///                 dataTypes: [dataType], arraySize?, annotation?, source? }
    ///   dataType:   { name, fullName, resolvedName?, source? }
    ///   connection: { path, from: end, to: end, delay?, interpolation?, source? }
    ///   end:        { node: id | null, endpoint, path, nodeIndex?, endpointIndex? }
    ///   location:   { file, line, column }     (the type of every "source" and "nodeSource")
    ///
    /// "id" is only unique within one description. "path" is the stable name for a node,
    /// endpoint or connection (see cmaj::graph_paths), which is what anything reporting runtime
    /// information about the graph must use to refer to it.
    ///
    /// nodeIndex is set when a connection uses one element of a node array (`voices[2].out`),
    /// and endpointIndex when it uses one element of an endpoint array (`node.out[2]`).
    ///
    /// A dataType's source is the declaration of the struct, enum or type alias it is built
    /// from (so for `Message[4]` it's the declaration of `Message`), and is omitted for
    /// built-in types. A type written using an alias keeps the alias in its name and fullName
    /// (e.g. "Sample<2>"), and gives the type it refers to as resolvedName (e.g. "float32<2>").
    /// Sources are omitted for anything that doesn't come from a file, which includes everything
    /// in the standard library.
    ///
    struct GraphStructureGenerator
    {
        GraphStructureGenerator (const AST::Program& p) : program (p) {}

        static constexpr std::string_view formatName = "cmajor-patch-graph";
        static constexpr int32_t formatVersion = 1;

        /// Builds the structure description for the program's main processor
        [[nodiscard]] choc::value::Value createGraphStructure()
        {
            nextNodeID = 0;
            auto& mainProcessor = program.getMainProcessor();

            return choc::json::create ("format", formatName,
                                       "version", formatVersion,
                                       "root", createNode (createNodeID(), {}, nullptr, mainProcessor));
        }

        /// Returns the result of createGraphStructure() as a JSON string
        [[nodiscard]] std::string createJSON()
        {
            return choc::json::toString (createGraphStructure(), true);
        }

    private:
        //==============================================================================
        struct NodeID
        {
            const AST::GraphNode& node;
            std::string id, path;
        };

        using NodeIDList = std::vector<NodeID>;

        const AST::Program& program;
        uint32_t nextNodeID = 0;

        std::string createNodeID()
        {
            return "n" + std::to_string (nextNodeID++);
        }

        static void addSourceLocation (choc::value::Value& target, std::string_view propertyName, const AST::Object& o)
        {
            auto location = o.context.getFullLocation();

            if (! location.filename.empty() && location.lineAndColumn.isValid())
                target.addMember (propertyName, choc::json::create ("file", location.filename,
                                                                    "line", static_cast<int32_t> (location.lineAndColumn.line),
                                                                    "column", static_cast<int32_t> (location.lineAndColumn.column)));
        }

        //==============================================================================
        choc::value::Value createNode (const std::string& id, const std::string& path,
                                       ptr<const AST::GraphNode> graphNode, const AST::ProcessorBase& processor)
        {
            auto graph = AST::castTo<AST::Graph> (processor);
            auto name = graphNode != nullptr ? graphNode->getOriginalName() : processor.getOriginalName();

            auto node = choc::json::create ("id", id,
                                            "path", path,
                                            "name", std::string (name),
                                            "processor", std::string (processor.getOriginalName()),
                                            "processorFullName", processor.getFullyQualifiedReadableName(),
                                            "isGraph", graph != nullptr);

            addSourceLocation (node, "source", processor);

            if (graphNode != nullptr)
                addGraphNodeProperties (node, *graphNode);

            node.addMember ("inputs", createEndpointList (processor.getInputEndpoints (false), path));
            node.addMember ("outputs", createEndpointList (processor.getOutputEndpoints (false), path));

            if (graph != nullptr)
                addGraphContent (node, *graph, path);

            return node;
        }

        static void addGraphNodeProperties (choc::value::Value& node, const AST::GraphNode& graphNode)
        {
            node.addMember ("isImplicit", graphNode.isImplicitlyCreated());

            if (! graphNode.isImplicitlyCreated())
                addSourceLocation (node, "nodeSource", graphNode);

            if (auto size = graphNode.getArraySize())
                node.addMember ("arraySize", static_cast<int32_t> (*size));

            if (auto multiplier = AST::getAsFoldedConstant (graphNode.clockMultiplierRatio))
                addNumber (node, "clockMultiplier", *multiplier);

            if (auto divider = AST::getAsFoldedConstant (graphNode.clockDividerRatio))
                addNumber (node, "clockDivider", *divider);
        }

        /// Adds a constant as an integer if it is one, or as a float otherwise
        static void addNumber (choc::value::Value& target, std::string_view propertyName, const AST::ConstantValueBase& value)
        {
            if (auto d = value.getAsFloat64())
            {
                if (auto i = value.getAsInt32(); i && static_cast<double> (*i) == *d)
                    target.addMember (propertyName, *i);
                else
                    target.addMember (propertyName, *d);
            }
        }

        //==============================================================================
        template <typename EndpointList>
        static choc::value::Value createEndpointList (const EndpointList& endpoints, const std::string& nodePath)
        {
            auto list = choc::value::createEmptyArray();

            for (auto& endpoint : endpoints)
                list.addArrayElement (createEndpoint (endpoint, nodePath));

            return list;
        }

        static choc::value::Value createEndpoint (const AST::EndpointDeclaration& endpoint, const std::string& nodePath)
        {
            auto dataTypes = choc::value::createEmptyArray();

            // this iterates the property list rather than using getDataTypes(), because that
            // skips references, and so would lose the type aliases that we want to link to
            for (auto& item : endpoint.dataTypes)
                if (auto type = AST::castToTypeBase (item))
                    dataTypes.addArrayElement (createDataType (*type, item->getObject()));

            auto result = choc::json::create ("path", graph_paths::createEndpointPath (nodePath, endpoint.getName()),
                                              "name", std::string (endpoint.getName()),
                                              "endpointType", std::string (getEndpointTypeName (endpoint.getEndpointType())),
                                              "dataTypes", dataTypes);

            if (auto size = endpoint.getArraySize())
                result.addMember ("arraySize", static_cast<int32_t> (*size));

            if (auto annotation = AST::getAnnotationAsChocValue (endpoint.annotation); ! annotation.isVoid())
                result.addMember ("annotation", annotation);

            addSourceLocation (result, "source", endpoint);
            return result;
        }

        static choc::value::Value createDataType (const AST::TypeBase& type, ptr<const AST::Object> declaredForm)
        {
            auto declaration = declaredForm != nullptr ? findTypeDeclaration (*declaredForm) : nullptr;

            // a type written using an alias is named the way it was written (e.g. "Sample<2>"),
            // with the type it resolves to as its resolvedName
            if (declaration != nullptr && AST::castTo<AST::Alias> (*declaration) != nullptr)
            {
                auto result = choc::json::create ("name", AST::print (*declaredForm, AST::PrintOptionFlags::useShortNames),
                                                  "fullName", AST::print (*declaredForm, AST::PrintOptionFlags::useFullyQualifiedNames),
                                                  "resolvedName", AST::print (type, static_cast<AST::PrintOptionFlags> (AST::PrintOptionFlags::useFullyQualifiedNames
                                                                                                                         | AST::PrintOptionFlags::skipAliases)));
                addSourceLocation (result, "source", *declaration);
                return result;
            }

            auto result = choc::json::create ("name", AST::print (type, AST::PrintOptionFlags::useShortNames),
                                              "fullName", AST::print (type, AST::PrintOptionFlags::useFullyQualifiedNames));

            if (declaration != nullptr)
                addSourceLocation (result, "source", *declaration);

            return result;
        }

        /// Finds the user-declared type (a struct, enum or type alias) that a type is built
        /// from, by looking through arrays, vectors, const/ref modifiers and references. Built-in
        /// types have no declaration, so for those this returns nullptr.
        static ptr<const AST::Object> findTypeDeclaration (const AST::Object& o)
        {
            // NB: aliases must be checked first, because they also pretend to be the type they refer to
            if (auto alias = AST::castTo<AST::Alias> (o))
            {
                if (alias->aliasType.get() == AST::AliasTypeEnum::Enum::typeAlias)
                    return alias;

                return {};
            }

            if (auto reference = AST::castTo<AST::NamedReference> (o))
                return findTypeDeclarationOf (reference->target);

            if (auto modifier = AST::castTo<AST::MakeConstOrRef> (o))
                return findTypeDeclarationOf (modifier->source);

            if (auto array = AST::castTo<AST::ArrayType> (o))
                return findTypeDeclarationOf (array->elementType);

            if (auto vector = AST::castTo<AST::VectorType> (o))
                return findTypeDeclarationOf (vector->elementType);

            if (AST::castTo<AST::StructType> (o) || AST::castTo<AST::EnumType> (o))
                return o;

            return {};
        }

        static ptr<const AST::Object> findTypeDeclarationOf (const AST::Property& p)
        {
            if (auto o = p.getObject())
                return findTypeDeclaration (*o);

            return {};
        }

        //==============================================================================
        void addGraphContent (choc::value::Value& node, const AST::Graph& graph, const std::string& graphPath)
        {
            GraphConnectivityModel model (graph, true);
            NodeIDList nodeIDs;
            auto nodes = choc::value::createEmptyArray();

            for (auto& n : model.nodes)
            {
                auto id = createNodeID();
                auto path = graph_paths::createNodePath (graphPath, n.node.getOriginalName());
                nodeIDs.push_back ({ n.node, id, path });
                nodes.addArrayElement (createNode (id, path, n.node, *n.node.getProcessorType()));
            }

            auto connections = choc::value::createEmptyArray();
            std::unordered_map<std::string, int> connectionPathCounts;

            auto addConnection = [&] (const GraphConnectivityModel::Node::Source& source)
            {
                if (! isConsoleConnection (source))
                    connections.addArrayElement (createConnection (nodeIDs, graphPath, source, connectionPathCounts));
            };

            for (auto& n : model.nodes)
                for (auto& source : n.sources)
                    addConnection (source);

            for (auto& output : model.outputs)
                addConnection (output);

            node.addMember ("nodes", nodes);
            node.addMember ("connections", connections);
        }

        /// The console endpoints are left out of the endpoint lists, so connections to them
        /// (which the compiler adds to route console output up to the top level) are too
        static bool isConsoleConnection (const GraphConnectivityModel::Node::Source& source)
        {
            auto isConsole = [] (const AST::EndpointInstance& instance, bool isSource)
            {
                auto endpoint = instance.getEndpoint (isSource);
                return endpoint != nullptr && endpoint->getName() == getConsoleEndpointID();
            };

            return isConsole (source.sourceEndpointInstance, true) || isConsole (source.destEndpointInstance, false);
        }

        static choc::value::Value createConnection (const NodeIDList& nodeIDs,
                                                    const std::string& graphPath,
                                                    const GraphConnectivityModel::Node::Source& source,
                                                    std::unordered_map<std::string, int>& pathCounts)
        {
            auto from = createConnectionEnd (nodeIDs, graphPath, source.connection.sources, source.sourceEndpointInstance, true);
            auto to   = createConnectionEnd (nodeIDs, graphPath, source.connection.dests, source.destEndpointInstance, false);

            // the same two ends can be connected more than once, so later duplicates get a suffix
            auto path = graph_paths::createConnectionPath (from["path"].getString(), to["path"].getString());

            if (auto count = ++pathCounts[path]; count > 1)
                path += " #" + std::to_string (count);

            auto connection = choc::json::create ("path", path,
                                                  "from", from,
                                                  "to",   to);

            if (source.isDelayed())
            {
                if (auto length = AST::getAsFoldedConstant (source.connection.delayLength))
                    addNumber (connection, "delay", *length);
                else if (auto delay = source.connection.delayLength.getObject())
                    connection.addMember ("delay", AST::print (*delay));
            }

            if (source.connection.interpolation.get() != AST::InterpolationTypeEnum::Enum::none)
                connection.addMember ("interpolation", std::string (source.connection.interpolation.getEnumString()));

            addSourceLocation (connection, "source", source.connection);
            return connection;
        }

        static choc::value::Value createConnectionEnd (const NodeIDList& nodeIDs,
                                                       const std::string& graphPath,
                                                       const AST::ListProperty& connectionEnds,
                                                       const AST::EndpointInstance& instance,
                                                       bool isSource)
        {
            auto end = choc::value::createObject ({});
            auto node = findNode (nodeIDs, instance);

            if (node != nullptr)
                end.addMember ("node", node->id);
            else
                end.addMember ("node", choc::value::Value());

            auto endpoint = instance.getEndpoint (isSource);
            CMAJ_ASSERT (endpoint != nullptr);
            end.addMember ("endpoint", std::string (endpoint->getName()));

            std::optional<int32_t> nodeIndex, endpointIndex;

            if (auto index = instance.getNodeIndex())
                if (auto folded = AST::getAsFoldedConstant (*index))
                    nodeIndex = folded->getAsInt32();

            endpointIndex = findEndpointIndex (connectionEnds, instance);

            end.addMember ("path", graph_paths::createEndpointPath (node != nullptr ? node->path : graphPath,
                                                                    endpoint->getName(), nodeIndex, endpointIndex));

            if (nodeIndex)
                end.addMember ("nodeIndex", *nodeIndex);

            if (endpointIndex)
                end.addMember ("endpointIndex", *endpointIndex);

            return end;
        }

        /// If the connection refers to a single element of an endpoint array (e.g. `node.out[2]`),
        /// this returns that element's index. The connectivity model only keeps the endpoint
        /// instance, so it has to be found among the connection's original sources or dests.
        static std::optional<int32_t> findEndpointIndex (const AST::ListProperty& connectionEnds, const AST::EndpointInstance& instance)
        {
            for (auto& item : connectionEnds)
                if (auto element = AST::castToSkippingReferences<AST::GetElement> (item))
                    if (! element->indexes.empty() && refersToEndpointInstance (element->parent, instance))
                        if (auto folded = AST::getAsFoldedConstant (element->getSingleIndex()))
                            return folded->getAsInt32();

            return {};
        }

        /// True if the property is the given endpoint instance, or a direct read from it
        /// (which is how a source like `node.out[2]` appears once it has been resolved)
        static bool refersToEndpointInstance (const AST::Property& p, const AST::EndpointInstance& instance)
        {
            auto target = AST::castToSkippingReferences<AST::EndpointInstance> (p);

            if (target == nullptr)
                if (auto read = AST::castToSkippingReferences<AST::ReadFromEndpoint> (p))
                    target = AST::castToSkippingReferences<AST::EndpointInstance> (read->endpointInstance);

            return target != nullptr && std::addressof (*target) == std::addressof (instance);
        }

        /// Returns the child node an endpoint instance refers to, or nullptr if it
        /// refers to an endpoint of the parent graph itself
        static const NodeID* findNode (const NodeIDList& nodeIDs, const AST::EndpointInstance& instance)
        {
            if (instance.isParentEndpoint())
                return nullptr;

            auto& node = instance.getNode();

            for (auto& n : nodeIDs)
                if (std::addressof (n.node) == std::addressof (node))
                    return std::addressof (n);

            // fall back to matching by name, in case the instance refers to a copy of the node
            for (auto& n : nodeIDs)
                if (n.node.getName() == node.getName())
                    return std::addressof (n);

            CMAJ_ASSERT_FALSE;
        }
    };

} // namespace cmaj
