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

// ============ PianoView (single octave, opaque keys) ============

PianoView::PianoView(MidiNoteModifierProcessor& proc_, std::function<void()> onChanged_)
    : p(proc_), onChanged(std::move(onChanged_)) {}

bool PianoView::isBlack(int n) const
{
    int pc = ((n % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

int PianoView::noteAtPoint(juce::Point<int> pt) const
{
    auto bounds = getLocalBounds().toFloat();
    int nWhite = 0;
    for (int i = 0; i < kKeys; ++i) if (! isBlack(kStart + i)) ++nWhite;
    float wWhite = bounds.getWidth() / (float) nWhite;
    float h = bounds.getHeight();
    float blackH = h * 0.62f;
    float blackW = wWhite * 0.6f;

    if (pt.y < blackH)
    {
        int wi = 0;
        for (int i = 0; i < kKeys; ++i)
        {
            int n = kStart + i;
            if (! isBlack(n)) { ++wi; continue; }
            float x = wi * wWhite - blackW * 0.5f;
            if (pt.x >= x && pt.x < x + blackW) return n;
        }
    }
    int wi = 0;
    for (int i = 0; i < kKeys; ++i)
    {
        int n = kStart + i;
        if (isBlack(n)) continue;
        float x = wi * wWhite;
        if (pt.x >= x && pt.x < x + wWhite) return n;
        ++wi;
    }
    return -1;
}

juce::Rectangle<float> PianoView::rectForKey(int note, bool black, float wWhite, float h) const
{
    int wi = 0;
    for (int n = kStart; n < note; ++n) if (! isBlack(n)) ++wi;
    if (! black)
        return { wi * wWhite + 1.5f, 0.0f, wWhite - 3.0f, h };
    float blackW = wWhite * 0.6f;
    return { wi * wWhite - blackW * 0.5f, 0.0f, blackW, h * 0.62f };
}

void PianoView::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    int nWhite = 0;
    for (int i = 0; i < kKeys; ++i) if (! isBlack(kStart + i)) ++nWhite;
    float wWhite = bounds.getWidth() / (float) nWhite;
    float h = bounds.getHeight();

    auto set = p.currentPcSet();
    int lastIn = p.lastInputNote.load();
    int lastSnapped = p.lastSnappedNote.load();
    auto names = Scales::noteNames();

    for (int i = 0; i < kKeys; ++i)
    {
        int n = kStart + i;
        if (isBlack(n)) continue;
        auto r = rectForKey(n, false, wWhite, h);
        int pc = ((n % 12) + 12) % 12;
        bool inScale = set[(size_t) pc];
        bool active = (n == lastSnapped || n == lastIn || n == heldUiNote);
        juce::Colour fill = inScale ? Theme::light() : juce::Colour(0xFF3A3A42);
        if (active) fill = juce::Colours::white;
        if (active)
        {
            g.setColour(Theme::mid().withAlpha(0.3f));
            g.fillRoundedRectangle(r.expanded(3.0f), 8.0f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(juce::Colour(0xFF1E1E23));
        g.drawRoundedRectangle(r, 6.0f, 1.5f);
        g.setColour(inScale ? juce::Colour(0xFF55555F) : juce::Colour(0xFF8E8E9A));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(names[(size_t) pc],
                   juce::Rectangle<float>(r.getX(), r.getBottom() - 26, r.getWidth(), 20),
                   juce::Justification::centred, false);
        if (inScale && ! active)
        {
            g.setColour(Theme::dark().withAlpha(0.7f));
            g.fillEllipse(r.getCentreX() - 3.0f, r.getBottom() - 44.0f, 6.0f, 6.0f);
        }
    }
    for (int i = 0; i < kKeys; ++i)
    {
        int n = kStart + i;
        if (! isBlack(n)) continue;
        auto r = rectForKey(n, true, wWhite, h);
        int pc = ((n % 12) + 12) % 12;
        bool inScale = set[(size_t) pc];
        bool active = (n == lastSnapped || n == lastIn || n == heldUiNote);
        juce::Colour fill = inScale ? Theme::mid() : juce::Colour(0xFF1E1E23);
        if (active) fill = Theme::light();
        if (active)
        {
            g.setColour(Theme::mid().withAlpha(0.35f));
            g.fillRoundedRectangle(r.expanded(3.0f), 8.0f);
        }
        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(inScale ? Theme::light() : juce::Colour(0xFF45454E));
        g.drawRoundedRectangle(r, 6.0f, 1.2f);
    }
}

void PianoView::mouseDown(const juce::MouseEvent& e)
{
    int n = noteAtPoint(e.getPosition());
    if (n < 0) return;
    bool toggle = editMode || e.mods.isRightButtonDown() || e.mods.isShiftDown();
    if (toggle)
    {
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
    else
    {
        heldUiNote = n;
        p.queueAuditionNote(true, n, 100);
        repaint();
    }
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
    : juce::AudioProcessorEditor(proc_), proc(proc_), piano(proc_, [this]
      {
          refreshPcButtons();
          refreshScaleBox();
          refreshChordSlots();
      })
{
    setLookAndFeel(&lnf);
    setSize(960, 800);
    setResizable(true, true);
    setResizeLimits(860, 720, 1500, 1150);

    mkLabel(titleLabel, "Midi Note Modifier", false);
    titleLabel.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    addAndMakeVisible(titleLabel);
    mkLabel(subtitleLabel, "Scale | Chords | Arp", true);
    addAndMakeVisible(subtitleLabel);

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

    panicButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE5484D));
    panicButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicButton.onClick = [this] { proc.panic(); };
    addAndMakeVisible(panicButton);

    // ---- scale ----
    mkLabel(scaleTitle, "Scale", false);
    scaleTitle.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
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

    scaleA = std::make_unique<AttachC>(proc.apvts, "scale", scaleBox);
    scaleBox.onChange = [this]
    {
        juce::String sel = scaleBox.getText();
        if (sel.isEmpty()) return;
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
                par->setValueNotifyingHost(par->convertTo0to1(idx));
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

    presetName.setTextToShowWhenEmpty("Custom Scale Name...", juce::Colour(0xFF8E8E9A));
    addAndMakeVisible(presetName);
    saveButton.onClick = [this]
    {
        juce::String n = presetName.getText().trim();
        if (n.isEmpty()) return;
        proc.saveCustomScale(n);
        refreshScaleBox();
        presetName.clear();
    };
    addAndMakeVisible(saveButton);
    mkLabel(pianoHint, "Click Keys To Audition  |  Shift-Click A Key (Or Edit Mode) To Toggle It In The Scale", true);
    addAndMakeVisible(pianoHint);
    addAndMakeVisible(piano);

    // ---- chords ----
    mkLabel(chordTitle, "Chords", false);
    chordTitle.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    addAndMakeVisible(chordTitle);
    chordOnA = std::make_unique<AttachT>(proc.apvts, "chordOn", chordOn);
    chordOn.onClick = [this] { refreshEnabledStates(); };
    addAndMakeVisible(chordOn);
    for (int i = 0; i < 12; ++i)
    {
        chordDegLabels[i].setText("D" + juce::String(i + 1), juce::dontSendNotification);
        chordDegLabels[i].setJustificationType(juce::Justification::centred);
        chordDegLabels[i].setColour(juce::Label::textColourId, juce::Colour(0xFFA7A3B8));
        chordDegLabels[i].setFont(juce::Font(juce::FontOptions(11.0f)));
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
    diatonicButton.onClick = [this] { proc.resetDiatonicChords(); refreshChordSlots(); };
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
    customChordName.setTextToShowWhenEmpty("Name, e.g. Major 6th...", juce::Colour(0xFF8E8E9A));
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
        proc.saveChordType(chordEditSlot, customChordName.getText(), ivs);
        refreshCustomDesigner();
        refreshChordTypeNames();
    };
    addAndMakeVisible(customChordSave);
    customChordDelete.onClick = [this]
    {
        proc.resetChordType(chordEditSlot);
        refreshCustomDesigner();
        refreshChordTypeNames();
    };
    addAndMakeVisible(customChordDelete);

    // ---- arp ----
    mkLabel(arpTitle, "Arpeggiator", false);
    arpTitle.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    addAndMakeVisible(arpTitle);
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

    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(statusLabel);

    refreshScaleBox();
    refreshChordSlots();
    refreshPcButtons();
    refreshCustomDesigner();
    refreshChordTypeNames();
    refreshEnabledStates();
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
                dim ? juce::Colour(0xFFA7A3B8) : juce::Colour(0xFFF2F0F5));
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
    scaleBox.clear(juce::dontSendNotification);
    int id = 1;
    for (auto& n : names) scaleBox.addItem(n, id++);
    int scaleIdx = (int) proc.apvts.getRawParameterValue("scale")->load();
    juce::String cur;
    auto& facts = Scales::factoryScales();
    if (scaleIdx >= 0 && scaleIdx < (int) facts.size()) cur = facts[(size_t) scaleIdx].name;
    else cur = "Custom";
    if (cur == "Custom")
        scaleBox.setSelectedItemIndex(scaleBox.getNumItems() - 1, juce::dontSendNotification);
    else
        for (int i = 0; i < scaleBox.getNumItems(); ++i)
            if (scaleBox.getItemText(i) == cur)
            {
                scaleBox.setSelectedItemIndex(i, juce::dontSendNotification);
                break;
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

void MidiNoteModifierEditor::refreshEnabledStates()
{
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

void MidiNoteModifierEditor::timerCallback()
{
    int themeIdx = (int) proc.apvts.getRawParameterValue("theme")->load();
    if (themeIdx != Theme::get())
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
                             playing ? Theme::light() : juce::Colour(0xFF8E8E9A));

    juce::String st = "Voices " + juce::String(proc.activeVoices.load());
    int li = proc.lastInputNote.load(), ls = proc.lastSnappedNote.load();
    if (li >= 0)
        st += "    " + juce::MidiMessage::getMidiNoteName(li, true, true, 3)
            + " -> " + juce::MidiMessage::getMidiNoteName(ls, true, true, 3);
    statusLabel.setText(st, juce::dontSendNotification);
    statusLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFA7A3B8));

    refreshEnabledStates();
    refreshPcButtons();
    piano.repaint();
}

static void paintPanel(juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour(juce::Colour(0xFF222228));
    g.fillRoundedRectangle(r.toFloat(), 12.0f);
    g.setColour(juce::Colour(0xFF45454E));
    g.drawRoundedRectangle(r.toFloat(), 12.0f, 1.0f);
}

void MidiNoteModifierEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF161619));
    if (! scalePanelRect.isEmpty()) paintPanel(g, scalePanelRect);
    if (! chordPanelRect.isEmpty()) paintPanel(g, chordPanelRect);
    if (! arpPanelRect.isEmpty()) paintPanel(g, arpPanelRect);
}

// Panel heights derived from content (kKnobBox included); the keyboard takes the rest.
static constexpr int kKnobBox = 44;
static constexpr int kChordH = 10 + 26 + 6 + 26 + 14 + 8 + (14 + kKnobBox) + 8 + 26 + 10; // 208
static constexpr int kArpH = 10 + 26 + 8 + (14 + kKnobBox) + 10;  // 128
static constexpr int kScaleFixed = 10 + 26 + 8 + 26 + 8 + 26 + 8 + 16 + 6;                // 134
static constexpr int kChromeTop = 62, kGap = 8, kStatusH = 30;

void MidiNoteModifierEditor::resized()
{
    const int W = getWidth();
    const int M = 28; // content left
    const int R = W - 28; // content right
    const int CW = R - M; // content width

    // header
    titleLabel.setBounds(M, 10, 280, 26);
    subtitleLabel.setBounds(M, 34, 280, 16);
    panicButton.setBounds(W - 112, 14, 84, 32);
    transportLabel.setBounds(W - 334, 14, 210, 32);
    themeBox.setBounds(W - 492, 14, 150, 32);

    // ---- scale panel (keyboard takes remaining height) ----
    int keyboardH = getHeight() - (kChromeTop + kScaleFixed + 10 + kGap + kChordH + kGap + kArpH + kGap + kStatusH);
    keyboardH = juce::jmax(90, keyboardH);
    int scaleH = kScaleFixed + 10 + keyboardH;
    int scaleBottom = kChromeTop + scaleH;
    scalePanelRect = { 12, kChromeTop, W - 24, scaleH };
    chordPanelRect = { 12, scaleBottom + kGap, W - 24, kChordH };
    arpPanelRect = { 12, scaleBottom + kGap + kChordH + kGap, W - 24, kArpH };
    int y = kChromeTop + 10;
    scaleTitle.setBounds(M, y, 80, 20);
    lockButton.setBounds(M + 90, y - 2, 125, 26);
    int comboY = y - 2;
    int snapW = 100, rootW = 76;
    int rootLblX = M + 90 + 125 + 8;
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
    int saveW = 80, wkW = 150;
    whiteKeysButton.setBounds(M, y - 2, wkW, 26);
    presetName.setBounds(M + wkW + 8, y, CW - wkW - 8 - saveW - 8, 26);
    saveButton.setBounds(R - saveW, y, saveW, 26);

    y += 30;
    pianoHint.setBounds(M, y, CW, 16);
    y += 18;
    piano.setBounds(M, y, CW, scaleBottom - 12 - y);

    // ---- chord panel ----
    y = chordPanelRect.getY() + 10;
    chordTitle.setBounds(M, y, 90, 20);
    chordOn.setBounds(M + 100, y - 2, 90, 26);
    diatonicButton.setBounds(R - 140, y - 2, 140, 26);

    y += 32;
    int len = currentScaleLength();
    int slotW = len > 0 ? juce::jmin(110, (CW - (len - 1) * 6) / len) : 60;
    for (int i = 0; i < 12; ++i)
    {
        chordBoxes[i].setBounds(M + i * (slotW + 6), y, slotW, 26);
        chordDegLabels[i].setBounds(M + i * (slotW + 6), y + 26, slotW, 14);
    }

    y += 26 + 14 + 8;
    int knobBox = kKnobBox;
    inversionLabel.setBounds(M, y, 90, 14);
    inversionBox.setBounds(M, y + 14, 90, 26);
    invLowButton.setBounds(M + 90 + 18, y + 14, 120, 28);
    bassButton.setBounds(M + 90 + 18 + 120 + 18, y + 14, 150, 28);
    strumLabel.setBounds(R - knobBox, y, knobBox, 14);
    strumKnob.setBounds(R - knobBox, y + 14, knobBox, knobBox);

    y += 14 + knobBox + 8;
    customChordLabel.setBounds(M, y + 4, 60, 22);
    customSlotBox.setBounds(M + 64, y + 2, 86, 26);
    int delW = 70, saveW2 = 70;
    int ivW = juce::jmax(44, juce::jmin(58, (CW - 64 - 86 - 150 - delW - saveW2 - 6 * 8 - 16) / 6));
    int cx = M + 64 + 86 + 8;
    customChordName.setBounds(cx, y + 2, 150, 26);
    cx += 150 + 8;
    for (int k = 0; k < MidiNoteModifierProcessor::kMaxChordNotes; ++k)
    {
        customIvBoxes[k].setBounds(cx, y + 2, ivW, 26);
        cx += ivW + 6;
    }
    customChordSave.setBounds(cx, y + 2, saveW2, 26);
    customChordDelete.setBounds(cx + saveW2 + 6, y + 2, delW, 26);

    // ---- arp panel ----
    y = arpPanelRect.getY() + 10;
    arpTitle.setBounds(M, y, 150, 20);
    arpOn.setBounds(M + 160, y - 2, 90, 26);
    arpLatch.setBounds(M + 250, y - 2, 110, 26);

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

    statusLabel.setBounds(W - 358, getHeight() - 28, 334, 20);
}

juce::AudioProcessorEditor* MidiNoteModifierProcessor::createEditor()
{
    return new MidiNoteModifierEditor(*this);
}
