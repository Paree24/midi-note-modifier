// VST3 host check (dev only, never shipped): loads the BUILT .vst3 through
// JUCE's VST3 hosting, like a DAW would: MIDI I/O, params, sample-accurate
// MIDI, state, editor. Ported from the Pastel Drum Sampler's HostCheck.
//   HostCheck <path/to/Midi Note Modifier.vst3>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* par : p.getParameters())
        if (par->getName (64) == name) return par;
    return nullptr;
}

// set a choice/bool parameter by index the way a host does (normalised value)
static void setIndex (juce::AudioProcessorParameter* par, int index)
{
    const int steps = juce::jmax (2, par->getNumSteps());
    par->beginChangeGesture();
    par->setValueNotifyingHost ((float) index / (float) (steps - 1));
    par->endChangeGesture();
}

struct NoteEv { int pos; bool on; int note; };

static std::vector<NoteEv> runBlock (juce::AudioPluginInstance& inst, juce::AudioBuffer<float>& buf,
                                     juce::MidiBuffer in)
{
    buf.clear();
    inst.processBlock (buf, in);
    std::vector<NoteEv> evs;
    for (const auto meta : in)
    {
        auto m = meta.getMessage();
        if (m.isNoteOn()) evs.push_back ({ meta.samplePosition, true, m.getNoteNumber() });
        else if (m.isNoteOff()) evs.push_back ({ meta.samplePosition, false, m.getNoteNumber() });
    }
    return evs;
}

int main (int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { printf ("usage: HostCheck <plugin.vst3>\n"); return 1; }
    int fails = 0;
    auto check = [&] (bool ok, const juce::String& what) { printf ("%s  %s\n", ok ? "  ok " : "FAIL ", what.toRawUTF8()); if (! ok) ++fails; };
    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile (descs, juce::File (argv[1]).getFullPathName());
    check (descs.size() == 1, "VST3 scanned: " + juce::String (descs.size()) + " plugin(s)");
    if (descs.isEmpty()) return 1;
    auto& d = *descs[0];
    check (! d.isInstrument, "registered as an effect, not an instrument: " + d.name + " / " + d.category);
    juce::String err;
    auto inst = fmt.createInstanceFromDescription (d, 48000.0, 512, err);
    check (inst != nullptr, "instantiated " + err);
    if (! inst) return 1;
    check (inst->acceptsMidi() && inst->producesMidi(), "MIDI in and MIDI out");
    check (inst->getParameters().size() >= 50, "parameters: " + juce::String (inst->getParameters().size()));
    inst->enableAllBuses();
    inst->prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buf (juce::jmax (1, inst->getTotalNumOutputChannels(),
                                              inst->getTotalNumInputChannels()), 512);

    // plain pass-through (default scale is Chromatic), sample-accurate
    {
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 100);
        auto evs = runBlock (*inst, buf, mb);
        check (evs.size() == 1 && evs[0].on && evs[0].note == 61 && evs[0].pos == 100,
               "note-on passes through at its sample position");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 61), 300);
        evs = runBlock (*inst, buf, off);
        check (evs.size() == 1 && ! evs[0].on && evs[0].note == 61 && evs[0].pos == 300, "note-off matches");
    }

    // host-side parameter changes take effect: Major + snap Down maps C#4 -> C4
    auto* scale = findParam (*inst, "Scale");
    auto* snap = findParam (*inst, "Snap Mode");
    check (scale != nullptr && snap != nullptr, "Scale / Snap Mode parameters found");
    if (scale != nullptr && snap != nullptr)
    {
        setIndex (scale, 1);
        setIndex (snap, 1);
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100), 0);
        mb.addEvent (juce::MidiMessage::noteOff (1, 61), 200);
        auto evs = runBlock (*inst, buf, mb);
        check (evs.size() == 2 && evs[0].note == 60 && evs[1].note == 60, "host param change snaps C#4 to C4");
    }

    // chords through the host: C major triad on C4
    if (auto* chordOn = findParam (*inst, "Chord Enable"))
    {
        setIndex (chordOn, 1);
        juce::MidiBuffer mb;
        mb.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        auto evs = runBlock (*inst, buf, mb);
        check (evs.size() == 3, "chord mode emits a triad (" + juce::String ((int) evs.size()) + " notes)");
        juce::MidiBuffer off;
        off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        evs = runBlock (*inst, buf, off);
        check (evs.size() == 3, "triad released");
        setIndex (chordOn, 0);
    }
    else check (false, "Chord Enable parameter found");

    // state round trip restores the scale
    juce::MemoryBlock st;
    inst->getStateInformation (st);
    check (st.getSize() > 200, "state saved (" + juce::String ((int) st.getSize()) + " bytes)");
    if (scale != nullptr)
    {
        const float saved = scale->getValue();
        setIndex (scale, 2);
        inst->setStateInformation (st.getData(), (int) st.getSize());
        check (std::abs (scale->getValue() - saved) < 1.0e-4f, "state restored (scale param back)");
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
    check (ed != nullptr && ed->getWidth() >= 860 && ed->getHeight() >= 720,
           "editor opens (" + (ed ? juce::String (ed->getWidth()) + "x" + juce::String (ed->getHeight()) : juce::String ("none")) + ")");
    ed.reset();
    inst->releaseResources();
    inst.reset();
    printf (fails == 0 ? "HOST CHECK PASSED\n" : "%d HOST CHECK(S) FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}
