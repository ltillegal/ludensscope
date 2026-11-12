#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour bg         { 0xff011922 };
    inline const juce::Colour bgBottom   { 0xff012833 };
    inline const juce::Colour panel      { 0xff013643 };
    inline const juce::Colour panelTop   { 0xff01495b };
    inline const juce::Colour panelLine  { 0xff0a5a70 };
    inline const juce::Colour grid       { 0xff012b37 };
    inline const juce::Colour gridStrong { 0xff0a4f63 };
    inline const juce::Colour text       { 0xffd2e9f0 };
    inline const juce::Colour textDim    { 0xff83a7b4 };
    inline const juce::Colour accent     { 0xff5ee1f0 };
    inline const juce::Colour accent2    { 0xffffc46b };
    inline const juce::Colour hot        { 0xffff7a6b };
    inline const juce::Colour cold       { 0xff8fb4ff };

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
