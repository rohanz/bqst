cmake_minimum_required(VERSION 3.22)

# Default macOS builds to a universal binary pinned to macOS 10.13, matching the
# release invariant in AGENTS.md. Set before project() so the deployment target is
# baked in correctly, and do not let a release build inherit the build machine's
# macOS version. Any -D override (e.g. from scripts/build-macos-release.sh) wins,
# because the cache entry it creates makes these variables already defined.
if (NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures")
endif()
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment target")
endif()

project(BQST VERSION 1.0.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found. Add it at external/JUCE, for example: git submodule add https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

add_subdirectory(external/JUCE)

set(BQST_PLUGIN_FORMATS VST3 Standalone)

if (APPLE)
    list(APPEND BQST_PLUGIN_FORMATS AU)
endif()

juce_add_binary_data(BqtAssets
    SOURCES
        assets/gui/knob-large-skirted.png
        assets/gui/knob-small-pointer.png
        assets/gui/vu-frame.png
        assets/fonts/Chillax-Regular.otf
        assets/fonts/Chillax-Semibold.otf
)

juce_add_plugin(BQST
    COMPANY_NAME "EBR Audio Tech"
    BUNDLE_ID tech.ebraudio.bqst
    PLUGIN_MANUFACTURER_CODE RohK
    PLUGIN_CODE Bqs1
    FORMATS ${BQST_PLUGIN_FORMATS}
    PRODUCT_NAME "BQST"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(BQST
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginProcessor.h
        src/BqtParameterLayout.cpp
        src/BqtProcessorDsp.cpp
        src/BqtEditorWidgets.cpp
        src/BqtEditorWidgets.h
        src/BqtEditorStyle.h
        src/BqtPresetManager.cpp
        src/BqtPresetManager.h
        src/BqtEditorInteraction.cpp
        src/BqtEditorLayout.cpp
        src/BqtEditorPresets.cpp
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/BqtDsp.h
)

target_compile_definitions(BQST
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(BQST
    PRIVATE
        BqtAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

option(BQST_BUILD_TESTS "Build BQST unit tests" ON)

if (BQST_BUILD_TESTS)
    enable_testing()

    # Pure DSP tests: no JUCE link, just the header-only helpers in src/BqtDsp.h.
    add_executable(BqstDspTests tests/dsp_tests.cpp)
    set_target_properties(BqstDspTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

    add_test(NAME BqstDspTests COMMAND BqstDspTests)
endif()
