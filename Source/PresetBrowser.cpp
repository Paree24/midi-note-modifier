#include "PresetBrowser.h"
#include "PluginProcessor.h"

static const juce::Colour kText { 0xFFF2F0F5 }, kTextDim { 0xFFA7A3B8 };

PresetBrowser::PresetBrowser(MidiNoteModifierProcessor& p, ModifierLookAndFeel& l)
    : proc(p), lnf(l)
{
    search.setTextToShowWhenEmpty("Search setups...", juce::Colour(0xFF8E8E9A));
    search.onTextChange = [this] { rebuildRows(); };
    addAndMakeVisible(search);

    for (int b = 0; b < 3; ++b)
    {
        bankBtns[b].onClick = [this, b] { activeBank = b; refreshBankButtons(); rebuildRows(); };
        addAndMakeVisible(bankBtns[b]);
    }
    refreshBankButtons();

    list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setRowHeight(26);
    addAndMakeVisible(list);

    closeBtn.onClick = [this] { if (onClose) onClose(); };
    nameBox.setTextToShowWhenEmpty("Setup name...", juce::Colour(0xFF8E8E9A));
    nameBox.onTextChange = [this] { setOverwriteArmed(false); };
    nameBox.onReturnKey = [this] { clickSave(); };
    addAndMakeVisible(nameBox);
    saveBtn.setTooltip("Save the current scale, chords and arp as a user setup");
    saveBtn.onClick = [this] { clickSave(); };
    delBtn.setTooltip("Delete the selected user setup");
    delBtn.onClick = [this]
    {
        int r = list.getSelectedRow();
        if (r < 0 || r >= (int) rows.size() || ! rows[(size_t) r].isUser) return;
        if (SetupPresets::deleteUserPreset(rows[(size_t) r].file))
            rebuildRows();
    };
    for (auto* b : { &closeBtn, &saveBtn, &delBtn })
        addAndMakeVisible(b);
    rebuildRows();
}

// Save (inline, no pop-up): an existing user setup needs a second click
// ("Overwrite?"); editing the name disarms it.
void PresetBrowser::clickSave()
{
    if (nameBox.getText().trim().isEmpty()) { nameBox.grabKeyboardFocus(); return; }
    auto name = SetupPresets::sanitiseName(nameBox.getText());
    bool exists = SetupPresets::getUserPresetDir().getChildFile(name + ".xml").existsAsFile();
    if (exists && ! overwriteArmed) { setOverwriteArmed(true); return; }
    setOverwriteArmed(false);
    if (! SetupPresets::writeUserPreset(name, proc.captureSetup())) return;
    proc.apvts.state.setProperty("setupName", name, nullptr);
    activeBank = 2;
    search.setText("", false);
    nameBox.setText(name, false);
    refreshBankButtons();
    rebuildRows();
}

void PresetBrowser::setOverwriteArmed(bool on)
{
    overwriteArmed = on;
    saveBtn.setButtonText(on ? "Overwrite?" : "Save");
    saveBtn.setToggleState(on, juce::dontSendNotification);
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        nameBox.setText(proc.getSetupName() == "Init" ? juce::String() : proc.getSetupName(), false);
        setOverwriteArmed(false);
        rebuildRows();
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::refreshBankButtons()
{
    for (int b = 0; b < 3; ++b)
        bankBtns[b].setToggleState(b == activeBank, juce::dontSendNotification);
}

void PresetBrowser::rebuildRows()
{
    rows.clear();
    juce::String q = search.getText().trim().toLowerCase();
    if (activeBank != 2)
    {
        const auto& f = SetupPresets::factory();
        for (size_t k = 0; k < f.size(); ++k)
            if (q.isEmpty() || f[k].name.toLowerCase().contains(q))
                rows.push_back({ false, (int) k, f[k].name, {} });
    }
    if (activeBank != 1)
        for (auto& u : SetupPresets::scanUserPresets())
            if (q.isEmpty() || u.name.toLowerCase().contains(q))
                rows.push_back({ true, -1, u.name, u.file });
    list.deselectAllRows();
    list.updateContent();
    list.repaint();
    delBtn.setEnabled(false);
}

int PresetBrowser::getNumRows() { return (int) rows.size(); }

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size()) return;
    auto& r = rows[(size_t) row];
    bool isCurrent = r.name == proc.getSetupName();
    if (selected)
    {
        g.setColour(lnf.theme.mid().withAlpha(0.35f));
        g.fillRoundedRectangle(2.0f, 1.0f, (float) w - 4.0f, (float) h - 2.0f, 6.0f);
    }
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.setColour(kTextDim);
    g.drawText(r.isUser ? "USER" : "FACTORY", 10, 0, 64, h, juce::Justification::centredLeft, false);
    g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
    g.setColour(isCurrent ? lnf.theme.light() : kText);
    g.drawText(r.name, 80, 0, w - 88, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::selectedRowsChanged(int)
{
    int r = list.getSelectedRow();
    delBtn.setEnabled(r >= 0 && r < (int) rows.size() && rows[(size_t) r].isUser);
    if (r < 0 || r >= (int) rows.size()) return;
    auto& row = rows[(size_t) r];
    if (row.isUser)
    {
        auto t = SetupPresets::readUserPreset(row.file);
        if (t.isValid()) proc.applySetup(t, row.name);
    }
    else
    {
        const auto& f = SetupPresets::factory()[(size_t) row.index];
        proc.applySetup(f.tree, f.name);
    }
    nameBox.setText(row.name == "Init" ? juce::String() : row.name, false);
    setOverwriteArmed(false);
    list.repaint();
}

void PresetBrowser::listBoxItemDoubleClicked(int, const juce::MouseEvent&)
{
    if (onClose) onClose();
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f)); // scrim
    g.setColour(juce::Colour(0xFF222228));
    g.fillRoundedRectangle(panel.toFloat(), 12.0f);
    g.setColour(juce::Colour(0xFF45454E));
    g.drawRoundedRectangle(panel.toFloat().reduced(0.5f), 12.0f, 1.0f);
    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    g.drawText("Setups", panel.getX() + 16, panel.getY() + 10, 200, 26, juce::Justification::centredLeft, false);
    g.setColour(kTextDim);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("Scale + chords + arp", panel.getX() + 96, panel.getY() + 10, 220, 26,
               juce::Justification::centredLeft, false);
}

void PresetBrowser::resized()
{
    const int pw = juce::jmin(560, getWidth() - 40), ph = juce::jmin(520, getHeight() - 80);
    panel = juce::Rectangle<int>(pw, ph).withCentre(getLocalBounds().getCentre());
    auto inner = panel.reduced(16, 12);
    auto top = inner.removeFromTop(26);
    closeBtn.setBounds(top.removeFromRight(32));
    inner.removeFromTop(10);
    search.setBounds(inner.removeFromTop(26));
    inner.removeFromTop(8);
    auto banks = inner.removeFromTop(26);
    for (int b = 0; b < 3; ++b)
    {
        bankBtns[b].setBounds(banks.removeFromLeft(90));
        banks.removeFromLeft(6);
    }
    inner.removeFromTop(8);
    auto bottom = inner.removeFromBottom(26);
    inner.removeFromBottom(10);
    list.setBounds(inner);
    delBtn.setBounds(bottom.removeFromRight(80));
    bottom.removeFromRight(6);
    saveBtn.setBounds(bottom.removeFromRight(100));
    bottom.removeFromRight(8);
    nameBox.setBounds(bottom);
}

void PresetBrowser::mouseDown(const juce::MouseEvent& e)
{
    if (! panel.contains(e.getPosition()) && onClose) onClose(); // scrim click closes
}
