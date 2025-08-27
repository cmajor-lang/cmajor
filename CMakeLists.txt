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

OPTION (BUILD_CMAJ          "Whether to build the command line tool" ON)
OPTION (BUILD_CMAJ_LIB      "Whether to build the Cmajor shared library" ON)
OPTION (BUILD_PLUGIN        "Whether to build the plugin" OFF)
OPTION (BUILD_EXAMPLES      "Whether to build the examples" ON)
OPTION (WEBKIT2_GTK_VERSION "Which version of webkit to use")

include(tools/scripts/cmake_warning_flags)

if (NOT CMAJ_VERSION)
    # Determine the version - use the latest tag for the checkout out repo
    set (CMAJ_VERSION "1.0")
    find_package (Git)

    if (GIT_FOUND)
        execute_process (
            COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE GIT_RESULT
            OUTPUT_VARIABLE GIT_STDOUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if (GIT_RESULT EQUAL 0)
            set (CMAJ_VERSION ${GIT_STDOUT})
        endif()
    endif()

    message ("Setting CMAJ_VERSION to ${CMAJ_VERSION}")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    if (NOT WEBKIT2_GTK_VERSION)
        find_package (PkgConfig REQUIRED)

        pkg_check_modules (package_to_be_found webkit2gtk-4.1 QUIET)

        if(package_to_be_found_FOUND)
            set (WEBKIT2_GTK_VERSION "webkit2gtk-4.1")
        else()
            set (WEBKIT2_GTK_VERSION "webkit2gtk-4.0")
        endif()
    endif()

    message ("Using webkit ${WEBKIT2_GTK_VERSION}")
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

    if (JUCE_PATH)
       set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
       add_subdirectory(${JUCE_PATH} juce)
       add_subdirectory(tools/CmajPlugin)
    else()
        message (FATAL_ERROR "You must define the JUCE_PATH variable to point to your local JUCE folder")
    endif()
endif()

if (BUILD_EXAMPLES)
    message ("Including example projects")
    add_subdirectory(examples/native_apps)
endif()