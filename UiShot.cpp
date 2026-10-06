#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

#define CHECK(cond, label) \
    do { if (! (cond)) { printf("FAIL: %s\n", label); fails++; } \
         else { printf("ok: %s\n", label); } } while (0)

int main()
{
    int fails = 0;
    juce::ScopedJuceInitialiser_GUI init;
    MidiNoteModifierProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    // --- custom scale save/delete ---
    for (int i = 0; i < 12; ++i)
        proc.apvts.getParameter("pc" + juce::String(i))->setValueNotifyingHost(
            (i == 0 || i == 2 || i == 4 || i == 5 || i == 7 || i == 9 || i == 11) ? 1.0f : 0.0f);
    CHECK(proc.saveCustomScale("TestScale"), "save custom scale");
    CHECK(proc.getAllScaleNames().contains("TestScale"), "scale preset listed");
    CHECK(proc.deleteCustomScale("TestScale"), "delete custom scale");
    CHECK(! proc.getAllScaleNames().contains("TestScale"), "scale preset gone");

    // --- custom chord type save/load/reset (G6 = Major 6th) ---
    CHECK(proc.saveChordType(0, "Major 6th", { 0, 4, 7, 9 }), "save chord type Major 6th");
    CHECK(proc.customChordDisplayName(0) == "Major 6th", "chord type display name");
    CHECK(proc.getCustomChordIntervals(0) == std::vector<int>({ 0, 4, 7, 9 }), "chord type intervals");
    // persistence round-trip through state
    {
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        MidiNoteModifierProcessor proc2;
        proc2.setStateInformation(mb.getData(), (int) mb.getSize());
        CHECK(proc2.customChordDisplayName(0) == "Major 6th", "chord type survives state round-trip");
        CHECK(proc2.getCustomChordIntervals(0) == std::vector<int>({ 0, 4, 7, 9 }), "chord ivs survive round-trip");
    }
    CHECK(proc.resetChordType(0), "reset chord type");
    CHECK(proc.customChordDisplayName(0) == "Custom 1", "chord type reset to default name");

    // --- white-keys degree mapping: C D E F G A B -> C D# F F# G A# C (C minor blues) ---
    {
        std::vector<int> mb = { 0, 3, 5, 6, 7, 10 };
        int expect[7] = { 60, 63, 65, 66, 67, 70, 72 };
        int in[7] = { 60, 62, 64, 65, 67, 69, 71 };
        for (int i = 0; i < 7; ++i)
            CHECK(Scales::degreeMapNote(in[i], 0, mb) == expect[i], "degree map white key");
        CHECK(Scales::degreeMapNote(61, 0, mb) == 60, "degree map black key snaps down");
    }

    // --- global default colour scheme persists across instances ---
    MidiNoteModifierProcessor::storeDefaultTheme(2);
    CHECK(MidiNoteModifierProcessor::loadDefaultTheme() == 2, "default theme stored and loaded");
    MidiNoteModifierProcessor::storeDefaultTheme(0);
    CHECK(MidiNoteModifierProcessor::loadDefaultTheme() == 0, "default theme restored");

    // --- screenshot in a musically interesting state ---
    for (int i = 0; i < 12; ++i)
        printf("chd%d raw=%f\n", i, proc.apvts.getRawParameterValue("chd" + juce::String(i))->load());
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    ed->setSize(960, 800);
    if (auto* p = proc.apvts.getParameter("root"))
        p->setValueNotifyingHost(p->convertTo0to1(5));
    if (auto* p = proc.apvts.getParameter("scale"))
        p->setValueNotifyingHost(p->convertTo0to1(8));
    if (auto* p = proc.apvts.getParameter("chordOn"))
        p->setValueNotifyingHost(1.0f);
    if (auto* p = proc.apvts.getParameter("arpOn"))
        p->setValueNotifyingHost(1.0f);
    if (auto* p = proc.apvts.getParameter("whiteKeys"))
        p->setValueNotifyingHost(1.0f);
    proc.saveChordType(1, "Major 6th", { 0, 4, 7, 9 });
    ed->resized();
    juce::Image img(juce::Image::RGB, 960, 800, true);
    {
        juce::Graphics g(img);
        ed->paintEntireComponent(g, false);
    }
    juce::File f("/tmp/opencode/ui.png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    png.writeImageToStream(img, os);

    printf(fails == 0 ? "ALL TESTS PASSED\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
