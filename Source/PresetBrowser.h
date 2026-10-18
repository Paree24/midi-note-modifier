#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ModifierLookAndFeel.h"
#include "SetupPresets.h"

class MidiNoteModifierProcessor;

// Setup preset browser overlay (adapted from the ERSA/Monosynth browser):
// search + All/Factory/User filter + audition list. Clicking a row loads it,
// double-click or a click on the scrim closes. Inline name field + Save;
// saving over an existing user preset needs a second click.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    PresetBrowser(MidiNoteModifierProcessor& p, ModifierLookAndFeel& lnf);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void visibilityChanged() override;

    std::function<void()> onClose;

private:
    struct Row { bool isUser = false; int index = -1; juce::String name; juce::File file; };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged(int lastRow) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    void rebuildRows();
    void refreshBankButtons();
    void clickSave();
    void setOverwriteArmed(bool on);

    MidiNoteModifierProcessor& proc;
    ModifierLookAndFeel& lnf;
    juce::TextEditor search, nameBox;
    bool overwriteArmed = false;
    juce::ListBox list { "presets", this };
    juce::TextButton closeBtn { "X" }, saveBtn { "Save" }, delBtn { "Delete" };
    juce::TextButton bankBtns[3] { juce::TextButton { "All" }, juce::TextButton { "Factory" }, juce::TextButton { "User" } };
    std::vector<Row> rows;
    int activeBank = 0; // 0 all, 1 factory, 2 user
    juce::Rectangle<int> panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
