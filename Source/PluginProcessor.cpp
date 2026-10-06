#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"
#include <algorithm>

static juce::StringArray themeChoiceNames()
{
    juce::StringArray names;
    for (auto& t : Theme::all()) names.add(t.name);
    return names;
}

static std::unique_ptr<juce::PropertiesFile> createSettingsFile()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "MidiNoteModifier";
    o.filenameSuffix = "settings";
    o.folderName = "MidiNoteModifier";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile>(o);
}

MidiNoteModifierProcessor::MidiNoteModifierProcessor()
    : AudioProcessor(BusesProperties()), apvts(*this, nullptr, "Params", createLayout())
{
    for (int s = 0; s < kNumCustomChords; ++s)
    {
        customChordNames[s] = "";
        customChordIvs[s] = { 0 };
    }
    // fresh instances start on the user's global default theme
    // (a host state recall later overrides this per instance)
    if (auto* p = apvts.getParameter("theme"))
        p->setValueNotifyingHost(p->convertTo0to1(loadDefaultTheme()));
}

int MidiNoteModifierProcessor::loadDefaultTheme()
{
    auto f = createSettingsFile();
    return f ? juce::jlimit(0, Theme::count() - 1, f->getIntValue("defaultTheme", 0)) : 0;
}

void MidiNoteModifierProcessor::storeDefaultTheme(int i)
{
    auto f = createSettingsFile();
    if (f != nullptr)
    {
        f->setValue("defaultTheme", juce::jlimit(0, Theme::count() - 1, i));
        f->saveIfNeeded();
    }
}

MidiNoteModifierProcessor::~MidiNoteModifierProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout MidiNoteModifierProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto choice = [](const juce::String& id, const juce::String& name,
                     const juce::StringArray& choices, int defIdx) {
        return std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID(id, 1), name, choices, defIdx);
    };

    juce::StringArray rootNames;
    for (auto& n : Scales::noteNames()) rootNames.add(n);
    p.push_back(choice("root", "Root Note", rootNames, 0));

    juce::StringArray scaleNames;
    for (auto& s : Scales::factoryScales()) scaleNames.add(s.name);
    p.push_back(choice("scale", "Scale", scaleNames, 0));
    p.push_back(choice("snap", "Snap Mode", { "Nearest", "Down", "Up" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("scaleLock", 1), "Scale Lock", true));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("whiteKeys", 1), "White Keys", false));

    // custom pitch-class pattern (used when scale == Custom)
    for (int i = 0; i < 12; ++i)
    {
        static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 };
        bool defOn = false;
        for (int m : major) if (m == i) { defOn = true; break; }
        p.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID("pc" + juce::String(i), 1),
            "Custom " + Scales::pcName(i), defOn));
    }

    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("chordOn", 1), "Chord Enable", false));
    juce::StringArray chordNames;
    for (int t = -1; t <= 12; ++t) chordNames.add(Scales::chordTypeName(t));
    for (int s = 0; s < kNumCustomChords; ++s)
        chordNames.add("Custom " + juce::String(s + 1));
    for (int i = 0; i < kNumChordSlots; ++i)
        p.push_back(choice("chd" + juce::String(i), "Chord Degree " + juce::String(i + 1), chordNames, 0));
    p.push_back(choice("inversion", "Inversion", { "Root", "1st", "2nd" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("invLower", 1), "Inversion Lower", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("bassBelow", 1), "Bass Below", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("strum", 1), "Strum",
        juce::NormalisableRange<float>(0.0f, 40.0f, 0.5f), 0.0f, "ms"));

    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("arpOn", 1), "Arp Enable", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("arpLatch", 1), "Arp Latch", false));
    p.push_back(choice("arpMode", "Arp Mode", { "Up", "Down", "UpDown", "DownUp", "Random", "Played" }, 0));
    p.push_back(choice("arpRate", "Arp Rate",
        { "1/4", "1/4D", "1/4T", "1/8", "1/8D", "1/8T", "1/16", "1/16D", "1/16T",
          "1/32", "1/32D", "1/32T", "1/64", "1/64D", "1/64T" }, 6));
    p.push_back(choice("arpOct", "Arp Octaves", { "1", "2", "3", "4" }, 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("swing", 1), "Swing",
        juce::NormalisableRange<float>(0.0f, 60.0f, 0.5f), 0.0f, "%"));
    p.push_back(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("gate", 1), "Gate",
        juce::NormalisableRange<float>(10.0f, 100.0f, 1.0f), 90.0f, "%"));
    p.push_back(choice("theme", "Theme", themeChoiceNames(), 0));

    return { p.begin(), p.end() };
}

void MidiNoteModifierProcessor::prepareToPlay(double sampleRate, int) { currentSampleRate = sampleRate; }

bool MidiNoteModifierProcessor::isBusesLayoutSupported(const BusesLayout&) const { return true; }

std::vector<int> MidiNoteModifierProcessor::currentIntervals()
{
    int scaleIdx = (int) apvts.getRawParameterValue("scale")->load();
    auto& facts = Scales::factoryScales();
    if (scaleIdx < 0) scaleIdx = 0;
    if (scaleIdx >= (int) facts.size()) scaleIdx = 0;
    if (scaleIdx == Scales::customIndex())
    {
        std::vector<int> iv;
        for (int i = 0; i < 12; ++i)
            if (apvts.getRawParameterValue("pc" + juce::String(i))->load() > 0.5f)
                iv.push_back(i);
        if (iv.empty()) iv.push_back(0);
        return iv;
    }
    return facts[(size_t) scaleIdx].intervals;
}

int MidiNoteModifierProcessor::currentRoot()
{
    return juce::jlimit(0, 11, (int) apvts.getRawParameterValue("root")->load());
}

std::array<bool,12> MidiNoteModifierProcessor::currentPcSet()
{
    return Scales::pcSet(currentRoot(), currentIntervals());
}

juce::StringArray MidiNoteModifierProcessor::getAllScaleNames() const
{
    juce::StringArray names;
    for (auto& s : Scales::factoryScales())
        if (s.name != "Custom") names.add(s.name);
    auto user = apvts.state.getChildWithName("USER_SCALES");
    for (int i = 0; i < user.getNumChildren(); ++i)
        names.add(user.getChild(i).getProperty("name").toString());
    names.add("Custom");
    return names;
}

bool MidiNoteModifierProcessor::saveCustomScale(const juce::String& name)
{
    if (name.isEmpty()) return false;
    juce::String pattern;
    for (int i = 0; i < 12; ++i)
        pattern += (apvts.getRawParameterValue("pc" + juce::String(i))->load() > 0.5f) ? "1" : "0";
    auto user = apvts.state.getChildWithName("USER_SCALES");
    if (! user.isValid())
    {
        apvts.state.appendChild(juce::ValueTree("USER_SCALES"), nullptr);
        user = apvts.state.getChildWithName("USER_SCALES");
    }
    for (int i = 0; i < user.getNumChildren(); ++i)
    {
        if (user.getChild(i).getProperty("name").toString() == name)
        {
            user.getChild(i).setProperty("pattern", pattern, nullptr);
            return true;
        }
    }
    juce::ValueTree v("SCALE");
    v.setProperty("name", name, nullptr);
    v.setProperty("pattern", pattern, nullptr);
    user.appendChild(v, nullptr);
    return true;
}

bool MidiNoteModifierProcessor::deleteCustomScale(const juce::String& name)
{
    auto user = apvts.state.getChildWithName("USER_SCALES");
    for (int i = 0; i < user.getNumChildren(); ++i)
        if (user.getChild(i).getProperty("name").toString() == name)
        {
            user.removeChild(i, nullptr);
            return true;
        }
    return false;
}

static juce::ValueTree getOrCreateChild(juce::ValueTree parent, const juce::Identifier& type)
{
    auto c = parent.getChildWithName(type);
    if (! c.isValid())
    {
        parent.appendChild(juce::ValueTree(type), nullptr);
        c = parent.getChildWithName(type);
    }
    return c;
}

static juce::String ivsToString(const std::vector<int>& ivs)
{
    juce::String s;
    for (size_t i = 0; i < ivs.size(); ++i)
    {
        if (i > 0) s += ",";
        s += juce::String(ivs[i]);
    }
    return s;
}

static std::vector<int> stringToIvs(const juce::String& s)
{
    std::vector<int> ivs;
    for (auto& tok : juce::StringArray::fromTokens(s, ",", ""))
    {
        int v = juce::jlimit(0, 24, tok.getIntValue());
        if (ivs.size() < (size_t) MidiNoteModifierProcessor::kMaxChordNotes)
            ivs.push_back(v);
    }
    if (ivs.empty()) ivs.push_back(0);
    std::sort(ivs.begin(), ivs.end());
    ivs.erase(std::unique(ivs.begin(), ivs.end()), ivs.end());
    return ivs;
}

bool MidiNoteModifierProcessor::saveChordType(int slot, const juce::String& name,
                                              const std::vector<int>& intervals)
{
    if (slot < 0 || slot >= kNumCustomChords) return false;
    auto ivs = stringToIvs(ivsToString(intervals)); // sanitise
    {
        juce::ScopedLock sl(chordTypeLock);
        customChordNames[slot] = name.trim().substring(0, 24);
        customChordIvs[slot] = ivs;
    }
    storeCustomChordTypesToState();
    return true;
}

bool MidiNoteModifierProcessor::resetChordType(int slot)
{
    if (slot < 0 || slot >= kNumCustomChords) return false;
    {
        juce::ScopedLock sl(chordTypeLock);
        customChordNames[slot] = "";
        customChordIvs[slot] = { 0 };
    }
    storeCustomChordTypesToState();
    // any degree using this slot falls back to Auto
    for (int i = 0; i < kNumChordSlots; ++i)
    {
        int idx = (int) apvts.getRawParameterValue("chd" + juce::String(i))->load();
        if (idx == 14 + slot)
            if (auto* par = apvts.getParameter("chd" + juce::String(i)))
                par->setValueNotifyingHost(0.0f);
    }
    return true;
}

juce::String MidiNoteModifierProcessor::getCustomChordName(int slot) const
{
    juce::ScopedLock sl(chordTypeLock);
    if (slot < 0 || slot >= kNumCustomChords) return {};
    return customChordNames[slot];
}

std::vector<int> MidiNoteModifierProcessor::getCustomChordIntervals(int slot) const
{
    juce::ScopedLock sl(chordTypeLock);
    if (slot < 0 || slot >= kNumCustomChords) return { 0 };
    return customChordIvs[slot];
}

juce::String MidiNoteModifierProcessor::customChordDisplayName(int slot) const
{
    juce::String n = getCustomChordName(slot);
    if (n.isNotEmpty()) return n;
    return "Custom " + juce::String(slot + 1);
}

void MidiNoteModifierProcessor::storeCustomChordTypesToState()
{
    auto node = getOrCreateChild(apvts.state, "CUSTOM_CHORDS");
    juce::ScopedLock sl(chordTypeLock);
    for (int s = 0; s < kNumCustomChords; ++s)
    {
        node.setProperty("name" + juce::String(s), customChordNames[s], nullptr);
        node.setProperty("iv" + juce::String(s), ivsToString(customChordIvs[s]), nullptr);
    }
}

void MidiNoteModifierProcessor::loadCustomChordTypesFromState()
{
    auto node = apvts.state.getChildWithName("CUSTOM_CHORDS");
    if (! node.isValid()) return;
    juce::ScopedLock sl(chordTypeLock);
    for (int s = 0; s < kNumCustomChords; ++s)
    {
        customChordNames[s] = node.getProperty("name" + juce::String(s), "").toString()
                                  .substring(0, 24);
        customChordIvs[s] = stringToIvs(node.getProperty("iv" + juce::String(s), "0").toString());
    }
}

void MidiNoteModifierProcessor::resetDiatonicChords()
{
    for (int i = 0; i < kNumChordSlots; ++i)
        if (auto* par = apvts.getParameter("chd" + juce::String(i)))
            par->setValueNotifyingHost(0.0f); // choice index 0 = Auto
}

void MidiNoteModifierProcessor::panic() { panicFlag.store(true); }

void MidiNoteModifierProcessor::queueAuditionNote(bool on, int midiNote, int velocity)
{
    juce::ScopedLock sl(stateLock);
    auditionQueue.push_back({ on, midiNote, velocity });
}

float MidiNoteModifierProcessor::getRateBeats() const
{
    int r = (int) apvts.getRawParameterValue("arpRate")->load();
    switch (r)
    {
        case 0:  return 1.0f;        // 1/4
        case 1:  return 1.5f;        // 1/4D
        case 2:  return 2.0f / 3.0f; // 1/4T
        case 3:  return 0.5f;        // 1/8
        case 4:  return 0.75f;       // 1/8D
        case 5:  return 1.0f / 3.0f; // 1/8T
        case 6:  return 0.25f;       // 1/16
        case 7:  return 0.375f;      // 1/16D
        case 8:  return 1.0f / 6.0f; // 1/16T
        case 9:  return 0.125f;      // 1/32
        case 10: return 0.1875f;     // 1/32D
        case 11: return 1.0f / 12.0f;// 1/32T
        case 12: return 0.0625f;     // 1/64
        case 13: return 0.09375f;    // 1/64D
        case 14: return 1.0f / 24.0f;// 1/64T
        default: return 0.25f;
    }
}

int MidiNoteModifierProcessor::buildChordForSnapped(int snapped, std::vector<int>& out, int& degreeOut)
{
    int root = currentRoot();
    auto intervals = currentIntervals();
    int scaleIdx = (int) apvts.getRawParameterValue("scale")->load();
    auto& facts = Scales::factoryScales();
    bool powerDefault = scaleIdx >= 0 && scaleIdx < (int) facts.size()
                        && facts[(size_t) scaleIdx].powerChords;
    int deg = Scales::scaleDegree(snapped, root, intervals);
    degreeOut = deg;
    bool chordOn = apvts.getRawParameterValue("chordOn")->load() > 0.5f;
    if (! chordOn) { out = { snapped }; return deg; }

    std::vector<int> rel;
    if (deg >= 0)
    {
        int slot = deg % kNumChordSlots;
        int c = (int) apvts.getRawParameterValue("chd" + juce::String(slot))->load();
        if (c >= 14) // user-defined chord type (Custom 1..6)
        {
            juce::ScopedLock sl(chordTypeLock);
            rel = customChordIvs[juce::jlimit(0, kNumCustomChords - 1, c - 14)];
            if (rel.empty()) rel = { 0 };
        }
        else
        {
            int forced = c - 1;
            if (forced == 0) rel = { 0 };                                    // Single
            else if (forced > 0) rel = Scales::forcedChordIntervals(forced); // explicit override
            else if (powerDefault) rel = Scales::forcedChordIntervals(7);   // Maschine default
            else rel = Scales::diatonicTriad(deg, intervals);               // Auto diatonic
        }
    }
    else rel = { 0 };

    out.clear();
    for (int iv : rel)
    {
        int n = snapped + iv;
        if (n >= 0 && n <= 127) out.push_back(n);
    }
    int inv = (int) apvts.getRawParameterValue("inversion")->load();
    bool lower = apvts.getRawParameterValue("invLower")->load() > 0.5f;
    bool bass = apvts.getRawParameterValue("bassBelow")->load() > 0.5f;
    applyInversionBass(out, inv, lower, bass);
    return deg;
}

void MidiNoteModifierProcessor::applyInversionBass(std::vector<int>& chord, int inversion,
                                                   bool lower, bool bassBelow)
{
    if (chord.size() > 1)
    {
        for (int k = 0; k < inversion && k < 2; ++k)
        {
            int low = chord.front();
            chord.erase(chord.begin());
            int up = low + 12;
            if (up <= 127) chord.push_back(up);
            else { chord.insert(chord.begin(), low); break; }
        }
        std::sort(chord.begin(), chord.end());
    }
    if (lower && ! chord.empty())
    {
        // same inversion shape, one octave down (e.g. E5 G5 C6 -> E4 G4 C5)
        std::vector<int> down;
        for (int n : chord)
            if (n - 12 >= 0) down.push_back(n - 12);
        if (! down.empty()) chord = down;
    }
    if (bassBelow && ! chord.empty())
    {
        int b = chord.front() - 12;
        // bass is the chord root an octave below (use lowest note as root ref)
        if (b >= 0) chord.insert(chord.begin(), b);
    }
}

void MidiNoteModifierProcessor::noteOnOut(juce::MidiBuffer& out, int pos, int note, int ch, float vel)
{
    out.addEvent(juce::MidiMessage::noteOn(ch, note, vel), pos);
}

void MidiNoteModifierProcessor::noteOffOut(juce::MidiBuffer& out, int pos, int note, int ch)
{
    out.addEvent(juce::MidiMessage::noteOff(ch, note), pos);
}

void MidiNoteModifierProcessor::killAllSound(juce::MidiBuffer& out)
{
    for (int ch = 1; ch <= 16; ++ch)
    {
        out.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
        out.addEvent(juce::MidiMessage::controllerEvent(ch, 123, 0), 0);
    }
}

int MidiNoteModifierProcessor::pickArpNote()
{
    if (arpPool.empty()) return -1;
    int mode = (int) apvts.getRawParameterValue("arpMode")->load();
    int oct = juce::jlimit(1, 4, (int) apvts.getRawParameterValue("arpOct")->load() + 1);

    // Build octave-expanded list ascending
    std::vector<int> exp;
    for (int o = 0; o < oct; ++o)
        for (int n : arpPool)
        {
            int v = n + 12 * o;
            if (v <= 127) exp.push_back(v);
        }
    if (exp.empty()) return -1;
    int N = (int) exp.size();

    switch (mode)
    {
        case 0: return exp[arpStepIndex % N];                       // Up
        case 1: return exp[N - 1 - (arpStepIndex % N)];             // Down
        case 2: { // UpDown (exclusive ends)
            if (N == 1) return exp[0];
            int cyc = 2 * N - 2;
            int p = arpStepIndex % cyc;
            return p < N ? exp[p] : exp[cyc - p];
        }
        case 3: { // DownUp
            if (N == 1) return exp[0];
            int cyc = 2 * N - 2;
            int p = arpStepIndex % cyc;
            return p < N ? exp[N - 1 - p] : exp[p - (N - 1)];
        }
        case 4: return exp[juce::Random::getSystemRandom().nextInt(N)]; // Random
        case 5: { // Played order expanded with octaves
            std::vector<int> po;
            for (int o = 0; o < oct; ++o)
                for (int n : arpPlayedOrder)
                {
                    int v = n + 12 * o;
                    if (v <= 127) po.push_back(v);
                }
            if (po.empty()) return -1;
            return po[arpStepIndex % (int) po.size()];
        }
        default: return exp[arpStepIndex % N];
    }
}

void MidiNoteModifierProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    juce::MidiBuffer out;
    const int numSamples = buffer.getNumSamples();

    // --- Panic flag from UI ---
    if (panicFlag.exchange(false))
    {
        juce::ScopedLock sl(stateLock);
        killAllSound(out);
        voices.clear();
        arpPool.clear(); arpPlayedOrder.clear(); arpRefCount.clear();
        sustainedNotes.clear(); deferredOffs.clear();
        arpImmediatePending = false;
        if (arpCurrentNote >= 0) arpCurrentNote = -1;
        nextPpq = -1.0;
        activeVoices.store(0);
        midi.swapWith(out);
        return;
    }

    // --- Read params (audio thread safe via atomic loads) ---
    int root = currentRoot();
    int snapMode = juce::jlimit(0, 2, (int) apvts.getRawParameterValue("snap")->load());
    bool scaleLock = apvts.getRawParameterValue("scaleLock")->load() > 0.5f;
    bool whiteKeys = apvts.getRawParameterValue("whiteKeys")->load() > 0.5f;
    bool arpOn = apvts.getRawParameterValue("arpOn")->load() > 0.5f;
    bool arpLatch = apvts.getRawParameterValue("arpLatch")->load() > 0.5f;
    float strumMs = apvts.getRawParameterValue("strum")->load();
    float gatePct = apvts.getRawParameterValue("gate")->load();
    float swingPct = apvts.getRawParameterValue("swing")->load();
    auto intervals = currentIntervals();

    static bool prevLatch = false;
    if (! arpLatch && prevLatch)
    {
        // latch released: clear latched pool
        juce::ScopedLock sl(stateLock);
        arpPool.clear(); arpPlayedOrder.clear(); arpRefCount.clear(); voices.clear();
        arpImmediatePending = false;
        if (arpCurrentNote >= 0)
        {
            noteOffOut(out, 0, arpCurrentNote, arpCurrentChannel);
            arpCurrentNote = -1;
        }
        nextPpq = -1.0;
    }
    prevLatch = arpLatch;

    // --- Transport: DAW tempo only. When stopped, keep the last seen tempo
    // so the arp can free-run; there is no manual BPM (this is a plugin).
    double hostBpm = lastHostBpm.load();
    bool playing = false;
    double ppqPos = 0.0;
    bool hasPpq = false;
    if (auto* head = getPlayHead())
    {
        if (auto pos = head->getPosition())
        {
            if (auto b = pos->getBpm()) { hostBpm = *b; lastHostBpm.store(hostBpm); }
            playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ppqPos = *q; hasPpq = true; }
        }
    }
    hostPlaying.store(playing);

    // --- Audition queue (from editor keyboard) ---
    {
        juce::ScopedLock sl(stateLock);
        for (auto& a : auditionQueue)
        {
            if (a.on)
            {
                int snapped = a.note;
                if (scaleLock)
                    snapped = whiteKeys ? Scales::degreeMapNote(a.note, root, intervals)
                                        : Scales::snapNote(a.note, root, intervals, snapMode);
                std::vector<int> chord; int deg = -1;
                // reuse build (reads atomics, fine)
                // inline simplified: build without lock recursion issues
                {
                    juce::ScopedUnlock ul(stateLock);
                    buildChordForSnapped(snapped, chord, deg);
                }
                float vel = juce::jlimit(0.0f, 1.0f, a.vel / 127.0f);
                if (arpOn)
                {
                    bool wasEmpty = arpPool.empty();
                    for (int n : chord)
                    {
                        if (arpRefCount[n]++ == 0)
                        {
                            arpPool.push_back(n);
                            std::sort(arpPool.begin(), arpPool.end());
                            arpPlayedOrder.push_back(n);
                        }
                    }
                    arpLastVel = vel;
                    voices[a.note] = { snapped, chord };
                    if (wasEmpty)
                    {
                        resetArpPosition();
                        arpImmediatePending = true; arpImmediatePos = 0; arpImmediateCh = 1;
                    }
                }
                else
                {
                    int i = 0;
                    for (int n : chord)
                    {
                        int off = (int) (i * strumMs * currentSampleRate / 1000.0);
                        noteOnOut(out, juce::jmin(off, juce::jmax(0, numSamples - 1)), n, 1, vel);
                        ++i;
                    }
                    voices[a.note] = { snapped, chord };
                }
                lastInputNote.store(a.note); lastSnappedNote.store(snapped);
            }
            else
            {
                auto it = voices.find(a.note);
                if (it != voices.end())
                {
                    if (arpOn)
                    {
                        if (! arpLatch && ! sustainDown)
                            for (int n : it->second.emitted)
                            {
                                auto rc = arpRefCount.find(n);
                                if (rc != arpRefCount.end() && --rc->second <= 0)
                                {
                                    arpRefCount.erase(rc);
                                    arpPool.erase(std::remove(arpPool.begin(), arpPool.end(), n), arpPool.end());
                                    arpPlayedOrder.erase(std::remove(arpPlayedOrder.begin(), arpPlayedOrder.end(), n), arpPlayedOrder.end());
                                }
                            }
                        voices.erase(it);
                    }
                    else
                    {
                        for (int n : it->second.emitted) noteOffOut(out, 0, n, 1);
                        voices.erase(it);
                    }
                }
            }
        }
        auditionQueue.clear();
        activeVoices.store((int) voices.size() + (arpOn && arpCurrentNote >= 0 ? 1 : 0));
    }

    // --- Incoming MIDI ---
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        int pos = meta.samplePosition;

        if (msg.isNoteOn())
        {
            int inNote = msg.getNoteNumber();
            int ch = msg.getChannel();
            float vel = msg.getFloatVelocity();

            int snapped = inNote;
            if (scaleLock)
                snapped = whiteKeys ? Scales::degreeMapNote(inNote, root, intervals)
                                    : Scales::snapNote(inNote, root, intervals, snapMode);
            std::vector<int> chord; int deg = -1;
            buildChordForSnapped(snapped, chord, deg);

            juce::ScopedLock sl(stateLock);
            // retrigger same input note: release old first
            auto old = voices.find(inNote);
            if (old != voices.end())
            {
                if (! arpOn)
                    for (int n : old->second.emitted)
                    {
                        if (sustainDown) deferredOffs.emplace_back(n, ch);
                        else noteOffOut(out, pos, n, ch);
                    }
                voices.erase(old);
            }

            if (arpOn)
            {
                bool wasEmpty = arpPool.empty();
                for (int n : chord)
                {
                    if (arpRefCount[n]++ == 0)
                    {
                        arpPool.push_back(n);
                        std::sort(arpPool.begin(), arpPool.end());
                        arpPlayedOrder.push_back(n);
                    }
                }
                arpLastVel = vel;
                if (wasEmpty)
                {
                    // Fire the first arp step right on this note-on: no trigger latency.
                    resetArpPosition();
                    arpImmediatePending = true; arpImmediatePos = pos; arpImmediateCh = ch;
                }
            }
            else
            {
                int i = 0;
                for (int n : chord)
                {
                    int off = pos + (int) (i * strumMs * currentSampleRate / 1000.0);
                    off = juce::jlimit(0, juce::jmax(0, numSamples - 1), off);
                    noteOnOut(out, off, n, ch, vel);
                    ++i;
                }
            }
            voices[inNote] = { snapped, chord };
            lastInputNote.store(inNote); lastSnappedNote.store(snapped);
        }
        else if (msg.isNoteOff())
        {
            int inNote = msg.getNoteNumber();
            int ch = msg.getChannel();
            juce::ScopedLock sl(stateLock);
            auto it = voices.find(inNote);
            if (it != voices.end())
            {
                if (arpOn)
                {
                    if (arpLatch || sustainDown)
                    {
                        if (sustainDown)
                            deferredOffs.emplace_back(-1000 - inNote, ch); // marker: pool removal deferred
                    }
                    else
                    {
                        for (int n : it->second.emitted)
                        {
                            auto rc = arpRefCount.find(n);
                            if (rc != arpRefCount.end() && --rc->second <= 0)
                            {
                                arpRefCount.erase(rc);
                                arpPool.erase(std::remove(arpPool.begin(), arpPool.end(), n), arpPool.end());
                                arpPlayedOrder.erase(std::remove(arpPlayedOrder.begin(), arpPlayedOrder.end(), n), arpPlayedOrder.end());
                            }
                        }
                        voices.erase(it);
                        if (arpPool.empty() && arpCurrentNote >= 0)
                        {
                            noteOffOut(out, pos, arpCurrentNote, arpCurrentChannel);
                            arpCurrentNote = -1;
                            nextPpq = -1.0;
                        }
                    }
                    if (arpLatch) voices.erase(it); // latch keeps pool, forget voice mapping
                    else if (sustainDown) { /* keep voice until sustain up */ }
                }
                else
                {
                    if (sustainDown)
                    {
                        for (int n : it->second.emitted) deferredOffs.emplace_back(n, ch);
                    }
                    else
                    {
                        for (int n : it->second.emitted) noteOffOut(out, pos, n, ch);
                        voices.erase(it);
                    }
                }
            }
        }
        else if (msg.isSustainPedalOn())
        {
            juce::ScopedLock sl(stateLock);
            sustainDown = true;
        }
        else if (msg.isSustainPedalOff())
        {
            juce::ScopedLock sl(stateLock);
            sustainDown = false;
            for (auto& d : deferredOffs)
            {
                if (d.first <= -1000) // deferred pool removal, input note = -1000 - first
                {
                    int inNote = -1000 - d.first;
                    auto it = voices.find(inNote);
                    if (it != voices.end() && ! arpLatch)
                    {
                        for (int n : it->second.emitted)
                        {
                            auto rc = arpRefCount.find(n);
                            if (rc != arpRefCount.end() && --rc->second <= 0)
                            {
                                arpRefCount.erase(rc);
                                arpPool.erase(std::remove(arpPool.begin(), arpPool.end(), n), arpPool.end());
                                arpPlayedOrder.erase(std::remove(arpPlayedOrder.begin(), arpPlayedOrder.end(), n), arpPlayedOrder.end());
                            }
                        }
                        voices.erase(it);
                    }
                }
                else noteOffOut(out, pos, d.first, d.second);
            }
            deferredOffs.clear();
            if (arpOn && arpPool.empty() && arpCurrentNote >= 0)
            {
                noteOffOut(out, pos, arpCurrentNote, arpCurrentChannel);
                arpCurrentNote = -1;
                nextPpq = -1.0;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            juce::ScopedLock sl(stateLock);
            killAllSound(out);
            voices.clear(); arpPool.clear(); arpPlayedOrder.clear(); arpRefCount.clear();
            if (arpCurrentNote >= 0) arpCurrentNote = -1;
            nextPpq = -1.0;
        }
        else if (msg.isMidiClock() || msg.isMidiStart() || msg.isMidiStop() || msg.isMidiContinue())
        {
            // loop guard: never forward clock/transport
        }
        else
        {
            // thru: CC, pitchbend, aftertouch, program change
            out.addEvent(msg, pos);
        }
    }

    // --- Arp tick generation ---
    {
        juce::ScopedLock sl(stateLock);
        if (arpOn && ! arpPool.empty())
        {
            float intervalBeats = getRateBeats();
            float swingFrac = juce::jlimit(0.0f, 60.0f, swingPct) / 100.0f;
            float gateFrac = juce::jlimit(0.1f, 1.0f, gatePct / 100.0f);
            double freeBpm = juce::jmax(20.0, lastHostBpm.load());

            // First step fires immediately on the triggering note-on: zero trigger latency.
            if (arpImmediatePending)
            {
                arpImmediatePending = false;
                if (arpCurrentNote >= 0)
                    noteOffOut(out, arpImmediatePos, arpCurrentNote, arpCurrentChannel);
                int n = pickArpNote();
                if (n >= 0)
                {
                    noteOnOut(out, arpImmediatePos, n, arpImmediateCh, arpLastVel);
                    arpCurrentNote = n;
                    arpCurrentChannel = arpImmediateCh;
                }
                ++arpStepIndex;
                if (playing && hasPpq)
                    nextPpq = ppqPos + intervalBeats;
                else
                    samplesToNextTick = 60.0 / freeBpm * intervalBeats * currentSampleRate;
            }

            if (playing && hasPpq)
            {
                double samplesPerBeat = 60.0 / juce::jmax(20.0, hostBpm) * currentSampleRate;
                double samplesPerPpq = samplesPerBeat; // 1 ppq beat = 1 quarter
                double blockSpanPpq = numSamples / samplesPerPpq;
                if (nextPpq < 0)
                {
                    nextPpq = std::ceil(ppqPos / intervalBeats) * intervalBeats;
                    if (nextPpq < ppqPos) nextPpq = ppqPos;
                    resetArpPosition();
                }
                // drop stale scheduler far behind
                if (nextPpq < ppqPos - intervalBeats * 4)
                {
                    nextPpq = std::ceil(ppqPos / intervalBeats) * intervalBeats;
                }
                int guard = 0;
                while (nextPpq < ppqPos + blockSpanPpq && guard++ < 32)
                {
                    double deltaPpq = nextPpq - ppqPos;
                    int sampleOff = juce::jlimit(0, numSamples - 1, (int) (deltaPpq * samplesPerPpq));
                    // swing on odd steps
                    if ((arpStepIndex % 2) == 1)
                        sampleOff = juce::jlimit(0, numSamples - 1,
                            sampleOff + (int) (swingFrac * intervalBeats * samplesPerPpq * 0.5));

                    if (arpCurrentNote >= 0)
                        noteOffOut(out, sampleOff, arpCurrentNote, arpCurrentChannel);
                    int n = pickArpNote();
                    if (n >= 0)
                    {
                        noteOnOut(out, sampleOff, n, arpCurrentChannel, arpLastVel);
                        arpCurrentNote = n;
                        // schedule internal gate-off (approx within same block if short)
                        double tickSamples = intervalBeats * samplesPerPpq;
                        int offAt = sampleOff + (int) (tickSamples * gateFrac);
                        if (offAt < numSamples)
                        {
                            // keep sounding until next tick; store pending cut via next tick's note-off.
                            // For gates < 100% with long intervals, cut early:
                            if (gateFrac < 0.98f)
                            {
                                // emit note-off early but keep arpCurrentNote so next tick is clean;
                                // simplest: emit the off now-scheduled and mark still sounding
                                // (host will treat extra note-off at next tick harmlessly)
                                juce::MidiBuffer cut;
                                cut.addEvent(juce::MidiMessage::noteOff(arpCurrentChannel, n), offAt);
                                // merge: shift into out (out already has events; add with offset)
                                for (auto m : cut) out.addEvent(m.getMessage(), m.samplePosition);
                            }
                        }
                    }
                    ++arpStepIndex;
                    nextPpq += intervalBeats;
                }
            }
            else if (! playing)
            {
                // Free-run on the last DAW tempo while the transport is stopped.
                double tickSamples = 60.0 / freeBpm * intervalBeats * currentSampleRate;
                int guard = 0;
                if (nextPpq < -0.5 && samplesToNextTick <= 0.0 && ! arpImmediatePending)
                {
                    // pool became active with no triggering note in this block:
                    // fire immediately at block start
                    if (arpCurrentNote >= 0) noteOffOut(out, 0, arpCurrentNote, arpCurrentChannel);
                    int n = pickArpNote();
                    if (n >= 0) { noteOnOut(out, 0, n, arpCurrentChannel, arpLastVel); arpCurrentNote = n; }
                    ++arpStepIndex;
                    samplesToNextTick = tickSamples;
                    if ((arpStepIndex % 2) == 0) samplesToNextTick += swingFrac * tickSamples * 0.5;
                    nextPpq = 0.0; // mark active
                }
                samplesToNextTick -= numSamples;
                while (samplesToNextTick <= 0.0 && guard++ < 16)
                {
                    int sampleOff = juce::jlimit(0, numSamples - 1, numSamples + (int) samplesToNextTick);
                    if ((arpStepIndex % 2) == 1)
                        sampleOff = juce::jlimit(0, numSamples - 1, sampleOff + (int) (swingFrac * tickSamples * 0.5));
                    if (arpCurrentNote >= 0) noteOffOut(out, sampleOff, arpCurrentNote, arpCurrentChannel);
                    int n = pickArpNote();
                    if (n >= 0) { noteOnOut(out, sampleOff, n, arpCurrentChannel, arpLastVel); arpCurrentNote = n; }
                    ++arpStepIndex;
                    samplesToNextTick += tickSamples;
                }
            }
        }
        else
        {
            arpImmediatePending = false;
            if (arpCurrentNote >= 0)
            {
                noteOffOut(out, 0, arpCurrentNote, arpCurrentChannel);
                arpCurrentNote = -1;
            }
            nextPpq = -1.0;
        }
        activeVoices.store((int) voices.size() + (arpCurrentNote >= 0 ? 1 : 0));
    }

    midi.swapWith(out);
}

void MidiNoteModifierProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MidiNoteModifierProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml && xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        loadCustomChordTypesFromState();
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MidiNoteModifierProcessor(); }
