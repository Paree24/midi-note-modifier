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

void MidiNoteModifierProcessor::prepareToPlay(double sampleRate, int)
{
    juce::ScopedLock sl(stateLock);
    currentSampleRate = sampleRate;
    pendingOns.reserve(256);
}

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
    juce::ScopedLock sl(chordTypeLock);
    for (int s = 0; s < kNumCustomChords; ++s)
    {
        if (! node.isValid()) { customChordNames[s] = ""; customChordIvs[s] = { 0 }; continue; }
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
            else if (powerDefault) rel = Scales::forcedChordIntervals(7);   // power-chord default
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
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
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

void MidiNoteModifierProcessor::resetAllNoteState()
{
    voices.clear();
    clearArpPool();
    sustainedNotes.clear(); deferredOffs.clear();
    pendingOns.clear();
    for (auto& ch : outRefCount) std::fill(std::begin(ch), std::end(ch), 0);
    arpImmediatePending = false;
    arpCurrentNote = -1;
    gateOffIn = -1;
    nextPpq = -1.0;
    activeVoices.store(0);
}

void MidiNoteModifierProcessor::voiceNoteOn(juce::MidiBuffer& out, int pos, int note, int ch, float vel)
{
    ++outRefCount[(ch - 1) & 15][note & 127];
    noteOnOut(out, pos, note, ch, vel);
}

void MidiNoteModifierProcessor::voiceNoteOff(juce::MidiBuffer& out, int pos, int note, int ch)
{
    auto& rc = outRefCount[(ch - 1) & 15][note & 127];
    if (rc > 0) --rc;
    if (rc == 0) noteOffOut(out, pos, note, ch);
}

// Release one plain voice note: a strummed note that hasn't sounded yet is
// simply cancelled, otherwise its note-off is sent (ref-counted).
void MidiNoteModifierProcessor::releaseVoiceNote(juce::MidiBuffer& out, int pos, int note, int ch)
{
    for (auto it = pendingOns.begin(); it != pendingOns.end(); ++it)
        if (it->note == note && it->channel == ch)
        {
            pendingOns.erase(it);
            return;
        }
    voiceNoteOff(out, pos, note, ch);
}

void MidiNoteModifierProcessor::addToArpPool(const std::vector<int>& notes)
{
    for (int n : notes)
        if (arpRefCount[n]++ == 0)
        {
            arpPool.insert(std::lower_bound(arpPool.begin(), arpPool.end(), n), n);
            arpPlayedOrder.push_back(n);
        }
}

void MidiNoteModifierProcessor::removeFromArpPool(const std::vector<int>& notes)
{
    for (int n : notes)
    {
        auto rc = arpRefCount.find(n);
        if (rc != arpRefCount.end() && --rc->second <= 0)
        {
            arpRefCount.erase(rc);
            arpPool.erase(std::remove(arpPool.begin(), arpPool.end(), n), arpPool.end());
            arpPlayedOrder.erase(std::remove(arpPlayedOrder.begin(), arpPlayedOrder.end(), n), arpPlayedOrder.end());
        }
    }
}

void MidiNoteModifierProcessor::clearArpPool()
{
    arpPool.clear(); arpPlayedOrder.clear(); arpRefCount.clear();
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
        resetAllNoteState();
        auditionQueue.clear();
        sampleClock += numSamples;
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

    {
        juce::ScopedLock sl(stateLock);

        // Arp switched on/off while notes are held: hand the held notes over
        // instead of leaving them stuck in the other mode's bookkeeping.
        if (arpOn != prevArpOn)
        {
            if (arpOn)
            {
                for (auto& d : deferredOffs)
                    if (d.first >= 0) releaseVoiceNote(out, 0, d.first, d.second);
                deferredOffs.clear();
                clearArpPool();
                for (auto& [inNote, v] : voices)
                {
                    for (int n : v.emitted) releaseVoiceNote(out, 0, n, v.channel);
                    addToArpPool(v.emitted);
                }
                if (! arpPool.empty())
                {
                    resetArpPosition();
                    arpImmediatePending = true; arpImmediatePos = 0;
                    arpImmediateCh = voices.begin()->second.channel;
                }
            }
            else
            {
                // held keys go silent until re-pressed; the arp note is stopped below
                clearArpPool();
                voices.clear();
                deferredOffs.clear();
            }
            prevArpOn = arpOn;
        }

        // Latch released: keep arpeggiating only the keys still held.
        if (! arpLatch && prevLatch && arpOn)
        {
            clearArpPool();
            for (auto& [inNote, v] : voices) addToArpPool(v.emitted);
        }
        prevLatch = arpLatch;

        // Editor keyboard auditions are fed through the normal MIDI path (channel 1).
        for (auto& a : auditionQueue)
        {
            int note = juce::jlimit(0, 127, a.note);
            midi.addEvent(a.on ? juce::MidiMessage::noteOn(1, note, (juce::uint8) juce::jlimit(1, 127, a.vel))
                               : juce::MidiMessage::noteOff(1, note), 0);
        }
        auditionQueue.clear();
    }

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

    const int strumStep = (int) (strumMs * currentSampleRate / 1000.0);

    // --- Incoming MIDI ---
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        int pos = juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition);

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
            bool poolWasEmpty = arpPool.empty();

            // retrigger same input note: release old first
            auto old = voices.find(inNote);
            if (old != voices.end())
            {
                if (arpOn)
                {
                    removeFromArpPool(old->second.emitted);
                    // drop any pool removal still waiting on the sustain pedal
                    deferredOffs.erase(std::remove(deferredOffs.begin(), deferredOffs.end(),
                                                   std::make_pair(-1000 - inNote, old->second.channel)),
                                       deferredOffs.end());
                }
                else
                    for (int n : old->second.emitted)
                    {
                        if (sustainDown) deferredOffs.emplace_back(n, old->second.channel);
                        else releaseVoiceNote(out, pos, n, old->second.channel);
                    }
                voices.erase(old);
            }

            if (arpOn)
            {
                // latch: a new chord after all keys were released replaces the latched one
                if (arpLatch && voices.empty() && ! poolWasEmpty)
                {
                    clearArpPool();
                    resetArpPosition();
                }
                addToArpPool(chord);
                arpLastVel = vel;
                if (poolWasEmpty)
                {
                    // Fire the first arp step right on this note-on: no trigger latency.
                    resetArpPosition();
                    arpImmediatePending = true; arpImmediatePos = pos; arpImmediateCh = ch;
                }
            }
            else
            {
                for (size_t i = 0; i < chord.size(); ++i)
                {
                    int off = (int) i * strumStep;
                    if (off == 0) voiceNoteOn(out, pos, chord[i], ch, vel);
                    else pendingOns.push_back({ sampleClock + pos + off, chord[i], ch, vel });
                }
            }
            voices[inNote] = { snapped, ch, chord };
            lastInputNote.store(inNote); lastSnappedNote.store(snapped);
        }
        else if (msg.isNoteOff())
        {
            int inNote = msg.getNoteNumber();
            juce::ScopedLock sl(stateLock);
            auto it = voices.find(inNote);
            if (it == voices.end()) continue;
            const int vch = it->second.channel;

            if (arpOn)
            {
                if (arpLatch)
                    voices.erase(it); // latch keeps the pool, forget the key
                else if (sustainDown)
                {
                    auto marker = std::make_pair(-1000 - inNote, vch); // pool removal deferred
                    if (std::find(deferredOffs.begin(), deferredOffs.end(), marker) == deferredOffs.end())
                        deferredOffs.push_back(marker);
                }
                else
                {
                    removeFromArpPool(it->second.emitted);
                    voices.erase(it);
                    if (arpPool.empty() && arpCurrentNote >= 0)
                    {
                        noteOffOut(out, pos, arpCurrentNote, arpCurrentChannel);
                        arpCurrentNote = -1;
                        gateOffIn = -1;
                        nextPpq = -1.0;
                    }
                }
            }
            else
            {
                for (int n : it->second.emitted)
                {
                    if (sustainDown) deferredOffs.emplace_back(n, vch);
                    else releaseVoiceNote(out, pos, n, vch);
                }
                voices.erase(it);
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
                    auto it = voices.find(-1000 - d.first);
                    if (it != voices.end())
                    {
                        if (! arpLatch) removeFromArpPool(it->second.emitted);
                        voices.erase(it);
                    }
                }
                else releaseVoiceNote(out, pos, d.first, d.second);
            }
            deferredOffs.clear();
            if (arpOn && arpPool.empty() && arpCurrentNote >= 0)
            {
                noteOffOut(out, pos, arpCurrentNote, arpCurrentChannel);
                arpCurrentNote = -1;
                gateOffIn = -1;
                nextPpq = -1.0;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            juce::ScopedLock sl(stateLock);
            killAllSound(out);
            resetAllNoteState();
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
            const double intervalBeats = getRateBeats();
            const double swingFrac = juce::jlimit(0.0f, 60.0f, swingPct) / 100.0;
            const double gateFrac = juce::jlimit(0.1f, 1.0f, gatePct / 100.0f);
            const bool synced = playing && hasPpq;
            const double bpm = juce::jmax(20.0, synced ? hostBpm : lastHostBpm.load());
            const double samplesPerBeat = 60.0 / bpm * currentSampleRate;
            const double tickSamples = intervalBeats * samplesPerBeat;
            // odd steps are delayed by the swing amount
            auto swingDelayBeats = [&] { return (arpStepIndex % 2) == 1 ? swingFrac * intervalBeats * 0.5 : 0.0; };

            // One arp step: stop the previous note (at its gate end if that came
            // first), start the next, and schedule its gate-off, possibly in a
            // later block. stepLen is the time until the following step.
            auto fireStep = [&](int sampleOff, int ch, double stepLen)
            {
                if (arpCurrentNote >= 0)
                {
                    int offPos = (gateOffIn >= 0 && gateOffIn <= sampleOff) ? gateOffIn : sampleOff;
                    noteOffOut(out, offPos, arpCurrentNote, arpCurrentChannel);
                }
                arpCurrentNote = -1;
                gateOffIn = -1;
                int n = pickArpNote();
                if (n >= 0)
                {
                    noteOnOut(out, sampleOff, n, ch, arpLastVel);
                    arpCurrentNote = n;
                    arpCurrentChannel = ch;
                    if (gateFrac < 0.98)
                        gateOffIn = sampleOff + juce::jmax(1, (int) (stepLen * gateFrac));
                }
                ++arpStepIndex;
            };

            if (synced != arpWasSynced)
            {
                // transport started: realign to the host grid; stopped: free-run on
                if (synced) nextPpq = -1.0;
                else samplesToNextTick = tickSamples;
                arpWasSynced = synced;
            }

            // First step fires immediately on the triggering note-on: zero trigger latency.
            if (arpImmediatePending)
            {
                arpImmediatePending = false;
                fireStep(arpImmediatePos, arpImmediateCh, tickSamples);
                if (synced) nextPpq = ppqPos + arpImmediatePos / samplesPerBeat + intervalBeats;
                else samplesToNextTick = arpImmediatePos + tickSamples;
            }

            int guard = 0;
            if (synced)
            {
                const double blockEndPpq = ppqPos + numSamples / samplesPerBeat;
                // first run, loop/jump backwards, or a stale schedule far behind: snap to the grid
                if (nextPpq < 0.0 || nextPpq < ppqPos - intervalBeats * 0.5
                    || nextPpq > blockEndPpq + intervalBeats)
                    nextPpq = std::ceil(ppqPos / intervalBeats - 1.0e-9) * intervalBeats;

                while (guard++ < 64)
                {
                    double swing = swingDelayBeats();
                    double at = nextPpq + swing;
                    if (at >= blockEndPpq) break;
                    int sampleOff = juce::jlimit(0, numSamples - 1, (int) ((at - ppqPos) * samplesPerBeat));
                    fireStep(sampleOff, arpCurrentChannel, (intervalBeats - swing) * samplesPerBeat);
                    nextPpq += intervalBeats;
                }
            }
            else
            {
                // Free-run on the last DAW tempo while the transport is stopped.
                if (samplesToNextTick < 0.0) samplesToNextTick = 0.0;
                while (guard++ < 64)
                {
                    double swing = swingDelayBeats() * samplesPerBeat;
                    double at = samplesToNextTick + swing;
                    if (at >= numSamples) break;
                    fireStep(juce::jlimit(0, numSamples - 1, (int) at), arpCurrentChannel, tickSamples - swing);
                    samplesToNextTick += tickSamples;
                }
                samplesToNextTick -= numSamples;
            }

            // gate-off due in this block, or carry it into the next one
            if (gateOffIn >= 0 && arpCurrentNote >= 0)
            {
                if (gateOffIn < numSamples)
                {
                    noteOffOut(out, gateOffIn, arpCurrentNote, arpCurrentChannel);
                    arpCurrentNote = -1;
                    gateOffIn = -1;
                }
                else gateOffIn -= numSamples;
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
            gateOffIn = -1;
            nextPpq = -1.0;
        }

        // strummed chord notes that are due within this block
        const auto blockEnd = sampleClock + numSamples;
        for (auto it = pendingOns.begin(); it != pendingOns.end();)
        {
            if (it->time < blockEnd)
            {
                voiceNoteOn(out, (int) juce::jmax((juce::int64) 0, it->time - sampleClock),
                            it->note, it->channel, it->vel);
                it = pendingOns.erase(it);
            }
            else ++it;
        }
        sampleClock = blockEnd;

        activeVoices.store((int) voices.size() + (arpOn && arpCurrentNote >= 0 ? 1 : 0));
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
