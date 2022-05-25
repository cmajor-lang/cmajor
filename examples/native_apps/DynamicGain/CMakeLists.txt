cmake_minimum_required(VERSION 3.16..3.22)

project(
    DynamicGain
    VERSION 0.1
    LANGUAGES CXX C)

add_compile_definitions (
    CMAJOR_DLL=1
)

add_executable(DynamicGain)

target_compile_features(DynamicGain PRIVATE cxx_std_17)
target_compile_options(DynamicGain PRIVATE ${CMAJ_WARNING_FLAGS})

target_sources(DynamicGain
    PRIVATE
    DynamicGain.cpp)

target_link_libraries(DynamicGain
    PRIVATE
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
