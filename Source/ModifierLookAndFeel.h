#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <MidiFonts.h>
#include <vector>
#include "Theme.h"

// Colour themes: same dark-grey shell, swappable accent triple (light/mid/dark).

// v3 look (after Pastel EFX Grid; see mockup/redesign_v3.png): charcoal panels
// with thin edges, the theme's light colour as the one accent (filled pills
// and buttons with dark text when on), arc knobs, Lato.
class ModifierLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModifierLookAndFeel()
    {
        latoRegular = juce::Typeface::createSystemTypefaceFor(
            MidiFonts::LatoRegular_ttf, MidiFonts::LatoRegular_ttfSize);
        latoBold = juce::Typeface::createSystemTypefaceFor(
            MidiFonts::LatoBold_ttf, MidiFonts::LatoBold_ttfSize);
        setDefaultSansSerifTypeface(latoBold);

        // dark grey constants
        setColour(juce::Label::textColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::Label::textWhenEditingColourId, juce::Colour(0xFFD9D5D0));

        setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF232529));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFD9D5D0));
        setColour(juce::TextButton::textColourOnId, ink());

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF232529));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF3A3D43));

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF1C1F24));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xFFFFFFFF));

        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF232529));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF3A3D43));

        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF232529));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF3A3D43));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xFFFFFFFF));

        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xFF1C1F24));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(0xFFD9D5D0));
        setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xFF1C1F24));

        applyThemeColours();
    }

    void setTheme(int i)
    {
        theme.set(i);
        applyThemeColours();
    }

    void applyThemeColours()
    {
        // accent-driven roles follow the active theme
        setColour(juce::TextButton::buttonOnColourId, theme.light());
        setColour(juce::ComboBox::arrowColourId, theme.light());
        setColour(juce::ComboBox::focusedOutlineColourId, theme.light());
        setColour(juce::ComboBox::buttonColourId, theme.light());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, theme.dark());
        setColour(juce::PopupMenu::headerTextColourId, theme.light());
        setColour(juce::Slider::textBoxHighlightColourId, theme.dark());
        setColour(juce::TextEditor::focusedOutlineColourId, theme.light());
        setColour(juce::TextEditor::highlightColourId, theme.dark());
        setColour(juce::CaretComponent::caretColourId, theme.light());
        setColour(juce::TooltipWindow::outlineColourId, theme.dark());
        setColour(juce::BubbleComponent::outlineColourId, theme.dark());
    }

    juce::Colour accent() const      { return theme.light(); }
    juce::Colour accentHi() const    { return theme.light(); }
    juce::Colour accentDeep() const  { return theme.dark(); }
    static juce::Colour panel()       { return juce::Colour(0xFF1C1F24); }
    static juce::Colour panelEdge()   { return juce::Colour(0xFF33363C); }
    static juce::Colour trackGrey()   { return juce::Colour(0xFF33363C); }
    static juce::Colour ink()         { return juce::Colour(0xFF1B1D22); } // text on a filled accent
    static juce::Colour dim()         { return juce::Colour(0xFF8F8B85); }

    Theme theme; // per-instance accent theme
    juce::Typeface::Ptr latoRegular, latoBold;

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override
    {
        // bold Lato everywhere
        if (latoBold != nullptr)
            return latoBold;
        if (latoRegular != nullptr)
            return latoRegular;
        return juce::LookAndFeel_V4::getTypefaceForFont(juce::Font {});
    }

    // ---- buttons: filled accent when on, dark with a thin edge when off ----
    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        bool on = b.getToggleState();
        juce::Colour fill = on ? b.findColour(juce::TextButton::buttonOnColourId)
                               : b.findColour(juce::TextButton::buttonColourId);
        if (down) fill = fill.darker(0.15f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, 6.0f);
        if (! on)
        {
            g.setColour(highlighted ? dim() : panelEdge().brighter(0.08f));
            g.drawRoundedRectangle(r, 6.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                        bool, bool) override
    {
        bool on = b.getToggleState();
        auto c = on ? b.findColour(juce::TextButton::textColourOnId)
                    : b.findColour(juce::TextButton::textColourOffId);
        g.setColour(b.isEnabled() ? c : c.withAlpha(0.4f));
        g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
    }

    // ---- switches: a pill around the label, filled with the accent when on ----
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                          bool highlighted, bool) override
    {
        bool on = b.getToggleState();
        auto bounds = b.getLocalBounds().toFloat();
        const auto font = juce::Font(juce::FontOptions(11.5f, juce::Font::bold));
        const auto text = b.getButtonText().toUpperCase();
        float w = juce::jmin(bounds.getWidth() - 4.0f, juce::GlyphArrangement::getStringWidth(font, text) + 28.0f);
        float h = juce::jmin(24.0f, bounds.getHeight() - 4.0f);
        juce::Rectangle<float> pill(bounds.getX() + 2.0f, bounds.getCentreY() - h * 0.5f, w, h);
        if (on)
        {
            g.setColour(accent());
            g.fillRoundedRectangle(pill, h * 0.5f);
        }
        else
        {
            g.setColour(accent().withAlpha(highlighted ? 0.8f : 0.5f));
            g.drawRoundedRectangle(pill.reduced(0.5f), h * 0.5f, 1.2f);
        }
        g.setColour(on ? ink() : accent());
        g.setFont(font);
        g.drawText(text, pill, juce::Justification::centred, true);
    }

    // ---- combo boxes ----
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(1.0f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(r, 6.0f);
        bool focused = box.hasKeyboardFocus(true);
        g.setColour(focused ? box.findColour(juce::ComboBox::focusedOutlineColourId)
                            : box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(r, 6.0f, 1.0f);

        // chevron
        juce::Path chev;
        float cx = (float) width - 18.0f, cy = (float) height * 0.5f;
        chev.startNewSubPath(cx - 4, cy - 2);
        chev.lineTo(cx, cy + 2);
        chev.lineTo(cx + 4, cy - 2);
        g.setColour(dim());
        g.strokePath(chev, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(12.5f, juce::Font::bold));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(8, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centred);
    }

    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
    {
        g.setColour(findColour(juce::PopupMenu::backgroundColourId));
        g.fillRoundedRectangle(0, 0, (float) w, (float) h, 6.0f);
        g.setColour(panelEdge());
        g.drawRoundedRectangle(0.5f, 0.5f, (float) w - 1, (float) h - 1, 6.0f, 1.0f);
    }

    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool, bool isHighlighted, bool,
                           bool, const juce::String& text, const juce::String&,
                           const juce::Drawable*, const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            auto r = area.reduced(8, 0);
            g.setColour(panelEdge());
            g.fillRect(r.getX(), r.getCentreY(), r.getWidth(), 1);
            return;
        }
        auto r = area.reduced(3, 1);
        if (isHighlighted)
        {
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRoundedRectangle(r.toFloat(), 5.0f);
        }
        juce::Colour tc = (textColour != nullptr) ? *textColour
                          : findColour(juce::PopupMenu::textColourId);
        g.setColour(isHighlighted ? findColour(juce::PopupMenu::highlightedTextColourId) : tc);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(text, r, juce::Justification::centred, true);
    }

    // ---- sliders: thin track, accent fill, light round handle ----
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override
    {
        float cy = y + h * 0.5f;
        juce::Rectangle<float> track((float) x + 8, cy - 2.0f, (float) w - 16, 4.0f);
        g.setColour(trackGrey());
        g.fillRoundedRectangle(track, 2.0f);
        bool horizontal = s.isHorizontal();
        float fillEnd = horizontal ? sliderPos : track.getRight();
        float fillStart = horizontal ? track.getX() : sliderPos;
        juce::Rectangle<float> fill(juce::jmin(fillStart, fillEnd), track.getY(),
                                    std::abs(fillEnd - fillStart), 4.0f);
        if (fill.getWidth() > 0.5f)
        {
            g.setColour(accent());
            g.fillRoundedRectangle(fill, 2.0f);
        }
        g.setColour(juce::Colour(0xFFF4F1EC));
        g.fillEllipse(sliderPos - 7, cy - 7, 14, 14);
    }

    int getSliderThumbRadius(juce::Slider&) override { return 7; }

    // ---- rotary knobs: neutral track, accent arc, body + light needle ----
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float start, float end, juce::Slider& s) override
    {
        const float cx = x + width * 0.5f, cy = y + height * 0.5f;
        const float r = juce::jmin(width, height) * 0.5f - 3.0f;
        const float sw = juce::jmax(3.0f, r * 0.16f), arcR = r - sw * 0.5f;
        const float a = start + pos * (end - start);
        const auto stroke = juce::PathStrokeType(sw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f, start, end, true);
        g.setColour(s.isEnabled() ? trackGrey().brighter(0.08f) : trackGrey());
        g.strokePath(track, stroke);
        if (s.isEnabled() && pos > 0.002f)
        {
            juce::Path fill;
            fill.addCentredArc(cx, cy, arcR, arcR, 0.0f, start, a, true);
            g.setColour(accent());
            g.strokePath(fill, stroke);
        }
        const float br = r - sw - 1.5f;
        g.setColour(juce::Colour(0xFF26292F));
        g.fillEllipse(cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.setColour(panelEdge().brighter(0.08f));
        g.drawEllipse(cx - br, cy - br, br * 2.0f, br * 2.0f, 1.0f);
        g.setColour(s.isEnabled() ? juce::Colour(0xFFF4F1EC) : dim());
        g.drawLine(cx + std::sin(a) * br * 0.25f, cy - std::cos(a) * br * 0.25f,
                   cx + std::sin(a) * (br - 2.0f), cy - std::cos(a) * (br - 2.0f), 2.0f);
    }
};
