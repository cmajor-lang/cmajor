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

#include "../../../modules/compiler/include/cmaj_CppGenerationUtils.h"
#include "cmaj_command_EmbeddedPluginHelpersFolder.h"
#include "cmaj_command_EmbeddedIncludeFolder.h"
#include "choc/containers/choc_ZipFile.h"


namespace generate_cpp
{

inline std::string unzipCmajorHeaders (const std::filesystem::path& outputFolder)
{
    auto compressed = std::string (reinterpret_cast<const char*> (cmajorIncludeFolderZip), sizeof (cmajorIncludeFolderZip));
    auto in = std::make_shared<std::istringstream> (compressed, std::ios::binary);
    choc::zip::ZipFile zip (in);
    zip.uncompressToFolder (outputFolder / "include", true, false);
    return "include";
}

template <typename PredicateFn>
std::filesystem::path unzipCmajorPluginHelpers (const std::filesystem::path& pathToOutput, const PredicateFn& shouldExtract)
{
    auto compressed = std::string (reinterpret_cast<const char*> (cmajorPluginHelpersFolderZip), sizeof (cmajorPluginHelpersFolderZip));
    auto in = std::make_shared<std::istringstream> (compressed, std::ios::binary);
    choc::zip::ZipFile zip (in);

    std::sort (zip.items.begin(),
               zip.items.end(),
               [] (auto& a, auto& b) { return a.filename < b.filename; });

    auto targetFolder = pathToOutput / "helpers";

    for (auto& item : zip.items)
    {
        if (! shouldExtract (item.filename))
            continue;

        if (! item.uncompressToFile (targetFolder, true, false))
            throw std::runtime_error ("Failed to unpack helper files");
    }

    return "${CMAKE_CURRENT_SOURCE_DIR}/helpers";
}

inline std::string createFileData (const GeneratedFiles& files)
{
    std::string result;
    std::string fileList = "\n    static constexpr std::array files =\n    {\n";

    bool first = true;

    for (auto& f : files.files)
    {
        auto name = cmaj::cpp_utils::makeSafeIdentifier (f.filename);

        if (first)
            first = false;
        else
            fileList += ",\n";

        if (f.content.length() < 32768
             && f.content.find ('\0') == std::string_view::npos
             && choc::text::findInvalidUTF8Data (f.content.data(), f.content.size()) == nullptr)
        {
            result += "    static constexpr const char* " + name + " =\n        "
                       + cmaj::cpp_utils::createMultiLineStringLiteral (f.content, "        ") + ";\n";
        }
        else
        {
            result += "    static constexpr const char " + name + "[] = {\n        "
                       + cmaj::cpp_utils::createDataLiteral (f.content) + " };\n";
        }

        name = "std::string_view (" + name + ", " + std::to_string (f.content.length()) + ")";

        fileList += "        File { " + cmaj::cpp_utils::createStringLiteral (f.filename) + ", " + name + " }";
    }

    return result + "\n" + fileList + "\n    };\n";
}

//==============================================================================
struct GeneratedMainClass
{
    std::string mainClassName;
    std::string generatedCode;
};

inline GeneratedMainClass generateMainClass (cmaj::Patch& patch,
                                             const cmaj::Patch::LoadParams& loadParams,
                                             const std::string& performerNamespace)
{
    auto cpp = generateCodeAndCheckResult (patch, loadParams, "cpp",
                                           "{ \"bare\": false, \"namespace\": \"" + performerNamespace + "\" }");

    const auto& manifest = loadParams.manifest;

    auto manifestFilePath = std::filesystem::path (manifest.manifestFile);
    auto manifestFilename = manifestFilePath.filename().generic_string();

    GeneratedFiles embeddedFiles;

    embeddedFiles.addPatchResources (manifest);
    embeddedFiles.addFile (manifestFilename, choc::json::toString (manifest.getStrippedManifest(), true));
    embeddedFiles.sort();

    auto mainCpp = choc::text::replace (R"(
${performerClass}

struct ${mainClassName}
{
    using PerformerClass = ${performerNamespace}::${mainClassName};
    static constexpr auto filename = "${manifestFilename}";

    struct File { std::string_view name, content; };

${fileData}
};

)",
    "${performerClass}", cpp.generatedCode,
    "${manifestFilename}", manifestFilename,
    "${mainClassName}", cpp.mainClassName,
    "${performerNamespace}", performerNamespace,
    "${fileData}", createFileData (embeddedFiles)
    );

    return { cpp.mainClassName, mainCpp };
}

//==============================================================================
inline void createJucePluginFiles (GeneratedFiles& generatedFiles,
                                   cmaj::Patch& patch,
                                   const cmaj::Patch::LoadParams& loadParams,
                                   std::string cmajorIncludePath,
                                   std::string jucePath,
                                   std::optional<std::string> formats)
{
    std::string performerNamespace = "performer";

    const auto cpp = generateMainClass (patch, loadParams, performerNamespace);

    std::string mainSourceFile       = "cmajor_plugin.cpp";
    std::string projectName          = cmaj::makeSafeIdentifierName (cpp.mainClassName);
    std::string version              = loadParams.manifest.version;
    std::string productName          = cmaj::makeSafeIdentifierName (choc::text::replace (loadParams.manifest.name, " ", ""));
    std::string icon                 = loadParams.manifest.manifest["icon"].toString();
    std::string pluginCode;
    std::string manufacturerCode;
    std::string pluginFormats        = "Standalone AU AUv3 VST3";

    auto plugin = loadParams.manifest.manifest["plugin"];

    if (plugin.isObject())
    {
        pluginCode       = plugin["pluginCode"].toString();
        manufacturerCode = plugin["manufacturerCode"].toString();
    }

    if (pluginCode.empty())
    {
        std::cerr << "No plugin/pluginCode specified, defaulting to 'plug'" << std::endl;
        pluginCode = "plug";
    }

    if (manufacturerCode.empty())
    {
        std::cerr << "No plugin/manufacturerCode specified, defaulting to 'Cmaj'" << std::endl;
        manufacturerCode = "Cmaj";
    }

    if (formats)
    {
        pluginFormats = *formats;
    }

    const auto& manifest = loadParams.manifest;

    bool hasMidiIn =  false;
    bool hasMidiOut = false;
    bool hasAudioIn = false;
    bool hasAudioOut = false;

    {
        patch.unload();

        choc::messageloop::initialise();

        auto t = std::thread ([&]
        {
            patch.preload (manifest);
            choc::messageloop::stop();
        });

        choc::messageloop::run();
        t.join();
    }

    for (auto& e : patch.getInputEndpoints())
    {
        if (e.isMIDI())
            hasMidiIn = true;
        else if (e.getNumAudioChannels() != 0)
            hasAudioIn = true;
    }

    for (auto& e : patch.getOutputEndpoints())
    {
        if (e.isMIDI())
            hasMidiOut = true;
        else if (e.getNumAudioChannels() != 0)
            hasAudioOut = true;
    }

    (void) hasAudioOut;

    std::string pluginExtras;

    if (! icon.empty())
    {
        generatedFiles.addFile (icon, choc::file::loadFileAsString (loadParams.manifest.getFullPathForFile (icon)));
        pluginExtras += "ICON_BIG \"" + icon + "\"\n";
    }

    auto mainCpp = choc::text::replace (R"(//
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

// Auto-generated Cmajor code for patch '${mainClassName}'

#include <JuceHeader.h>
#include "cmajor/helpers/cmaj_JUCEPlugin.h"
#include "choc/javascript/choc_javascript_QuickJS.h"

${mainClass}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cmaj::plugin::GeneratedPlugin<::${mainClassName}> (std::make_shared<cmaj::Patch>());
}
)",
    "${mainClass}", cpp.generatedCode,
    "${mainClassName}", cpp.mainClassName
    );

    auto makefile = choc::text::replace (R"cmake(
cmake_minimum_required(VERSION 3.16)

project(
    ${projectName}
    VERSION ${version}
    LANGUAGES CXX C
)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)

${jucePath}

if (JUCE_PATH)
    add_subdirectory(${JUCE_PATH} juce)
else()
    message (FATAL_ERROR "You must define the JUCE_PATH variable to point to your local JUCE folder")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
endif()

if (MSVC)
    add_compile_options (/Zc:__cplusplus)
endif()

juce_add_plugin(${productName}
    FORMATS ${pluginFormats}
    DESCRIPTION "${description}"
    BUNDLE_ID "${ID}"
    PLUGIN_CODE "${pluginCode}"
    PLUGIN_MANUFACTURER_CODE "${manufacturerCode}"
    COMPANY_NAME "${manufacturer}"
    COMPANY_WEBSITE "${URL}"
    NEEDS_MIDI_INPUT ${hasMidiIn}
    NEEDS_MIDI_OUTPUT ${hasMidiOut}
    MICROPHONE_PERMISSION_ENABLED ${hasAudioIn}
    IS_SYNTH ${isInstrument}
    ${pluginExtras}
)

juce_generate_juce_header(${productName})

add_compile_definitions (
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:CMAJ_ENABLE_ALLOCATION_CHECKER=1>
    CMAJ_ENABLE_WEBVIEW_DEV_TOOLS=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CURL=0
    CMAJOR_DLL=1
)

file(GLOB_RECURSE HEADERS
    include/*.h
)

target_sources(${productName} PRIVATE
    ${mainSourceFile}
    ${HEADERS}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/" FILES ${HEADERS})

target_compile_features(${productName} PRIVATE cxx_std_17)

target_include_directories(${productName} PRIVATE ${cmajorIncludePath})

target_link_libraries(${productName}
    PRIVATE
        juce::juce_audio_utils
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
)cmake",
        "${projectName}", projectName,
        "${productName}", productName,
        "${version}", version,
        "${description}", manifest.description,
        "${ID}", manifest.ID,
        "${pluginCode}", pluginCode,
        "${manufacturerCode}", manufacturerCode,
        "${manufacturer}", manifest.manufacturer,
        "${URL}", manifest.manifest["URL"].getWithDefault<std::string> ({}),
        "${hasMidiIn}", hasMidiIn ? "TRUE" : "FALSE",
        "${hasMidiOut}", hasMidiOut ? "TRUE" : "FALSE",
        "${hasAudioIn}", hasAudioIn ? "TRUE" : "FALSE",
        "${isInstrument}", manifest.isInstrument ? "TRUE" : "FALSE",
        "${jucePath}", jucePath.empty() ? std::string() : "set(JUCE_PATH " + choc::text::replace (jucePath, "\\", "\\\\") + ")",
        "${mainSourceFile}", mainSourceFile,
        "${cmajorIncludePath}", cmajorIncludePath,
        "${pluginExtras}", pluginExtras,
        "${pluginFormats}", pluginFormats);

    generatedFiles.addFile (mainSourceFile, std::move (mainCpp));
    generatedFiles.addFile ("CMakeLists.txt", std::move (makefile));
}

//==============================================================================
inline void createClapPluginFiles (GeneratedFiles& generatedFiles,
                                   cmaj::Patch& patch,
                                   const cmaj::Patch::LoadParams& loadParams,
                                   const std::filesystem::path& cmajorIncludePath,
                                   const std::filesystem::path& clapIncludePath,
                                   const std::filesystem::path& clapWrapperPath,
                                   const std::filesystem::path& pathToOutput)
{
    const auto cmajorPluginHelpersPath = unzipCmajorPluginHelpers (pathToOutput, [] (const auto& path)
    {
        return choc::text::startsWith (path, "clap") || choc::text::startsWith (path, "common");
    });

    std::string pluginCode;
    std::string manufacturerCode;

    const auto& manifest = loadParams.manifest;
    auto plugin = manifest.manifest["plugin"];

    if (plugin.isObject())
    {
        pluginCode       = plugin["pluginCode"].toString();
        manufacturerCode = plugin["manufacturerCode"].toString();
    }

    if (pluginCode.empty())
    {
        std::cerr << "No plugin/pluginCode specified, defaulting to 'plug'" << std::endl;
        pluginCode = "plug";
    }

    if (manufacturerCode.empty())
    {
        std::cerr << "No plugin/manufacturerCode specified, defaulting to 'Cmaj'" << std::endl;
        manufacturerCode = "Cmaj";
    }

    const auto mainCppTemplate = R"cpp(
#include "cmaj_CLAPPlugin.h"
#include "choc/javascript/choc_javascript_QuickJS.h"

${mainClass}

extern "C"
{

CLAP_EXPORT const clap_plugin_entry clap_entry = cmaj::plugin::clap::createGeneratedCppPluginEntryPoint<::${mainClassName}>();

}
)cpp";

    const auto cmakeTemplate = R"cmake(
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)

set(CMAJ_CMAKE_PROJECT_NAME "${projectName}")
set(CMAJ_TARGET_BUNDLE_ID "${macOSBundleId}")
set(CMAJ_TARGET_PATCH_VERSION "${version}")
set(CMAJ_TARGET_NAME "${productName}")
set(CMAJ_TARGET_BUNDLE_NAME "${CMAJ_TARGET_NAME}")
set(CMAJ_TARGET_BUNDLE_VERSION "${CMAJ_TARGET_PATCH_VERSION}")
set(CMAJ_TARGET_SHORT_VERSION_STRING "${CMAJ_TARGET_PATCH_VERSION}")
set(CMAJ_TARGET_MANUFACTURER "${manufacturer}")
set(CMAJ_TARGET_MANUFACTURER_CODE "${manufacturerCode}")
set(CMAJ_TARGET_PLUGIN_CODE "${pluginCode}")
${setClapIncludePathExplicitly}
${setClapWrapperPathExplicitly}

if(NOT CMAJ_INCLUDE_PATH)
    set(CMAJ_INCLUDE_PATH "${cmajorIncludePath}")
endif()

if(NOT CMAJ_PLUGIN_HELPERS_PATH)
    set(CMAJ_PLUGIN_HELPERS_PATH "${cmajorPluginHelpersPath}")
endif()

project("${CMAJ_CMAKE_PROJECT_NAME}" VERSION "${CMAJ_TARGET_PATCH_VERSION}" LANGUAGES CXX C)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
endif()

if(NOT CMAJ_PLUGIN_HELPERS_PATH)
    message (FATAL_ERROR "You must define the CMAJ_PLUGIN_HELPERS_PATH variable to point to your local plugin helpers folder")
endif()

add_subdirectory("${CMAJ_PLUGIN_HELPERS_PATH}/common" cmaj_plugin_helpers)
add_subdirectory("${CMAJ_PLUGIN_HELPERS_PATH}/clap" cmaj_clap_helpers)

if (CLAP_WRAPPER_PATH)
    set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
    set(CLAP_WRAPPER_DONT_ADD_TARGETS TRUE)
    set(CLAP_WRAPPER_BUILD_AUV2 TRUE)
    add_subdirectory(${CLAP_WRAPPER_PATH} clap_wrapper)
endif()

add_library(${CMAJ_TARGET_NAME} MODULE ${mainSourceFile})
target_link_libraries(${CMAJ_TARGET_NAME} PRIVATE cmaj_clap)
target_compile_options(${CMAJ_TARGET_NAME} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>)
target_compile_definitions(${CMAJ_TARGET_NAME} PRIVATE $<$<CONFIG:Debug>:CMAJ_ENABLE_WEBVIEW_DEV_TOOLS=1> CMAJOR_DLL=1)

if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj")
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_options(${CMAJ_TARGET_NAME} PRIVATE -exported_symbols_list "${CMAJ_PLUGIN_HELPERS_PATH}/clap/macos-symbols.txt")

    set_target_properties(${CMAJ_TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        BUNDLE True
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${CMAJ_TARGET_BUNDLE_ID}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${CMAJ_TARGET_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME ${CMAJ_TARGET_BUNDLE_NAME}
        MACOSX_BUNDLE_BUNDLE_VERSION ${CMAJ_TARGET_BUNDLE_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${CMAJ_TARGET_SHORT_VERSION_STRING}
    )
else()
    set_target_properties(${CMAJ_TARGET_NAME} PROPERTIES
        SUFFIX ".clap"
        PREFIX ""
    )
endif()

if (CLAP_WRAPPER_PATH)
    if (APPLE)
        set(VST3_TARGET ${CMAJ_TARGET_NAME}_vst3)
        add_library(${VST3_TARGET} MODULE)
        target_link_libraries(${VST3_TARGET} PRIVATE cmaj_clap)
        target_sources(${VST3_TARGET} PRIVATE entry.cpp)
        target_add_vst3_wrapper(TARGET ${VST3_TARGET} OUTPUT_NAME ${CMAJ_CMAKE_PROJECT_NAME})
        target_compile_definitions(${VST3_TARGET} PRIVATE CMAJOR_DLL=1)

        set(STANDALONE_TARGET ${CMAJ_TARGET_NAME}_standalone)
        add_executable(${STANDALONE_TARGET})
        target_sources(${STANDALONE_TARGET} PRIVATE entry.cpp)
        target_link_libraries(${STANDALONE_TARGET} PRIVATE cmaj_clap)
        target_compile_definitions(${STANDALONE_TARGET} PRIVATE CMAJOR_DLL=1)
        target_add_standalone_wrapper(
            TARGET ${STANDALONE_TARGET}
            OUTPUT_NAME "${CMAJ_TARGET_NAME}"
            STATICALLY_LINKED_CLAP_ENTRY True
            PLUGIN_ID "${CMAJ_TARGET_BUNDLE_ID}"
        )

        set(AUV2_TARGET ${CMAJ_TARGET_NAME}_auv2)
        add_library(${AUV2_TARGET} MODULE)
        target_link_libraries(${AUV2_TARGET} PRIVATE cmaj_clap)
        target_sources(${AUV2_TARGET} PRIVATE entry.cpp)
        target_add_auv2_wrapper(
            TARGET ${AUV2_TARGET}
            OUTPUT_NAME ${CMAJ_CMAKE_PROJECT_NAME}
            BUNDLE_IDENTIFIER ${CMAJ_TARGET_BUNDLE_ID}
            BUNDLE_VERSION ${CMAJ_TARGET_PATCH_VERSION}
            MANUFACTURER_NAME "${CMAJ_TARGET_MANUFACTURER}"
            MANUFACTURER_CODE "${CMAJ_TARGET_MANUFACTURER_CODE}"
            SUBTYPE_CODE "${CMAJ_TARGET_PLUGIN_CODE}"
            INSTRUMENT_TYPE "aumu"
        )
        target_compile_definitions(${AUV2_TARGET} PRIVATE CMAJOR_DLL=1)
    endif()
endif()
)cmake";

    const auto performerNamespace = "performer";
    const auto cpp = generateMainClass (patch, loadParams, performerNamespace);

    const auto mainCpp = choc::text::replace (
        mainCppTemplate,
        "${mainClass}", cpp.generatedCode,
        "${mainClassName}", cpp.mainClassName
    );

    const auto mainSourceFile = "entry.cpp";

    const auto isUsingEmbeddedCmajorHeaders = cmajorIncludePath == "include";

    const auto cmake = choc::text::replace (
        cmakeTemplate,
        "${cmajorIncludePath}", isUsingEmbeddedCmajorHeaders ? "${CMAKE_CURRENT_SOURCE_DIR}/include" : cmajorIncludePath.generic_string(),
        "${mainSourceFile}", mainSourceFile,
        "${projectName}", cmaj::makeSafeIdentifierName (cpp.mainClassName),
        "${macOSBundleId}", loadParams.manifest.ID,
        "${version}", loadParams.manifest.version,
        "${productName}", cmaj::makeSafeIdentifierName (choc::text::replace (loadParams.manifest.name, " ", "")),
        "${pluginCode}", pluginCode,
        "${manufacturerCode}", manufacturerCode,
        "${manufacturer}", manifest.manufacturer,
        "${cmajorPluginHelpersPath}", cmajorPluginHelpersPath.generic_string(),
        "${setClapIncludePathExplicitly}", clapIncludePath.empty() ? "" : "set(CLAP_INCLUDE_PATH \"" + clapIncludePath.generic_string() + "\")",
        "${setClapWrapperPathExplicitly}", clapWrapperPath.empty() ? "" : "set(CLAP_WRAPPER_PATH \"" + clapWrapperPath.generic_string() + "\")"
    );

    generatedFiles.addFile (mainSourceFile, mainCpp);
    generatedFiles.addFile ("CMakeLists.txt", cmake);
}

//==============================================================================
inline void generatePluginProject (choc::ArgumentList& args, std::string outputFile, cmaj::Patch& patch,
                                   const cmaj::Patch::LoadParams& loadParams, bool isCLAP)
{
    std::string cmajorIncludePath;

    if (auto includePath = args.removeValueFor ("--cmajorIncludePath"))
        cmajorIncludePath = *includePath;
    else
        cmajorIncludePath = unzipCmajorHeaders (outputFile);

    GeneratedFiles generatedFiles;

    auto getLibraryPath = [&] (const char* argName) -> std::string
    {
        if (args.contains (argName))
            return args.getExistingFolder (argName, true).string();

        return {};
    };

    if (isCLAP)
        createClapPluginFiles (generatedFiles, patch, loadParams, cmajorIncludePath, getLibraryPath ("--clapIncludePath"), getLibraryPath ("--clapWrapperPath"), outputFile);
    else
        createJucePluginFiles (generatedFiles, patch, loadParams, cmajorIncludePath, getLibraryPath ("--jucePath"), args.getValueFor ("--juceFormats", true));

    generatedFiles.writeToOutputFolder (outputFile);
}

}
