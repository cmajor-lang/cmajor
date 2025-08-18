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
    cmaj_command
    VERSION ${CMAJ_VERSION}
    LANGUAGES CXX C)

add_executable(cmaj)

add_compile_definitions (
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:CMAJ_ENABLE_ALLOCATION_CHECKER=1>
    CMAJ_ENABLE_WEBVIEW_DEV_TOOLS=1
    CHOC_ASSERT=must_include_assertion_header_before_any_choc_headers
)

target_compile_features(cmaj PRIVATE ${CMAJ_TARGET_COMPILER})
target_compile_options(cmaj PRIVATE ${CMAJ_WARNING_FLAGS})

file(GLOB_RECURSE CPPs
    ./Source/*.cpp
    ./Source/*.h
    ../../tests/unit_tests/*.h
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    file(GLOB_RECURSE MMs
         ./Source/*.mm
    )
endif()

target_sources(cmaj PRIVATE ${CPPs} ${MMs})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/" FILES ${SOURCE})

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules (GTK3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules (WEBKIT2 REQUIRED webkit2gtk-4.1 IMPORTED_TARGET)
    target_link_libraries (cmaj PUBLIC ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})

    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
        target_link_libraries (cmaj PUBLIC atomic)
    endif()

    set (EXTRA_LIBS dl ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES} "-pthread" "-lasound" "-ljack")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(cmaj PRIVATE "-framework WebKit")
    target_link_libraries(cmaj PRIVATE "-framework Accelerate")
    target_link_libraries(cmaj PRIVATE "-framework CoreAudio")
    target_link_libraries(cmaj PRIVATE "-framework CoreMIDI")
    target_link_libraries(cmaj PRIVATE "-framework AudioToolbox")
    target_link_libraries(cmaj PRIVATE "-framework Foundation")
    target_link_libraries(cmaj PRIVATE "-framework IOKit")
    target_link_libraries(cmaj PRIVATE "-framework Cocoa")
    target_link_libraries(cmaj PRIVATE "-framework Security")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set (EXTRA_LIBS "dsound")
endif()

MAKE_CMAJ_LIBRARY (
    LIBRARY_NAME command_cmaj_lib
    INCLUDE_SERVER
    INCLUDE_SCRIPTING
    INCLUDE_PLAYBACK
    ENABLE_PERFORMER_LLVM
    ENABLE_PERFORMER_WEBVIEW
    ENABLE_PERFORMER_CPP
    ENABLE_CODEGEN_LLVM_WASM
    ENABLE_CODEGEN_CPP
)

target_link_libraries(cmaj
    PRIVATE
        command_cmaj_lib
        cmaj_clap
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
        ${EXTRA_LIBS}
)
