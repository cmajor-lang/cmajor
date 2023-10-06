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

#include <sstream>
#include "choc/text/choc_StringUtilities.h"
#include "choc/text/choc_HTML.h"
#include "../AST/cmaj_AST.h"
#include "../utilities/cmaj_GraphConnectivityModel.h"
#include "cmaj_ProgramPrinter.h"

std::string convertDOTtoSVG (const std::string& DOT);


namespace cmaj
{
    struct GraphVizGenerator
    {
        GraphVizGenerator (const AST::Program& p)
            : program (p),
              mainProcessor (program.getMainProcessor()),
              mainProcessorName (mainProcessor.getName())
        {
            addProcessor (mainProcessorName, mainProcessor, nullptr);
        }

        std::string createGraphSVG()
        {
            return replaceLinkIDs (convertDOTtoSVG (createDOT()));
        }

        std::string createDOT()
        {
            choc::text::CodePrinter out;

            out << "digraph" << newLine;
            {
                auto indent = out.createIndentWithBraces();
                out << "rankdir=LR;"<< newLine;
                out << "splines=polyline;"<< newLine;
                out << "bgcolor=\"#5B6A66\"" << newLine;

                for (auto& node : nodes)
                    node.print (out, true);

                for (auto& connection : connections)
                    connection.print (out);
            }

            return out.toString();
        }

    private:
        //==============================================================================
        static constexpr choc::text::CodePrinter::NewLine newLine = {};
        static constexpr choc::text::CodePrinter::BlankLine blankLine = {};

        struct Node;

        struct Endpoint
        {
            ptr<Node> node;
            std::string_view name;
            AST::ObjectRefVector<const AST::TypeBase> dataTypes;
            cmaj::FullCodeLocation endpointLocation;

            std::string getTypeNames()
            {
                std::vector<std::string> names;

                for (auto t : dataTypes)
                    names.push_back (AST::print (t, AST::PrintOptionFlags::useShortNames));

                auto types = choc::text::joinStrings (names, ", ");

                return types;
            }
        };

        uint32_t nextNodeId = 0;
        uint32_t linkIndex = 0;
        std::unordered_map<std::string, std::string> linkPlaceholders;

        std::string createFileLink (const cmaj::FullCodeLocation& location)
        {
            auto file = choc::text::trim (location.getLocationDescription());

            if (! file.empty())
            {
                auto linkID = "_link_placeholder_" + std::to_string (linkIndex++) + "_";
                linkPlaceholders[linkID] = "javascript:openSourceFile('" + choc::json::addEscapeCharacters (file) + "');";
                return linkID;
            }

            return {};
        }

        std::string replaceLinkIDs (std::string text)
        {
            for (auto l : linkPlaceholders)
                text = choc::text::replace (text, l.first, l.second);

            return text;
        }

        struct Node
        {
            Node (GraphVizGenerator& o) : owner (o) {}

            GraphVizGenerator& owner;
            uint32_t nodeId = 0;
            std::string name, nodeName, originalName;
            bool isGraph = false;
            bool isImplicitlyCreated = false;
            std::vector<Endpoint> inputs, outputs;
            std::vector<Node> nodes;
            cmaj::FullCodeLocation codeLocation;
            cmaj::FullCodeLocation nodeLocation;

            ptr<const Node> getNodeWithName (std::string_view n) const
            {
                for (auto& node : nodes)
                    if (node.nodeName == n)
                        return node;

                return {};
            }

            void writeTableRow (choc::text::CodePrinter& out,
                                const std::vector<Endpoint>& endpoints,
                                bool includeTypes, choc::html::HTMLElement htmlLabels)
            {
                for (auto e : endpoints)
                {
                    auto& tableRow = htmlLabels.addChild ("TR");
                    auto& tableData = tableRow.addChild ("TD");
                    tableData.setProperty ("bgcolor", "#C1C7C6");
                    tableData.setProperty ("PORT", e.name);
                    tableData.setProperty ("BORDER", "1");
                    tableData.setProperty ("href", owner.createFileLink (e.endpointLocation));

                    if (includeTypes)
                        tableData.addContent (e.getTypeNames()).addChild("BR");

                    tableData.addContent (e.name);
                }
                out << htmlLabels.toDocument(false);
            }

            void writeProcessorTableRow (choc::text::CodePrinter& out,
                                         const std::vector<Endpoint>& endpoints,
                                         bool includeTypes)
            {
                choc::html::HTMLElement htmlLabels ("TD");
                auto& endpointTable = htmlLabels.addChild ("TABLE").setProperty ("BORDER","0")
                                                                   .setProperty ("CELLSPACING","0");

                for (auto e : endpoints)
                {
                    auto& endpointRow = endpointTable.addChild ("TR");
                    auto& endpointData = endpointRow.addChild ("TD").setProperty ("bgcolor","#C1C7C6")
                                                                    .setProperty ("PORT", e.name)
                                                                    .setProperty ("BORDER", "1")
                                                                    .setProperty ("style", "rounded")
                                                                    .setProperty ("CELLSPACING", "0")
                                                                    .setProperty ("href", owner.createFileLink (e.endpointLocation));

                    if (includeTypes)
                        endpointData.addContent (e.getTypeNames()).addChild("BR");

                    endpointData.addContent (e.name);
                }

                out << htmlLabels.toDocument(false);
            }

            void print (choc::text::CodePrinter& out, bool topLevelGraph)
            {
                if (isGraph)
                {
                    out << "subgraph cluster_" << name << newLine;
                    auto indent = out.createIndentWithBraces();
                    out << "fontname=Courier" << newLine <<"fontcolor=white" << newLine;
                    out << "label = \""<< nodeName << "\"" << newLine;

                    if (topLevelGraph)
                        out << "color=\"#5B6A66\"" << newLine << "bgcolor=\"#5B6A66\"" << newLine;

                    if (! topLevelGraph)
                        out << "bgcolor=\"#73807C\"" << newLine;

                    if (! inputs.empty())
                    {
                        choc::html::HTMLElement root ("TABLE");
                        root.setProperty ("BORDER","0");
                        out << name << "_in";
                        out << "[ clusterrank=local shape = none fontname=Courier label=<";
                        writeTableRow (out, inputs, true, root);
                        out << "> ]" << newLine;
                    }

                    if (! outputs.empty())
                    {
                        choc::html::HTMLElement root ("TABLE");
                        root.setProperty ("BORDER","0");
                        out << name << "_out";
                        out << "[ shape = none fontname=Courier label=<";
                        writeTableRow (out, outputs, true, root);
                        out << "> ]" << newLine;
                    }

                    for (auto node : nodes)
                        node.print (out, false);
                }
                else
                {
                    out << name;
                    out << "[ shape = none fontname=Courier label=<<TABLE BORDER = \"0\" CELLSPACING = \"0\"><TR>";

                    if (! inputs.empty())
                        writeProcessorTableRow (out, inputs, false);

                    if (! isImplicitlyCreated)
                    {
                        std::string processorHref, nodeHref;

                        if (auto link = owner.createFileLink (codeLocation); ! link.empty())
                            processorHref = " href=\"" + link + "\"";

                        if (auto link = owner.createFileLink (nodeLocation); ! link.empty())
                            nodeHref = " href=\"" + link + "\"";

                        out << "<TD>";
                        out << "<TABLE BORDER = \"1\" CELLSPACING = \"0\" bgcolor=\"#b7aab4\">";

                        out << "<TR><TD BORDER = \"0\" colspan=\"100\"" << nodeHref << ">"
                            << nodeName << "</TD></TR>";

                        out << "<TR><TD BORDER = \"0\" colspan=\"100\"" << processorHref << ">(processor " << originalName << ")" << "</TD></TR>";

                        out << "</TABLE></TD>";
                    }
                    else
                    {
                        out << "<TD BORDER = \"1\" colspan=\"100\" bgcolor=\"#b7aab4\">"  << originalName << "</TD>";
                    }

                    if (! outputs.empty())
                        writeProcessorTableRow (out, outputs, false);

                    out << "</TR></TABLE>> ]";
                }

                out << newLine;
            }
        };

        struct Connection
        {
            std::string sourceName, targetName;

            void print (choc::text::CodePrinter& out)
            {
                out << sourceName << " -> " << targetName << "[ arrowsize=0.7 penwidth=1 ]" << newLine;
            }
        };

        void addProcessor (AST::PooledString nodeName, const AST::ProcessorBase& processor, ptr<Node> parentNode)
        {
            auto& newNode = addNode (false, nodeName, processor, parentNode, processor.context.getFullLocation());

            if (auto graph = AST::castTo<AST::Graph> (processor))
            {
                cmaj::GraphConnectivityModel model (*graph);

                for (auto& node : model.nodes)
                {
                    if (auto nodeGraph = AST::castTo<AST::Graph> (*node.node.getProcessorType()))
                        addProcessor (node.node.getOriginalName(), *nodeGraph, newNode);
                    else
                        addNode (node.node.isImplicitlyCreated(), node.node.nodeName, *node.node.getProcessorType(), newNode, node.node.context.getFullLocation());
                }

                for (auto& node : model.nodes)
                    for (auto& source : node.sources)
                        addConnection (newNode, source);

                for (auto& output : model.outputs)
                    addConnection (newNode, output);
            }
        }

        Node& addNode (bool implicitlyCreated, AST::PooledString name, const AST::ProcessorBase& processor, ptr<Node> parentNode, cmaj::FullCodeLocation nodeLocation)
        {
            Node n (*this);

            n.nodeId = nextNodeId++;
            n.name = std::string (processor.getName()) + "_" + std::to_string (n.nodeId);
            n.originalName = processor.getOriginalName();
            n.nodeName = name;
            n.isGraph = false;
            n.isImplicitlyCreated = implicitlyCreated;
            n.codeLocation = processor.context.getFullLocation();
            n.nodeLocation = nodeLocation;

            if (AST::castTo<AST::Graph> (processor))
                n.isGraph = true;

            for (auto input : processor.getInputEndpoints (false))
                n.inputs.emplace_back(Endpoint { n,
                                                 input->getName(),
                                                 input->getDataTypes(),
                                                 input->context.getFullLocation() });

            for (auto output : processor.getOutputEndpoints (false))
                n.outputs.emplace_back(Endpoint { n,
                                                  output->getName(),
                                                  output->getDataTypes(),
                                                  output->context.getFullLocation() });

            if (parentNode == nullptr)
            {
                nodes.push_back (n);
                return nodes.back();
            }

            parentNode->nodes.push_back (n);
            return parentNode->nodes.back();
        }

        std::string getNodeName (const Node& graphNode, const AST::EndpointInstance& i, bool isInput) const
        {
            if (i.node.hasDefaultValue())
                return graphNode.name + (isInput ? "_in" : "_out");

            if (i.isParentEndpoint())
                return graphNode.name + (isInput ? "_out" : "_in");

            auto node = graphNode.getNodeWithName (i.getNode().getName());

            if (! node)
                node = graphNode.getNodeWithName (i.getNode().getOriginalName());

            if (node->isGraph)
                return node->name + (isInput ? "_out" : "_in");

            return node->name;
        }

        void addConnection (const Node& graphNode, const cmaj::GraphConnectivityModel::Node::Source& source)
        {
            Connection c;

            c.sourceName = getNodeName (graphNode, source.sourceEndpointInstance, true) + ":" + std::string (source.sourceEndpointInstance.getEndpoint (true)->getName());
            c.targetName = getNodeName (graphNode, source.destEndpointInstance, false) + ":" + std::string (source.destEndpointInstance.getEndpoint (false)->getName());

            connections.push_back (c);
        }

        std::vector<Node> nodes;
        std::vector<Connection> connections;

        const AST::Program& program;
        const AST::ProcessorBase& mainProcessor;
        AST::PooledString mainProcessorName;
    };
}
