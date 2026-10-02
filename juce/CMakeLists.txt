# Bitey — JUCE plugin shell (VST3 / AU / Standalone)
#
# The entire sound lives in ../dsp/BiteyDsp.{h,cpp} (framework-free). This
# directory is only the plugin wrapper: parameters, DAW I/O, and GUI.
#
# Build (macOS, Xcode):
#   cmake -B build -G Xcode -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
# Build (Windows, VS2022):
#   cmake -B build -G "Visual Studio 17 2022"
#   cmake --build build --config Release
# Build (Linux, VST3/Standalone):
#   sudo apt install libasound2-dev libjack-jackd2-dev libfreetype6-dev \
#       libx11-dev libxrandr-dev libxcursor-dev libxinerama-dev \
#       libgl1-mesa-dev libcurl4-openssl-dev
#   cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#
# JUCE is fetched automatically on first configure (needs internet).
# NOTE: JUCE 8 is AGPLv3-or-commercial. A closed-source paid plugin needs a
# commercial JUCE license (see ../README.md). The DSP core has no JUCE
# dependency, so the wrapper can be re-targeted at iPlug2/CLAP later.

cmake_minimum_required(VERSION 3.22)
project(Bitey VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Bitey
    COMPANY_NAME "Sabatino Audio"
    PLUGIN_NAME "Bitey"
    DESCRIPTION "PA-600 reverb mixer: tube channel strips, tape slap, spring reverb."
    PLUGIN_MANUFACTURER_CODE SabA
    PLUGIN_CODE BitY
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Bitey"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Bitey PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    ../dsp/BiteyDsp.cpp
)

# Embedded panel fonts (all OFL-licensed): Roboto Condensed for panel lettering
# (Peavey PA-600 style), Michroma for accents, Metal Mania for VU branding,
# Permanent Marker for the board tape.
juce_add_binary_data(BiteyAssets SOURCES
    assets/fonts/Michroma-Regular.ttf
    assets/fonts/MetalMania-Regular.ttf
    assets/fonts/PermanentMarker-Regular.ttf
    assets/fonts/RobotoCondensed.ttf
    assets/bitey-logo.png
)

target_include_directories(Bitey PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(Bitey PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Bitey PRIVATE
    BiteyAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)

# ---------------------------------------------------------------------------
# BiteyTestHost — standalone listening rig (NOT shipped): loads an audio file
# and runs it through the real BiteyAudioProcessor + editor so the plugin can
# be heard without a DAW. Built on CI for macOS; uploaded as its own artifact.
# ---------------------------------------------------------------------------
juce_add_gui_app(BiteyTestHost
    PRODUCT_NAME "Bitey Test Host"
    COMPANY_NAME "Sabatino Audio"
)

target_sources(BiteyTestHost PRIVATE
    test_host/Main.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    ../dsp/BiteyDsp.cpp
)

target_include_directories(BiteyTestHost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_compile_definitions(BiteyTestHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(BiteyTestHost PRIVATE
    BiteyAssets
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
)
