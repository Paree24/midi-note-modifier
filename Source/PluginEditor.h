#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "PluginProcessor.h"
#include "ModifierLookAndFeel.h"
#include "PresetBrowser.h"

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

// One-octave piano (C4..C5, drag audition ported from the Pastel PianoStrip)
// with scale highlighting and note names: the keys are the scale's pitch classes.
class PianoView : public juce::Component
{
public:
    PianoView(MidiNoteModifierProcessor& proc, const Theme& theme, std::function<void()> onChanged);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    int getLowestC() const { return loKey; }
    int getHighestKey() const { return hiKey; }

    bool editMode = false;
    int heldUiNote = -1; // auditioning note

private:
    MidiNoteModifierProcessor& p;
    const Theme& theme;
    std::function<void()> onChanged;
    int loKey = 60, hiKey = 72; // one octave, C4..C5 (the scale editor)
    int numOctaves = 1;
    std::vector<int> whiteKeys;
    struct BlackGeom { int midi; float x0, x1; };
    std::vector<BlackGeom> blackKeys;
    float whiteW = 20.0f;

    static bool isBlack(int midiNote);
    void rebuildLayout();
    int noteAtPoint(juce::Point<float> pt) const;
    void auditionAt(juce::Point<float> pt);
};

// v3: the pitch classes a scale degree's chord plays, on a one-octave mini keyboard.
class ChordPreview : public juce::Component
{
public:
    ChordPreview(MidiNoteModifierProcessor& p, const Theme& t, int degree) : proc(p), theme(t), deg(degree)
    {
        setInterceptsMouseClicks(false, false);
    }
    void paint(juce::Graphics& g) override;
private:
    MidiNoteModifierProcessor& proc;
    const Theme& theme;
    int deg;
};

// v3: what the arpeggiator will play (degree 1's chord, mode, octaves, gate, swing), 16 steps.
class ArpPreview : public juce::Component
{
public:
    ArpPreview(MidiNoteModifierProcessor& p, const Theme& t) : proc(p), theme(t) { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override;
private:
    MidiNoteModifierProcessor& proc;
    const Theme& theme;
};

class MidiNoteModifierEditor : public juce::AudioProcessorEditor, public juce::Timer
{
public:
    MidiNoteModifierEditor(MidiNoteModifierProcessor&);
    ~MidiNoteModifierEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    MidiNoteModifierProcessor& proc;
    ModifierLookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 600 }; // makes the setTooltip() hints show
    using AttachC = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using AttachT = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using AttachS = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<AttachC> themeA;

    // header
    juce::Label titleLabel;
    juce::Label transportLabel;
    juce::ComboBox themeBox;
    juce::TextButton panicButton { "Panic" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton presetButton; // opens the setup browser; shows the loaded setup

    // scale section
    juce::Label scaleTitle { {}, "Scale" };
    juce::ToggleButton lockButton { "Lock" };
    juce::ToggleButton whiteKeysButton { "White Keys" };
    juce::Label rootLabel { {}, "Root" }, scaleLabel { {}, "Scale" }, snapLabel { {}, "Snap" };
    juce::ComboBox rootBox, scaleBox, snapBox;
    std::unique_ptr<AttachC> rootA, snapA; // scale box is synced by hand (its items != param choices)
    std::unique_ptr<AttachT> lockA, whiteKeysA;

    juce::ToggleButton editToggle { "Edit" };
    juce::TextButton pcButtons[12];
    juce::TextEditor presetName { "presetName" };
    juce::TextButton saveButton { "Save" };
    juce::TextButton deleteScaleButton { "Delete" };
    juce::Label pianoHint { {}, "Click keys to audition" };

    PianoView piano;

    // chord section
    juce::Label chordTitle { {}, "Chords" };
    juce::ToggleButton chordOn { "On" };
    std::unique_ptr<AttachT> chordOnA;
    juce::ComboBox chordBoxes[12];
    juce::Label chordDegLabels[12];
    std::unique_ptr<ChordPreview> chordPreviews[12];
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
    std::unique_ptr<ArpPreview> arpPreview;

    juce::Label statusLabel;

    juce::Rectangle<int> scalePanelRect, chordPanelRect, arpPanelRect;

    PresetBrowser presetBrowser { proc, lnf }; // overlay, added last so it sits on top
    void refreshPresetButton();

    void refreshScaleBox();
    void syncScaleBoxSelection();
    int lastScaleParam = -1;
    int lastStateVersion = 0;
    void refreshAfterStateChange();
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
