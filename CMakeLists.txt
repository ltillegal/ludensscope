cmake_minimum_required(VERSION 3.22)

# Sensible macOS defaults for native builds; override with -D if needed.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

set(LUDENSCOPE_VERSION "0.3.2")
project(LudensScope VERSION ${LUDENSCOPE_VERSION} LANGUAGES C CXX)
add_compile_definitions (LUDENSCOPE_VERSION_STRING="${LUDENSCOPE_VERSION}")

enable_testing()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.15
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE)

FetchContent_MakeAvailable(JUCE)

juce_add_binary_data(LudensScopeBinaryData SOURCES
  Source/assets/ludens_logo.png)

juce_add_plugin(LudensScope
  COMPANY_NAME "LudensScope"
  BUNDLE_ID "local.ludensscope"
  PLUGIN_MANUFACTURER_CODE LDNS
  PLUGIN_CODE LDSC
  FORMATS VST3 AU
  PRODUCT_NAME "LudensScope"
  DESCRIPTION "Stereo spectrum, loudness and scope analyzer"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
  NEEDS_CURL TRUE
  VST3_CATEGORIES "Fx Analyzer")

target_sources(LudensScope PRIVATE
  Source/PluginProcessor.cpp
  Source/PluginEditor.cpp
  Source/dsp/LoudnessMeter.cpp
  Source/dsp/SpectrumAnalyzer.cpp)

target_include_directories(LudensScope PRIVATE Source)

target_compile_definitions(LudensScope PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_REPORT_APP_USAGE=0)

target_link_libraries(LudensScope PRIVATE
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_cryptography
  LudensScopeBinaryData
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

option(LUDENSCOPE_BUILD_TESTS "Build the offline calibration test" OFF)

if(LUDENSCOPE_BUILD_TESTS)
  juce_add_console_app(LudensScopeCalibration PRODUCT_NAME "LudensScopeCalibration")
  target_sources(LudensScopeCalibration PRIVATE
    Tests/Calibration.cpp
    Source/dsp/SpectrumAnalyzer.cpp)
  target_include_directories(LudensScopeCalibration PRIVATE Source)
  target_link_libraries(LudensScopeCalibration PRIVATE
    juce::juce_dsp
    juce::juce_recommended_config_flags)

  add_executable(LudensScopeLoudnessCalibration
    Tests/LoudnessCalibration.cpp
    Source/dsp/LoudnessMeter.cpp)
  target_include_directories(LudensScopeLoudnessCalibration PRIVATE Source)
  target_compile_features(LudensScopeLoudnessCalibration PRIVATE cxx_std_17)

  juce_add_console_app(LudensScopeUiSnapshot PRODUCT_NAME "LudensScopeUiSnapshot" NEEDS_CURL TRUE)
  target_sources(LudensScopeUiSnapshot PRIVATE
    Tests/UiSnapshot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/dsp/LoudnessMeter.cpp
    Source/dsp/SpectrumAnalyzer.cpp)
  target_include_directories(LudensScopeUiSnapshot PRIVATE Source)
  target_compile_definitions(LudensScopeUiSnapshot PRIVATE
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0)
  target_link_libraries(LudensScopeUiSnapshot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_cryptography
    LudensScopeBinaryData
    juce::juce_recommended_config_flags)

  add_test(NAME loudness COMMAND LudensScopeLoudnessCalibration)
  add_test(NAME spectrum COMMAND LudensScopeCalibration)

  # The UI snapshot needs a display (Xvfb on Linux); hosted macOS/Windows
  # runners do not provide a GUI session for this.
  if(UNIX AND NOT APPLE)
    add_test(NAME ui_snapshot COMMAND LudensScopeUiSnapshot)
  endif()
endif()
