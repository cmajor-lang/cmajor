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

#include <string>
#include <string_view>
#include <optional>

/// Stable, human-readable names for the things inside a graph: nodes, their endpoints,
/// and the connections between them.
///
/// These are the names that the "graph" structure description uses to identify
/// things, and anything that later reports runtime information about a graph (e.g. live
/// signal levels on its connections) must name things in exactly the same way, so that
/// the two can be matched up. So they should only ever be built with these functions.
///
///   node:        the dot-separated chain of node names from the main processor, which
///                itself has the empty path, e.g. "voices.osc"
///   endpoint:    "<node path>.<endpoint name>", or just the endpoint name for the main
///                processor's own endpoints, e.g. "voices.osc.out"
///   connection:  "<source endpoint> -> <destination endpoint>", where each end may also
///                carry a node index and/or endpoint index, e.g. "voices[2].out -> mix.in[1]"
namespace cmaj::graph_paths
{
    /// Returns the path of a node called nodeName inside the graph whose path is parentPath
    [[nodiscard]] inline std::string createNodePath (std::string_view parentPath, std::string_view nodeName)
    {
        if (parentPath.empty())
            return std::string (nodeName);

        return std::string (parentPath) + "." + std::string (nodeName);
    }

    /// Returns the path of an endpoint of the node (or graph) whose path is nodePath.
    /// The indexes are for connection ends that refer to one element of a node array
    /// (`voices[2].out`) or of an endpoint array (`node.out[2]`).
    [[nodiscard]] inline std::string createEndpointPath (std::string_view nodePath,
                                                         std::string_view endpointName,
                                                         std::optional<int32_t> nodeIndex = {},
                                                         std::optional<int32_t> endpointIndex = {})
    {
        std::string result (nodePath);

        if (nodeIndex)
            result += "[" + std::to_string (*nodeIndex) + "]";

        if (! result.empty())
            result += ".";

        result += endpointName;

        if (endpointIndex)
            result += "[" + std::to_string (*endpointIndex) + "]";

        return result;
    }

    /// Returns the path of a connection between two endpoint paths
    [[nodiscard]] inline std::string createConnectionPath (std::string_view sourceEndpointPath,
                                                           std::string_view destEndpointPath)
    {
        return std::string (sourceEndpointPath) + " -> " + std::string (destEndpointPath);
    }

} // namespace cmaj::graph_paths
