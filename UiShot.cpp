#include "Source/PluginProcessor.h"
#include "Source/PluginEditor.h"

#define CHECK(cond, label) \
    do { if (! (cond)) { printf("FAIL: %s\n", label); fails++; } \
         else { printf("ok: %s\n", label); } } while (0)


// ---- MIDI behaviour harness: run blocks through a processor, collect output ----
struct Ev { juce::int64 t; bool on; int note; };
struct Rig
{
    MidiNoteModifierProcessor p;
    juce::int64 clock = 0;
    std::vector<Ev> evs;
    static constexpr int B = 512;
    Rig() { p.prepareToPlay(44100.0, B); }
    void set(const char* id, float v)
    {
        auto* par = p.apvts.getParameter(id);
        par->setValueNotifyingHost(par->convertTo0to1(v));
    }
    void run(juce::MidiBuffer in, int blocks = 1)
    {
        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf(0, B);
            juce::MidiBuffer m; if (b == 0) m.swapWith(in);
            p.processBlock(buf, m);
            for (const auto meta : m)
            {
                auto msg = meta.getMessage();
                if (msg.isNoteOn()) evs.push_back({ clock + meta.samplePosition, true, msg.getNoteNumber() });
                else if (msg.isNoteOff()) evs.push_back({ clock + meta.samplePosition, false, msg.getNoteNumber() });
            }
            clock += B;
        }
    }
    void on(int n)  { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0); run(m); }
    void off(int n) { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::noteOff(1, n), 0); run(m); }
    void cc64(bool down) { juce::MidiBuffer m; m.addEvent(juce::MidiMessage::controllerEvent(1, 64, down ? 127 : 0), 0); run(m); }
    void idle(int blocks) { run({}, blocks); }
    int count(bool isOn, int note = -1) const
    {
        int c = 0; for (auto& e : evs) if (e.on == isOn && (note < 0 || e.note == note)) ++c; return c;
    }
    // notes still sounding (on without matching off)
    int sounding() const
    {
        int d[128] = {}; for (auto& e : evs) d[e.note] += e.on ? 1 : -1;
        int c = 0; for (int x : d) if (x > 0) ++c; return c;
    }
};

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


    // ================= MIDI behaviour =================
    { // two keys snapping to the same note must not cut each other off
        Rig r; r.set("scale", 1); r.set("snap", 1); // C major, snap down
        r.on(60); r.on(61);       // both -> C4
        r.off(60);
        CHECK(r.count(false, 60) == 0, "shared snapped note survives first release");
        r.off(61);
        CHECK(r.count(false, 60) == 1 && ! r.evs.back().on, "shared snapped note released by last key");
    }
    { // sustain pedal: deferred note-off, voice freed
        Rig r; r.on(60); r.cc64(true); r.off(60);
        CHECK(r.sounding() == 1, "sustain holds note");
        r.cc64(false);
        CHECK(r.sounding() == 0, "pedal up releases note");
        CHECK(r.p.activeVoices.load() == 0, "no leaked voice after sustain");
    }
    { // strum spanning several blocks keeps its timing
        Rig r; r.set("scale", 1); r.set("chordOn", 1); r.set("strum", 40.0f); // C major triad
        r.on(60); r.idle(10);
        CHECK(r.count(true) == 3, "all strummed notes play");
        bool spaced = r.evs.size() >= 3 && r.evs[2].t - r.evs[1].t >= 1700 && r.evs[1].t - r.evs[0].t >= 1700;
        CHECK(spaced, "strum spacing ~40ms across blocks");
        r.off(60);
        CHECK(r.sounding() == 0, "strummed chord fully released");
    }
    { // releasing before the strum finishes cancels pending notes cleanly
        Rig r; r.set("chordOn", 1); r.set("strum", 40.0f);
        r.on(60); r.off(60); r.idle(10);
        CHECK(r.sounding() == 0, "early release leaves nothing stuck");
    }
    { // power+octave first inversion must not double a note
        Rig r; r.set("chordOn", 1); r.set("chd0", 12); r.set("inversion", 1); // 5+Oct
        r.on(60);
        CHECK(r.count(true) == 2, "inverted 5+Oct has no duplicate notes");
        r.off(60);
        CHECK(r.sounding() == 0, "inverted chord released");
    }
    { // arp gate works even when a step is longer than a block
        Rig r; r.set("arpOn", 1); r.set("arpRate", 0); r.set("gate", 50.0f); // 1/4 at 120 = 22050 smp
        r.on(60); r.idle(60);
        bool ok = r.evs.size() >= 2 && r.evs[0].on && ! r.evs[1].on
                  && std::abs((int) (r.evs[1].t - r.evs[0].t) - 11025) < 600;
        CHECK(ok, "arp 50% gate cuts note at half a step");
        r.off(60); r.idle(2);
        CHECK(r.sounding() == 0, "arp stops on release");
    }
    { // arp + sustain + re-press must not leave notes stuck in the pool
        Rig r; r.set("arpOn", 1);
        r.on(60); r.cc64(true); r.off(60); r.on(60); r.off(60); r.cc64(false); r.idle(20);
        CHECK(r.sounding() == 0, "arp pool empties after sustain re-press");
    }
    { // switching arp on while a plain note is held hands it to the arp
        Rig r; r.on(60);
        r.set("arpOn", 1); r.idle(4);
        r.off(60); r.idle(4);
        CHECK(r.sounding() == 0, "no stuck note after enabling arp mid-hold");
    }
    { // latch release keeps arpeggiating keys still held
        Rig r; r.set("arpOn", 1); r.set("arpLatch", 1);
        r.on(60); r.on(64); r.off(64);
        r.set("arpLatch", 0); r.idle(40);
        int last = -1; for (auto& e : r.evs) if (e.on) last = e.note;
        CHECK(last == 60, "held key keeps arpeggiating after latch off");
        r.off(60); r.idle(2);
        CHECK(r.sounding() == 0, "arp stops after latch off and release");
    }
    { // latch: new chord after releasing everything replaces the latched one
        Rig r; r.set("arpOn", 1); r.set("arpLatch", 1);
        r.on(60); r.off(60); r.idle(4);
        r.on(67); r.off(67);
        r.evs.clear(); r.idle(60);
        bool only67 = r.count(true) > 0 && r.count(true, 67) == r.count(true);
        CHECK(only67, "latched pool replaced by new chord");
    }
    { // editor audition goes through the normal path
        Rig r; r.p.queueAuditionNote(true, 61, 100); r.idle(1);
        r.p.queueAuditionNote(false, 61, 0); r.idle(1);
        CHECK(r.count(true) == 1 && r.sounding() == 0, "audition note on/off");
    }

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
    const int shotW = juce::SystemStats::getEnvironmentVariable("UISHOT_W", "960").getIntValue();
    const int shotH = juce::SystemStats::getEnvironmentVariable("UISHOT_H", "800").getIntValue();
    ed->setSize(shotW, shotH);
    if (auto* me = dynamic_cast<MidiNoteModifierEditor*>(ed.get()))
        me->timerCallback(); // populate live status labels
    ed->resized();
    juce::Image img(juce::Image::RGB, shotW, shotH, true);
    {
        juce::Graphics g(img);
        ed->paintEntireComponent(g, false);
    }
    juce::File f(juce::SystemStats::getEnvironmentVariable("UISHOT_OUT", "/tmp/opencode/ui.png"));
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::FileOutputStream os(f);
    juce::PNGImageFormat png;
    png.writeImageToStream(img, os);

    printf(fails == 0 ? "ALL TESTS PASSED\n" : "FAILURES: %d\n", fails);
    return fails == 0 ? 0 : 1;
}
