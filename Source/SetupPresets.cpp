#include "SetupPresets.h"
#include "Scales.h"
#include <algorithm>
#include <initializer_list>
#include <utility>

namespace SetupPresets
{
static int scaleIndex(const char* name)
{
    auto& facts = Scales::factoryScales();
    for (size_t i = 0; i < facts.size(); ++i)
        if (facts[i].name == name) return (int) i;
    jassertfalse;
    return 0;
}

// chord selector choice index: 0 Auto, 1 Single, then Scales::chordTypeName(1..12)
enum Chd { Auto = 0, Single, Major, Minor, Dim, Aug, Sus2, Sus4, Power, Dom7, Maj7, Min7, PowerOct, Add9 };

// one recipe: plain parameter values; everything not listed loads its default
static juce::ValueTree recipe(std::initializer_list<std::pair<const char*, float>> values)
{
    juce::ValueTree t(kRootType);
    for (auto& [id, v] : values)
    {
        juce::ValueTree p("PARAM");
        p.setProperty("id", id, nullptr);
        p.setProperty("value", v, nullptr);
        t.appendChild(p, nullptr);
    }
    return t;
}

const std::vector<Factory>& factory()
{
    static const std::vector<Factory> f = {
        { "Init", recipe({}) },
        { "Major Triads", recipe({ { "scale", (float) scaleIndex("Major") }, { "chordOn", 1 } }) },
        { "Dorian Strum", recipe({ { "scale", (float) scaleIndex("Dorian") }, { "root", 2 }, { "chordOn", 1 },
                                   { "inversion", 1 }, { "strum", 22 } }) },
        { "Minor 7ths", recipe({ { "scale", (float) scaleIndex("Natural Minor") }, { "root", 9 }, { "chordOn", 1 },
                                 { "chd0", Min7 }, { "chd1", Dim }, { "chd2", Maj7 }, { "chd3", Min7 },
                                 { "chd4", Min7 }, { "chd5", Maj7 }, { "chd6", Dom7 }, { "bassBelow", 1 } }) },
        { "Pentatonic Arp", recipe({ { "scale", (float) scaleIndex("Minor Pentatonic") }, { "root", 4 },
                                     { "arpOn", 1 }, { "arpMode", 2 }, { "arpRate", 6 }, { "arpOct", 1 } }) },
        { "Power Chord Arp", recipe({ { "scale", (float) scaleIndex("Minor Blues") }, { "chordOn", 1 },
                                      { "arpOn", 1 }, { "arpMode", 0 }, { "arpRate", 3 }, { "arpOct", 1 },
                                      { "gate", 60 } }) },
        { "Swing Latch", recipe({ { "scale", (float) scaleIndex("Mixolydian") }, { "root", 7 }, { "chordOn", 1 },
                                  { "arpOn", 1 }, { "arpLatch", 1 }, { "arpMode", 5 }, { "arpRate", 6 },
                                  { "arpOct", 1 }, { "swing", 30 }, { "gate", 55 } }) },
        { "White Keys Blues", recipe({ { "scale", (float) scaleIndex("Minor Blues") }, { "whiteKeys", 1 } }) },
        { "In Sen Pluck Arp", recipe({ { "scale", (float) scaleIndex("In Sen") }, { "root", 2 }, { "arpOn", 1 },
                                       { "arpMode", 3 }, { "arpRate", 5 }, { "arpOct", 2 }, { "gate", 35 } }) },
    };
    return f;
}

// ---------------- user presets ----------------
static juce::File customUserDir;
static bool useCustomUserDir = false;

void setUserPresetDirForTest(const juce::File& dir) { customUserDir = dir; useCustomUserDir = true; }

juce::File getUserPresetDir()
{
    if (useCustomUserDir) return customUserDir;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Midi Note Modifier Presets");
}

std::vector<UserPreset> scanUserPresets()
{
    std::vector<UserPreset> out;
    auto dir = getUserPresetDir();
    if (! dir.isDirectory()) return out;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*.xml"))
        out.push_back({ f.getFileNameWithoutExtension(), f });
    std::sort(out.begin(), out.end(),
              [](const UserPreset& a, const UserPreset& b) { return a.name.compareIgnoreCase(b.name) < 0; });
    return out;
}

juce::String sanitiseName(juce::String name)
{
    name = name.trim().substring(0, 64);
    name = name.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-#");
    name = name.trim();
    return name.isEmpty() ? juce::String("Untitled") : name;
}

bool writeUserPreset(const juce::String& name, const juce::ValueTree& setup)
{
    auto dir = getUserPresetDir();
    if (! dir.isDirectory() && ! dir.createDirectory()) return false;
    auto file = dir.getChildFile(sanitiseName(name) + ".xml");
    if (auto xml = setup.createXml())
        return xml->writeTo(file);
    return false;
}

juce::ValueTree readUserPreset(const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse(file))
        if (xml->hasTagName(kRootType.toString()))
            return juce::ValueTree::fromXml(*xml);
    return {};
}

bool deleteUserPreset(const juce::File& file)
{
    if (file.getParentDirectory() != getUserPresetDir()) return false; // safety: own dir only
    return file.existsAsFile() && file.deleteFile();
}
}
