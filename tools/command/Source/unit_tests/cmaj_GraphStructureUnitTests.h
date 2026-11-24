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

#include "choc/text/choc_JSON.h"
#include "../cmaj_command_PatchGraphHTML.h"

/// Structural tests for the "graph" code generation target. These parse the JSON
/// and check the properties that matter, rather than comparing against a golden string,
/// so they don't break when unrelated details (ordering, formatting) change.
namespace cmaj::graph_structure_tests
{
    /// Compiles some source and returns the parsed "graph" output, or a void value
    /// if anything failed
    static choc::value::Value generateGraphStructure (choc::test::TestProgress& progress, const std::string& source)
    {
        auto engine = cmaj::Engine::create();
        cmaj::DiagnosticMessageList messages;
        cmaj::Program program;

        if (! program.parse (messages, "test", source))
        {
            CHOC_FAIL ("Failed to parse: " + messages.toString());
            return {};
        }

        engine.setBuildSettings (cmaj::BuildSettings()
                                    .setFrequency (44100)
                                    .setSessionID (123456));

        if (! engine.load (messages, program, {}, {}))
        {
            CHOC_FAIL ("Failed to load: " + messages.toString());
            return {};
        }

        auto output = engine.generateCode ("graph", {});

        if (output.messages.hasErrors() || output.generatedCode.empty())
        {
            CHOC_FAIL ("Failed to generate: " + output.messages.toString());
            return {};
        }

        try
        {
            auto json = choc::json::parse (output.generatedCode);
            CHOC_EXPECT_EQ (json["format"].getString(), "cmajor-patch-graph");
            CHOC_EXPECT_EQ (json["version"].getWithDefault<int32_t> (0), 1);
            return json;
        }
        catch (const std::exception& e)
        {
            CHOC_FAIL (std::string ("Invalid JSON: ") + e.what());
        }

        return {};
    }

    static choc::value::ValueView findNode (const choc::value::ValueView& graph, std::string_view name)
    {
        if (graph.hasObjectMember ("nodes"))
            for (auto node : graph["nodes"])
                if (node["name"].getString() == name)
                    return node;

        return {};
    }

    static std::string getNodeName (const choc::value::ValueView& graph, const choc::value::ValueView& end)
    {
        if (end["node"].isVoid())
            return {};

        for (auto node : graph["nodes"])
            if (node["id"].getString() == end["node"].getString())
                return std::string (node["name"].getString());

        return "<unknown node>";
    }

    /// Finds a connection within a graph. A node name of "" means the graph's own endpoint.
    static choc::value::ValueView findConnection (const choc::value::ValueView& graph,
                                                  std::string_view fromNode, std::string_view fromEndpoint,
                                                  std::string_view toNode, std::string_view toEndpoint)
    {
        for (auto c : graph["connections"])
            if (getNodeName (graph, c["from"]) == fromNode && c["from"]["endpoint"].getString() == fromEndpoint
                 && getNodeName (graph, c["to"]) == toNode && c["to"]["endpoint"].getString() == toEndpoint)
                return c;

        return {};
    }

    static choc::value::ValueView findEndpoint (const choc::value::ValueView& node, std::string_view listName, std::string_view name)
    {
        for (auto e : node[listName])
            if (e["name"].getString() == name)
                return e;

        return {};
    }

    /// True if the value is a { file, line, column } location at the given line of the test source
    static bool isLocationAtLine (const choc::value::ValueView& location, int32_t line)
    {
        return location.isObject()
                && location["file"].getWithDefault<std::string> ({}) == "test"
                && location["line"].getWithDefault<int32_t> (0) == line
                && location["column"].getWithDefault<int32_t> (0) > 0;
    }

    static constexpr auto gainProcessorSource = R"(
processor GainProcessor
{
    input  stream float in;
    output stream float out;

    void main()
    {
        loop
        {
            out <- in * 0.5f;
            advance();
        }
    }
}
)";

    //==============================================================================
    static void checkSimpleGraph (choc::test::TestProgress& progress)
    {
        CHOC_TEST (SimpleGraph);

        auto json = generateGraphStructure (progress, std::string (R"(
graph TwoGains [[ main ]]
{
    input  stream float in;
    input  value float level [[ name: "Level", min: 0, max: 1 ]];
    output stream float out;

    node gain1 = GainProcessor;
    node gain2 = GainProcessor;

    connection in -> gain1 -> gain2 -> out;
}
)") + gainProcessorSource);

        if (json.isVoid())
            return;

        auto root = json["root"];
        CHOC_EXPECT_EQ (root["name"].getString(), "TwoGains");
        CHOC_EXPECT_EQ (root["processor"].getString(), "TwoGains");
        CHOC_EXPECT_TRUE (root["isGraph"].getBool());
        CHOC_EXPECT_TRUE (isLocationAtLine (root["source"], 2));
        CHOC_EXPECT_EQ (root["path"].getString(), "");
        CHOC_EXPECT_FALSE (root.hasObjectMember ("arraySize"));

        CHOC_EXPECT_EQ (root["inputs"].size(), 2u);
        CHOC_EXPECT_EQ (root["outputs"].size(), 1u);

        auto in = findEndpoint (root, "inputs", "in");
        CHOC_EXPECT_EQ (in["endpointType"].getString(), "stream");
        CHOC_EXPECT_EQ (in["dataTypes"].size(), 1u);
        CHOC_EXPECT_EQ (in["dataTypes"][0]["name"].getString(), "float32");
        CHOC_EXPECT_TRUE (isLocationAtLine (in["source"], 4));
        CHOC_EXPECT_EQ (in["path"].getString(), "in");

        auto level = findEndpoint (root, "inputs", "level");
        CHOC_EXPECT_EQ (level["endpointType"].getString(), "value");
        CHOC_EXPECT_EQ (level["annotation"]["name"].getString(), "Level");

        CHOC_EXPECT_EQ (root["nodes"].size(), 2u);

        auto gain1 = findNode (root, "gain1");
        CHOC_EXPECT_FALSE (gain1.isVoid());
        CHOC_EXPECT_EQ (gain1["processor"].getString(), "GainProcessor");
        CHOC_EXPECT_FALSE (gain1["isGraph"].getBool());
        CHOC_EXPECT_FALSE (gain1["isImplicit"].getBool());
        CHOC_EXPECT_TRUE (isLocationAtLine (gain1["nodeSource"], 8));
        CHOC_EXPECT_EQ (gain1["path"].getString(), "gain1");
        CHOC_EXPECT_EQ (gain1["inputs"][0]["path"].getString(), "gain1.in");
        CHOC_EXPECT_FALSE (gain1.hasObjectMember ("nodes"));
        CHOC_EXPECT_EQ (gain1["inputs"][0]["name"].getString(), "in");
        CHOC_EXPECT_EQ (gain1["outputs"][0]["name"].getString(), "out");

        // node ids must be unique
        CHOC_EXPECT_TRUE (gain1["id"].getString() != findNode (root, "gain2")["id"].getString());
        CHOC_EXPECT_TRUE (gain1["id"].getString() != root["id"].getString());

        // the chain is split into three separate connections
        CHOC_EXPECT_EQ (root["connections"].size(), 3u);
        CHOC_EXPECT_FALSE (findConnection (root, "", "in", "gain1", "in").isVoid());
        CHOC_EXPECT_FALSE (findConnection (root, "gain1", "out", "gain2", "in").isVoid());
        CHOC_EXPECT_FALSE (findConnection (root, "gain2", "out", "", "out").isVoid());

        auto first = findConnection (root, "", "in", "gain1", "in");
        CHOC_EXPECT_TRUE (first["from"]["node"].isVoid());
        CHOC_EXPECT_EQ (first["path"].getString(), "in -> gain1.in");
        CHOC_EXPECT_EQ (first["from"]["path"].getString(), "in");
        CHOC_EXPECT_EQ (first["to"]["path"].getString(), "gain1.in");
        CHOC_EXPECT_EQ (findConnection (root, "gain1", "out", "gain2", "in")["path"].getString(), "gain1.out -> gain2.in");
        CHOC_EXPECT_FALSE (first.hasObjectMember ("delay"));
        CHOC_EXPECT_FALSE (first.hasObjectMember ("interpolation"));
    }

    //==============================================================================
    static void checkNestedGraphsAndArrays (choc::test::TestProgress& progress)
    {
        CHOC_TEST (NestedGraphsAndArrays);

        auto json = generateGraphStructure (progress, std::string (R"(
graph Outer [[ main ]]
{
    input  stream float in;
    output stream float out;

    node voices = GainProcessor[4];
    node fast   = GainProcessor * 2;
    node slow   = GainProcessor / 2;
    node inner  = Inner;

    connection
    {
        in -> voices.in;
        voices[1].out -> fast.in;
        fast.out -> slow.in;
        slow.out -> inner.in;
        voices.out -> inner.in;
        inner.out -> out;
    }
}

graph Inner
{
    input  stream float in;
    output stream float out;

    connection in -> GainProcessor -> out;
}
)") + gainProcessorSource);

        if (json.isVoid())
            return;

        auto root = json["root"];

        auto voices = findNode (root, "voices");
        CHOC_EXPECT_EQ (voices["arraySize"].getWithDefault<int32_t> (0), 4);

        auto fast = findNode (root, "fast");
        CHOC_EXPECT_EQ (fast["clockMultiplier"].getWithDefault<int32_t> (0), 2);
        CHOC_EXPECT_FALSE (fast.hasObjectMember ("clockDivider"));
        CHOC_EXPECT_FALSE (fast.hasObjectMember ("arraySize"));

        auto slow = findNode (root, "slow");
        CHOC_EXPECT_EQ (slow["clockDivider"].getWithDefault<int32_t> (0), 2);

        auto indexed = findConnection (root, "voices", "out", "fast", "in");
        CHOC_EXPECT_FALSE (indexed.isVoid());
        CHOC_EXPECT_EQ (indexed["from"]["nodeIndex"].getWithDefault<int32_t> (-1), 1);
        CHOC_EXPECT_EQ (indexed["path"].getString(), "voices[1].out -> fast.in");
        CHOC_EXPECT_FALSE (findConnection (root, "", "in", "voices", "in")["to"].hasObjectMember ("nodeIndex"));

        auto inner = findNode (root, "inner");
        CHOC_EXPECT_TRUE (inner["isGraph"].getBool());
        CHOC_EXPECT_EQ (inner["processor"].getString(), "Inner");
        CHOC_EXPECT_EQ (inner["nodes"].size(), 1u);

        // the inner graph's connections live in the inner graph, not the outer one
        CHOC_EXPECT_FALSE (findConnection (root, "slow", "out", "inner", "in").isVoid());
        CHOC_EXPECT_FALSE (findConnection (root, "inner", "out", "", "out").isVoid());
        CHOC_EXPECT_EQ (inner["connections"].size(), 2u);

        // the node created by the inline chain is flagged as implicit, and named after its processor
        auto implicitNode = inner["nodes"][0];
        CHOC_EXPECT_TRUE (implicitNode["isImplicit"].getBool());
        CHOC_EXPECT_EQ (implicitNode["name"].getString(), "GainProcessor");
        CHOC_EXPECT_FALSE (implicitNode.hasObjectMember ("nodeSource"));
        CHOC_EXPECT_EQ (implicitNode["path"].getString(), "inner.GainProcessor");
        CHOC_EXPECT_EQ (findConnection (inner, "", "in", "GainProcessor", "in")["path"].getString(), "inner.in -> inner.GainProcessor.in");
        CHOC_EXPECT_FALSE (findConnection (inner, "", "in", "GainProcessor", "in").isVoid());
        CHOC_EXPECT_FALSE (findConnection (inner, "GainProcessor", "out", "", "out").isVoid());
    }

    //==============================================================================
    static void checkDelayedAndEventConnections (choc::test::TestProgress& progress)
    {
        CHOC_TEST (DelayedAndEventConnections);

        auto json = generateGraphStructure (progress, R"(
graph Feedback [[ main ]]
{
    input  stream float in;
    input  event  (int, float) eventIn;
    output stream float out;
    output event  (int, float) eventOut;

    node mixer = Mixer;

    connection
    {
        in -> mixer.in1;
        mixer.out -> [10] -> mixer.in2;
        mixer.out -> out;
        eventIn -> [5] -> eventOut;
    }
}

processor Mixer
{
    input  stream float in1, in2;
    output stream float out;

    void main()
    {
        loop
        {
            out <- (in1 + in2) * 0.5f;
            advance();
        }
    }
}
)");

        if (json.isVoid())
            return;

        auto root = json["root"];

        // delayed connections are reported as connections rather than as synthesised delay nodes
        CHOC_EXPECT_EQ (root["nodes"].size(), 1u);
        CHOC_EXPECT_EQ (root["nodes"][0]["name"].getString(), "mixer");

        auto feedback = findConnection (root, "mixer", "out", "mixer", "in2");
        CHOC_EXPECT_FALSE (feedback.isVoid());
        CHOC_EXPECT_EQ (feedback["delay"].getWithDefault<int32_t> (0), 10);
        CHOC_EXPECT_TRUE (isLocationAtLine (feedback["source"], 14));

        auto eventDelay = findConnection (root, "", "eventIn", "", "eventOut");
        CHOC_EXPECT_FALSE (eventDelay.isVoid());
        CHOC_EXPECT_EQ (eventDelay["delay"].getWithDefault<int32_t> (0), 5);

        auto eventIn = findEndpoint (root, "inputs", "eventIn");
        CHOC_EXPECT_EQ (eventIn["endpointType"].getString(), "event");
        CHOC_EXPECT_EQ (eventIn["dataTypes"].size(), 2u);
        CHOC_EXPECT_EQ (eventIn["dataTypes"][0]["name"].getString(), "int32");
        CHOC_EXPECT_EQ (eventIn["dataTypes"][1]["name"].getString(), "float32");
        CHOC_EXPECT_FALSE (eventIn["dataTypes"][0].hasObjectMember ("source"));

        auto undelayed = findConnection (root, "mixer", "out", "", "out");
        CHOC_EXPECT_FALSE (undelayed.isVoid());
        CHOC_EXPECT_FALSE (undelayed.hasObjectMember ("delay"));
        CHOC_EXPECT_FALSE (undelayed["from"].hasObjectMember ("endpointIndex"));
    }

    //==============================================================================
    static void checkEndpointArrayElements (choc::test::TestProgress& progress)
    {
        CHOC_TEST (EndpointArrayElements);

        auto json = generateGraphStructure (progress, R"(
graph Taps [[ main ]]
{
    input  stream float in;
    output stream float out;

    node splitter = Splitter;

    connection
    {
        in -> splitter.in;
        splitter.taps[2] -> out;
    }
}

processor Splitter
{
    input  stream float in;
    output stream float taps[3];

    void main()
    {
        loop
        {
            taps[0] <- in;
            taps[1] <- in * 0.5f;
            taps[2] <- in * 0.25f;
            advance();
        }
    }
}
)");

        if (json.isVoid())
            return;

        auto root = json["root"];
        auto splitter = findNode (root, "splitter");
        CHOC_EXPECT_EQ (findEndpoint (splitter, "outputs", "taps")["arraySize"].getWithDefault<int32_t> (0), 3);

        auto tap = findConnection (root, "splitter", "taps", "", "out");
        CHOC_EXPECT_FALSE (tap.isVoid());
        CHOC_EXPECT_EQ (tap["from"]["endpointIndex"].getWithDefault<int32_t> (-1), 2);
        CHOC_EXPECT_EQ (tap["path"].getString(), "splitter.taps[2] -> out");
        CHOC_EXPECT_FALSE (tap["from"].hasObjectMember ("nodeIndex"));
        CHOC_EXPECT_FALSE (findConnection (root, "", "in", "splitter", "in")["to"].hasObjectMember ("endpointIndex"));
    }

    //==============================================================================
    static void checkInterpolation (choc::test::TestProgress& progress)
    {
        CHOC_TEST (Interpolation);

        auto json = generateGraphStructure (progress, std::string (R"(
graph Oversampled [[ main ]]
{
    input  stream float in;
    output stream float out;

    node fast = GainProcessor * 4;

    connection
    {
        [linear] in -> fast.in;
        fast.out -> out;
    }
}
)") + gainProcessorSource);

        if (json.isVoid())
            return;

        auto root = json["root"];
        CHOC_EXPECT_EQ (findConnection (root, "", "in", "fast", "in")["interpolation"].getString(), "linear");
        CHOC_EXPECT_FALSE (findConnection (root, "fast", "out", "", "out").hasObjectMember ("interpolation"));
        CHOC_EXPECT_EQ (findNode (root, "fast")["clockMultiplier"].getWithDefault<int32_t> (0), 4);
    }

    //==============================================================================
    static void checkTypeSources (choc::test::TestProgress& progress)
    {
        CHOC_TEST (TypeSources);

        auto json = generateGraphStructure (progress, R"(
namespace types
{
    struct Note
    {
        int pitch;
    }

    enum Mode { quiet, loud }

    using Sample = float32;
}

processor Typed [[ main ]]
{
    input  event types::Note noteIn;
    input  value types::Note[4] chordIn;
    input  event types::Mode modeIn;
    input  event std::midi::Message midiIn;
    output stream types::Sample<2> out;

    void main()
    {
        loop
        {
            advance();
        }
    }
}
)");

        if (json.isVoid())
            return;

        auto root = json["root"];
        CHOC_EXPECT_FALSE (root.hasObjectMember ("nodes"));

        auto getType = [&] (std::string_view listName, std::string_view endpoint) { return findEndpoint (root, listName, endpoint)["dataTypes"][0]; };

        // a struct links to its declaration
        auto note = getType ("inputs", "noteIn");
        CHOC_EXPECT_EQ (note["name"].getString(), "Note");
        CHOC_EXPECT_EQ (note["fullName"].getString(), "types::Note");
        CHOC_EXPECT_TRUE (isLocationAtLine (note["source"], 4));

        // an array of a struct links to the struct
        auto chord = getType ("inputs", "chordIn");
        CHOC_EXPECT_EQ (chord["name"].getString(), "Note[4]");
        CHOC_EXPECT_TRUE (isLocationAtLine (chord["source"], 4));

        auto mode = getType ("inputs", "modeIn");
        CHOC_EXPECT_TRUE (isLocationAtLine (mode["source"], 9));

        // a struct isn't an alias, so it has no resolvedName
        CHOC_EXPECT_FALSE (note.hasObjectMember ("resolvedName"));

        // a vector of an alias is named as written, and links to the alias, not to the
        // built-in type it refers to
        auto sample = getType ("outputs", "out");
        CHOC_EXPECT_TRUE (isLocationAtLine (sample["source"], 11));
        CHOC_EXPECT_EQ (sample["name"].getString(), "Sample<2>");
        CHOC_EXPECT_EQ (sample["fullName"].getString(), "types::Sample<2>");
        CHOC_EXPECT_EQ (sample["resolvedName"].getString(), "float32<2>");

        // the standard library isn't in a file, so its types have names but no source
        auto midi = getType ("inputs", "midiIn");
        CHOC_EXPECT_EQ (midi["name"].getString(), "Message");
        CHOC_EXPECT_EQ (midi["fullName"].getString(), "std::midi::Message");
        CHOC_EXPECT_FALSE (midi.hasObjectMember ("source"));
    }

    //==============================================================================
    /// Checks that the graph view's files are embedded (i.e. that create_embedded_files.py has
    /// been run since they were added), and that the HTML page made from them is complete
    static void checkPatchGraphHTML (choc::test::TestProgress& progress)
    {
        CHOC_TEST (PatchGraphHTML);

        auto& assets = cmaj::EmbeddedAssets::getInstance();
        auto isModuleInList = [] (std::string_view name)
        {
            for (auto m : patch_graph_html::moduleNames)
                if (name == m)
                    return true;

            return false;
        };

        CHOC_EXPECT_FALSE (assets.findContent (patch_graph_html::templateName).empty());
        CHOC_EXPECT_FALSE (assets.findContent ("panel_api/cmaj-graph.js").empty());
        CHOC_EXPECT_FALSE (assets.findContent (patch_graph_html::tokensName).empty());

        for (auto name : patch_graph_html::moduleNames)
        {
            auto content = assets.findContent (std::string (patch_graph_html::moduleFolder) + name);
            CHOC_EXPECT_FALSE (content.empty());

            // every module that one of them imports must be inlined into the page too
            for (auto& line : choc::text::splitIntoLines (content, false))
            {
                auto start = line.find ("from \"./");

                if (start == std::string::npos || ! choc::text::startsWith (choc::text::trimStart (line), "import"))
                    continue;

                start += std::string_view ("from \"./").length();
                CHOC_EXPECT_TRUE (isModuleInList (line.substr (start, line.find ('"', start) - start)));
            }
        }

        auto json = choc::json::toString (choc::json::create ("format", "cmajor-patch-graph",
                                                              "version", 1,
                                                              "root", choc::json::create ("id", "n0", "name", "</script><b>")), false);

        auto html = patch_graph_html::createPatchGraphHTML (json, "A & <B>");

        CHOC_EXPECT_TRUE (html.find ("PATCH_GRAPH_") == std::string::npos);
        CHOC_EXPECT_TRUE (html.find ("<title>A &amp; &lt;B&gt;</title>") != std::string::npos);
        CHOC_EXPECT_TRUE (html.find ("export class PatchGraphView") != std::string::npos);
        CHOC_EXPECT_TRUE (html.find ("--cmaj-graph-background:") != std::string::npos);   // (the design tokens)
        CHOC_EXPECT_TRUE (html.find ("<\\/script><b>") != std::string::npos);

        // the only closing script tag is the page's own
        CHOC_EXPECT_EQ (html.find ("</script>"), html.rfind ("</script>"));

        // a value that happens to contain a placeholder's name is inserted as it is
        CHOC_EXPECT_TRUE (patch_graph_html::fillInPlaceholders ("[A] [B]", {{ "A", "B" }, { "B", "A" }}) == "[B] [A]");
    }

    //==============================================================================
    /// Resolves an import specifier against the embedded file that contains it, returning
    /// the embedded asset path it refers to
    static std::string resolveAssetImport (std::string_view importingFile, std::string_view specifier)
    {
        std::vector<std::string> parts;

        if (! choc::text::startsWith (specifier, "/"))
            for (auto& p : choc::text::splitString (importingFile, '/', false))
                parts.push_back (p);

        if (! parts.empty() && ! choc::text::startsWith (specifier, "/"))
            parts.pop_back();   // (the importing file's own name)

        for (auto& p : choc::text::splitString (specifier, '/', false))
        {
            if (p.empty() || p == ".")
                continue;

            if (p == "..")
            {
                if (! parts.empty())
                    parts.pop_back();
            }
            else
            {
                parts.push_back (p);
            }
        }

        return choc::text::joinStrings (parts, "/");
    }

    /// Checks that every module and stylesheet the patch panel imports is embedded (i.e. that
    /// create_embedded_files.py has been run since they were added, and nothing refers to a file
    /// that has been removed)
    static void checkPatchPanelAssets (choc::test::TestProgress& progress)
    {
        CHOC_TEST (PatchPanelAssets);

        auto& assets = cmaj::EmbeddedAssets::getInstance();
        auto names = cmaj::EmbeddedAssets::getFileNames();
        size_t numFilesChecked = 0, numImportsChecked = 0;

        auto isAvailable = [&] (const std::string& path)
        {
            if (path == "cmaj-patch-server.js")     // (generated by the server for each session)
                return true;

            if (choc::text::startsWith (path, "cmaj_api/"))
                return ! assets.findContent (path).empty();

            return std::find (names.begin(), names.end(), path) != names.end();
        };

        CHOC_EXPECT_TRUE (isAvailable ("panel_api/cmaj-patch-panel.js"));
        CHOC_EXPECT_TRUE (isAvailable ("panel_api/theme/panel.css"));

        for (auto name : names)
        {
            bool isPanelFile = choc::text::startsWith (name, "panel_api/") || choc::text::startsWith (name, "embedded_patch_");

            if (! isPanelFile)
                continue;

            ++numFilesChecked;

            for (auto& line : choc::text::splitIntoLines (assets.getContent (name), false))
            {
                auto trimmed = choc::text::trimStart (line);

                // import ... from "x";  import "x";  @import url("x");
                if (choc::text::startsWith (trimmed, "import ") || choc::text::startsWith (trimmed, "@import"))
                {
                    auto lastQuote = trimmed.find_last_of ("\"'");

                    if (lastQuote == std::string::npos)
                        continue;

                    auto firstQuote = trimmed.find_last_of ("\"'", lastQuote - 1);

                    if (firstQuote == std::string::npos)
                        continue;

                    auto specifier = trimmed.substr (firstQuote + 1, lastQuote - firstQuote - 1);

                    if (specifier.empty() || choc::text::contains (specifier, "://"))
                        continue;

                    auto resolved = resolveAssetImport (name, specifier);
                    ++numImportsChecked;

                    if (! isAvailable (resolved))
                        CHOC_FAIL ("Missing asset: " + std::string (name) + " imports " + resolved);
                }
            }
        }

        CHOC_EXPECT_TRUE (numFilesChecked > 30);
        CHOC_EXPECT_TRUE (numImportsChecked > 50);
    }

    //==============================================================================
    void runUnitTests (choc::test::TestProgress& progress)
    {
        CHOC_CATEGORY (GraphStructure);

        checkSimpleGraph (progress);
        checkNestedGraphsAndArrays (progress);
        checkDelayedAndEventConnections (progress);
        checkEndpointArrayElements (progress);
        checkInterpolation (progress);
        checkTypeSources (progress);
        checkPatchGraphHTML (progress);
        checkPatchPanelAssets (progress);
    }
}
