#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// Setup presets: a whole scale + chords + arp configuration (every parameter
// except the colour theme, plus the custom chord types those settings may
// use). Factory setups are small recipes below; user setups are XML files in
// Documents/Midi Note Modifier Presets. Adapted from the ERSA preset files.
namespace SetupPresets
{
    static const juce::Identifier kRootType { "MNM_SETUP" };

    struct Factory { juce::String name; juce::ValueTree tree; };
    const std::vector<Factory>& factory(); // built once, message thread

    struct UserPreset { juce::String name; juce::File file; };
    void setUserPresetDirForTest(const juce::File& dir); // harness override, else Documents
    juce::File getUserPresetDir();
    std::vector<UserPreset> scanUserPresets();
    juce::String sanitiseName(juce::String name);
    bool writeUserPreset(const juce::String& name, const juce::ValueTree& setup);
    juce::ValueTree readUserPreset(const juce::File& file); // invalid tree on failure
    bool deleteUserPreset(const juce::File& file);
}
