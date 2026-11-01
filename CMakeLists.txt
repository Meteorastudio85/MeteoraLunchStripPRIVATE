cmake_minimum_required(VERSION 3.22)
project(TheLunchStrip VERSION 0.9.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Compila i test offline (aliasing, DSP) senza JUCE" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_executable(aliasing_test Tests/aliasing_test.cpp)
    add_test(NAME aliasing COMMAND aliasing_test)
    add_executable(dsp_test Tests/dsp_test.cpp)
    add_test(NAME dsp COMMAND dsp_test)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TheLunchStrip
    COMPANY_NAME "Meteora Plugins"
    PLUGIN_MANUFACTURER_CODE Mtrp
    PLUGIN_CODE Lnch
    BUNDLE_ID com.meteoraplugins.thelunchstrip
    FORMATS VST3
    PRODUCT_NAME "THE LUNCH STRIP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(TheLunchStrip PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp)

# Immagine di sfondo e texture delle manopole, incorporate nel plugin (BinaryData)
file(GLOB LUNCH_ASSETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/Assets/*)
juce_add_binary_data(LunchAssets SOURCES ${LUNCH_ASSETS})
set_target_properties(LunchAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

target_compile_definitions(TheLunchStrip PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(TheLunchStrip
    PRIVATE
        LunchAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
