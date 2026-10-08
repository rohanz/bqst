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

project(BQST VERSION 1.2.0 LANGUAGES C CXX)

# Single source of truth for the version and bundle identifier. Everything downstream -- the
# plugin bundles, the generated packaging templates, and the shell/PowerShell packaging scripts
# via scripts/lib/project-info.sh -- derives from these two lines.
set(BQST_VERSION "${PROJECT_VERSION}")
set(BQST_IDENTIFIER "tech.ebraudio.bqst")

# Opt-in audition build. The released BQST identity and signal path remain unchanged.
option(BQST_GRIT_LAB "Build the separately identified experimental LA500A Grit model" OFF)
set(BQST_PRODUCT_NAME "BQST")
set(BQST_PLUGIN_CODE Bqs1)
if (BQST_GRIT_LAB)
    set(BQST_PRODUCT_NAME "BQST Grit Lab")
    set(BQST_IDENTIFIER "tech.ebraudio.bqst.gritlab")
    set(BQST_PLUGIN_CODE BqGL)
endif()


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
        assets/fonts/DMSans-Medium.ttf
        assets/fonts/DMSans-SemiBold.ttf
)

juce_add_plugin(BQST
    COMPANY_NAME "EBR Audio Tech"
    BUNDLE_ID ${BQST_IDENTIFIER}
    PLUGIN_MANUFACTURER_CODE RohK
    PLUGIN_CODE ${BQST_PLUGIN_CODE}
    FORMATS ${BQST_PLUGIN_FORMATS}
    PRODUCT_NAME "${BQST_PRODUCT_NAME}"
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
        src/BqtCreamModel.h
        src/BqtCreamParams.h
        src/BqtPresetManager.cpp
        src/BqtPresetManager.h
        src/BqtEditorInteraction.cpp
        src/BqtEditorLayout.cpp
        src/BqtEditorPresets.cpp
        src/PluginEditor.cpp
        src/PluginEditor.h
        src/BqtDsp.h
        src/BqtParameterIds.h
        src/BqtEditorLogic.h
)

target_compile_definitions(BQST
    PUBLIC
        BQST_GRIT_LAB=$<BOOL:${BQST_GRIT_LAB}>
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

# Generate the versioned packaging files from the templates next to them, so nothing restates the
# version. These used to be maintained by hand in nine separate places, where a half-landed bump
# produced an installer whose payload disagreed with its own filename.
foreach(template
        packaging/macos/Distribution.xml
        packaging/macos/resources/Welcome.html
        packaging/macos/resources/ReadMe.html
        packaging/windows/BQST.iss)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${template}.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/${template}"
                   @ONLY)
endforeach()

# The installer resources directory must hold the generated HTML alongside the static artwork
# that Distribution.xml references, so copy the non-template files in beside them.
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/resources/"
     DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/packaging/macos/resources"
     PATTERN "*.in" EXCLUDE)

option(BQST_BUILD_TESTS "Build BQST unit tests" ON)
option(BQST_WERROR "Treat compiler warnings as errors" OFF)

if (BQST_WERROR)
    target_compile_options(BQST PRIVATE -Werror)
endif()

if (BQST_BUILD_TESTS)
    enable_testing()

    # Pure DSP tests: no JUCE link, just the header-only helpers in src/BqtDsp.h.
    add_executable(BqstDspTests tests/dsp_tests.cpp)
    set_target_properties(BqstDspTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(BqstDspTests PRIVATE juce::juce_recommended_warning_flags)

    add_test(NAME BqstDspTests COMMAND BqstDspTests)

    add_executable(BqstCreamTests tests/cream_model_tests.cpp)
    set_target_properties(BqstCreamTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(BqstCreamTests PRIVATE juce::juce_recommended_warning_flags)
    add_test(NAME BqstCreamTests COMMAND BqstCreamTests ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/cream_fixture.bin)

    add_executable(BqstGritLabTests tests/grit_lab_tests.cpp)
    target_link_libraries(BqstGritLabTests PRIVATE juce::juce_recommended_warning_flags)
    add_test(NAME BqstGritLabTests COMMAND BqstGritLabTests ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/grit_lab_fixture.bin)

    # Chain tests: drive the real processor through processBlock. These must link JUCE, but not
    # the editor or its binary assets, so BQST_HEADLESS_TESTS compiles createEditor out.
    juce_add_console_app(BqstChainTests PRODUCT_NAME "BqstChainTests")

    target_sources(BqstChainTests
        PRIVATE
            tests/chain_tests.cpp
            src/PluginProcessor.cpp
            src/BqtParameterLayout.cpp
            src/BqtProcessorDsp.cpp
            src/BqtPresetManager.cpp
    )

    target_compile_definitions(BqstChainTests
        PRIVATE
            BQST_GRIT_LAB=$<BOOL:${BQST_GRIT_LAB}>
            BQST_HEADLESS_TESTS=1
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_STANDALONE_APPLICATION=1
            JucePlugin_Name="BQST"
    )

    target_link_libraries(BqstChainTests
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    add_test(NAME BqstChainTests COMMAND BqstChainTests)
    set_tests_properties(BqstChainTests PROPERTIES
        ENVIRONMENT "BQST_TEST_PRESET_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-presets")

    # Bit-exact refactor check (not a CTest): render before and after a refactor, then cmp.
    juce_add_console_app(BqstGoldenRender PRODUCT_NAME "BqstGoldenRender")
    target_sources(BqstGoldenRender PRIVATE tests/golden_render.cpp src/PluginProcessor.cpp
                                            src/BqtParameterLayout.cpp src/BqtProcessorDsp.cpp)
    target_compile_definitions(BqstGoldenRender PRIVATE BQST_GRIT_LAB=$<BOOL:${BQST_GRIT_LAB}> BQST_HEADLESS_TESTS=1 JUCE_WEB_BROWSER=0
                               JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_Name="BQST")
    target_link_libraries(BqstGoldenRender PRIVATE juce::juce_audio_processors juce::juce_audio_utils
                          juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)
endif()
