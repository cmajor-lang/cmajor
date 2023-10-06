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

project (cmaj)

OPTION (BUILD_CMAJ      "Whether to build the comnand line tool" ON)
OPTION (BUILD_CMAJ_LIB  "Whether to build the Cmajor shared library" ON)
OPTION (BUILD_PLUGIN    "Whether to build the plugin" ON)
OPTION (BUILD_EXAMPLES  "Whether to build the examples" ON)

include(tools/scripts/cmake_warning_flags)

if (NOT JUCE_DIR)
    set(JUCE_DIR "3rdParty/JUCE")
endif()

if(NOT CMAJ_VERSION)
    set(CMAJ_VERSION "1.0")
    message ("Defaulting CMAJ_VERSION to ${CMAJ_VERSION}")
endif()

if(NOT CMAJ_TARGET_COMPILER)
    set(CMAJ_TARGET_COMPILER
        $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:cxx_std_17>
        $<$<CXX_COMPILER_ID:GNU>:cxx_std_17>
        $<$<CXX_COMPILER_ID:MSVC>:cxx_std_17>
    )
endif()

add_subdirectory(modules)
add_subdirectory(modules/plugin cmaj_plugin_wrappers)

if (BUILD_CMAJ)
    message ("Including the command line tool")
    add_subdirectory(tools/command)
endif()

if (BUILD_CMAJ_LIB)
    message ("Including the Cmajor shared library")
    add_subdirectory(tools/CmajDLL)
endif()

if (BUILD_PLUGIN)
    message ("Including plugin build")
    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
    add_subdirectory(${JUCE_DIR} juce)
    add_subdirectory(tools/CmajPlugin)
endif()

if (BUILD_EXAMPLES)
    message ("Including example projects")
    add_subdirectory(examples/native_apps/HelloCmajor)
    add_subdirectory(examples/native_apps/DiodeClipper)
    add_subdirectory(examples/native_apps/DynamicGain)
    add_subdirectory(examples/native_apps/RenderPatch)
endif ()