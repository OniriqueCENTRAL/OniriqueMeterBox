cmake_minimum_required(VERSION 3.22)

project(OniriqueMeterBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    # Universal binary, and the oldest macOS the JUCE 9 runtime supports.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE is looked for next to this file first, then in the PitchBox checkout
# beside this one, so the plugins of the range can share a single copy.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    set(METERBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
else()
    set(METERBOX_JUCE_DEFAULT "${CMAKE_CURRENT_SOURCE_DIR}/../PitchBox/JUCE")
endif()

set(METERBOX_JUCE_DIR "${METERBOX_JUCE_DEFAULT}" CACHE PATH "Path to JUCE")

if(NOT EXISTS "${METERBOX_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE not found at ${METERBOX_JUCE_DIR}.\n"
        "Run:  git clone --depth 1 --branch 9.0.3 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory("${METERBOX_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The measurements, built on their own so the offline tests can link them
# without dragging in JUCE. Every number the plugin shows is worked out here.
add_library(meterbox_core STATIC
    Source/dsp/Loudness.cpp
    Source/dsp/Meter.cpp
    Source/dsp/Spectrum.cpp
    Source/dsp/Tonal.cpp)

target_include_directories(meterbox_core PUBLIC Source)
set_target_properties(meterbox_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(meterbox_core PRIVATE /W4)
else()
    target_compile_options(meterbox_core PRIVATE -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# The plugin.
set(METERBOX_FORMATS VST3)

if(APPLE)
    list(APPEND METERBOX_FORMATS AU)
endif()

# The system VST3 folder on Windows is under Program Files, which a normal build
# cannot write to, so installing after the build is opt-in there.
if(WIN32)
    option(METERBOX_COPY_AFTER_BUILD "Install the plugin after building" OFF)
else()
    option(METERBOX_COPY_AFTER_BUILD "Install the plugin after building" ON)
endif()

juce_add_plugin(OniriqueMeterBox
    COMPANY_NAME "Onirique"
    BUNDLE_ID "com.onirique.meterbox"
    PLUGIN_MANUFACTURER_CODE Onrq
    PLUGIN_CODE Mtbx
    FORMATS ${METERBOX_FORMATS}
    PRODUCT_NAME "MeterBox"
    # What hosts list it as. The file on disk keeps the short name.
    PLUGIN_NAME "ONIRIQUE MeterBox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    # Nothing is typed into a meter: the keys stay the host's.
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD ${METERBOX_COPY_AFTER_BUILD})

# The Onirique sigil, as it appears in the header of the site.
juce_add_binary_data(MeterBoxAssets SOURCES Resources/sigil.png)

target_sources(OniriqueMeterBox PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/HistoryGraph.cpp
    Source/ui/LoudnessPanel.cpp
    Source/ui/PhasePanel.cpp
    Source/ui/SpectrumDisplay.cpp
    Source/ui/Theme.cpp
    Source/ui/WaveformPanel.cpp)

# The licence every Onirique plugin carries: the folder is copied as it is from
# plugin-license/ in the ONIRIQUE CENTRAL repository, and is only ever changed there.
include(License/OniriqueLicense.cmake)
onirique_add_license(OniriqueMeterBox)

target_compile_definitions(OniriqueMeterBox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# The descriptions are in French: the sources are UTF-8, and MSVC has to be told.
if(MSVC)
    target_compile_options(OniriqueMeterBox PRIVATE /utf-8)
endif()

target_link_libraries(OniriqueMeterBox
    PRIVATE
        meterbox_core
        MeterBoxAssets
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: every measurement without a host, against the signals the
# standards give for it.
enable_testing()

add_executable(test_meter tests/test_meter.cpp)
target_link_libraries(test_meter PRIVATE meterbox_core)
target_include_directories(test_meter PRIVATE tests)
add_test(NAME meter COMMAND test_meter)

# A development tool, not part of the product: renders the editor to a PNG
# without a host. Built only when asked for by name.
add_executable(meterbox_snapshot EXCLUDE_FROM_ALL tools/snapshot.cpp)

target_include_directories(meterbox_snapshot PRIVATE
    tests
    $<TARGET_PROPERTY:OniriqueMeterBox,INCLUDE_DIRECTORIES>)

target_compile_definitions(meterbox_snapshot PRIVATE
    $<TARGET_PROPERTY:OniriqueMeterBox,COMPILE_DEFINITIONS>)

if(MSVC)
    target_compile_options(meterbox_snapshot PRIVATE /utf-8)
endif()

target_link_libraries(meterbox_snapshot PRIVATE OniriqueMeterBox meterbox_core MeterBoxAssets)
