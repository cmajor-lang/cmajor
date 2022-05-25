cmake_minimum_required(VERSION 3.16..3.22)

project(
    DiodeClipper
    VERSION 0.1
    LANGUAGES CXX C)

add_compile_definitions (
    CMAJOR_DLL=1
)

add_executable(DiodeClipper)

target_compile_features(DiodeClipper PRIVATE cxx_std_17)
target_compile_options(DiodeClipper PRIVATE ${CMAJ_WARNING_FLAGS})

target_sources(DiodeClipper
    PRIVATE
        DiodeClipper.cpp)

target_link_libraries(DiodeClipper
    PRIVATE
        ${CMAKE_DL_LIBS}
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)
