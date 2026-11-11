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

    void mouseMove (const juce::MouseEvent& e) override
    {
        cursorActive = true;
        cursorPosition = e.position;
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        cursorActive = false;
        repaint();
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

        if (cursorActive && plot.contains (cursorPosition))
        {
            const float fraction = juce::jlimit (0.0f, 1.0f,
                                                 (cursorPosition.x - plot.getX()) / plot.getWidth());
            const float hz = 20.0f * std::pow (1000.0f, fraction);

            const float binF = fraction * (float) (numBins - 1);
            const int b0 = (int) binF;
            const int b1 = std::min (b0 + 1, numBins - 1);
            const float mix = binF - (float) b0;

            const float levelDb = levels[(size_t) b0]
                                  + mix * (levels[(size_t) b1] - levels[(size_t) b0]);
            const float peakLevelDb = peaks[(size_t) b0]
                                      + mix * (peaks[(size_t) b1] - peaks[(size_t) b0]);

            const float x = xFor (fraction);
            const float y = yFor (levelDb);

            g.setColour (theme::text.withAlpha (0.45f));
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

            g.setColour (theme::accent);
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);

            const juce::String frequency = hz < 1000.0f
                ? juce::String (juce::roundToInt (hz)) + " Hz"
                : juce::String (hz / 1000.0f, 2) + " kHz";

            const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
            const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" };
            const juce::String note = juce::String (noteNames[((midi % 12) + 12) % 12])
                                      + juce::String (midi / 12 - 1);

            const juce::String line1 = frequency + "  " + note + "   "
                                       + juce::String (levelDb, 1) + " dBFS";
            const juce::String line2 = "peak " + juce::String (peakLevelDb, 1) + " dBFS";

            constexpr float boxWidth = 196.0f;
            constexpr float boxHeight = 34.0f;

            const float rawX = x > plot.getCentreX() ? x - boxWidth - 10.0f : x + 10.0f;
            const float boxX = juce::jlimit (plot.getX() + 4.0f,
                                             plot.getRight() - boxWidth - 4.0f, rawX);
            const juce::Rectangle<float> box (boxX, plot.getY() + 6.0f, boxWidth, boxHeight);

            g.setColour (theme::bg.withAlpha (0.92f));
            g.fillRoundedRectangle (box, 4.0f);
            g.setColour (theme::panelLine);
            g.drawRoundedRectangle (box, 4.0f, 1.0f);

            auto textArea = box.reduced (7.0f, 4.0f);

            g.setFont (theme::monoFont (10.0f));
            g.setColour (theme::text);
            g.drawText (line1, textArea.removeFromTop (13.0f), juce::Justification::centredLeft);

            g.setColour (theme::textDim);
            g.drawText (line2, textArea, juce::Justification::centredLeft);
        }
    }

private:
    std::array<float, numBins> levels {};
    std::array<float, numBins> peaks {};

    bool cursorActive = false;
    juce::Point<float> cursorPosition;
};
