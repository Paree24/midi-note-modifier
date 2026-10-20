#include "PluginEditor.h"

// ============ double-click precise value entry ============

class KnobValueEntry : public juce::Component, private juce::TextEditor::Listener
{
public:
    explicit KnobValueEntry(Knob* t) : target(t)
    {
        addAndMakeVisible(ed);
        ed.setText(target->getTextFromValue(target->getValue()), false);
        ed.selectAll();
        ed.addListener(this);
        setSize(140, 42);
    }

    void resized() override { ed.setBounds(6, 6, getWidth() - 12, getHeight() - 12); }

    void textEditorReturnKeyPressed(juce::TextEditor&) override { applyAndClose(); }
    void textEditorEscapeKeyPressed(juce::TextEditor&) override { dismiss(); }
    void textEditorFocusLost(juce::TextEditor&) override { applyAndClose(); }

private:
    void applyAndClose()
    {
        double v = target->getValueFromText(ed.getText());
        target->setValue(v, juce::sendNotificationSync);
        dismiss();
    }
    void dismiss()
    {
        if (auto* cb = findParentComponentOfClass<juce::CallOutBox>())
            cb->dismiss();
    }

    Knob* target;
    juce::TextEditor ed;
};

// ============ PianoView (single octave C4..C5, opaque keys) ============

PianoView::PianoView(MidiNoteModifierProcessor& proc_, const Theme& theme_, std::function<void()> onChanged_)
    : p(proc_), theme(theme_), onChanged(std::move(onChanged_)) {}

bool PianoView::isBlack(int n)
{
    int pc = ((n % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

void PianoView::resized()
{
    rebuildLayout(); // one octave: the scale's pitch classes, C4..C5
}

void PianoView::rebuildLayout()
{
    hiKey = loKey + 12 * numOctaves;
    whiteKeys.clear();
    blackKeys.clear();
    for (int m = loKey; m <= hiKey; ++m)
        if (! isBlack(m)) whiteKeys.push_back(m);
    float W = (float) juce::jmax(1, getWidth());
    whiteW = whiteKeys.empty() ? W : W / (float) whiteKeys.size();
    float bw = whiteW * 0.6f;
    int wi = 0;
    for (int m = loKey; m <= hiKey; ++m)
    {
        if (! isBlack(m)) { ++wi; continue; }
        float cx = wi * whiteW;
        blackKeys.push_back({ m, cx - bw * 0.5f, cx + bw * 0.5f });
    }
}

int PianoView::noteAtPoint(juce::Point<float> pt) const
{
    if (whiteKeys.empty() || ! getLocalBounds().toFloat().contains(pt)) return -1;
    float blackH = (float) getHeight() * 0.62f;
    if (pt.y < blackH)
        for (auto& b : blackKeys)
            if (pt.x >= b.x0 && pt.x < b.x1) return b.midi;
    int wi = juce::jlimit(0, (int) whiteKeys.size() - 1, (int) (pt.x / whiteW));
    return whiteKeys[(size_t) wi];
}

void PianoView::paint(juce::Graphics& g)
{
    float h = (float) getHeight();
    float blackH = h * 0.62f;

    auto set = p.currentPcSet();
    int lastIn = p.lastInputNote.load();
    int lastSnapped = p.lastSnappedNote.load();
    auto names = Scales::noteNames();
    const bool wide = whiteW > 30.0f; // room for every note name

    for (size_t i = 0; i < whiteKeys.size(); ++i)
    {
        int n = whiteKeys[i];
        juce::Rectangle<float> r { (float) i * whiteW + 1.5f, 0.0f, whiteW - 3.0f, h };
        int pc = ((n % 12) + 12) % 12;
        bool inScale = set[(size_t) pc];
        bool active = (n == lastSnapped || n == lastIn || n == heldUiNote);
        juce::Colour fill = inScale ? theme.light() : juce::Colour(0xFF3A3A42);
        if (active) fill = juce::Colours::white;
        if (active)
        {
            g.setColour(theme.mid().withAlpha(0.3f));
            g.fillRoundedRectangle(r.expanded(3.0f), 8.0f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colour(0xFF1E1E23));
        g.drawRoundedRectangle(r, 6.0f, 1.5f);
        if (wide || pc == 0)
        {
            g.setColour(inScale ? juce::Colour(0xFF55555F) : juce::Colour(0xFF8F8B85));
            g.setFont(juce::Font(juce::FontOptions(wide ? 13.0f : 11.0f, juce::Font::bold)));
            juce::String label = names[(size_t) pc];
            if (pc == 0) label << (n / 12 - 1); // octave on every C
            g.drawText(label, juce::Rectangle<float>(r.getX(), r.getBottom() - 26, r.getWidth(), 20),
                       juce::Justification::centred, false);
        }
        if (inScale && ! active)
        {
            g.setColour(theme.dark().withAlpha(0.7f));
            g.fillEllipse(r.getCentreX() - 3.0f, r.getBottom() - 44.0f, 6.0f, 6.0f);
        }
    }
    for (auto& b : blackKeys)
    {
        int n = b.midi;
        juce::Rectangle<float> r { b.x0, 0.0f, b.x1 - b.x0, blackH };
        int pc = ((n % 12) + 12) % 12;
        bool inScale = set[(size_t) pc];
        bool active = (n == lastSnapped || n == lastIn || n == heldUiNote);
        juce::Colour fill = inScale ? theme.mid() : juce::Colour(0xFF1E1E23);
        if (active) fill = theme.light();
        if (active)
        {
            g.setColour(theme.mid().withAlpha(0.35f));
            g.fillRoundedRectangle(r.expanded(3.0f), 8.0f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(inScale ? theme.light() : juce::Colour(0xFF45454E));
        g.drawRoundedRectangle(r, 6.0f, 1.2f);
    }
}

void PianoView::auditionAt(juce::Point<float> pt)
{
    int n = noteAtPoint(pt);
    if (n == heldUiNote) return;
    if (heldUiNote >= 0) p.queueAuditionNote(false, heldUiNote, 0);
    heldUiNote = n;
    if (heldUiNote >= 0) p.queueAuditionNote(true, heldUiNote, 100);
    repaint();
}

void PianoView::mouseDown(const juce::MouseEvent& e)
{
    int n = noteAtPoint(e.position);
    if (n < 0) return;
    bool toggle = editMode || e.mods.isRightButtonDown() || e.mods.isShiftDown();
    if (toggle)
    {
        SnapshotUndo::Transaction undoStep(p.undoHistory);
        int pc = ((n % 12) + 12) % 12;
        juce::String id = "pc" + juce::String(pc);
        if (auto* par = p.apvts.getParameter(id))
        {
            bool cur = par->getValue() > 0.5f;
            par->setValueNotifyingHost(cur ? 0.0f : 1.0f);
            if (auto* sp = p.apvts.getParameter("scale"))
                sp->setValueNotifyingHost(sp->convertTo0to1(Scales::customIndex()));
        }
        if (onChanged) onChanged();
        repaint();
    }
    else auditionAt(e.position);
}

void PianoView::mouseDrag(const juce::MouseEvent& e)
{
    // glide across keys while auditioning (not in edit/toggle mode)
    if (heldUiNote >= 0) auditionAt(e.position);
}

void PianoView::mouseUp(const juce::MouseEvent&)
{
    if (heldUiNote >= 0)
    {
        p.queueAuditionNote(false, heldUiNote, 0);
        heldUiNote = -1;
        repaint();
    }
}

// ============ Editor ============

MidiNoteModifierEditor::MidiNoteModifierEditor(MidiNoteModifierProcessor& proc_)
    : juce::AudioProcessorEditor(proc_), proc(proc_), piano(proc_, lnf.theme, [this]
      {
          refreshPcButtons();
          refreshScaleBox();
          refreshChordSlots();
      })
{
    lnf.setTheme((int) proc.apvts.getRawParameterValue("theme")->load());
    setLookAndFeel(&lnf);
    setSize(960, 800);
    setResizable(true, true);
    setResizeLimits(860, 720, 1500, 1150);

    mkLabel(titleLabel, "Midi Note Modifier", false);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);
    presetButton.setTooltip("Setups: whole scale + chords + arp presets (factory and your own)");
    presetButton.onClick = [this]
    {
        presetBrowser.setVisible(! presetBrowser.isVisible());
        if (presetBrowser.isVisible()) presetBrowser.toFront(false);
    };
    presetButton.setComponentID("presetButton");
    addAndMakeVisible(presetButton);

    transportLabel.setJustificationType(juce::Justification::centred);
    transportLabel.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    addAndMakeVisible(transportLabel);

    for (auto& t : Theme::all()) themeBox.addItem(t.name, themeBox.getNumItems() + 1);
    themeA = std::make_unique<AttachC>(proc.apvts, "theme", themeBox);
    themeBox.onChange = [this]
    {
        // the last-used theme becomes the default for new instances
        MidiNoteModifierProcessor::storeDefaultTheme(themeBox.getSelectedItemIndex());
    };
    addAndMakeVisible(themeBox);

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE88A8A));
    panicButton.setColour(juce::TextButton::textColourOffId, ModifierLookAndFeel::ink());
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicButton.onClick = [this] { proc.panic(); };
    addAndMakeVisible(panicButton);
    undoButton.setTooltip("Undo the last change (Ctrl/Cmd+Z)");
    redoButton.setTooltip("Redo what Undo took back (Ctrl/Cmd+Shift+Z)");
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    addAndMakeVisible(undoButton);
    addAndMakeVisible(redoButton);

    // ---- scale ----
    mkLabel(scaleTitle, "Scale", false);
    scaleTitle.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(scaleTitle);
    lockA = std::make_unique<AttachT>(proc.apvts, "scaleLock", lockButton);
    addAndMakeVisible(lockButton);
    whiteKeysA = std::make_unique<AttachT>(proc.apvts, "whiteKeys", whiteKeysButton);
    whiteKeysButton.setTooltip("White keys play consecutive scale degrees (C D E F G A B = degrees 1-7)");
    addAndMakeVisible(whiteKeysButton);

    mkLabel(rootLabel, "Root", true);           addAndMakeVisible(rootLabel);
    mkLabel(scaleLabel, "Scale", true);         addAndMakeVisible(scaleLabel);
    mkLabel(snapLabel, "Snap", true);           addAndMakeVisible(snapLabel);
    for (int i = 0; i < 12; ++i) rootBox.addItem(Scales::pcName(i), i + 1);
    rootA = std::make_unique<AttachC>(proc.apvts, "root", rootBox);
    addAndMakeVisible(rootBox);
    snapBox.addItem("Nearest", 1); snapBox.addItem("Down", 2); snapBox.addItem("Up", 3);
    snapA = std::make_unique<AttachC>(proc.apvts, "snap", snapBox);
    addAndMakeVisible(snapBox);

    // No ComboBoxAttachment here: the box lists factory scales, then user
    // scales, then Custom, so item indices don't match the parameter's choices.
    scaleBox.onChange = [this]
    {
        juce::String sel = scaleBox.getText();
        if (sel.isEmpty()) return;
        SnapshotUndo::Transaction undoStep(proc.undoHistory);
        auto& facts = Scales::factoryScales();
        int idx = -1;
        for (size_t i = 0; i < facts.size(); ++i)
            if (facts[i].name == sel.toStdString()) { idx = (int) i; break; }
        if (idx < 0)
        {
            auto user = proc.apvts.state.getChildWithName("USER_SCALES");
            for (int i = 0; i < user.getNumChildren(); ++i)
            {
                if (user.getChild(i).getProperty("name").toString() == sel)
                {
                    juce::String pat = user.getChild(i).getProperty("pattern").toString();
                    for (int k = 0; k < 12 && k < pat.length(); ++k)
                        if (auto* par = proc.apvts.getParameter("pc" + juce::String(k)))
                            par->setValueNotifyingHost(pat[k] == '1' ? 1.0f : 0.0f);
                    idx = Scales::customIndex();
                    break;
                }
            }
        }
        if (idx >= 0)
            if (auto* par = proc.apvts.getParameter("scale"))
            {
                par->beginChangeGesture();
                par->setValueNotifyingHost(par->convertTo0to1(idx));
                par->endChangeGesture();
            }
        lastScaleParam = (int) proc.apvts.getRawParameterValue("scale")->load();
        refreshChordSlots();
        refreshPcButtons();
        refreshEnabledStates();
    };
    addAndMakeVisible(scaleBox);

    auto names = Scales::noteNames();
    for (int i = 0; i < 12; ++i)
    {
        pcButtons[i].setButtonText(names[(size_t) i]);
        pcButtons[i].setClickingTogglesState(true);
        pcButtons[i].onClick = [this, i]
        {
            // pills always show the current scale: adopting a factory scale
            // copies it into the Custom pattern first, then toggles this pc
            SnapshotUndo::Transaction undoStep(proc.undoHistory);
            int scaleIdx = (int) proc.apvts.getRawParameterValue("scale")->load();
            if (scaleIdx != Scales::customIndex())
            {
                auto set = proc.currentPcSet();
                for (int k = 0; k < 12; ++k)
                    if (auto* par = proc.apvts.getParameter("pc" + juce::String(k)))
                        par->setValueNotifyingHost(set[(size_t) k] ? 1.0f : 0.0f);
                if (auto* sp = proc.apvts.getParameter("scale"))
                    sp->setValueNotifyingHost(sp->convertTo0to1(Scales::customIndex()));
            }
            if (auto* par = proc.apvts.getParameter("pc" + juce::String(i)))
            {
                bool cur = par->getValue() > 0.5f;
                par->setValueNotifyingHost(cur ? 0.0f : 1.0f);
            }
            refreshScaleBox();
            refreshChordSlots();
            refreshPcButtons();
        };
        addAndMakeVisible(pcButtons[i]);
    }

    addAndMakeVisible(editToggle);
    editToggle.onClick = [this] { piano.editMode = editToggle.getToggleState(); };

    presetName.setTextToShowWhenEmpty("Custom Scale Name...", juce::Colour(0xFF8F8B85));
    addAndMakeVisible(presetName);
    saveButton.onClick = [this]
    {
        juce::String n = presetName.getText().trim();
        if (n.isEmpty()) return;
        if (MidiNoteModifierProcessor::isReservedScaleName(n))
        {
            presetName.clear();
            presetName.setTextToShowWhenEmpty("\"" + n + "\" is a factory scale, pick another name",
                                              juce::Colour(0xFFE88A8A));
            return;
        }
        {
            SnapshotUndo::Transaction undoStep(proc.undoHistory);
            proc.saveCustomScale(n);
        }
        refreshScaleBox();
        presetName.clear();
        presetName.setTextToShowWhenEmpty("Custom Scale Name...", juce::Colour(0xFF8F8B85));
    };
    addAndMakeVisible(saveButton);
    deleteScaleButton.setTooltip("Delete the selected user scale");
    deleteScaleButton.onClick = [this]
    {
        juce::String sel = scaleBox.getText();
        if (! proc.isUserScale(sel)) return;
        {
            SnapshotUndo::Transaction undoStep(proc.undoHistory);
            proc.deleteCustomScale(sel);
        }
        // the pattern stays loaded as Custom, so nothing currently playing changes
        scaleBox.setText("Custom", juce::dontSendNotification);
        refreshScaleBox();
        refreshEnabledStates();
    };
    addAndMakeVisible(deleteScaleButton);
    mkLabel(pianoHint, "Click Keys To Audition  |  Shift-Click A Key (Or Edit Mode) To Toggle It In The Scale", true);
    addAndMakeVisible(pianoHint);
    addAndMakeVisible(piano);

    // ---- chords ----
    mkLabel(chordTitle, "Chords", false);
    chordTitle.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(chordTitle);
    chordOnA = std::make_unique<AttachT>(proc.apvts, "chordOn", chordOn);
    chordOn.onClick = [this] { refreshEnabledStates(); };
    addAndMakeVisible(chordOn);
    for (int i = 0; i < 12; ++i)
    {
        chordDegLabels[i].setText("D" + juce::String(i + 1), juce::dontSendNotification);
        chordDegLabels[i].setJustificationType(juce::Justification::centred);
        chordDegLabels[i].setColour(juce::Label::textColourId, juce::Colour(0xFF8F8B85));
        chordDegLabels[i].setFont(juce::Font(juce::FontOptions(11.0f)));
        chordPreviews[i] = std::make_unique<ChordPreview>(proc, lnf.theme, i);
        addAndMakeVisible(*chordPreviews[i]);
        addAndMakeVisible(chordDegLabels[i]);
        for (int t = -1; t <= 12; ++t) chordBoxes[i].addItem(Scales::chordTypeName(t), t + 2);
        for (int s = 0; s < MidiNoteModifierProcessor::kNumCustomChords; ++s)
            chordBoxes[i].addItem(proc.customChordDisplayName(s), 15 + s);
        chordA[i] = std::make_unique<AttachC>(proc.apvts, "chd" + juce::String(i), chordBoxes[i]);
        addAndMakeVisible(chordBoxes[i]);
    }
    mkLabel(inversionLabel, "Inversion", true); addAndMakeVisible(inversionLabel);
    inversionBox.addItem("Root", 1); inversionBox.addItem("1st", 2); inversionBox.addItem("2nd", 3);
    inversionA = std::make_unique<AttachC>(proc.apvts, "inversion", inversionBox);
    addAndMakeVisible(inversionBox);
    invLowA = std::make_unique<AttachT>(proc.apvts, "invLower", invLowButton);
    invLowButton.setTooltip("Lower: same inversion shape one octave down (e.g. E5 G5 C6 becomes E4 G4 C5)");
    addAndMakeVisible(invLowButton);
    bassA = std::make_unique<AttachT>(proc.apvts, "bassBelow", bassButton);
    addAndMakeVisible(bassButton);
    mkLabel(strumLabel, "Strum", true); addAndMakeVisible(strumLabel);
    strumA = std::make_unique<AttachS>(proc.apvts, "strum", strumKnob);
    strumKnob.onDoubleClickEntry = [this](Knob* k) { showValueEntry(k); };
    addAndMakeVisible(strumKnob);
    diatonicButton.onClick = [this]
    {
        { SnapshotUndo::Transaction undoStep(proc.undoHistory); proc.resetDiatonicChords(); }
        refreshChordSlots();
    };
    addAndMakeVisible(diatonicButton);

    // ---- custom chord-type designer ----
    mkLabel(customChordLabel, "Custom", true); addAndMakeVisible(customChordLabel);
    for (int s = 0; s < MidiNoteModifierProcessor::kNumCustomChords; ++s)
        customSlotBox.addItem("Slot " + juce::String(s + 1), s + 1);
    customSlotBox.setSelectedItemIndex(0, juce::dontSendNotification);
    customSlotBox.onChange = [this]
    {
        chordEditSlot = juce::jlimit(0, MidiNoteModifierProcessor::kNumCustomChords - 1,
                                     customSlotBox.getSelectedItemIndex());
        refreshCustomDesigner();
    };
    addAndMakeVisible(customSlotBox);
    customChordName.setTextToShowWhenEmpty("Name, e.g. Major 6th...", juce::Colour(0xFF8F8B85));
    addAndMakeVisible(customChordName);
    for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
    {
        customIvBoxes[k].addItem("--", 1);
        for (int st = 0; st <= 24; ++st)
            customIvBoxes[k].addItem(Scales::intervalName(st), st + 2);
        addAndMakeVisible(customIvBoxes[k]);
    }
    customChordSave.onClick = [this]
    {
        std::vector<int> ivs;
        for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
        {
            int v = customIvBoxes[k].getSelectedId() - 2; // -1 = unused
            if (v >= 0) ivs.push_back(v);
        }
        {
            SnapshotUndo::Transaction undoStep(proc.undoHistory);
            proc.saveChordType(chordEditSlot, customChordName.getText(), ivs);
        }
        refreshCustomDesigner();
        refreshChordTypeNames();
    };
    addAndMakeVisible(customChordSave);
    customChordDelete.onClick = [this]
    {
        {
            SnapshotUndo::Transaction undoStep(proc.undoHistory);
            proc.resetChordType(chordEditSlot);
        }
        refreshCustomDesigner();
        refreshChordTypeNames();
    };
    addAndMakeVisible(customChordDelete);

    // ---- arp ----
    mkLabel(arpTitle, "Arpeggiator", false);
    arpTitle.setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
    addAndMakeVisible(arpTitle);
    arpPreview = std::make_unique<ArpPreview>(proc, lnf.theme);
    addAndMakeVisible(*arpPreview);
    arpOnA = std::make_unique<AttachT>(proc.apvts, "arpOn", arpOn);
    arpLatchA = std::make_unique<AttachT>(proc.apvts, "arpLatch", arpLatch);
    arpOn.onClick = [this] { refreshEnabledStates(); };
    addAndMakeVisible(arpOn); addAndMakeVisible(arpLatch);
    mkLabel(arpModeLabel, "Direction", true); addAndMakeVisible(arpModeLabel);
    mkLabel(arpRateLabel, "Rate", true);       addAndMakeVisible(arpRateLabel);
    mkLabel(arpOctLabel, "Octaves", true);     addAndMakeVisible(arpOctLabel);
    arpModeBox.addItemList({ "Up", "Down", "Up-Down", "Down-Up", "Random", "Played" }, 1);
    arpRateBox.addItemList({ "1/4", "1/4 Dotted", "1/4 Triplet", "1/8", "1/8 Dotted",
                             "1/8 Triplet", "1/16", "1/16 Dotted", "1/16 Triplet", "1/32",
                             "1/32 Dotted", "1/32 Triplet", "1/64", "1/64 Dotted",
                             "1/64 Triplet" }, 1);
    arpOctBox.addItemList({ "1", "2", "3", "4" }, 1);
    arpModeA = std::make_unique<AttachC>(proc.apvts, "arpMode", arpModeBox);
    arpRateA = std::make_unique<AttachC>(proc.apvts, "arpRate", arpRateBox);
    arpOctA = std::make_unique<AttachC>(proc.apvts, "arpOct", arpOctBox);
    addAndMakeVisible(arpModeBox); addAndMakeVisible(arpRateBox); addAndMakeVisible(arpOctBox);
    swingA = std::make_unique<AttachS>(proc.apvts, "swing", swingKnob);
    gateA = std::make_unique<AttachS>(proc.apvts, "gate", gateKnob);
    swingKnob.onDoubleClickEntry = [this](Knob* k) { showValueEntry(k); };
    gateKnob.onDoubleClickEntry = [this](Knob* k) { showValueEntry(k); };
    addAndMakeVisible(swingKnob); addAndMakeVisible(gateKnob);
    mkLabel(swingLabel, "Swing", true); addAndMakeVisible(swingLabel);
    mkLabel(gateLabel, "Gate", true);   addAndMakeVisible(gateLabel);

    statusLabel.setJustificationType(juce::Justification::centredRight);
    statusLabel.setBorderSize(juce::BorderSize<int>(0));

    // titles sit flush with the content column's left edge
    for (auto* l : { &titleLabel, &scaleTitle, &chordTitle, &arpTitle, &customChordLabel })
    {
        l->setJustificationType(juce::Justification::centredLeft);
        l->setBorderSize(juce::BorderSize<int>(0));
    }
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(statusLabel);

    refreshScaleBox();
    refreshChordSlots();
    refreshPcButtons();
    refreshCustomDesigner();
    refreshChordTypeNames();
    refreshEnabledStates();
    presetBrowser.onClose = [this] { presetBrowser.setVisible(false); };
    addChildComponent(presetBrowser);
    refreshPresetButton();
    lastStateVersion = proc.stateVersion.load();
    startTimerHz(15);
}

MidiNoteModifierEditor::~MidiNoteModifierEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

juce::Label* MidiNoteModifierEditor::mkLabel(juce::Label& l, const juce::String& text, bool dim)
{
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId,
                dim ? juce::Colour(0xFF8F8B85) : juce::Colour(0xFFD9D5D0));
    return &l;
}


int MidiNoteModifierEditor::currentScaleLength()
{
    return (int) proc.currentIntervals().size();
}

void MidiNoteModifierEditor::showValueEntry(Knob* knob)
{
    if (knob == nullptr) return;
    auto* content = new KnobValueEntry(knob);
    juce::CallOutBox::launchAsynchronously(std::unique_ptr<juce::Component>(content),
                                          knob->getScreenBounds(), nullptr);
}

void MidiNoteModifierEditor::refreshScaleBox()
{
    auto names = proc.getAllScaleNames();
    juce::String prevSel = scaleBox.getText();
    scaleBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& n : names) scaleBox.addItem(n, id++);
    // keep a selected user scale if it is still listed
    for (int i = 0; i < scaleBox.getNumItems(); ++i)
        if (prevSel.isNotEmpty() && scaleBox.getItemText(i) == prevSel)
            scaleBox.setSelectedItemIndex(i, juce::dontSendNotification);
    lastScaleParam = -1;
    syncScaleBoxSelection();
}

// Mirror the "scale" parameter in the box (host automation, state recall,
// pill/piano edits). A selected user scale stays shown while the Custom
// pattern still matches it.
void MidiNoteModifierEditor::syncScaleBoxSelection()
{
    int scaleIdx = (int) proc.apvts.getRawParameterValue("scale")->load();
    auto& facts = Scales::factoryScales();
    juce::String want = "Custom";
    if (scaleIdx >= 0 && scaleIdx < (int) facts.size()) want = facts[(size_t) scaleIdx].name;

    if (scaleIdx == Scales::customIndex())
    {
        juce::String pattern;
        for (int i = 0; i < 12; ++i)
            pattern += proc.apvts.getRawParameterValue("pc" + juce::String(i))->load() > 0.5f ? "1" : "0";
        auto user = proc.apvts.state.getChildWithName("USER_SCALES");
        juce::String sel = scaleBox.getText();
        for (int i = 0; i < user.getNumChildren(); ++i)
            if (user.getChild(i).getProperty("name").toString() == sel
                && user.getChild(i).getProperty("pattern").toString() == pattern)
                want = sel;
    }

    if (scaleBox.getText() != want)
        for (int i = 0; i < scaleBox.getNumItems(); ++i)
            if (scaleBox.getItemText(i) == want)
            {
                scaleBox.setSelectedItemIndex(i, juce::dontSendNotification);
                break;
            }

    if (scaleIdx != lastScaleParam)
    {
        lastScaleParam = scaleIdx;
        refreshChordSlots();
    }
}

void MidiNoteModifierEditor::refreshPcButtons()
{
    // pills mirror the current scale (Custom pattern when Custom is selected)
    int scaleIdx = (int) proc.apvts.getRawParameterValue("scale")->load();
    std::array<bool, 12> show;
    if (scaleIdx == Scales::customIndex())
        for (int i = 0; i < 12; ++i)
            show[(size_t) i] = proc.apvts.getRawParameterValue("pc" + juce::String(i))->load() > 0.5f;
    else
        show = proc.currentPcSet();
    bool changed = false;
    for (int i = 0; i < 12; ++i)
    {
        bool want = show[(size_t) i];
        if (pcButtons[i].getToggleState() != want)
        {
            pcButtons[i].setToggleState(want, juce::dontSendNotification);
            changed = true;
        }
    }
    if (changed) repaint();
}

void MidiNoteModifierEditor::refreshChordSlots()
{
    int len = currentScaleLength();
    for (int i = 0; i < 12; ++i)
    {
        bool vis = i < len;
        chordBoxes[i].setVisible(vis);
        chordDegLabels[i].setVisible(vis);
        if (chordPreviews[i] != nullptr) chordPreviews[i]->setVisible(vis);
    }
    resized();
}

void MidiNoteModifierEditor::refreshChordTypeNames()
{
    for (int i = 0; i < 12; ++i)
        for (int s = 0; s < MidiNoteModifierProcessor::kNumCustomChords; ++s)
        {
            juce::String want = proc.customChordDisplayName(s);
            if (chordBoxes[i].getItemText(14 + s) != want)
                chordBoxes[i].changeItemText(15 + s, want);
        }
}

void MidiNoteModifierEditor::refreshCustomDesigner()
{
    customSlotBox.setSelectedItemIndex(chordEditSlot, juce::dontSendNotification);
    customChordName.setText(proc.getCustomChordName(chordEditSlot), false);
    auto ivs = proc.getCustomChordIntervals(chordEditSlot);
    for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
    {
        int v = k < (int) ivs.size() ? ivs[k] : -1;
        customIvBoxes[k].setSelectedId(v + 2, juce::dontSendNotification);
    }
}

// Host recalled a whole state: user scales, custom chord names and the
// designer contents may all have changed under us.
void MidiNoteModifierEditor::refreshPresetButton()
{
    auto n = proc.getSetupName();
    presetButton.setButtonText(n.isEmpty() ? juce::String("Setups...") : "Setup: " + n);
}

void MidiNoteModifierEditor::refreshAfterStateChange()
{
    refreshPresetButton();
    refreshScaleBox();
    refreshChordSlots();
    refreshPcButtons();
    refreshChordTypeNames();
    refreshCustomDesigner();
    refreshEnabledStates();
}

void MidiNoteModifierEditor::refreshEnabledStates()
{
    deleteScaleButton.setEnabled(proc.isUserScale(scaleBox.getText()));

    bool ch = chordOn.getToggleState();
    for (int i = 0; i < 12; ++i)
    {
        chordBoxes[i].setEnabled(ch);
        chordDegLabels[i].setEnabled(ch);
    }
    inversionBox.setEnabled(ch); inversionLabel.setEnabled(ch);
    invLowButton.setEnabled(ch);
    bassButton.setEnabled(ch);
    strumKnob.setEnabled(ch); strumLabel.setEnabled(ch);
    diatonicButton.setEnabled(ch);
    customChordLabel.setEnabled(ch);
    customSlotBox.setEnabled(ch);
    customChordName.setEnabled(ch);
    for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
        customIvBoxes[k].setEnabled(ch);
    customChordSave.setEnabled(ch);
    customChordDelete.setEnabled(ch);

    bool ar = arpOn.getToggleState();
    arpLatch.setEnabled(ar);
    arpModeBox.setEnabled(ar); arpModeLabel.setEnabled(ar);
    arpRateBox.setEnabled(ar); arpRateLabel.setEnabled(ar);
    arpOctBox.setEnabled(ar);  arpOctLabel.setEnabled(ar);
    swingKnob.setEnabled(ar); swingLabel.setEnabled(ar);
    gateKnob.setEnabled(ar);  gateLabel.setEnabled(ar);
}

bool MidiNoteModifierEditor::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo(); return true; }
    if (key == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0)) { proc.redo(); return true; }
    return false;
}

void MidiNoteModifierEditor::timerCallback()
{
    undoButton.setEnabled(proc.undoHistory.canUndo());
    refreshPresetButton();
    redoButton.setEnabled(proc.undoHistory.canRedo());
    int sv = proc.stateVersion.load();
    if (sv != lastStateVersion)
    {
        lastStateVersion = sv;
        refreshAfterStateChange();
    }

    int themeIdx = (int) proc.apvts.getRawParameterValue("theme")->load();
    if (themeIdx != lnf.theme.get())
    {
        lnf.setTheme(themeIdx);
        repaint();
    }

    bool playing = proc.hostPlaying.load();
    double bpm = proc.lastHostBpm.load();
    transportLabel.setText(playing ? (">  " + juce::String(bpm, 1) + " BPM")
                                   : ("-  Stopped | " + juce::String(bpm, 1) + " BPM"),
                           juce::dontSendNotification);
    transportLabel.setColour(juce::Label::textColourId,
                             playing ? lnf.theme.light() : juce::Colour(0xFF8F8B85));

    juce::String st = "Voices " + juce::String(proc.activeVoices.load());
    int li = proc.lastInputNote.load(), ls = proc.lastSnappedNote.load();
    if (li >= 0)
        st += "    " + juce::MidiMessage::getMidiNoteName(li, true, true, 3)
            + " -> " + juce::MidiMessage::getMidiNoteName(ls, true, true, 3);
    statusLabel.setText(st, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF8F8B85));

    refreshEnabledStates();
    refreshPcButtons();
    syncScaleBoxSelection();
    piano.repaint();
    for (auto& cp : chordPreviews) if (cp != nullptr && cp->isVisible()) cp->repaint();
    if (arpPreview != nullptr) arpPreview->repaint();
}

// v3 panel: charcoal, thin edge, the theme accent as a dot before the title
static void paintPanel(juce::Graphics& g, juce::Rectangle<int> r, juce::Colour dot)
{
    g.setColour(ModifierLookAndFeel::panel());
    g.fillRoundedRectangle(r.toFloat(), 10.0f);
    g.setColour(ModifierLookAndFeel::panelEdge());
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
    g.setColour(dot);
    g.fillEllipse((float) r.getX() + 14.0f, (float) r.getY() + 17.0f, 8.0f, 8.0f);
}

void MidiNoteModifierEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF141518));
    // the Pastel family mark: four rounded squares
    const juce::uint32 cols[4] = { 0xffe88a8a, 0xff8fd0c2, 0xffe9a3b8, 0xffa9d58e };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour(juce::Colour(cols[i]));
        g.fillRoundedRectangle(16.0f + (float) (i % 2) * 10.0f, 12.0f + (float) (i / 2) * 10.0f, 8.0f, 8.0f, 2.0f);
    }
    for (auto r : { scalePanelRect, chordPanelRect, arpPanelRect })
        if (! r.isEmpty()) paintPanel(g, r, lnf.accent());
}

// Panel heights derived from content (kKnobBox included); the keyboard takes the rest.
static constexpr int kKnobBox = 44;
static constexpr int kChordH = 10 + 26 + 6 + 26 + 32 + 8 + (14 + kKnobBox) + 8 + 26 + 10; // 226: + chord previews
static constexpr int kArpH = 10 + 26 + 8 + (14 + kKnobBox) + 10;  // 128
static constexpr int kScaleFixed = 10 + 26 + 8 + 26 + 8 + 26 + 8 + 22 + 6;                // 140
static constexpr int kChromeTop = 62, kGap = 8, kStatusH = 30;

void MidiNoteModifierEditor::resized()
{
    const int W = getWidth();
    const int M = 28; // content left
    const int R = W - 28; // content right
    const int CW = R - M; // content width

    // header
    titleLabel.setBounds(M + 14, 6, 280, 26);
    presetButton.setBounds(M + 12, 34, 200, 22);
    presetBrowser.setBounds(getLocalBounds());
    const int toggleX = M + 120; // section on/off toggles share one column
    panicButton.setBounds(W - 112, 14, 84, 32);
    transportLabel.setBounds(W - 334, 14, 210, 32);
    themeBox.setBounds(W - 492, 14, 150, 32);
    redoButton.setBounds(W - 492 - 8 - 56, 17, 56, 26);
    undoButton.setBounds(W - 492 - 8 - 56 - 4 - 56, 17, 56, 26);

    // ---- scale panel (keyboard takes remaining height) ----
    int keyboardH = getHeight() - (kChromeTop + kScaleFixed + 10 + kGap + kChordH + kGap + kArpH + kGap + kStatusH);
    keyboardH = juce::jmax(90, keyboardH);
    int scaleH = kScaleFixed + 10 + keyboardH;
    int scaleBottom = kChromeTop + scaleH;
    scalePanelRect = { 12, kChromeTop, W - 24, scaleH };
    chordPanelRect = { 12, scaleBottom + kGap, W - 24, kChordH };
    arpPanelRect = { 12, scaleBottom + kGap + kChordH + kGap, W - 24, kArpH };
    int y = kChromeTop + 10;
    scaleTitle.setBounds(M + 10, y, 100, 22);
    lockButton.setBounds(toggleX, y - 2, 95, 26);
    int comboY = y - 2;
    int snapW = 100, rootW = 76;
    int rootLblX = toggleX + 95 + 8;
    int scaleLblX = rootLblX + 44 + rootW + 8;
    int snapX = R - snapW; // snapBox starts here
    rootLabel.setBounds(rootLblX, y, 44, 22);  rootBox.setBounds(rootLblX + 44, comboY, rootW, 26);
    scaleLabel.setBounds(scaleLblX, y, 44, 22);
    scaleBox.setBounds(scaleLblX + 44, comboY, snapX - 52 - (scaleLblX + 44), 26);
    snapLabel.setBounds(snapX - 44, y, 44, 22);  snapBox.setBounds(snapX, comboY, snapW, 26);

    y += 36;
    int editW = 92;
    int pillW = (CW - editW - 8 - 11 * 6) / 12;
    for (int i = 0; i < 12; ++i)
        pcButtons[i].setBounds(M + i * (pillW + 6), y, pillW, 26);
    editToggle.setBounds(R - editW, y - 2, editW, 28);

    y += 34;
    int saveW = 80, delSW = 80, wkW = 150;
    whiteKeysButton.setBounds(M, y - 2, wkW, 26);
    presetName.setBounds(M + wkW + 8, y, CW - wkW - 8 - saveW - 6 - delSW - 8, 26);
    saveButton.setBounds(R - delSW - 6 - saveW, y, saveW, 26);
    deleteScaleButton.setBounds(R - delSW, y, delSW, 26);

    y += 30;
    pianoHint.setBounds(M, y + 3, CW, 16);
    y += 24;
    piano.setBounds(M, y, CW, scaleBottom - 12 - y);

    // ---- chord panel ----
    y = chordPanelRect.getY() + 10;
    chordTitle.setBounds(M + 10, y, 100, 22);
    chordOn.setBounds(toggleX, y - 2, 90, 26);
    diatonicButton.setBounds(R - 140, y - 2, 140, 26);

    y += 32;
    int len = currentScaleLength();
    int slotW = len > 0 ? juce::jmin(110, (CW - (len - 1) * 6) / len) : 60;
    for (int i = 0; i < 12; ++i)
    {
        chordBoxes[i].setBounds(M + i * (slotW + 6), y, slotW, 26);
        if (chordPreviews[i] != nullptr) // setSize() runs resized() before the previews exist
            chordPreviews[i]->setBounds(M + i * (slotW + 6) + 6, y + 30, slotW - 12, 16);
        chordDegLabels[i].setBounds(M + i * (slotW + 6), y + 46, slotW, 14);
    }

    y += 26 + 32 + 8;
    int knobBox = kKnobBox;
    inversionLabel.setBounds(M, y, 90, 14);
    inversionBox.setBounds(M, y + 14, 90, 26);
    invLowButton.setBounds(M + 90 + 18, y + 14, 120, 28);
    bassButton.setBounds(M + 90 + 18 + 120 + 18, y + 14, 150, 28);
    strumLabel.setBounds(R - knobBox, y, knobBox, 14);
    strumKnob.setBounds(R - knobBox, y + 14, knobBox, knobBox);

    y += 14 + knobBox + 8;
    customChordLabel.setBounds(M, y + 2, 60, 26);
    customSlotBox.setBounds(M + 64, y + 2, 86, 26);
    // the row spans the full content width: interval boxes and the name field share the slack
    int delW = CW < 880 ? 64 : 70, saveW2 = delW;
    const int nIv = MidiNoteModifierProcessor::kMaxChordNotes;
    int flex = CW - 64 - 86 - 8 - 8 - nIv * 6 - saveW2 - 6 - delW;
    int ivW = juce::jlimit(62, 90, (flex - 150) / nIv);
    int nameW = juce::jmax(90, flex - nIv * ivW);
    int cx = M + 64 + 86 + 8;
    customChordName.setBounds(cx, y + 2, nameW, 26);
    cx += nameW + 8;
    for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
    {
        customIvBoxes[k].setBounds(cx, y + 2, ivW, 26);
        cx += ivW + 6;
    }
    customChordDelete.setBounds(R - delW, y + 2, delW, 26);
    customChordSave.setBounds(R - delW - 6 - saveW2, y + 2, saveW2, 26);

    // ---- arp panel ----
    y = arpPanelRect.getY() + 10;
    arpTitle.setBounds(M + 10, y, 105, 22);
    arpOn.setBounds(toggleX, y - 2, 90, 26);
    arpLatch.setBounds(toggleX + 90 + 18, y - 2, 110, 26);

    y += 34;
    int grp = 120, grpGap = 18;
    arpModeLabel.setBounds(M, y, grp, 14);  arpModeBox.setBounds(M, y + 14, grp, 26);
    arpRateLabel.setBounds(M + grp + grpGap, y, grp, 14);
    arpRateBox.setBounds(M + grp + grpGap, y + 14, grp, 26);
    arpOctLabel.setBounds(M + 2 * (grp + grpGap), y, 90, 14);
    arpOctBox.setBounds(M + 2 * (grp + grpGap), y + 14, 90, 26);
    int kx = M + 2 * (grp + grpGap) + 90 + grpGap;
    swingLabel.setBounds(kx, y, knobBox, 14); swingKnob.setBounds(kx, y + 14, knobBox, knobBox);
    gateLabel.setBounds(kx + knobBox + grpGap, y, knobBox, 14);
    gateKnob.setBounds(kx + knobBox + grpGap, y + 14, knobBox, knobBox);
    const int ax = kx + 2 * (knobBox + grpGap);
    if (arpPreview != nullptr)
        arpPreview->setBounds(ax, arpPanelRect.getY() + 10, R - ax, arpPanelRect.getHeight() - 20);

    statusLabel.setBounds(R - 334, getHeight() - 28, 334, 20);
}

// ---------- v3 previews ----------
void ChordPreview::paint(juce::Graphics& g)
{
    int notes[16] {}; // previewChord emits at most kMaxEmit (8)
    const int n = proc.previewChord(deg, notes);
    bool lit[12] {};
    int rootPc = -1;
    for (int k = 0; k < n; ++k) lit[((notes[k] % 12) + 12) % 12] = true;
    if (n > 0) rootPc = (((notes[0] % 12) + 12) % 12);
    auto r = getLocalBounds().toFloat();
    const float ww = r.getWidth() / 7.0f;
    const int whitePc[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
    {
        g.setColour(lit[whitePc[i]] ? theme.light() : juce::Colour(0xFF3A3B40));
        g.fillRoundedRectangle(r.getX() + ww * (float) i + 0.5f, r.getY(), ww - 1.0f, r.getHeight(), 2.0f);
    }
    const int blackAfter[5] = { 0, 1, 3, 4, 5 }; // black key sits after these white keys
    const int blackPc[5] = { 1, 3, 6, 8, 10 };
    for (int k = 0; k < 5; ++k)
    {
        const float bx = r.getX() + ww * (float) (blackAfter[k] + 1) - ww * 0.3f;
        g.setColour(lit[blackPc[k]] ? theme.mid().brighter(0.4f) : juce::Colour(0xFF1A1B1F));
        g.fillRoundedRectangle(bx, r.getY(), ww * 0.6f, r.getHeight() * 0.6f, 1.5f);
    }
    juce::ignoreUnused(rootPc);
}

void ArpPreview::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xFF16181C));
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colour(0xFF24272C));
    for (int i = 1; i < 16; ++i)
        g.drawVerticalLine((int) (r.getX() + r.getWidth() * (float) i / 16.0f), r.getY() + 1, r.getBottom() - 1);
    // the pattern for degree 1's chord
    int base[16] {}; // previewChord emits at most kMaxEmit (8)
    int n = juce::jmax(0, proc.previewChord(0, base));
    if (n == 0) return;
    std::sort(base, base + n);
    auto raw = [this](const char* id) { auto* v = proc.apvts.getRawParameterValue(id); return v != nullptr ? v->load() : 0.0f; };
    const int mode = (int) raw("arpMode"), octs = 1 + (int) raw("arpOct");
    const float gate = juce::jlimit(0.1f, 1.0f, raw("gate") / 100.0f);    // 10..100 %
    const float swing = juce::jlimit(0.0f, 0.6f, raw("swing") / 100.0f);   // 0..60 %
    std::vector<int> up;
    for (int o = 0; o < octs; ++o)
        for (int k = 0; k < n; ++k) up.push_back(base[k] + 12 * o);
    std::vector<int> seq;
    if (mode == 1) seq.assign(up.rbegin(), up.rend());
    else if (mode == 2 || mode == 3)
    {
        std::vector<int> a = up, b(up.rbegin(), up.rend());
        if (mode == 3) std::swap(a, b);
        seq = a;
        for (size_t i = 1; i + 1 < b.size(); ++i) seq.push_back(b[i]);
    }
    else if (mode == 4)
    {
        juce::Random rng(7);   // a fixed demo shuffle
        for (int i = 0; i < 16; ++i) seq.push_back(up[(size_t) rng.nextInt((int) up.size())]);
    }
    else seq = up;   // Up / Played
    if (seq.empty()) return;
    const int lo = up.front(), hi = up.back();
    const float cw = r.getWidth() / 16.0f;
    const float rows = (float) juce::jmax(1, hi - lo);
    for (int s = 0; s < 16; ++s)
    {
        const int note = seq[(size_t) s % seq.size()];
        const float y = r.getBottom() - 10.0f - (r.getHeight() - 22.0f) * (float) (note - lo) / rows;
        const float x = r.getX() + cw * (float) s + (s % 2 == 1 ? swing * cw * 0.5f : 0.0f);
        g.setColour(theme.light().withAlpha(s < (int) seq.size() ? 0.95f : 0.55f));
        g.fillRoundedRectangle(x + 2.0f, y - 4.0f, juce::jmax(3.0f, (cw - 4.0f) * gate), 8.0f, 3.0f);
    }
}

juce::AudioProcessorEditor* MidiNoteModifierProcessor::createEditor()
{
    return new MidiNoteModifierEditor(*this);
}
