#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace theme
{
    // Seven switchable palettes. The current (index 0) is the deep teal-blue
    // used since the redesign; the others are complete dark/light/colour
    // variants, not just an accent swap.
    struct Colours
    {
        juce::Colour bg, bgBottom, panel, panelTop, panelLine, grid, gridStrong;
        juce::Colour text, textDim, accent, accent2, hot, cold;
    };

    inline juce::Colour c (juce::uint32 argb) { return juce::Colour (argb); }

    inline const std::array<Colours, 7>& palettes()
    {
        static const std::array<Colours, 7> values = { {
            // 0: Blue (current)
            { c(0xff012d3a), c(0xff013746), c(0xff014e68), c(0xff01536e), c(0xff0d6a86),
              c(0xff01566f), c(0xff0d6a86), c(0xffeaf8fb), c(0xffa9cdd8), c(0xff7beeff),
              c(0xffffc46b), c(0xffff8a7a), c(0xffa6c6ff) },
            // 1: Dark
            { c(0xff07080a), c(0xff0c0f14), c(0xff0e1116), c(0xff141922), c(0xff1f262f),
              c(0xff161b21), c(0xff2a313a), c(0xffc3ccd6), c(0xff6e7884), c(0xff5ee1cf),
              c(0xfff0b45e), c(0xfff2705f), c(0xff6ea8ff) },
            // 2: Light
            { c(0xffdfe6ea), c(0xffeef3f6), c(0xfff7f9fb), c(0xffffffff), c(0xff8ea2ac),
              c(0xff9aabb4), c(0xff7d909a), c(0xff17262e), c(0xff5a707c), c(0xff0b7f96),
              c(0xffa86600), c(0xffc0392b), c(0xff2f62c9) },
            // 3: Magenta
            { c(0xff1a0a15), c(0xff240d1c), c(0xff33132a), c(0xff421a36), c(0xff5a2448),
              c(0xff2a1022), c(0xff6b2c55), c(0xfff6d9ea), c(0xffb07f9b), c(0xffff5fbf),
              c(0xffffc46b), c(0xffff6b6b), c(0xffc9a0ff) },
            // 4: Green
            { c(0xff071a10), c(0xff0b2418), c(0xff0f2e1f), c(0xff143c29), c(0xff1f5338),
              c(0xff0c2417), c(0xff256b48), c(0xffd6f2e0), c(0xff7fae94), c(0xff4fe08a),
              c(0xffffd166), c(0xffff7a6b), c(0xff8fd3ff) },
            // 5: Red
            { c(0xff1a0808), c(0xff260c0c), c(0xff331111), c(0xff421717), c(0xff5a2020),
              c(0xff2a0f0f), c(0xff6b2626), c(0xfff6dada), c(0xffb08080), c(0xffff7a5c),
              c(0xffffc46b), c(0xffff3b3b), c(0xff8fb4ff) },
            // 6: Orange
            { c(0xff1a0f05), c(0xff241507), c(0xff33200c), c(0xff422a12), c(0xff5a3a1f),
              c(0xff2a1a0c), c(0xff6b4724), c(0xfff6e6d2), c(0xffb09072), c(0xffff9f43),
              c(0xffffd166), c(0xffff6b5e), c(0xff8fb4ff) },
        } };

        return values;
    }

    inline juce::Colour bg         { 0xff012d3a };
    inline juce::Colour bgBottom   { 0xff013746 };
    inline juce::Colour panel      { 0xff014e68 };
    inline juce::Colour panelTop   { 0xff01536e };
    inline juce::Colour panelLine  { 0xff0d6a86 };
    inline juce::Colour grid       { 0xff01566f };
    inline juce::Colour gridStrong { 0xff0d6a86 };
    inline juce::Colour text       { 0xffeaf8fb };
    inline juce::Colour textDim    { 0xffa9cdd8 };
    inline juce::Colour accent     { 0xff7beeff };
    inline juce::Colour accent2    { 0xffffc46b };
    inline juce::Colour hot        { 0xffff8a7a };
    inline juce::Colour cold       { 0xffa6c6ff };

    inline int currentTheme = 0;

    inline void setTheme (int index)
    {
        currentTheme = juce::jlimit (0, 6, index);
        const auto& colours = palettes()[(size_t) currentTheme];

        bg         = colours.bg;
        bgBottom   = colours.bgBottom;
        panel      = colours.panel;
        panelTop   = colours.panelTop;
        panelLine  = colours.panelLine;
        grid       = colours.grid;
        gridStrong = colours.gridStrong;
        text       = colours.text;
        textDim    = colours.textDim;
        accent     = colours.accent;
        accent2    = colours.accent2;
        hot        = colours.hot;
        cold       = colours.cold;
    }

    inline int getTheme() noexcept { return currentTheme; }
    inline bool isLight() noexcept { return currentTheme == 2; }

    inline juce::File& settingsFileOverride()
    {
        static juce::File file;
        return file;
    }

    inline void setSettingsFileForTesting (const juce::File& file)
    {
        settingsFileOverride() = file;
    }

    inline juce::File settingsFile()
    {
        if (settingsFileOverride() != juce::File())
            return settingsFileOverride();

        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("LudensScope")
            .getChildFile ("theme.txt");
    }

    inline int loadSavedTheme()
    {
        const auto file = settingsFile();

        return file.existsAsFile()
                   ? juce::jlimit (0, 6, file.loadFileAsString().trim().getIntValue())
                   : 0;
    }

    inline void saveTheme (int index)
    {
        const auto file = settingsFile();
        file.getParentDirectory().createDirectory();
        file.replaceWithText (juce::String (juce::jlimit (0, 6, index)));
    }

    inline const char* themeName (int index)
    {
        static const char* names[] = { "Blue", "Dark", "Light", "Magenta",
                                       "Green", "Red", "Orange" };
        return names[(size_t) juce::jlimit (0, 6, index)];
    }

    // Shared panel chrome: a near-invisible vertical gradient, a one-pixel
    // top highlight and a thin border. Keeps every view visually consistent.
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds,
                           float radius = 10.0f)
    {
        const juce::ColourGradient gradient (panelTop, bounds.getX(), bounds.getY(),
                                             panel, bounds.getX(), bounds.getBottom(), false);

        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bounds, radius);

        g.setColour (text.withAlpha (0.05f));
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
