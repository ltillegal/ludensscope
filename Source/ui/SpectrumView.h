#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "Theme.h"

class SpectrumView : public juce::Component
{
public:
    static constexpr int numBins = 512;

    void setData (const float* levelsDb, const float* peaksDb) noexcept
    {
        std::copy (levelsDb, levelsDb + numBins, levels.begin());
        std::copy (peaksDb,  peaksDb  + numBins, peaks.begin());
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = bounds.reduced (12.0f);
        auto titleRow = area.removeFromTop (16.0f);

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("SPECTRUM  20 Hz - 20 kHz", titleRow, juce::Justification::centredLeft);
        g.drawText ("dBFS", titleRow, juce::Justification::centredRight);

        const auto plot = area.withTrimmedBottom (14.0f).withTrimmedRight (30.0f);

        constexpr float minDb = -96.0f;
        constexpr float maxDb = 0.0f;

        const auto xFor = [&] (float fraction)
        {
            return plot.getX() + plot.getWidth() * juce::jlimit (0.0f, 1.0f, fraction);
        };

        const auto yFor = [&] (float db)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
            return plot.getBottom() - plot.getHeight() * t;
        };

        const auto fractionForHz = [] (float hz)
        {
            return std::log (hz / 20.0f) / std::log (20000.0f / 20.0f);
        };

        g.setFont (theme::monoFont (9.0f));

        for (float db = maxDb; db >= minDb; db -= 12.0f)
        {
            const float y = yFor (db);
            g.setColour (theme::grid);
            g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());

            g.setColour (theme::textDim);
            g.drawText (juce::String ((int) db),
                        juce::Rectangle<float> (plot.getRight() + 5.0f, y - 6.0f, 24.0f, 12.0f),
                        juce::Justification::centredLeft);
        }

        const float frequencies[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                      1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f };
        const char* labels[] = { "20", "50", "100", "200", "500",
                                 "1k", "2k", "5k", "10k", "20k" };

        for (int i = 0; i < 10; ++i)
        {
            const float x = xFor (fractionForHz (frequencies[i]));

            g.setColour (i == 0 || i == 9 ? theme::gridStrong : theme::grid);
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

            g.setColour (theme::textDim);
            g.drawText (labels[i],
                        juce::Rectangle<float> (x - 14.0f, plot.getBottom() + 1.0f, 28.0f, 12.0f),
                        juce::Justification::centred);
        }

        juce::Path curve, filled;

        for (int i = 0; i < numBins; ++i)
        {
            const float x = xFor ((float) i / (float) (numBins - 1));
            const float y = yFor (levels[(size_t) i]);

            if (i == 0)
            {
                curve.startNewSubPath (x, y);
                filled.startNewSubPath (x, plot.getBottom());
                filled.lineTo (x, y);
            }
            else
            {
                curve.lineTo (x, y);
                filled.lineTo (x, y);
            }
        }

        filled.lineTo (xFor (1.0f), plot.getBottom());
        filled.closeSubPath();

        g.setGradientFill (juce::ColourGradient::vertical (theme::accent.withAlpha (0.32f), plot.getY(),
                                                           theme::accent.withAlpha (0.02f), plot.getBottom()));
        g.fillPath (filled);

        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.5f));

        juce::Path peakPath;

        for (int i = 0; i < numBins; ++i)
        {
            const float x = xFor ((float) i / (float) (numBins - 1));
            const float y = yFor (peaks[(size_t) i]);

            if (i == 0) peakPath.startNewSubPath (x, y);
            else        peakPath.lineTo (x, y);
        }

        g.setColour (theme::accent2.withAlpha (0.55f));
        g.strokePath (peakPath, juce::PathStrokeType (1.0f));
    }

private:
    std::array<float, numBins> levels {};
    std::array<float, numBins> peaks {};
};
