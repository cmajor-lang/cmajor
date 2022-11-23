cmake_minimum_required(VERSION 3.16..3.22)

project(
    cmaj_plugin
    VERSION ${CMAJ_VERSION}
    LANGUAGES CXX C)

juce_add_plugin(CmajPlugin
    PRODUCT_NAME "CmajPlugin"
    BUNDLE_ID "dev.cmajor.plugin"
    COMPANY_NAME "Sound Stacks Ltd"
    COMPANY_COPYRIGHT "(C)2022 Sound Stacks Ltd"
    PLUGIN_CODE Sssp
    FORMATS Standalone AU VST3
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
)

target_compile_features(CmajPlugin PRIVATE ${CMAJ_TARGET_COMPILER})
target_compile_options(CmajPlugin PRIVATE ${CMAJ_WARNING_FLAGS})

juce_generate_juce_header(CmajPlugin)

add_compile_definitions (
    $<$<CONFIG:Debug>:DEBUG=1>
    $<$<CONFIG:Debug>:CMAJ_ENABLE_ALLOCATION_CHECKER=1>
    JUCE_DISABLE_JUCE_VERSION_PRINTING=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CURL=0
)

file(GLOB_RECURSE SOURCE
    Source/*.cpp
    Source/*.h
)

target_sources(CmajPlugin PRIVATE ${SOURCE})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/" PREFIX "Source" FILES ${SOURCE})

target_link_libraries(CmajPlugin
    PRIVATE
        juce::juce_audio_utils
        $<$<AND:$<CXX_COMPILER_ID:GNU>,$<VERSION_LESS:$<CXX_COMPILER_VERSION>,9.0>>:stdc++fs>
)