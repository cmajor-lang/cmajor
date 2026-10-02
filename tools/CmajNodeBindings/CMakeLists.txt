cmake_minimum_required(VERSION 3.20)


project(cmaj-node-bindings
        VERSION 0.1.0
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAJ_INCLUDE_PLAYBACK 1)
set(CMAJ_VERSION "1.0")


get_filename_component(CMAJOR_ROOT
                       "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(CMAJOR_MODULES_DIR "${CMAJOR_ROOT}/modules")

add_subdirectory(
    "${CMAJOR_MODULES_DIR}"           
    "${CMAKE_BINARY_DIR}/cmj_modules" 
    EXCLUDE_FROM_ALL)

set(CMAJ_ENABLE_CODEGEN_LLVM_WASM 1)
set(CMAJ_ENABLE_PERFORMER_LLVM 1)
set(CMAJ_ENABLE_CODEGEN_CPP, 1)

message(STATUS "Building Cmajor core library …")
MAKE_CMAJ_LIBRARY(
	LIBRARY_NAME cmajor_bindings_cmaj_lib
	INCLUDE_PLAYBACK
	ENABLE_PERFORMER_LLVM
    ENABLE_CODEGEN_LLVM_WASM
    ENABLE_CODEGEN_CPP
	ENABLE_PERFORMER_INTERPRETER
)


add_library(cmaj-node-bindings SHARED src/binding.cpp)


set(CMAKE_JS_INC  ${CMAKE_JS_INC}  CACHE INTERNAL "node headers")
set(CMAKE_JS_LIB  ${CMAKE_JS_LIB}  CACHE INTERNAL "node library")

target_include_directories(cmaj-node-bindings PRIVATE
    "${CMAJOR_ROOT}/include"
    ${CMAKE_JS_INC}
)

target_link_libraries(cmaj-node-bindings PRIVATE
    cmajor_bindings_cmaj_lib      
    ${CMAKE_JS_LIB}
)

set_target_properties(cmaj-node-bindings PROPERTIES
    PREFIX ""                
    SUFFIX ".node"
    POSITION_INDEPENDENT_CODE ON
)


