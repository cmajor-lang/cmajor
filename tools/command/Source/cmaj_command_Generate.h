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

#include "../../../include/cmajor/API/cmaj_Program.h"
#include "../../../include/cmajor/helpers/cmaj_Patch.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedAssets.h"

#include "cmaj_command_GenerateHelpers.h"
#include "cmaj_command_GenerateJavascript.h"
#include "cmaj_command_GeneratePlugin.h"

void generateBinaryModule (const std::string& outputFile, const std::vector<std::string>& sourceFiles, bool shouldObfuscate);
void generateJSONSyntaxTree (const std::string& outputFile, const std::vector<std::string>& sourceFiles);
void generateHTMLDocs (const std::string& outputFile, const std::vector<std::string>& sourceFiles);
void generateBinaryStdLib (const std::string& outputFile);

static std::vector<std::string> getCodeGenTargetList()
{
    auto list = cmaj::Engine::create().getAvailableCodeGenTargetTypes();

   #if CMAJ_ENABLE_CODEGEN_BINARYEN || CMAJ_ENABLE_CODEGEN_LLVM_WASM
    list.push_back ("webaudio-html");
    list.push_back ("webaudio");
    list.push_back ("javascript");
   #endif
    list.push_back ("juce");
    list.push_back ("clap");
    list.push_back ("module");
    list.push_back ("syntaxtree");
    list.push_back ("html");

    return list;
}

static std::string getCodeGenTargetHelp (std::string_view type)
{
    if (type == "juce")           return "Converts a patch to a self-contained JUCE plugin project";
    if (type == "clap")           return "Converts a patch to a self-contained CLAP plugin project";
    if (type == "module")         return "Compiles a list of .cmajor files into a binary-format Cmajor module";
    if (type == "syntaxtree")     return "Dumps a JSON syntax tree for the code in a patch or some .cmajor files";
    if (type == "html")           return "Generates HTML documentation for some cmajor files";
    if (type == "graph")          return "Generates a graphviz diagram to show a patch's structure";
    if (type == "cpp")            return "Converts a patch to a self-contained raw C++ class";
    if (type == "javascript")     return "Converts a patch to a Javascript/WebAssembly class";
    if (type == "webaudio")       return "Converts a patch to Javascript/WebAssembly with WebAudio helpers";
    if (type == "webaudio-html")  return "Converts a patch to some HTML/Javascript which plays the patch and shows its GUI";
    if (type == "wast")           return "Compiles a patch to a chunk of WAST code";
    if (type == "llvm")           return "Dumps the LLVM IR for a patch or set of .cmajor files";

    return {};
}

static std::string getCodeGenTargetHelp()
{
    choc::text::TextTable table;

    for (auto& target : getCodeGenTargetList())
    {
        table << ("    --target=" + target) << getCodeGenTargetHelp (target);
        table.newRow();
    }

    return table.toString ({}, "  ", "\n");
}

static std::string getTargetWithoutBinaryenSuffix (const std::string& targetType)
{
   #if CMAJ_ENABLE_CODEGEN_BINARYEN
    if (choc::text::endsWith (targetType, "-binaryen"))
        return targetType.substr (0, targetType.length() - std::string_view ("-binaryen").length());
   #endif

   return targetType;
}

//==============================================================================
void generateFromPatch (juce::ArgumentList& args,
                        std::filesystem::path patchManifestFile,
                        std::string targetType,
                        std::string outputFile,
                        const cmaj::BuildSettings& buildSettings)
{
    cmaj::Patch patch (true, false);

    patch.createEngine = [&]
    {
        auto engine = cmaj::Engine::create();
        engine.setBuildSettings (buildSettings);
        return engine;
    };

    patch.setPlaybackParams ({ 44100, buildSettings.getMaxBlockSize(), 0, 0 });

    cmaj::Patch::LoadParams loadParams;
    loadParams.manifest.initialiseWithFile (patchManifestFile);

    if (targetType == "juce")  return generate_cpp::generatePluginProject (args, outputFile, patch, loadParams, false);
    if (targetType == "clap")  return generate_cpp::generatePluginProject (args, outputFile, patch, loadParams, true);

   #if CMAJ_ENABLE_CODEGEN_BINARYEN || CMAJ_ENABLE_CODEGEN_LLVM_WASM
    bool useBinaryen = choc::text::endsWith (targetType, "-binaryen");

    if (getTargetWithoutBinaryenSuffix (targetType) == "webaudio-html")
        return generate_javascript::generateWebAudioHTML (patch, loadParams, useBinaryen).writeToOutputFolder (outputFile);

    if (getTargetWithoutBinaryenSuffix (targetType) == "webaudio")
        return writeToFolderOrConsole (outputFile, generate_javascript::generateJavascriptWorklet (patch, loadParams, useBinaryen));
   #endif

    std::string optionsJSON;

    if (targetType == "llvm")
    {
        auto options = choc::value::createObject ({});

        if (args.containsOption ("--targetTriple"))
            options.addMember ("targetTriple", args.removeValueForOption ("--targetTriple").toStdString());

        if (args.containsOption ("--targetFormat"))
            options.addMember ("targetFormat", args.removeValueForOption ("--targetFormat").toStdString());

        optionsJSON = choc::json::toString (options, false);
    }

    writeToFolderOrConsole (outputFile, generateCodeAndCheckResult (patch, loadParams, targetType, optionsJSON).generatedCode);
}

//==============================================================================
void generate (juce::ArgumentList& args, cmaj::BuildSettings& buildSettings)
{
    std::string target;

    if (args.containsOption ("--maxFramesPerBlock"))
        buildSettings.setMaxBlockSize (static_cast<uint32_t> (args.removeValueForOption ("--maxFramesPerBlock").getIntValue()));
    else
        buildSettings.setMaxBlockSize (512);

    if (args.containsOption ("--eventBufferSize"))
        buildSettings.setEventBufferSize (static_cast<uint32_t> (args.removeValueForOption ("--eventBufferSize").getIntValue()));
    else
        buildSettings.setEventBufferSize (32);

    if (args.containsOption ("--target"))
        target = args.removeValueForOption ("--target").toStdString();
    else
        throw std::runtime_error ("Expected an argument --target=<format wanted>");

    auto possibleTargets = getCodeGenTargetList();

    if (std::find (possibleTargets.begin(), possibleTargets.end(), getTargetWithoutBinaryenSuffix (target)) == possibleTargets.end())
        throw std::runtime_error ("Unknown target \"" + target
                                    + "\"\n\nAvailable values for --target are:\n\n" + getCodeGenTargetHelp());

    std::string outputFile;

    if (args.containsOption ("--output"))
        outputFile = args.getFileForOptionAndRemove ("--output").getFullPathName().toStdString();

    bool shouldObfuscate = args.removeOptionIfFound ("--obfuscate");

    if (args.size() == 0)
        throw std::runtime_error ("Expected the name of a Cmajor file to compile");

    if (target == "module" && args[0] == "std_library")  // special magic name to generate binary std lib
        return generateBinaryStdLib (outputFile);

    auto getCmajorFileList = [&]
    {
        std::vector<std::string> files;

        for (int i = 0; i < args.size(); ++i)
            files.push_back (args[i].resolveAsExistingFile().getFullPathName().toStdString());

        return files;
    };

    if (target == "module")      return generateBinaryModule    (outputFile, getCmajorFileList(), shouldObfuscate);
    if (target == "syntaxtree")  return generateJSONSyntaxTree  (outputFile, getCmajorFileList());
    if (target == "html")        return generateHTMLDocs        (outputFile, getCmajorFileList());

    auto findPatchManifestFile = [&]
    {
        auto isPatchManifest = [] (const auto& arg) { return ! arg.isOption() && arg.text.endsWith (".cmajorpatch"); };

        if (const auto it = std::find_if (args.arguments.begin(), args.arguments.end(), isPatchManifest); it != args.arguments.end())
            return std::filesystem::path (it->resolveAsExistingFile().getFullPathName().toStdString());

        throw std::runtime_error ("Expected a .cmajorpatch file");
    };

    generateFromPatch (args, findPatchManifestFile(), target, outputFile, buildSettings);
}
