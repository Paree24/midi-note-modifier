#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include <vector>
#include <set>
#include "Scales.h"

class MidiNoteModifierProcessor : public juce::AudioProcessor
{
public:
    MidiNoteModifierProcessor();
    ~MidiNoteModifierProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Midi Note Modifier"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Live status for the UI
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> lastHostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> lastSnappedNote { -1 };
    std::atomic<int> lastInputNote { -1 };

    // UI-triggered actions
    void panic();
    void queueAuditionNote(bool on, int midiNote, int velocity = 100);
    void resetDiatonicChords();
    bool saveCustomScale(const juce::String& name);
    bool deleteCustomScale(const juce::String& name);
    juce::StringArray getAllScaleNames() const; // factory + user (+ Custom implicit)
    // Custom chord types (interval sets defined by the user, e.g. Major 6th)
    bool saveChordType(int slot, const juce::String& name, const std::vector<int>& intervals);
    bool resetChordType(int slot);
    juce::String getCustomChordName(int slot) const;
    std::vector<int> getCustomChordIntervals(int slot) const;
    juce::String customChordDisplayName(int slot) const; // "Major 6th" or "Custom 1"
    static int loadDefaultTheme(); // global default colour scheme for new instances
    static void storeDefaultTheme(int i);
    std::vector<int> currentIntervals(); // resolves scale param incl. Custom pattern
    int currentRoot();
    std::array<bool, 12> currentPcSet();

    static constexpr int kNumChordSlots = 12;
    static constexpr int kNumCustomChords = 6;
    static constexpr int kMaxChordNotes = 6;

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    struct Voice { int snapped = -1; std::vector<int> emitted; };
    struct ArpTick { int sampleOffset = 0; int note = -1; };

    double currentSampleRate = 44100.0;

    juce::CriticalSection stateLock;
    std::map<int, Voice> voices;             // inputNote -> emitted
    std::set<int> sustainedNotes;            // emitted notes with note-off deferred
    std::vector<std::pair<int,int>> deferredOffs; // (note, channel)
    bool sustainDown = false;

    // Arp pool (post scale+chord base notes, pre octave expansion)
    std::vector<int> arpPool;       // unique sorted base notes
    std::vector<int> arpPlayedOrder;// insertion order
    std::map<int,int> arpRefCount;  // base note -> holders
    int arpCurrentNote = -1;
    int arpCurrentChannel = 1;
    int arpStepIndex = 0;
    int arpPingDir = 1;
    double nextPpq = -1.0;
    double samplesToNextTick = 0.0;
    float arpLastVel = 0.9f;
    bool arpImmediatePending = false;
    int arpImmediatePos = 0;
    int arpImmediateCh = 1;
    bool latchHeldNotesWereReleased = false;

    struct Audition { bool on; int note; int vel; };
    std::vector<Audition> auditionQueue;

    std::atomic<bool> panicFlag { false };

    // Custom chord types (audio thread reads these under chordTypeLock)
    juce::CriticalSection chordTypeLock;
    juce::String customChordNames[kNumCustomChords];
    std::vector<int> customChordIvs[kNumCustomChords];
    void loadCustomChordTypesFromState();
    void storeCustomChordTypesToState();

    // helpers
    float getRateBeats() const; // quarter-note multiples for current rate param
    int buildChordForSnapped(int snapped, std::vector<int>& out, int& degreeOut);
    void applyInversionBass(std::vector<int>& chord, int inversion, bool lower, bool bassBelow);
    void noteOnOut(juce::MidiBuffer& out, int samplePos, int note, int channel, float vel);
    void noteOffOut(juce::MidiBuffer& out, int samplePos, int note, int channel);
    void killAllSound(juce::MidiBuffer& out);
    int pickArpNote();
    void resetArpPosition() { arpStepIndex = 0; arpPingDir = 1; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNoteModifierProcessor)
};
