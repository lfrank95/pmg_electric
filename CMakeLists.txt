cmake_minimum_required(VERSION 3.22)
project(PMGElectric VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statische C++-Runtime auf Windows: kein Visual-C++-Redistributable nötig
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: als Unterordner (JUCE/) oder per FetchContent
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.15 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(PMGElectric
    PRODUCT_NAME "PMG Electric"
    COMPANY_NAME "Lorenzo Frank"
    PLUGIN_MANUFACTURER_CODE Lfrk
    PLUGIN_CODE Pmge
    FORMATS VST3
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PMGElectric PRIVATE
    Source/plugin/PluginProcessor.cpp
    Source/plugin/PluginEditor.cpp)

# VST3-SDK-Header aus JUCE (für IKeyswitchController → Cubase Expression Maps)
target_include_directories(PMGElectric PRIVATE
    Source
    "${JUCE_MODULES_DIR}/juce_audio_processors_headless/format_types/VST3_SDK"
    "${JUCE_MODULES_DIR}/juce_audio_processors/format_types/VST3_SDK")

target_compile_definitions(PMGElectric PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PMGElectric PRIVATE
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(PMGElectric PRIVATE /O2 /fp:fast /utf-8 /bigobj)
else()
    target_compile_options(PMGElectric PRIVATE -O3 -fno-math-errno -ffp-contract=fast)
endif()

# Kommandozeilen-Renderer (MIDI → WAV, ohne JUCE) – zum Vergleich mit der Browser-Engine
add_executable(pmg_render tools/render_test.cpp)
target_include_directories(pmg_render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(NOT MSVC)
    target_compile_options(pmg_render PRIVATE -O2)
endif()
