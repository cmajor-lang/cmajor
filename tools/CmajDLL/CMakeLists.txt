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

add_library(CmajPerformer SHARED)

target_sources(CmajPerformer
    PRIVATE
        Source/cmaj_ExportedFunctions.cpp
)

include (../scripts/cmake_warning_flags)
target_compile_features(CmajPerformer PRIVATE ${CMAJ_TARGET_COMPILER})
target_compile_options(CmajPerformer PRIVATE ${CMAJ_WARNING_FLAGS})

add_compile_definitions (
    CMAJ_VERSION="${CMAJ_VERSION}"
    CHOC_ASSERT=must_include_assertion_header_before_any_choc_headers
)

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set (EXTRA_LIBS "-pthread")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Android")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if (STRIP_LIBARY)
    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
        target_link_options(CmajPerformer
            PRIVATE
            -Xlinker -x
            -exported_symbols_list ${CMAKE_CURRENT_SOURCE_DIR}/exports.txt
        )
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
        add_custom_command(
            TARGET CmajPerformer
            POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} ARGS --strip-all --wildcard --keep-symbols ${CMAKE_CURRENT_SOURCE_DIR}/exports.txt $<TARGET_LINKER_FILE:CmajPerformer>
        )
    elseif (${CMAKE_SYSTEM_NAME} MATCHES "Android")
        add_custom_command(
            TARGET CmajPerformer
            POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} ARGS --strip-all --wildcard --keep-symbols ${CMAKE_CURRENT_SOURCE_DIR}/exports.txt $<TARGET_LINKER_FILE:CmajPerformer>
        )
    endif()
endif()

MAKE_CMAJ_LIBRARY (
    LIBRARY_NAME performer_cmaj_lib
    INCLUDE_PLAYBACK
    ENABLE_PERFORMER_LLVM
)

target_link_libraries(CmajPerformer
    PRIVATE
        performer_cmaj_lib
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
        ${EXTRA_LIBS}
)
