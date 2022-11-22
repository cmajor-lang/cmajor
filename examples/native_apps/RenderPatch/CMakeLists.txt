cmake_minimum_required(VERSION 3.16..3.22)

project(
    RenderPatch
    VERSION 0.1
    LANGUAGES CXX C)

add_compile_definitions (
    CMAJOR_DLL=1
)

add_executable(RenderPatch)

target_compile_features(RenderPatch PRIVATE cxx_std_17)
target_compile_options(RenderPatch PRIVATE ${CMAJ_WARNING_FLAGS})

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
        Main.cpp)

target_link_libraries(RenderPatch
    PRIVATE
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
