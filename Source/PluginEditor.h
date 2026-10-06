#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"
#include "ModifierLookAndFeel.h"

// Rotary knob without a text box: drag to change (value bubble on drag),
// double-click to type a precise value.
class Knob : public juce::Slider
{
public:
    std::function<void(Knob*)> onDoubleClickEntry;

    Knob()
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setPopupDisplayEnabled(true, false, nullptr, 0);
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDoubleClick(e);
        if (onDoubleClickEntry) onDoubleClickEntry(this);
    }
};

// Single-octave piano (C4..C5) with scale highlighting, note names, opaque keys.
class PianoView : public juce::Component
{
public:
    PianoView(MidiNoteModifierProcessor& proc, std::function<void()> onChanged);
    void paint(juce::Graphics& g) override;
    void resized() override {}
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    bool editMode = false;
    int heldUiNote = -1; // auditioning note

private:
    MidiNoteModifierProcessor& p;
    std::function<void()> onChanged;
    static constexpr int kStart = 60; // C4
    static constexpr int kKeys = 13;  // C4..C5

    bool isBlack(int midiNote) const;
    int noteAtPoint(juce::Point<int> pt) const;
    juce::Rectangle<float> rectForKey(int midiNote, bool black, float wWhite, float h) const;
};

class MidiNoteModifierEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MidiNoteModifierEditor(MidiNoteModifierProcessor&);
    ~MidiNoteModifierEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    MidiNoteModifierProcessor& proc;
    ModifierLookAndFeel lnf;
    using AttachC = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using AttachT = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using AttachS = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<AttachC> themeA;

    // header
    juce::Label titleLabel, subtitleLabel;
    juce::Label transportLabel;
    juce::ComboBox themeBox;
    juce::TextButton panicButton { "Panic" };

    // scale section
    juce::Label scaleTitle { {}, "Scale" };
    juce::ToggleButton lockButton { "Lock" };
    juce::ToggleButton whiteKeysButton { "White Keys" };
    juce::Label rootLabel { {}, "Root" }, scaleLabel { {}, "Scale" }, snapLabel { {}, "Snap" };
    juce::ComboBox rootBox, scaleBox, snapBox;
    std::unique_ptr<AttachC> rootA, scaleA, snapA;
    std::unique_ptr<AttachT> lockA, whiteKeysA;

    juce::ToggleButton editToggle { "Edit" };
    juce::TextButton pcButtons[12];
    juce::TextEditor presetName { "presetName" };
    juce::TextButton saveButton { "Save" };
    juce::Label pianoHint { {}, "Click keys to audition" };

    PianoView piano;

    // chord section
    juce::Label chordTitle { {}, "Chords" };
    juce::ToggleButton chordOn { "On" };
    std::unique_ptr<AttachT> chordOnA;
    juce::ComboBox chordBoxes[12];
    juce::Label chordDegLabels[12];
    std::unique_ptr<AttachC> chordA[12];
    juce::Label inversionLabel { {}, "Inversion" };
    juce::ComboBox inversionBox;
    std::unique_ptr<AttachC> inversionA;
    juce::ToggleButton invLowButton { "Lower" };
    std::unique_ptr<AttachT> invLowA;
    juce::ToggleButton bassButton { "Bass -8ve" };
    std::unique_ptr<AttachT> bassA;
    Knob strumKnob;
    std::unique_ptr<AttachS> strumA;
    juce::Label strumLabel { {}, "Strum" };
    juce::TextButton diatonicButton { "Reset Diatonic" };

    // custom chord-type designer
    juce::Label customChordLabel { {}, "Custom" };
    juce::ComboBox customSlotBox;
    juce::TextEditor customChordName { "customChordName" };
    juce::ComboBox customIvBoxes[MidiNoteModifierProcessor::kMaxChordNotes];
    juce::TextButton customChordSave { "Save" };
    juce::TextButton customChordDelete { "Delete" };
    int chordEditSlot = 0;

    // arp section
    juce::Label arpTitle { {}, "Arpeggiator" };
    juce::ToggleButton arpOn { "On" }, arpLatch { "Latch" };
    std::unique_ptr<AttachT> arpOnA, arpLatchA;
    juce::Label arpModeLabel { {}, "Direction" }, arpRateLabel { {}, "Rate" },
                arpOctLabel { {}, "Octaves" };
    juce::ComboBox arpModeBox, arpRateBox, arpOctBox;
    std::unique_ptr<AttachC> arpModeA, arpRateA, arpOctA;
    Knob swingKnob, gateKnob;
    std::unique_ptr<AttachS> swingA, gateA;
    juce::Label swingLabel { {}, "Swing" }, gateLabel { {}, "Gate" };

    juce::Label statusLabel;

    juce::Rectangle<int> scalePanelRect, chordPanelRect, arpPanelRect;

    void refreshScaleBox();
    void refreshChordSlots();
    void refreshPcButtons();
    void refreshChordTypeNames();
    void refreshCustomDesigner();
    void refreshEnabledStates();
    int currentScaleLength();
    juce::Label* mkLabel(juce::Label& l, const juce::String& text, bool dim = true);
    void showValueEntry(Knob* knob);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiNoteModifierEditor)
};
