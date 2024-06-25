cmake_minimum_required(VERSION 3.16..3.22)

# -----------------------------------------------------------------------------
# Static build of the cmaj library suitable for use by the examples
# -----------------------------------------------------------------------------

MAKE_CMAJ_LIBRARY (
    LIBRARY_NAME examples_cmaj_lib
    INCLUDE_PLAYBACK
    ENABLE_PERFORMER_LLVM
)

# -----------------------------------------------------------------------------
# Diode Clipper example
# -----------------------------------------------------------------------------

project(
    DiodeClipper
    VERSION 0.1
    LANGUAGES CXX C
)

add_executable(DiodeClipper)

target_compile_features(DiodeClipper PRIVATE cxx_std_17)
target_compile_options(DiodeClipper PRIVATE ${CMAJ_WARNING_FLAGS})

target_sources(DiodeClipper
    PRIVATE
        DiodeClipper/DiodeClipper.cpp)

target_link_libraries(DiodeClipper
    PRIVATE
        examples_cmaj_lib
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)

# -----------------------------------------------------------------------------
# Dynamic Gain example
# -----------------------------------------------------------------------------

project(
    DynamicGain
    VERSION 0.1
    LANGUAGES CXX C)

add_executable(DynamicGain)

target_compile_features(DynamicGain PRIVATE cxx_std_17)
target_compile_options(DynamicGain PRIVATE ${CMAJ_WARNING_FLAGS})

target_sources(DynamicGain
    PRIVATE
    DynamicGain/DynamicGain.cpp)

target_link_libraries(DynamicGain
    PRIVATE
        examples_cmaj_lib
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)

# -----------------------------------------------------------------------------
# HelloCmajor example
# -----------------------------------------------------------------------------

project(
    HelloCmajor
    VERSION 0.1
    LANGUAGES CXX C)

add_executable(HelloCmajor)

target_compile_features(HelloCmajor PRIVATE cxx_std_17)
target_compile_options(HelloCmajor PRIVATE ${CMAJ_WARNING_FLAGS})

target_sources(HelloCmajor
    PRIVATE
        HelloCmajor/Main.cpp)

target_link_libraries(HelloCmajor
    PRIVATE
        examples_cmaj_lib
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)


# -----------------------------------------------------------------------------
# RenderPatch example
# -----------------------------------------------------------------------------

project(
    RenderPatch
    VERSION 0.1
    LANGUAGES CXX C)

add_executable(RenderPatch)

target_compile_features(RenderPatch PRIVATE cxx_std_17)
target_compile_options(RenderPatch PRIVATE ${CMAJ_WARNING_FLAGS})

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules (GTK3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules (WEBKIT2 REQUIRED webkit2gtk-4.0 IMPORTED_TARGET)
    target_include_directories (RenderPatch PUBLIC ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
    target_link_libraries (RenderPatch PUBLIC ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})

    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
        target_link_libraries (RenderPatch PUBLIC atomic)
    endif()

    set (EXTRA_LIBS dl ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES} "-pthread" "-lasound")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(RenderPatch PRIVATE "-framework WebKit")
    target_link_libraries(RenderPatch PRIVATE "-framework Accelerate")
    target_link_libraries(RenderPatch PRIVATE "-framework CoreAudio")
    target_link_libraries(RenderPatch PRIVATE "-framework CoreMIDI")
    target_link_libraries(RenderPatch PRIVATE "-framework AudioToolbox")
    target_link_libraries(RenderPatch PRIVATE "-framework Foundation")
    target_link_libraries(RenderPatch PRIVATE "-framework IOKit")
    target_link_libraries(RenderPatch PRIVATE "-framework Cocoa")
endif()

target_sources(RenderPatch
    PRIVATE
        RenderPatch/Main.cpp)

target_link_libraries(RenderPatch
    PRIVATE
        examples_cmaj_lib
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
        ${EXTRA_LIBS}
)



# -----------------------------------------------------------------------------
# RenderPatchSharedLib example
# -----------------------------------------------------------------------------

project(
    RenderPatchSharedLib
    VERSION 0.1
    LANGUAGES CXX C)

add_executable(RenderPatchSharedLib)

target_compile_features(RenderPatchSharedLib PRIVATE cxx_std_17)
target_compile_options(RenderPatchSharedLib PRIVATE ${CMAJ_WARNING_FLAGS})

if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules (GTK3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules (WEBKIT2 REQUIRED webkit2gtk-4.0 IMPORTED_TARGET)
    target_include_directories (RenderPatchSharedLib PUBLIC ${GTK3_INCLUDE_DIRS} ${WEBKIT2_INCLUDE_DIRS})
    target_link_libraries (RenderPatchSharedLib PUBLIC ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES})

    if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "armv7l")
        target_link_libraries (RenderPatchSharedLib PUBLIC atomic)
    endif()

    set (EXTRA_LIBS dl ${GTK3_LIBRARIES} ${WEBKIT2_LIBRARIES} "-pthread" "-lasound")
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework WebKit")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework Accelerate")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework CoreAudio")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework CoreMIDI")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework AudioToolbox")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework Foundation")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework IOKit")
    target_link_libraries(RenderPatchSharedLib PRIVATE "-framework Cocoa")
endif()

target_sources(RenderPatchSharedLib
    PRIVATE
    RenderPatchSharedLib/Main.cpp)

target_link_libraries(RenderPatchSharedLib
    PRIVATE
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
        ${EXTRA_LIBS}
)
