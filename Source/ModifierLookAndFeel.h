#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <MidiFonts.h>
#include <vector>
#include "Theme.h"

// Colour themes: same dark-grey shell, swappable accent triple (light/mid/dark).

// Dark-grey look and feel with a swappable accent theme.
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
        setColour(juce::Label::textColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::Label::textWhenEditingColourId, juce::Colour(0xFFF2F0F5));

        setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2B2B32));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFC9C9D2));
        setColour(juce::TextButton::textColourOnId, juce::Colour(0xFFFFFFFF));

        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF2B2B32));
        setColour(juce::ComboBox::textColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF45454E));

        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF26262D));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xFFFFFFFF));

        setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xFF2B2B32));
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xFF45454E));

        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF2B2B32));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF45454E));
        setColour(juce::TextEditor::highlightedTextColourId, juce::Colour(0xFFFFFFFF));

        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xFF26262D));
        setColour(juce::TooltipWindow::textColourId, juce::Colour(0xFFF2F0F5));
        setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xFF26262D));

        applyThemeColours();
    }

    void setTheme(int i)
    {
        Theme::set(i);
        applyThemeColours();
    }

    void applyThemeColours()
    {
        // accent-driven roles follow the active theme
        setColour(juce::TextButton::buttonOnColourId, Theme::mid());
        setColour(juce::ComboBox::arrowColourId, Theme::light());
        setColour(juce::ComboBox::focusedOutlineColourId, Theme::light());
        setColour(juce::ComboBox::buttonColourId, Theme::light());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Theme::dark());
        setColour(juce::PopupMenu::headerTextColourId, Theme::light());
        setColour(juce::Slider::textBoxHighlightColourId, Theme::dark());
        setColour(juce::TextEditor::focusedOutlineColourId, Theme::light());
        setColour(juce::TextEditor::highlightColourId, Theme::dark());
        setColour(juce::CaretComponent::caretColourId, Theme::light());
        setColour(juce::TooltipWindow::outlineColourId, Theme::dark());
        setColour(juce::BubbleComponent::outlineColourId, Theme::dark());
    }

    static juce::Colour accent()      { return Theme::mid(); }
    static juce::Colour accentHi()    { return Theme::light(); }
    static juce::Colour accentDeep()  { return Theme::dark(); }
    static juce::Colour panel()       { return juce::Colour(0xFF222228); }
    static juce::Colour panelEdge()   { return juce::Colour(0xFF45454E); }
    static juce::Colour trackGrey()   { return juce::Colour(0xFF3C3C45); }

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

    // ---- buttons: rounded, state-aware ----
    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour&, bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        bool on = b.getToggleState();
        juce::Colour fill = on ? b.findColour(juce::TextButton::buttonOnColourId)
                               : b.findColour(juce::TextButton::buttonColourId);
        if (highlighted) fill = fill.brighter(0.1f);
        if (down) fill = fill.darker(0.15f);
        g.setColour(fill);
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(on ? accentHi() : panelEdge());
        g.drawRoundedRectangle(r, 8.0f, on ? 1.5f : 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& b,
                        bool, bool) override
    {
        bool on = b.getToggleState();
        g.setColour(on ? b.findColour(juce::TextButton::textColourOnId)
                       : b.findColour(juce::TextButton::textColourOffId));
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText(b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, true);
    }

    // ---- power switches ----
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b,
                          bool, bool) override
    {
        bool on = b.getToggleState();
        auto bounds = b.getLocalBounds();
        float h = juce::jmin(24.0f, (float) bounds.getHeight() - 4.0f);
        float w = h * 1.9f;
        juce::Rectangle<float> pill(bounds.getX() + 2.0f,
                                    bounds.getCentreY() - h * 0.5f, w, h);
        juce::Colour track = on ? accentDeep() : juce::Colour(0xFF3C3C45);
        g.setColour(track);
        g.fillRoundedRectangle(pill, h * 0.5f);
        if (on)
        {
            g.setColour(accent().withAlpha(0.35f));
            g.drawRoundedRectangle(pill.expanded(2.0f), h * 0.5f + 2.0f, 2.0f);
        }
        float knobR = h * 0.5f - 3.0f;
        float knobX = on ? pill.getRight() - 3.0f - knobR * 2.0f : pill.getX() + 3.0f;
        g.setColour(on ? juce::Colours::white : juce::Colour(0xFF9C9CA8));
        g.fillEllipse(knobX, pill.getCentreY() - knobR, knobR * 2.0f, knobR * 2.0f);

        if (b.getButtonText().isNotEmpty())
        {
            g.setColour(on ? juce::Colour(0xFFF2F0F5) : juce::Colour(0xFFA7A3B8));
            g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
            g.drawText(b.getButtonText(),
                       juce::Rectangle<int>((int) (pill.getRight() + 8), bounds.getY(),
                                            bounds.getRight() - (int) (pill.getRight() + 8), bounds.getHeight()),
                       juce::Justification::centred, true);
        }
    }

    // ---- combo boxes ----
    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(1.0f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(r, 8.0f);
        bool focused = box.hasKeyboardFocus(true);
        g.setColour(focused ? box.findColour(juce::ComboBox::focusedOutlineColourId)
                            : box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(r, 8.0f, 1.0f);

        // chevron
        juce::Path chev;
        float cx = (float) width - 20.0f, cy = (float) height * 0.5f;
        chev.addTriangle(cx - 5, cy - 2, cx + 5, cy - 2, cx, cy + 3.5f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(chev);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(13.0f));
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
        g.fillRoundedRectangle(0, 0, (float) w, (float) h, 8.0f);
        g.setColour(panelEdge());
        g.drawRoundedRectangle(0.5f, 0.5f, (float) w - 1, (float) h - 1, 8.0f, 1.0f);
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

    // ---- sliders: purple track + glow thumb ----
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h,
                          float sliderPos, float, float,
                          juce::Slider::SliderStyle, juce::Slider& s) override
    {
        float trackH = 6.0f;
        float cy = y + h * 0.5f;
        juce::Rectangle<float> track((float) x + 10, cy - trackH * 0.5f,
                                     (float) w - 20, trackH);
        g.setColour(juce::Colour(0xFF3A3247));
        g.fillRoundedRectangle(track, trackH * 0.5f);

        bool horizontal = s.isHorizontal();
        float fillEnd = horizontal ? sliderPos : track.getRight();
        float fillStart = horizontal ? track.getX() : sliderPos;
        juce::Rectangle<float> fill(juce::jmin(fillStart, fillEnd), track.getY(),
                                    std::abs(fillEnd - fillStart), trackH);
        if (fill.getWidth() > 0.5f)
        {
            g.setColour(accentDeep());
            g.fillRoundedRectangle(fill, trackH * 0.5f);
        }
        g.setColour(accent().withAlpha(0.35f));
        g.fillEllipse(sliderPos - 11, cy - 11, 22, 22);
        g.setColour(accentHi());
        g.fillEllipse(sliderPos - 8, cy - 8, 16, 16);
        g.setColour(accentDeep());
        g.drawEllipse(sliderPos - 8, cy - 8, 16, 16, 1.5f);
    }

    int getSliderThumbRadius(juce::Slider&) override { return 8; }

    // ---- rotary knobs: grey track arc, purple fill arc, pointer knob ----
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float>((float) x, (float) y, (float) width, (float) height);
        float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
        auto knobRect = juce::Rectangle<float>(bounds.getCentreX() - size * 0.5f,
                                               bounds.getCentreY() - size * 0.5f,
                                               size, size).reduced(6.0f);
        auto centre = knobRect.getCentre();
        float radius = knobRect.getWidth() * 0.5f;

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                            rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(s.isEnabled() ? trackGrey() : trackGrey().withAlpha(0.5f));
        g.strokePath(track, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));

        float valueAngle = rotaryStartAngle
            + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
        if (s.isEnabled() && sliderPosProportional > 0.001f)
        {
            juce::Path fill;
            fill.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                               rotaryStartAngle, valueAngle, true);
            g.setColour(accent());
            g.strokePath(fill, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));
        }

        float bodyR = radius - 9.0f;
        g.setColour(s.isEnabled() ? juce::Colour(0xFF2B2B32) : juce::Colour(0xFF232328));
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(s.isEnabled() ? accent() : trackGrey());
        g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.5f);

        juce::Path pointer;
        float pointerLen = bodyR - 4.0f;
        pointer.addRectangle(-1.75f, -pointerLen, 3.5f, pointerLen);
        pointer.applyTransform(juce::AffineTransform::rotation(valueAngle).translated(centre.x, centre.y));
        g.setColour(s.isEnabled() ? accentHi() : juce::Colour(0xFF6E6E78));
        g.fillPath(pointer);
    }
};
