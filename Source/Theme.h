#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// Colour themes: same dark-grey shell, swappable accent triple (light/mid/dark).
struct AppTheme { const char* name; uint32_t light, mid, dark; };

class Theme
{
public:
    static const std::vector<AppTheme>& all()
    {
        static const std::vector<AppTheme> t = {
            { "Purple",    0xFFD0ACFF, 0xFF6228AD, 0xFF3F226E },
            { "Ember",     0xFFFFC79B, 0xFFC25E13, 0xFF572A0E },
            { "Teal",      0xFF9BF0E4, 0xFF0E8A7A, 0xFF0B3D38 },
            { "Neon Pink", 0xFFFFA6E8, 0xFFE0118B, 0xFF5E0A41 },
            { "Sky Blue",  0xFFA8D8FF, 0xFF2B6CB0, 0xFF1A365D },
        };
        return t;
    }
    static int count() { return (int) all().size(); }
    static void set(int i) { current = juce::jlimit(0, count() - 1, i); }
    static int get() { return current; }
    static juce::Colour light() { return juce::Colour(all()[(size_t) get()].light); }
    static juce::Colour mid()   { return juce::Colour(all()[(size_t) get()].mid); }
    static juce::Colour dark()  { return juce::Colour(all()[(size_t) get()].dark); }

private:
    inline static int current = 0;
};
