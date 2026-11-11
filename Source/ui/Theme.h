#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme
{
    inline const juce::Colour bg         { 0xff0b0e10 };
    inline const juce::Colour bgBottom   { 0xff12181b };
    inline const juce::Colour panel      { 0xff141a1d };
    inline const juce::Colour panelLine  { 0xff202a2e };
    inline const juce::Colour grid       { 0xff1c2529 };
    inline const juce::Colour gridStrong { 0xff2a363b };
    inline const juce::Colour text       { 0xff8ea1a8 };
    inline const juce::Colour textDim    { 0xff5a696f };
    inline const juce::Colour accent     { 0xff4fd1c5 };
    inline const juce::Colour accent2    { 0xffe8b04b };
    inline const juce::Colour hot        { 0xffe0675a };
    inline const juce::Colour cold       { 0xff5aa9e0 };

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
