cmake_minimum_required(VERSION 3.16..3.22)

set(CMAJ_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../cmajor/include")
set(CLAP_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../3rdParty/clap/include")

add_subdirectory(include/common)
add_subdirectory(include/clap)