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
#include "../../../include/cmajor/helpers/cmaj_PatchWorker_QuickJS.h"
#include "../../../include/cmajor/helpers/cmaj_PatchWorker_WebView.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedAssets.h"
#include "../../../modules/embedded_assets/cmaj_EmbeddedWamAssets.h"

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

   #if CMAJ_ENABLE_CODEGEN_LLVM_WASM
    list.push_back ("webaudio-html");
    list.push_back ("webaudio");
    list.push_back ("javascript");
    list.push_back ("wast");
    list.push_back ("wam");
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
    if (type == "wam")            return "Converts a patch to a Javascript web audio module";
    if (type == "wast")           return "Compiles a patch to a chunk of WAST code";
    if (type == "llvm")           return "Dumps the assembly code for a patch or set of .cmajor files for the specified target architecture";

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

//==============================================================================
void generateFromPatch (choc::ArgumentList& args,
                        std::filesystem::path patchManifestFile,
                        std::string targetType,
                        std::string outputFile,
                        const cmaj::BuildSettings& buildSettings,
                        const choc::value::Value& engineOptions)
{
    cmaj::Patch patch;

    patch.createEngine = [&]
    {
        auto engine = cmaj::Engine::create();
        engine.setBuildSettings (buildSettings);
        return engine;
    };

    patch.statusChanged = [] (const cmaj::Patch::Status& s)
    {
        std::cout << s.statusMessage << std::endl;

        if (s.messageList.hasWarnings())
            std::cout << s.messageList.toString() << std::endl;
    };

    patch.setHostDescription ("Cmajor Generate");

    if (engineOptions.isObject() && engineOptions["worker"].toString() == "quickjs")
        enableQuickJSPatchWorker (patch);
    else
        enableWebViewPatchWorker (patch);

    patch.setPlaybackParams ({ 44100, buildSettings.getMaxBlockSize(), 0, 0 });

    cmaj::Patch::LoadParams loadParams;
    loadParams.manifest.initialiseWithFile (patchManifestFile);

    if (targetType == "juce")  return generate_cpp::generatePluginProject (args, outputFile, patch, loadParams, false);
    if (targetType == "clap")  return generate_cpp::generatePluginProject (args, outputFile, patch, loadParams, true);

   #if CMAJ_ENABLE_CODEGEN_LLVM_WASM

    if (targetType == "webaudio-html")
        return generate_javascript::generateWebAudioHTML (patch, loadParams, engineOptions).writeToOutputFolder (outputFile);

    if (targetType == "wam")
        return generate_javascript::generateWebAudioModule (patch, loadParams, engineOptions).writeToOutputFolder (outputFile);

    if (targetType == "webaudio")
        return writeToFolderOrConsole (outputFile, generate_javascript::generateJavascriptWorklet (patch, loadParams, engineOptions));
   #endif

    std::string optionsJSON;

    if (targetType == "llvm")
    {
        auto options = choc::value::createObject ({});

        if (auto triple = args.removeValueFor ("--targetTriple"))
            options.addMember ("targetTriple", *triple);

        if (auto format = args.removeValueFor ("--targetFormat"))
            options.addMember ("targetFormat", *format);

        optionsJSON = choc::json::toString (options, false);
    }

    writeToFolderOrConsole (outputFile, generateCodeAndCheckResult (patch, loadParams, targetType, optionsJSON).generatedCode);
}

//==============================================================================
void generate (choc::ArgumentList& args,
               const choc::value::Value& engineOptions,
               cmaj::BuildSettings& buildSettings)
{
    if (auto blockSize = args.removeIntValue<uint32_t> ("--maxFramesPerBlock"))
        buildSettings.setMaxBlockSize (*blockSize);
    else
        buildSettings.setMaxBlockSize (512);

    std::string target;

    if (auto t = args.removeValueFor ("--target"))
        target = *t;
    else
        throw std::runtime_error ("Expected an argument --target=<format wanted>");

    auto possibleTargets = getCodeGenTargetList();

    if (std::find (possibleTargets.begin(), possibleTargets.end(), target) == possibleTargets.end())
        throw std::runtime_error ("Unknown target \"" + target
                                    + "\"\n\nAvailable values for --target are:\n\n" + getCodeGenTargetHelp());

    std::string outputFile;

    if (args.contains ("--output"))
        outputFile = args.getExistingFile ("--output", true).string();

    bool shouldObfuscate = args.removeIfFound ("--obfuscate");

    if (args.empty())
        throw std::runtime_error ("Expected the name of a Cmajor file to compile");

    if (target == "module" && args[0] == "std_library")  // special magic name to generate binary std lib
        return generateBinaryStdLib (outputFile);

    auto getCmajorFileList = [&]
    {
        std::vector<std::string> files;

        for (auto& f : args.getAllAsExistingFiles())
            files.push_back (f.string());

        return files;
    };

    if (target == "module")      return generateBinaryModule    (outputFile, getCmajorFileList(), shouldObfuscate);
    if (target == "syntaxtree")  return generateJSONSyntaxTree  (outputFile, getCmajorFileList());
    if (target == "html")        return generateHTMLDocs        (outputFile, getCmajorFileList());

    auto findPatchManifestFile = [&]
    {
        for (auto& f : args.getAllAsExistingFiles())
            if (f.extension() == ".cmajorpatch")
                return f;

        throw std::runtime_error ("Expected a .cmajorpatch file");
    };

    generateFromPatch (args, findPatchManifestFile(), target, outputFile, buildSettings, engineOptions);
}
