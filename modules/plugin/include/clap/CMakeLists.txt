cmake_minimum_required(VERSION 3.16..3.22)

if(NOT CLAP_INCLUDE_PATH)
    message (FATAL_ERROR "You must define the CLAP_INCLUDE_PATH variable to point to your local CLAP folder")
endif()

add_library(clap INTERFACE)
target_include_directories(clap INTERFACE $<BUILD_INTERFACE:${CLAP_INCLUDE_PATH}>)
target_compile_features(clap INTERFACE c_std_11)

add_library(cmaj_clap INTERFACE)
target_sources(cmaj_clap INTERFACE cmaj_CLAPPlugin.h)
target_include_directories(cmaj_clap INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(cmaj_clap INTERFACE cxx_std_17)
target_link_libraries(cmaj_clap INTERFACE cmaj_plugin_helpers clap)
