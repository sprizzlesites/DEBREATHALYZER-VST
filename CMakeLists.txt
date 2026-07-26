# CMakeLists.txt for DeBreathalyzer — a JUCE VST3/Standalone breath detection
# and removal plugin. JUCE is fetched via FetchContent and linked statically,
# so a fresh clone builds with nothing preinstalled beyond CMake + a C++17
# compiler; the shipped binary depends only on system libraries.

cmake_minimum_required(VERSION 3.22)

project(DeBreathalyzer VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Static MSVC runtime: the plugin must load on Windows without the VC++
# redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- JUCE: fetched and built statically into the plugin --------------------
# Keep a local checkout instead with -DDeBreathalyzer_JUCE_PATH=/path/to/JUCE.
option(DeBreathalyzer_JUCE_PATH "Path to a local JUCE checkout (empty to fetch)" "")

# JUCE 8 for native builds; the MinGW cross-build needs 7.0.12 (JUCE 8 dropped
# MinGW support). build-windows.sh passes the right tag for that target.
set(DeBreathalyzer_JUCE_TAG "8.0.6" CACHE STRING
    "JUCE git tag to fetch (use 7.0.12 for the MinGW cross-build)")

if(DeBreathalyzer_JUCE_PATH)
    add_subdirectory(${DeBreathalyzer_JUCE_PATH} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${DeBreathalyzer_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(MINGW)
    # The Steinberg manifest helper doesn't cross-compile; moduleinfo.json is
    # optional and hosts scan the binary directly without it.
    set(DeBreathalyzer_PLUGIN_EXTRA_ARGS VST3_AUTO_MANIFEST FALSE)
endif()

# --- The plugin ------------------------------------------------------------
# Audio EFFECT: IS_SYNTH FALSE + an Fx VST3 category is what makes the host
# expose it on audio tracks/insert slots instead of hiding it.
juce_add_plugin(DeBreathalyzer
    ${DeBreathalyzer_PLUGIN_EXTRA_ARGS}
    COMPANY_NAME              "Sprizzle"
    COMPANY_WEBSITE           "https://sprizzle.io"
    BUNDLE_ID                 "io.sprizzle.debreathalyzer"
    PLUGIN_MANUFACTURER_CODE  Sprz
    PLUGIN_CODE               DBr1
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "DeBreathalyzer"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Fx" "Restoration")

# Embed image assets into the binary so nothing is loaded from disk at runtime.
juce_add_binary_data(DeBreathalyzerAssets
    NAMESPACE DeBreathalyzerAssets
    SOURCES   Assets/SprizzleLogo.png)

target_sources(DeBreathalyzer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/DSP/BreathDetector.cpp)

target_include_directories(DeBreathalyzer PRIVATE Source)

target_compile_definitions(DeBreathalyzer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DeBreathalyzer
    PRIVATE
        DeBreathalyzerAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

if(MINGW)
    # Fully static so the DLL depends only on system libraries, and big-obj
    # because JUCE unity builds overflow the default COFF section limit.
    target_compile_options(DeBreathalyzer PUBLIC -Wa,-mbig-obj)
    target_link_options(DeBreathalyzer PUBLIC -static)
else()
    target_link_libraries(DeBreathalyzer PUBLIC juce::juce_recommended_lto_flags)
endif()

# --- Optional headless tests (DSP without a host) ---------------------------
option(DeBreathalyzer_BUILD_TESTS "Build offline tests" OFF)
if(DeBreathalyzer_BUILD_TESTS)
    juce_add_console_app(DeBreathalyzerTest PRODUCT_NAME "DeBreathalyzerTest")
    target_sources(DeBreathalyzerTest PRIVATE
        Test/BreathDetectorTest.cpp
        Source/DSP/BreathDetector.cpp)
    target_include_directories(DeBreathalyzerTest PRIVATE Source)
    target_compile_definitions(DeBreathalyzerTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DeBreathalyzerTest PRIVATE
        juce::juce_audio_basics juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
