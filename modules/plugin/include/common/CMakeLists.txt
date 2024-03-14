cmake_minimum_required(VERSION 3.16..3.22)

if(NOT CMAJ_INCLUDE_PATH)
    message (FATAL_ERROR "You must define the CMAJ_INCLUDE_PATH variable to point to your local Cmajor folder")
endif()

add_library(cmaj_public_deps_for_plugin_wrapper INTERFACE)
target_include_directories(cmaj_public_deps_for_plugin_wrapper INTERFACE $<BUILD_INTERFACE:${CMAJ_INCLUDE_PATH}>)
target_compile_features(cmaj_public_deps_for_plugin_wrapper INTERFACE cxx_std_17)

if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    find_library(MAC_WEBKIT WebKit)
    target_link_libraries(cmaj_public_deps_for_plugin_wrapper INTERFACE ${MAC_WEBKIT})
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gtk3 REQUIRED gtk+-3.0 IMPORTED_TARGET)
    pkg_check_modules(webkit2 REQUIRED webkit2gtk-4.0 IMPORTED_TARGET)
    target_link_libraries(cmaj_public_deps_for_plugin_wrapper INTERFACE PkgConfig::gtk3 PkgConfig::webkit2)
endif()

add_library(cmaj_plugin_helpers INTERFACE)
target_include_directories(cmaj_plugin_helpers INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
target_compile_features(cmaj_plugin_helpers INTERFACE cxx_std_17)
target_link_libraries(cmaj_plugin_helpers INTERFACE cmaj_public_deps_for_plugin_wrapper)