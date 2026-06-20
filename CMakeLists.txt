cmake_minimum_required(VERSION 3.22)

project(VoiceMelody VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VOICEMELODY_FETCH_JUCE "Download JUCE with FetchContent when JUCE is not already available" OFF)
set(VOICEMELODY_JUCE_GIT_TAG "8.0.10" CACHE STRING "JUCE git tag used when VOICEMELODY_FETCH_JUCE is ON")

find_package(JUCE CONFIG QUIET)

if (NOT JUCE_FOUND AND VOICEMELODY_FETCH_JUCE)
  include(FetchContent)
  FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG ${VOICEMELODY_JUCE_GIT_TAG}
  )
  FetchContent_MakeAvailable(JUCE)
endif()

if (NOT TARGET juce::juce_audio_utils)
  message(WARNING
    "JUCE was not found, so the VoiceMelody plugin target will not be generated. "
    "Configure remains successful for dependency-light environments. "
    "Install JUCE and set CMAKE_PREFIX_PATH, add JUCE as a subdirectory before this project, "
    "or rerun with -DVOICEMELODY_FETCH_JUCE=ON to build the actual plugin.")

  add_custom_target(VoiceMelody
    COMMAND ${CMAKE_COMMAND} -E echo "VoiceMelody plugin skipped: JUCE is not available."
    VERBATIM
  )
  return()
endif()

juce_add_plugin(VoiceMelody
  COMPANY_NAME "Voice Melody"
  BUNDLE_ID "com.voicemelody.plugin"
  PLUGIN_MANUFACTURER_CODE Vome
  PLUGIN_CODE Vmel
  FORMATS VST3 AU Standalone
  PRODUCT_NAME "Voice Melody"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(VoiceMelody
  PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/PitchDetector.cpp
)

target_compile_definitions(VoiceMelody
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(VoiceMelody
  PRIVATE
    juce::juce_audio_utils
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
