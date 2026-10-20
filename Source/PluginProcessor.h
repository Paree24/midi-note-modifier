#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>
#include <vector>
#include "Scales.h"
#include "SnapshotUndo.h"

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

    // Undo/redo: knob/combo/toggle gestures become single steps; editor actions
    // that change several things at once open a SnapshotUndo::Transaction.
    SnapshotUndo undoHistory;
    void undo() { undoHistory.undo(); }
    void redo() { undoHistory.redo(); }

    // Setup presets (scale + chords + arp): see SetupPresets.h
    juce::ValueTree captureSetup();                  // every parameter but the theme + custom chord types
    void applySetup(const juce::ValueTree& setup, const juce::String& name); // one undo step
    juce::String getSetupName() const { return apvts.state.getProperty("setupName").toString(); }

    // Live status for the UI
    std::atomic<int> activeVoices { 0 };
    std::atomic<double> lastHostBpm { 120.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int> lastSnappedNote { -1 };
    std::atomic<int> lastInputNote { -1 };
    std::atomic<int> stateVersion { 0 }; // bumped whenever the whole state is replaced

    // UI-triggered actions
    void panic();
    void queueAuditionNote(bool on, int midiNote, int velocity = 100);
    void resetDiatonicChords();
    // UI preview: the notes scale degree `degree` plays (its note in octave 4 run
    // through chords + inversion). Reads the same atomics as the audio thread; a
    // torn read only costs one stale frame. out needs 8 (kMaxEmit) entries.
    int previewChord(int degree, int* out);
    bool saveCustomScale(const juce::String& name);
    bool deleteCustomScale(const juce::String& name);
    bool isUserScale(const juce::String& name) const;
    static bool isReservedScaleName(const juce::String& name); // clashes with a factory scale / "Custom"
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
    // Everything below the parameter caches is audio-thread only: fixed-size
    // storage, no locks shared with the UI, no allocation per block or event.
    static constexpr int kMaxEmit = 8;          // chord notes + bass per input key
    static constexpr int kMaxDeferred = 512;
    static constexpr int kMaxPendingOns = 256;
    static constexpr int kAuditionFifoSize = 64;

    struct Voice { bool active = false; int snapped = -1; int channel = 1; int numEmitted = 0; int emitted[kMaxEmit] {}; };
    struct PendingOn { juce::int64 time = 0; int note = 0; int channel = 1; float vel = 0.0f; };
    struct Deferred { int first = 0; int second = 1; bool operator==(const Deferred& o) const { return first == o.first && second == o.second; } };

    double currentSampleRate = 44100.0;

    // cached parameter pointers (no String building / map lookups per block)
    std::atomic<float>* pRoot = nullptr; std::atomic<float>* pScale = nullptr;
    std::atomic<float>* pSnap = nullptr; std::atomic<float>* pScaleLock = nullptr;
    std::atomic<float>* pWhiteKeys = nullptr; std::atomic<float>* pChordOn = nullptr;
    std::atomic<float>* pInversion = nullptr; std::atomic<float>* pInvLower = nullptr;
    std::atomic<float>* pBassBelow = nullptr; std::atomic<float>* pStrum = nullptr;
    std::atomic<float>* pArpOn = nullptr; std::atomic<float>* pArpLatch = nullptr;
    std::atomic<float>* pArpMode = nullptr; std::atomic<float>* pArpRate = nullptr;
    std::atomic<float>* pArpOct = nullptr; std::atomic<float>* pSwing = nullptr;
    std::atomic<float>* pGate = nullptr;
    std::atomic<float>* pPc[12] {};
    std::atomic<float>* pChd[kNumChordSlots] {};

    Voice voices[128];                       // indexed by input note
    int numVoices = 0;
    Deferred deferredOffs[kMaxDeferred];     // (note, channel), or (-1000 - inNote, ch) pool removals
    int numDeferred = 0;
    bool sustainDown = false;

    // Plain (non-arp) output bookkeeping: per (channel, note) on-count so that two
    // inputs snapping to the same output note don't cut each other off, plus
    // strummed note-ons that fall beyond the current block.
    int outRefCount[16][128] {};
    PendingOn pendingOns[kMaxPendingOns];
    int numPendingOns = 0;
    juce::int64 sampleClock = 0;
    juce::MidiBuffer outBuffer;

    // scale intervals for the audio thread (capacity reserved, never reallocates)
    std::vector<int> rtIntervals;
    const std::vector<int>& currentIntervalsRT();

    // Arp pool (post scale+chord base notes, pre octave expansion)
    int arpPool[128] {};       // unique sorted base notes
    int arpPoolSize = 0;
    int arpPlayedOrder[128] {};// insertion order
    int arpPlayedSize = 0;
    int arpRefCount[128] {};   // base note -> holders
    int arpExpanded[128 * 4] {};
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
    int gateOffIn = -1;           // pending gate note-off, samples from block start
    bool arpWasSynced = false;
    bool prevLatch = false;
    bool prevArpOn = false;
    juce::Random arpRandom;

    // editor keyboard auditions: single-producer (UI) / single-consumer (audio) FIFO
    struct Audition { bool on; int note; int vel; };
    juce::AbstractFifo auditionFifo { kAuditionFifoSize };
    Audition auditionBuf[kAuditionFifoSize] {};

    std::atomic<bool> panicFlag { false };

    // Custom chord types. Names/interval vectors are for the UI side (under
    // chordTypeLock); the audio thread reads only the packed atomic copy.
    juce::CriticalSection chordTypeLock;
    juce::String customChordNames[kNumCustomChords];
    std::vector<int> customChordIvs[kNumCustomChords];
    std::atomic<juce::uint64> customChordPacked[kNumCustomChords];
    static juce::uint64 packIntervals(const std::vector<int>& ivs);
    static int unpackIntervals(juce::uint64 packed, int* out);
    void publishCustomChord(int slot);
    void loadCustomChordTypesFromState();
    void storeCustomChordTypesToState();
    juce::ValueTree captureUndoState();
    void restoreUndoState(const juce::ValueTree& snapshot);

    // helpers
    float getRateBeats() const; // quarter-note multiples for current rate param
    int buildChordForSnapped(int snapped, int* out, int& count, int& degreeOut);
    static void applyInversionBass(int* chord, int& count, int inversion, bool lower, bool bassBelow);
    void noteOnOut(juce::MidiBuffer& out, int samplePos, int note, int channel, float vel);
    void noteOffOut(juce::MidiBuffer& out, int samplePos, int note, int channel);
    void killAllSound(juce::MidiBuffer& out);
    void resetAllNoteState();
    void voiceNoteOn(juce::MidiBuffer& out, int samplePos, int note, int channel, float vel);
    void voiceNoteOff(juce::MidiBuffer& out, int samplePos, int note, int channel);
    void releaseVoiceNote(juce::MidiBuffer& out, int samplePos, int note, int channel);
    void addDeferred(juce::MidiBuffer& out, int samplePos, int note, int channel);
    void removeVoice(int inNote);
    int firstVoiceChannel() const;
    void addToArpPool(const int* notes, int count);
    void removeFromArpPool(const int* notes, int count);
    void clearArpPool();
    int pickArpNote();
    void resetArpPosition() { arpStepIndex = 0; arpPingDir = 1; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNoteModifierProcessor)
};
