#
#     ,ad888ba,                              88
#    d8"'    "8b
#   d8            88,dba,,adba,   ,aPP8A.A8  88     The Cmajor Toolkit
#   Y8,           88    88    88  88     88  88
#    Y8a.   .a8P  88    88    88  88,   ,88  88     (C)2024 Cmajor Software Ltd
#     '"Y888Y"'   88    88    88  '"8bbP"Y8  88     https://cmajor.dev
#                                           ,88
#                                        888P"
#
#  The Cmajor project is subject to commercial or open-source licensing.
#  You may use it under the terms of the GPLv3 (see www.gnu.org/licenses), or
#  visit https://cmajor.dev to learn about our commercial licence options.
#
#  CMAJOR IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
#  EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
#  DISCLAIMED.

cmake_minimum_required(VERSION 3.16..3.22)

project(
    cmaj_plugin
    VERSION ${CMAJ_VERSION}
    LANGUAGES CXX C)

juce_add_plugin(CmajPlugin
    PRODUCT_NAME "CmajPlugin"
    BUNDLE_ID "dev.cmajor.plugin"
    COMPANY_NAME "Cmajor Software Ltd"
    COMPANY_COPYRIGHT "(C)2024 Cmajor Software Ltd"
    PLUGIN_CODE Cmaj
    FORMATS AU VST3 Standalone
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
)

target_compile_features(CmajPlugin PRIVATE ${CMAJ_TARGET_COMPILER})
target_compile_options(CmajPlugin PRIVATE ${CMAJ_WARNING_FLAGS})

juce_generate_juce_header(CmajPlugin)

add_compile_definitions (
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:CMAJ_ENABLE_ALLOCATION_CHECKER=1>
    CMAJ_ENABLE_WEBVIEW_DEV_TOOLS=1
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CURL=0
)

file(GLOB_RECURSE SOURCE
    Source/*.cpp
    Source/*.h
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules (GTK3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules (WEBKIT2 REQUIRED webkit2gtk-4.0 IMPORTED_TARGET)
    target_link_libraries (CmajPlugin PUBLIC ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})
    target_include_directories(CmajPlugin SYSTEM PUBLIC ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
endif ()

target_sources(CmajPlugin PRIVATE ${SOURCE})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/" PREFIX "Source" FILES ${SOURCE})

add_subdirectory(../../modules cmajor)

MAKE_CMAJ_LIBRARY (
    LIBRARY_NAME cmajor_lib
    INCLUDE_PLAYBACK
    ENABLE_PERFORMER_LLVM
)

target_link_libraries(CmajPlugin
    PRIVATE
        cmajor_lib
        juce::juce_audio_utils
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
