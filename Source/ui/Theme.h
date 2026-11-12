#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour bg         { 0xff07080a };
    inline const juce::Colour bgBottom   { 0xff0c0f14 };
    inline const juce::Colour panel      { 0xff0e1116 };
    inline const juce::Colour panelTop   { 0xff141922 };
    inline const juce::Colour panelLine  { 0xff1f262f };
    inline const juce::Colour grid       { 0xff161b21 };
    inline const juce::Colour gridStrong { 0xff2a313a };
    inline const juce::Colour text       { 0xffc3ccd6 };
    inline const juce::Colour textDim    { 0xff6e7884 };
    inline const juce::Colour accent     { 0xff5ee1cf };
    inline const juce::Colour accent2    { 0xfff0b45e };
    inline const juce::Colour hot        { 0xfff2705f };
    inline const juce::Colour cold       { 0xff6ea8ff };

    // Shared panel chrome: a near-invisible vertical gradient, a one-pixel
    // top highlight and a thin border. Keeps every view visually consistent.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius = 10.0f)
    {
        const juce::ColourGradient gradient (panelTop, bounds.getX(), bounds.getY(),
                                             panel, bounds.getX(), bounds.getBottom(), false);

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (juce::Colour (0x0effffff));
        g.drawLine (bounds.getX() + radius, bounds.getY() + 1.0f,
                    bounds.getRight() - radius, bounds.getY() + 1.0f, 1.0f);

        g.setColour (panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
    }

    inline juce::Font labelFont (float size)
    {
        return juce::Font (juce::FontOptions (size));
    }

    inline juce::Font boldFont (float size)
    {
        return juce::Font (juce::FontOptions (size, juce::Font::bold));
    }

    inline juce::Font monoFont (float size)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                              size, juce::Font::plain));
    }

    inline juce::Colour levelColour (float lufs)
    {
        if (lufs > -9.0f)  return hot;
        if (lufs > -18.0f) return accent2;
        return accent;
    }

    inline juce::Colour peakColour (float dbtp)
    {
        if (dbtp > -1.0f) return hot;
        if (dbtp > -3.0f) return accent2;
        return accent;
    }
}
