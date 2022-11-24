cmake_minimum_required(VERSION 3.16..3.22)

project (cmajor-performer)

add_subdirectory(examples/native_apps/HelloCmajor)
add_subdirectory(examples/native_apps/DiodeClipper)
add_subdirectory(examples/native_apps/DynamicGain)
add_subdirectory(examples/native_apps/RenderPatch)

if (JUCE_DIR)

    if(NOT CMAJ_VERSION)
        set(CMAJ_VERSION "0.9")
        message ("Defaulting CMAJ_VERSION to ${CMAJ_VERSION}")
    endif()

    if(NOT CMAJ_TARGET_COMPILER)
        set(CMAJ_TARGET_COMPILER
            $<$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:cxx_std_17>
            $<$<CXX_COMPILER_ID:GNU>:cxx_std_17>
            $<$<CXX_COMPILER_ID:MSVC>:cxx_std_17>
        )
    endif()

    set(JUCE_ENABLE_MODULE_SOURCE_GROUPS ON)
    add_subdirectory(${JUCE_DIR} juce)
    add_subdirectory(tools/CmajPlugin)
else()
    message ("JUCE_DIR not set, skipping CmajPlugin")
endif()