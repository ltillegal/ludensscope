#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>

#include "Theme.h"

// Scrolling spectrogram. Rows are written into a ring buffer image, so no
// pixel copying is needed per frame; the newest row is drawn at the bottom.
class SpectrogramView : public juce::Component
{
public:
    SpectrogramView()
    {
        for (int i = 0; i < paletteSize; ++i)
            palette[(size_t) i] = heat ((float) i / (float) (paletteSize - 1));
    }

    void push (const float* levelsDb, int numBins)
    {
        if (! image.isValid() || numBins <= 0)
            return;

        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

        for (int x = 0; x < width; ++x)
        {
            const float fraction = (float) x / (float) juce::jmax (1, width - 1);
            const float hz = rangeLow * std::pow (rangeHigh / rangeLow, fraction);
            const int bin = juce::jlimit (
                0, numBins - 1,
                (int) std::lround (std::log (hz / 20.0f) / std::log (1000.0f)
                                   * (float) (numBins - 1)));
            const float t = juce::jlimit (0.0f, 1.0f,
                                          (levelsDb[bin] - minDb) / (maxDb - minDb));

            data.setPixelColour (x, writeRow,
                                 palette[(size_t) (int) std::lround (t * (float) (paletteSize - 1))]);
        }

        writeRow = (writeRow + 1) % height;
    }

    void setRange (float lowHz, float highHz)
    {
        if (std::abs (lowHz - rangeLow) < 0.5f && std::abs (highHz - rangeHigh) < 0.5f)
            return;

        rangeLow = lowHz;
        rangeHigh = highHz;
        clear();
    }

    void clear()
    {
        if (image.isValid())
            image.clear (image.getBounds(), theme::bg);

        writeRow = 0;
        repaint();
    }

    void resized() override
    {
        const auto plot = plotBounds().toNearestInt();

        if (plot.getWidth() != width || plot.getHeight() != height)
        {
            width = juce::jmax (1, plot.getWidth());
            height = juce::jmax (1, plot.getHeight());

            image = juce::Image (juce::Image::ARGB, width, height, true);
            image.clear (image.getBounds(), theme::bg);
            writeRow = 0;
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = bounds.reduced (12.0f);
        auto titleRow = area.removeFromTop (14.0f);

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("WATERFALL", titleRow, juce::Justification::centredLeft);

        g.setFont (theme::labelFont (9.0f));
        g.drawText ("older up", titleRow, juce::Justification::centredRight);

        const auto plot = plotBounds();

        if (! image.isValid())
            return;

        const int oldestRows = height - writeRow;

        if (oldestRows > 0)
            g.drawImage (image, (int) plot.getX(), (int) plot.getY(),
                         width, oldestRows, 0, writeRow, width, oldestRows);

        if (writeRow > 0)
            g.drawImage (image, (int) plot.getX(), (int) plot.getY() + oldestRows,
                         width, writeRow, 0, 0, width, writeRow);

        g.setColour (theme::panelLine);
        g.drawRect (plot, 1.0f);
    }

private:
    juce::Rectangle<float> plotBounds() const
    {
        return getLocalBounds().toFloat()
            .reduced (12.0f)
            .withTrimmedTop (16.0f)
            .withTrimmedRight (30.0f);
    }

    static juce::Colour heat (float t)
    {
        static constexpr int numStops = 7;

        static const juce::Colour stops[numStops] = {
            juce::Colour (0xff0b0e10),
            juce::Colour (0xff14303c),
            juce::Colour (0xff2a6f75),
            juce::Colour (0xff4fd1c5),
            juce::Colour (0xffe8b04b),
            juce::Colour (0xffe0675a),
            juce::Colour (0xfff6efe2),
        };

        static const float positions[numStops] = {
            0.0f, 0.35f, 0.55f, 0.70f, 0.85f, 0.95f, 1.0f,
        };

        for (int i = 1; i < numStops; ++i)
        {
            if (t <= positions[i])
            {
                const float span = positions[i] - positions[i - 1];
                const float f = span > 0.0f ? (t - positions[i - 1]) / span : 0.0f;

                return stops[i - 1].interpolatedWith (stops[i], f);
            }
        }

        return stops[numStops - 1];
    }

    static constexpr int paletteSize = 256;
    static constexpr float minDb = -96.0f;
    static constexpr float maxDb = 0.0f;

    std::array<juce::Colour, paletteSize> palette {};

    juce::Image image;
    int width = 0;
    int height = 0;
    int writeRow = 0;

    float rangeLow = 20.0f;
    float rangeHigh = 20000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
