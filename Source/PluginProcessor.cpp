#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Theme.h"
#include "SetupPresets.h"
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
    : AudioProcessor(BusesProperties()), apvts(*this, nullptr, "Params", createLayout()),
      undoHistory(*this, [this] { return captureUndoState(); },
                  [this](const juce::ValueTree& v) { restoreUndoState(v); })
{
    for (int s = 0; s < kNumCustomChords; ++s)
    {
        customChordNames[s] = "";
        customChordIvs[s] = { 0 };
        customChordPacked[s].store(packIntervals({ 0 }));
    }
    auto raw = [this](const juce::String& id) { return apvts.getRawParameterValue(id); };
    pRoot = raw("root"); pScale = raw("scale"); pSnap = raw("snap");
    pScaleLock = raw("scaleLock"); pWhiteKeys = raw("whiteKeys"); pChordOn = raw("chordOn");
    pInversion = raw("inversion"); pInvLower = raw("invLower"); pBassBelow = raw("bassBelow");
    pStrum = raw("strum"); pArpOn = raw("arpOn"); pArpLatch = raw("arpLatch");
    pArpMode = raw("arpMode"); pArpRate = raw("arpRate"); pArpOct = raw("arpOct");
    pSwing = raw("swing"); pGate = raw("gate");
    for (int i = 0; i < 12; ++i) pPc[i] = raw("pc" + juce::String(i));
    for (int i = 0; i < kNumChordSlots; ++i) pChd[i] = raw("chd" + juce::String(i));
    rtIntervals.reserve(12);
    outBuffer.ensureSize(4096);
    // fresh instances start on the user's global default theme
    // (a host state recall later overrides this per instance)
    if (auto* p = apvts.getParameter("theme"))
    {
        p->setValueNotifyingHost(p->convertTo0to1(loadDefaultTheme()));
        undoHistory.ignoreParameter(p->getParameterIndex()); // colour scheme isn't an edit
    }
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
    currentSampleRate = sampleRate;
    outBuffer.ensureSize(4096);
}

bool MidiNoteModifierProcessor::isBusesLayoutSupported(const BusesLayout&) const { return true; }

std::vector<int> MidiNoteModifierProcessor::currentIntervals()
{
    int scaleIdx = (int) pScale->load();
    auto& facts = Scales::factoryScales();
    if (scaleIdx < 0) scaleIdx = 0;
    if (scaleIdx >= (int) facts.size()) scaleIdx = 0;
    if (scaleIdx == Scales::customIndex())
    {
        std::vector<int> iv;
        for (int i = 0; i < 12; ++i)
            if (pPc[i]->load() > 0.5f)
                iv.push_back(i);
        if (iv.empty()) iv.push_back(0);
        return iv;
    }
    return facts[(size_t) scaleIdx].intervals;
}

// Audio-thread version: factory scales by reference, the Custom pattern
// rebuilt into reserved storage.
const std::vector<int>& MidiNoteModifierProcessor::currentIntervalsRT()
{
    int scaleIdx = (int) pScale->load();
    auto& facts = Scales::factoryScales();
    if (scaleIdx < 0 || scaleIdx >= (int) facts.size()) scaleIdx = 0;
    if (scaleIdx != Scales::customIndex())
        return facts[(size_t) scaleIdx].intervals;
    rtIntervals.clear();
    for (int i = 0; i < 12; ++i)
        if (pPc[i]->load() > 0.5f)
            rtIntervals.push_back(i);
    if (rtIntervals.empty()) rtIntervals.push_back(0);
    return rtIntervals;
}

int MidiNoteModifierProcessor::currentRoot()
{
    return juce::jlimit(0, 11, (int) pRoot->load());
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

bool MidiNoteModifierProcessor::isReservedScaleName(const juce::String& name)
{
    // factory scale names (incl. "Custom") can't be reused for user scales:
    // the scale menu resolves selections by name
    for (auto& s : Scales::factoryScales())
        if (name.trim().equalsIgnoreCase(juce::String(s.name)))
            return true;
    return false;
}

bool MidiNoteModifierProcessor::isUserScale(const juce::String& name) const
{
    auto user = apvts.state.getChildWithName("USER_SCALES");
    for (int i = 0; i < user.getNumChildren(); ++i)
        if (user.getChild(i).getProperty("name").toString() == name)
            return true;
    return false;
}

bool MidiNoteModifierProcessor::saveCustomScale(const juce::String& name)
{
    if (name.isEmpty() || isReservedScaleName(name)) return false;
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

// 6 intervals (0..24) in 5 bits each plus a 4-bit count: one lock-free word per slot.
juce::uint64 MidiNoteModifierProcessor::packIntervals(const std::vector<int>& ivs)
{
    juce::uint64 v = 0;
    int n = juce::jmin((int) ivs.size(), kMaxChordNotes);
    for (int k = 0; k < n; ++k)
        v |= (juce::uint64) (juce::jlimit(0, 24, ivs[(size_t) k]) & 31) << (4 + 5 * k);
    return v | (juce::uint64) n;
}

int MidiNoteModifierProcessor::unpackIntervals(juce::uint64 packed, int* out)
{
    int n = juce::jmin((int) (packed & 15), kMaxChordNotes);
    for (int k = 0; k < n; ++k)
        out[k] = (int) ((packed >> (4 + 5 * k)) & 31);
    return n;
}

void MidiNoteModifierProcessor::publishCustomChord(int slot)
{
    customChordPacked[slot].store(packIntervals(customChordIvs[slot]));
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
        publishCustomChord(slot);
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
        publishCustomChord(slot);
    }
    storeCustomChordTypesToState();
    // any degree using this slot falls back to Auto
    for (int i = 0; i < kNumChordSlots; ++i)
    {
        int idx = (int) pChd[i]->load();
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
        if (! node.isValid()) { customChordNames[s] = ""; customChordIvs[s] = { 0 }; publishCustomChord(s); continue; }
        customChordNames[s] = node.getProperty("name" + juce::String(s), "").toString()
                                  .substring(0, 24);
        customChordIvs[s] = stringToIvs(node.getProperty("iv" + juce::String(s), "0").toString());
        publishCustomChord(s);
    }
}

juce::ValueTree MidiNoteModifierProcessor::captureUndoState()
{
    return apvts.copyState();
}

// Undo/redo restores scale/chord/arp settings, user scales and custom chord
// types; the colour theme and the keyboard octave stay as they are.
void MidiNoteModifierProcessor::restoreUndoState(const juce::ValueTree& snapshot)
{
    auto v = snapshot.createCopy();
    for (int i = 0; i < v.getNumChildren(); ++i)
    {
        auto c = v.getChild(i);
        if (c.hasType("PARAM") && c.getProperty("id").toString() == "theme")
            c.setProperty("value", apvts.getRawParameterValue("theme")->load(), nullptr);
    }
    apvts.replaceState(v);
    loadCustomChordTypesFromState();
    ++stateVersion;
}

juce::ValueTree MidiNoteModifierProcessor::captureSetup()
{
    juce::ValueTree t(SetupPresets::kRootType);
    t.setProperty("version", 1, nullptr);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            if (rp->getParameterID() == "theme") continue;
            juce::ValueTree c("PARAM");
            c.setProperty("id", rp->getParameterID(), nullptr);
            c.setProperty("value", rp->convertFrom0to1(rp->getValue()), nullptr);
            t.appendChild(c, nullptr);
        }
    auto chords = apvts.state.getChildWithName("CUSTOM_CHORDS");
    if (chords.isValid()) t.appendChild(chords.createCopy(), nullptr);
    return t;
}

// Parameters missing from the setup load their defaults, so every setup is
// complete; the theme, user scales and keyboard octave are left alone.
void MidiNoteModifierProcessor::applySetup(const juce::ValueTree& setup, const juce::String& name)
{
    SnapshotUndo::Transaction undoStep(undoHistory);
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
        {
            if (rp->getParameterID() == "theme") continue;
            auto c = setup.getChildWithProperty("id", rp->getParameterID());
            float norm = c.isValid() ? rp->convertTo0to1((float) c.getProperty("value"))
                                     : rp->getDefaultValue();
            if (std::abs(rp->getValue() - norm) > 1.0e-6f)
                rp->setValueNotifyingHost(norm);
        }
    auto old = apvts.state.getChildWithName("CUSTOM_CHORDS");
    if (old.isValid()) apvts.state.removeChild(old, nullptr);
    auto chords = setup.getChildWithName("CUSTOM_CHORDS");
    if (chords.isValid()) apvts.state.appendChild(chords.createCopy(), nullptr);
    loadCustomChordTypesFromState();
    apvts.state.setProperty("setupName", name, nullptr);
    ++stateVersion;
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
    const auto scope = auditionFifo.write(1);
    if (scope.blockSize1 > 0) auditionBuf[scope.startIndex1] = { on, midiNote, velocity };
    else if (scope.blockSize2 > 0) auditionBuf[scope.startIndex2] = { on, midiNote, velocity };
}

float MidiNoteModifierProcessor::getRateBeats() const
{
    int r = (int) pArpRate->load();
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

int MidiNoteModifierProcessor::buildChordForSnapped(int snapped, int* out, int& count, int& degreeOut)
{
    int root = currentRoot();
    const auto& intervals = currentIntervalsRT();
    int scaleIdx = (int) pScale->load();
    auto& facts = Scales::factoryScales();
    bool powerDefault = scaleIdx >= 0 && scaleIdx < (int) facts.size()
                        && facts[(size_t) scaleIdx].powerChords;
    int deg = Scales::scaleDegree(snapped, root, intervals);
    degreeOut = deg;
    bool chordOn = pChordOn->load() > 0.5f;
    if (! chordOn) { out[0] = snapped; count = 1; return deg; }

    int rel[kMaxEmit];
    int nRel = 1;
    rel[0] = 0;
    if (deg >= 0)
    {
        int slot = deg % kNumChordSlots;
        int c = (int) pChd[slot]->load();
        if (c >= 14) // user-defined chord type (Custom 1..6)
        {
            nRel = unpackIntervals(customChordPacked[juce::jlimit(0, kNumCustomChords - 1, c - 14)].load(), rel);
            if (nRel == 0) { rel[0] = 0; nRel = 1; }
        }
        else
        {
            int forced = c - 1;
            if (forced == 0) nRel = 1;                                                // Single
            else if (forced > 0) nRel = Scales::forcedChordIntervals(forced, rel);   // explicit override
            else if (powerDefault) nRel = Scales::forcedChordIntervals(7, rel);      // power-chord default
            else nRel = Scales::diatonicTriad(deg, intervals, rel);                  // Auto diatonic
        }
    }

    count = 0;
    for (int k = 0; k < nRel; ++k)
    {
        int n = snapped + rel[k];
        if (n >= 0 && n <= 127) out[count++] = n;
    }
    int inv = (int) pInversion->load();
    bool lower = pInvLower->load() > 0.5f;
    bool bass = pBassBelow->load() > 0.5f;
    applyInversionBass(out, count, inv, lower, bass);
    std::sort(out, out + count);
    count = (int) (std::unique(out, out + count) - out);
    return deg;
}

// chord: sorted-or-not note array with room for kMaxEmit entries
void MidiNoteModifierProcessor::applyInversionBass(int* chord, int& count, int inversion,
                                                   bool lower, bool bassBelow)
{
    if (count > 1)
    {
        for (int k = 0; k < inversion && k < 2; ++k)
        {
            int low = chord[0];
            int up = low + 12;
            if (up > 127) break; // can't move the lowest note up: stop inverting
            std::rotate(chord, chord + 1, chord + count);
            chord[count - 1] = up;
        }
        std::sort(chord, chord + count);
    }
    if (lower && count > 0)
    {
        // same inversion shape, one octave down (e.g. E5 G5 C6 -> E4 G4 C5)
        int down[kMaxEmit];
        int nd = 0;
        for (int k = 0; k < count; ++k)
            if (chord[k] - 12 >= 0) down[nd++] = chord[k] - 12;
        if (nd > 0)
        {
            std::copy(down, down + nd, chord);
            count = nd;
        }
    }
    if (bassBelow && count > 0 && count < kMaxEmit)
    {
        int b = chord[0] - 12;
        // bass is the chord root an octave below (use lowest note as root ref)
        if (b >= 0)
        {
            std::copy_backward(chord, chord + count, chord + count + 1);
            chord[0] = b;
            ++count;
        }
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
    for (auto& v : voices) v.active = false;
    numVoices = 0;
    clearArpPool();
    numDeferred = 0;
    numPendingOns = 0;
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
    for (int i = 0; i < numPendingOns; ++i)
        if (pendingOns[i].note == note && pendingOns[i].channel == ch)
        {
            std::copy(pendingOns + i + 1, pendingOns + numPendingOns, pendingOns + i);
            --numPendingOns;
            return;
        }
    voiceNoteOff(out, pos, note, ch);
}

// Defer a note-off until the sustain pedal lifts (released right away if the
// fixed list is full, so nothing can hang).
void MidiNoteModifierProcessor::addDeferred(juce::MidiBuffer& out, int pos, int note, int ch)
{
    if (numDeferred < kMaxDeferred) deferredOffs[numDeferred++] = { note, ch };
    else releaseVoiceNote(out, pos, note, ch);
}

void MidiNoteModifierProcessor::removeVoice(int inNote)
{
    if (voices[inNote].active) { voices[inNote].active = false; --numVoices; }
}

int MidiNoteModifierProcessor::firstVoiceChannel() const
{
    for (auto& v : voices)
        if (v.active) return v.channel;
    return 1;
}

void MidiNoteModifierProcessor::addToArpPool(const int* notes, int count)
{
    for (int k = 0; k < count; ++k)
    {
        int n = notes[k] & 127;
        if (arpRefCount[n]++ == 0)
        {
            int at = (int) (std::lower_bound(arpPool, arpPool + arpPoolSize, n) - arpPool);
            std::copy_backward(arpPool + at, arpPool + arpPoolSize, arpPool + arpPoolSize + 1);
            arpPool[at] = n;
            ++arpPoolSize;
            arpPlayedOrder[arpPlayedSize++] = n;
        }
    }
}

void MidiNoteModifierProcessor::removeFromArpPool(const int* notes, int count)
{
    for (int k = 0; k < count; ++k)
    {
        int n = notes[k] & 127;
        if (arpRefCount[n] > 0 && --arpRefCount[n] <= 0)
        {
            arpRefCount[n] = 0;
            arpPoolSize = (int) (std::remove(arpPool, arpPool + arpPoolSize, n) - arpPool);
            arpPlayedSize = (int) (std::remove(arpPlayedOrder, arpPlayedOrder + arpPlayedSize, n) - arpPlayedOrder);
        }
    }
}

void MidiNoteModifierProcessor::clearArpPool()
{
    arpPoolSize = 0; arpPlayedSize = 0;
    std::fill(std::begin(arpRefCount), std::end(arpRefCount), 0);
}

int MidiNoteModifierProcessor::pickArpNote()
{
    if (arpPoolSize == 0) return -1;
    int mode = (int) pArpMode->load();
    int oct = juce::jlimit(1, 4, (int) pArpOct->load() + 1);

    // Played order uses the insertion order, the others the sorted pool;
    // both are expanded with octaves into fixed storage.
    const int* src = mode == 5 ? arpPlayedOrder : arpPool;
    const int srcSize = mode == 5 ? arpPlayedSize : arpPoolSize;
    int N = 0;
    for (int o = 0; o < oct; ++o)
        for (int i = 0; i < srcSize; ++i)
        {
            int v = src[i] + 12 * o;
            if (v <= 127) arpExpanded[N++] = v;
        }
    if (N == 0) return -1;
    const int* exp = arpExpanded;

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
        case 4: return exp[arpRandom.nextInt(N)]; // Random
        case 5: return exp[arpStepIndex % N];      // Played order expanded with octaves
        default: return exp[arpStepIndex % N];
    }
}

void MidiNoteModifierProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    buffer.clear();
    auto& out = outBuffer;
    out.clear();
    const int numSamples = buffer.getNumSamples();

    // --- Panic flag from UI ---
    if (panicFlag.exchange(false))
    {
        killAllSound(out);
        resetAllNoteState();
        { const auto drop = auditionFifo.read(auditionFifo.getNumReady()); } // discard queued auditions
        sampleClock += numSamples;
        midi.clear();
        midi.addEvents(out, 0, -1, 0);
        return;
    }

    // --- Read params (audio thread safe via atomic loads) ---
    int root = currentRoot();
    int snapMode = juce::jlimit(0, 2, (int) pSnap->load());
    bool scaleLock = pScaleLock->load() > 0.5f;
    bool whiteKeys = pWhiteKeys->load() > 0.5f;
    bool arpOn = pArpOn->load() > 0.5f;
    bool arpLatch = pArpLatch->load() > 0.5f;
    float strumMs = pStrum->load();
    float gatePct = pGate->load();
    float swingPct = pSwing->load();
    const auto& intervals = currentIntervalsRT();

    // Arp switched on/off while notes are held: hand the held notes over
    // instead of leaving them stuck in the other mode's bookkeeping.
    if (arpOn != prevArpOn)
    {
        if (arpOn)
        {
            for (int d = 0; d < numDeferred; ++d)
                if (deferredOffs[d].first >= 0) releaseVoiceNote(out, 0, deferredOffs[d].first, deferredOffs[d].second);
            numDeferred = 0;
            clearArpPool();
            for (auto& v : voices)
            {
                if (! v.active) continue;
                for (int k = 0; k < v.numEmitted; ++k) releaseVoiceNote(out, 0, v.emitted[k], v.channel);
                addToArpPool(v.emitted, v.numEmitted);
            }
            if (arpPoolSize > 0)
            {
                resetArpPosition();
                arpImmediatePending = true; arpImmediatePos = 0;
                arpImmediateCh = firstVoiceChannel();
            }
        }
        else
        {
            // held keys go silent until re-pressed; the arp note is stopped below
            clearArpPool();
            for (auto& v : voices) v.active = false;
            numVoices = 0;
            numDeferred = 0;
        }
        prevArpOn = arpOn;
    }

    // Latch released: keep arpeggiating only the keys still held.
    if (! arpLatch && prevLatch && arpOn)
    {
        clearArpPool();
        for (auto& v : voices)
            if (v.active) addToArpPool(v.emitted, v.numEmitted);
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

    const int strumStep = (int) (strumMs * currentSampleRate / 1000.0);

    // --- Incoming MIDI ---
    auto handleMessage = [&](const juce::MidiMessage& msg, int pos)
    {
        if (msg.isNoteOn())
        {
            int inNote = msg.getNoteNumber();
            int ch = msg.getChannel();
            float vel = msg.getFloatVelocity();

            int snapped = inNote;
            if (scaleLock)
                snapped = whiteKeys ? Scales::degreeMapNote(inNote, root, intervals)
                                    : Scales::snapNote(inNote, root, intervals, snapMode);
            int chord[kMaxEmit]; int chordSize = 0; int deg = -1;
            buildChordForSnapped(snapped, chord, chordSize, deg);

            bool poolWasEmpty = arpPoolSize == 0;

            // retrigger same input note: release old first
            auto& old = voices[inNote];
            if (old.active)
            {
                if (arpOn)
                {
                    removeFromArpPool(old.emitted, old.numEmitted);
                    // drop any pool removal still waiting on the sustain pedal
                    const Deferred marker { -1000 - inNote, old.channel };
                    numDeferred = (int) (std::remove(deferredOffs, deferredOffs + numDeferred, marker) - deferredOffs);
                }
                else
                    for (int k = 0; k < old.numEmitted; ++k)
                    {
                        if (sustainDown) addDeferred(out, pos, old.emitted[k], old.channel);
                        else releaseVoiceNote(out, pos, old.emitted[k], old.channel);
                    }
                removeVoice(inNote);
            }

            if (arpOn)
            {
                // latch: a new chord after all keys were released replaces the latched one
                if (arpLatch && numVoices == 0 && ! poolWasEmpty)
                {
                    clearArpPool();
                    resetArpPosition();
                }
                addToArpPool(chord, chordSize);
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
                for (int i = 0; i < chordSize; ++i)
                {
                    int off = i * strumStep;
                    if (off == 0 || numPendingOns >= kMaxPendingOns) voiceNoteOn(out, pos, chord[i], ch, vel);
                    else pendingOns[numPendingOns++] = { sampleClock + pos + off, chord[i], ch, vel };
                }
            }
            auto& v = voices[inNote];
            v.active = true; ++numVoices;
            v.snapped = snapped; v.channel = ch; v.numEmitted = chordSize;
            std::copy(chord, chord + chordSize, v.emitted);
            lastInputNote.store(inNote); lastSnappedNote.store(snapped);
        }
        else if (msg.isNoteOff())
        {
            int inNote = msg.getNoteNumber();
            auto& v = voices[inNote];
            if (! v.active) return;
            const int vch = v.channel;

            if (arpOn)
            {
                if (arpLatch)
                    removeVoice(inNote); // latch keeps the pool, forget the key
                else if (sustainDown)
                {
                    const Deferred marker { -1000 - inNote, vch }; // pool removal deferred
                    if (std::find(deferredOffs, deferredOffs + numDeferred, marker) == deferredOffs + numDeferred)
                    {
                        if (numDeferred < kMaxDeferred) deferredOffs[numDeferred++] = marker;
                        else { removeFromArpPool(v.emitted, v.numEmitted); removeVoice(inNote); }
                    }
                }
                else
                {
                    removeFromArpPool(v.emitted, v.numEmitted);
                    removeVoice(inNote);
                    if (arpPoolSize == 0 && arpCurrentNote >= 0)
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
                for (int k = 0; k < v.numEmitted; ++k)
                {
                    if (sustainDown) addDeferred(out, pos, v.emitted[k], vch);
                    else releaseVoiceNote(out, pos, v.emitted[k], vch);
                }
                removeVoice(inNote);
            }
        }
        else if (msg.isSustainPedalOn())
        {
            sustainDown = true;
        }
        else if (msg.isSustainPedalOff())
        {
            sustainDown = false;
            for (int d = 0; d < numDeferred; ++d)
            {
                const auto& def = deferredOffs[d];
                if (def.first <= -1000) // deferred pool removal, input note = -1000 - first
                {
                    int inNote = -1000 - def.first;
                    if (inNote >= 0 && inNote < 128 && voices[inNote].active)
                    {
                        if (! arpLatch) removeFromArpPool(voices[inNote].emitted, voices[inNote].numEmitted);
                        removeVoice(inNote);
                    }
                }
                else releaseVoiceNote(out, pos, def.first, def.second);
            }
            numDeferred = 0;
            if (arpOn && arpPoolSize == 0 && arpCurrentNote >= 0)
            {
                noteOffOut(out, pos, arpCurrentNote, arpCurrentChannel);
                arpCurrentNote = -1;
                gateOffIn = -1;
                nextPpq = -1.0;
            }
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
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
    };

    // Editor keyboard auditions go through the normal path (channel 1) at the
    // block start, right after any host events stamped at sample 0.
    auto feedAuditions = [&]
    {
        const auto scope = auditionFifo.read(auditionFifo.getNumReady());
        auto feed = [&](int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& a = auditionBuf[i];
                int note = juce::jlimit(0, 127, a.note);
                handleMessage(a.on ? juce::MidiMessage::noteOn(1, note, (juce::uint8) juce::jlimit(1, 127, a.vel))
                                   : juce::MidiMessage::noteOff(1, note), 0);
            }
        };
        feed(scope.startIndex1, scope.blockSize1);
        feed(scope.startIndex2, scope.blockSize2);
    };

    bool auditionsFed = false;
    for (const auto meta : midi)
    {
        if (! auditionsFed && meta.samplePosition > 0) { feedAuditions(); auditionsFed = true; }
        handleMessage(meta.getMessage(), juce::jlimit(0, juce::jmax(0, numSamples - 1), meta.samplePosition));
    }
    if (! auditionsFed) feedAuditions();

    // --- Arp tick generation ---
    if (arpOn && arpPoolSize > 0)
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
    int keep = 0;
    for (int i = 0; i < numPendingOns; ++i)
    {
        const auto& po = pendingOns[i];
        if (po.time < blockEnd)
            voiceNoteOn(out, (int) juce::jmax((juce::int64) 0, po.time - sampleClock),
                        po.note, po.channel, po.vel);
        else pendingOns[keep++] = po;
    }
    numPendingOns = keep;
    sampleClock = blockEnd;

    activeVoices.store(numVoices + (arpOn && arpCurrentNote >= 0 ? 1 : 0));

    // copy rather than swap, so outBuffer keeps its reserved storage
    midi.clear();
    midi.addEvents(out, 0, -1, 0);
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
        ++stateVersion; // open editors rebuild scale list / chord names / designer
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MidiNoteModifierProcessor(); }
