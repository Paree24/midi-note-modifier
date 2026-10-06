cmake_minimum_required(VERSION 3.22)
project(MidiNoteModifier VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    URL "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.9.tar.gz"
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MidiNoteModifier
    COMPANY_NAME "Midi Note Modifier"
    BUNDLE_ID "com.midinotemodifier.plugin"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE MnmM
    PLUGIN_CODE Mnm1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Midi Note Modifier"
    DESCRIPTION "Scale / chord / arp MIDI modifier"
)

target_sources(MidiNoteModifier PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(MidiNoteModifier PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

juce_add_binary_data(MidiFontData
    HEADER_NAME MidiFonts.h
    NAMESPACE MidiFonts
    SOURCES
        Fonts/Lato-Regular.ttf
        Fonts/Lato-Bold.ttf
)

target_link_libraries(MidiNoteModifier PRIVATE
    MidiFontData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_recommended_lto_flags
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Offscreen UI screenshot helper (headless verification, not part of the plugin)
add_executable(UiShot EXCLUDE_FROM_ALL
    UiShot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)
target_compile_definitions(UiShot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=0
)
target_link_libraries(UiShot PRIVATE
    MidiFontData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_data_structures
    juce::juce_events
    juce::juce_recommended_config_flags
)
