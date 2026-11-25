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

project(BQST VERSION 1.3.2 LANGUAGES C CXX)

# Single source of truth for the version and bundle identifier. Everything downstream -- the
# plugin bundles, the generated packaging templates, and the shell/PowerShell packaging scripts
# via scripts/lib/project-info.sh -- derives from these two lines.
set(BQST_VERSION "${PROJECT_VERSION}")
set(BQST_IDENTIFIER "tech.ebraudio.bqst")

set(BQST_PRODUCT_NAME "BQST")
set(BQST_PLUGIN_CODE Bqs1)


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found. Add it at external/JUCE, for example: git submodule add https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

add_subdirectory(external/JUCE)

set(BQST_PLUGIN_FORMATS VST3 Standalone)

# VU needles presented in a Core Animation layer on macOS, moved by a display-link clock of their own
# at the display's refresh rate, off the host's message thread (src/BqtVuNeedleLayer.h). OFF paints
# the needles with the rest of the editor, as other platforms always do.
option(BQST_VU_NATIVE_NEEDLE "Present the VU needles in a Core Animation layer on macOS" ON)
set(BQST_VU_NEEDLE_SOURCES src/BqtVuNeedleLayer.cpp src/BqtVuNeedleLayer.h)
if (APPLE)
    list(APPEND BQST_VU_NEEDLE_SOURCES src/BqtVuNeedleLayer_mac.mm)
    set_source_files_properties(src/BqtVuNeedleLayer_mac.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
set(BQST_VU_NEEDLE_DEFINITION BQST_VU_NATIVE_NEEDLE=$<AND:$<BOOL:${BQST_VU_NATIVE_NEEDLE}>,$<BOOL:${APPLE}>>)

if (APPLE)
    list(APPEND BQST_PLUGIN_FORMATS AU)
endif()

juce_add_binary_data(BqtAssets
    SOURCES
        assets/gui/knob-large-skirted.png
        assets/gui/knob-small-pointer.png
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
        src/BqtVuMeter.cpp
        src/BqtVuMeter.h
        ${BQST_VU_NEEDLE_SOURCES}
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
        ${BQST_VU_NEEDLE_DEFINITION}
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

    find_package(Threads REQUIRED)
    add_executable(BqstFrameLogTests tests/frame_log_tests.cpp)
    set_target_properties(BqstFrameLogTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(BqstFrameLogTests PRIVATE Threads::Threads juce::juce_recommended_warning_flags)
    add_test(NAME BqstFrameLogTests COMMAND BqstFrameLogTests)

    # Pure DSP tests: no JUCE link, just the header-only helpers in src/BqtDsp.h.
    add_executable(BqstDspTests tests/dsp_tests.cpp)
    set_target_properties(BqstDspTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(BqstDspTests PRIVATE juce::juce_recommended_warning_flags)

    # check.sh explicitly builds BqstDspTests; keep this headless diagnostic in that gate.
    add_dependencies(BqstDspTests BqstFrameLogTests)
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
            tests/grit_precision_tests.cpp
            src/PluginProcessor.cpp
            src/BqtParameterLayout.cpp
            src/BqtProcessorDsp.cpp
            src/BqtPresetManager.cpp
    )

    target_compile_definitions(BqstChainTests
        PRIVATE
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
    target_compile_definitions(BqstGoldenRender PRIVATE BQST_HEADLESS_TESTS=1 JUCE_WEB_BROWSER=0
                               JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_Name="BQST")
    target_link_libraries(BqstGoldenRender PRIVATE juce::juce_audio_processors juce::juce_audio_utils
                          juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

    # Offscreen editor picture (not a CTest): BqstEditorSnapshot <width> <out.png> [pixelScale].
    # Builds the real editor sources and paints them into an image, with no window on screen.
    juce_add_console_app(BqstEditorSnapshot PRODUCT_NAME "BqstEditorSnapshot")
    target_sources(BqstEditorSnapshot PRIVATE tests/editor_snapshot.cpp src/PluginProcessor.cpp
                   src/BqtParameterLayout.cpp src/BqtProcessorDsp.cpp src/BqtPresetManager.cpp
                   src/BqtEditorWidgets.cpp src/BqtVuMeter.cpp src/BqtEditorInteraction.cpp
                   src/BqtEditorLayout.cpp src/BqtEditorPresets.cpp src/PluginEditor.cpp ${BQST_VU_NEEDLE_SOURCES})
    target_compile_definitions(BqstEditorSnapshot PRIVATE ${BQST_VU_NEEDLE_DEFINITION} JUCE_WEB_BROWSER=0
                               JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_Name="BQST"
                               JucePlugin_VersionString="${BQST_VERSION}")
    target_link_libraries(BqstEditorSnapshot PRIVATE BqtAssets juce::juce_audio_processors juce::juce_audio_utils
                          juce::juce_dsp PUBLIC juce::juce_recommended_config_flags)

    # Native VU needle checks (macOS): layer geometry against the painted needle at several editor
    # sizes, fallback selection, attach and detach, the needle clock, and teardown with the clock
    # running. Hosts the editor in a window that is never shown, except for the presentation proof:
    # a small floating window (never activated, never focused, ignores the mouse) captured with
    # ScreenCaptureKit while the message thread is stalled; skipped without Screen Recording access.
    if (APPLE)
        juce_add_console_app(BqstVuNeedleTests PRODUCT_NAME "BqstVuNeedleTests")
        target_sources(BqstVuNeedleTests PRIVATE tests/vu_needle_tests.mm src/PluginProcessor.cpp
                       src/BqtParameterLayout.cpp src/BqtProcessorDsp.cpp src/BqtPresetManager.cpp
                       src/BqtEditorWidgets.cpp src/BqtVuMeter.cpp src/BqtEditorInteraction.cpp
                       src/BqtEditorLayout.cpp src/BqtEditorPresets.cpp src/PluginEditor.cpp ${BQST_VU_NEEDLE_SOURCES})
        set_source_files_properties(tests/vu_needle_tests.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        target_compile_definitions(BqstVuNeedleTests PRIVATE ${BQST_VU_NEEDLE_DEFINITION}
                                   JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_Name="BQST"
                                   JucePlugin_VersionString="${BQST_VERSION}")
        target_link_libraries(BqstVuNeedleTests PRIVATE BqtAssets juce::juce_audio_processors juce::juce_audio_utils
                              juce::juce_dsp "-framework ScreenCaptureKit" "-framework CoreMedia"
                              PUBLIC juce::juce_recommended_config_flags)
        add_test(NAME BqstVuNeedleTests COMMAND BqstVuNeedleTests)
        set_tests_properties(BqstVuNeedleTests PROPERTIES
            ENVIRONMENT "BQST_TEST_PRESET_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-presets")
    endif()
endif()
