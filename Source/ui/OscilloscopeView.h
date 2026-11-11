#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Theme.h"

class OscilloscopeView : public juce::Component
{
public:
    OscilloscopeView() = default;

    void setData (const float* leftSamples, const float* rightSamples,
                  int numSamples, bool frozen)
    {
        left.assign (leftSamples, leftSamples + numSamples);
        right.assign (rightSamples, rightSamples + numSamples);
        isFrozen = frozen;
    }

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    }

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (std::abs (wheel.deltaY) < 1.0e-4f)
            return;

        if (e.mods.isShiftDown())
        {
            verticalGain = juce::jlimit (0.25f, 8.0f,
                                         verticalGain * std::pow (2.0f, wheel.deltaY * 0.5f));
        }
        else
        {
            const int count = (int) left.size();

            if (count < 32)
                return;

            const int base = windowSamples > 0 ? windowSamples : count / 2;
            const float factor = std::pow (2.0f, -wheel.deltaY * 0.5f);

            windowSamples = juce::jlimit (32, count, (int) std::lround ((float) base * factor));
        }

        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        windowSamples = 0;
        verticalGain = 1.0f;
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

        auto header = area.removeFromTop (14.0f);

        const int count = (int) juce::jmin (left.size(), right.size());
        const int window = windowSamples > 0
            ? juce::jmin (windowSamples, count)
            : count / 2;

        const double milliseconds = 1000.0 * (double) window / sampleRate;

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("OSCILLOSCOPE  " + juce::String (milliseconds, milliseconds < 10.0 ? 2 : 1) + " ms",
                    header, juce::Justification::centredLeft);

        g.setFont (theme::labelFont (9.5f));
        g.setColour (theme::textDim);
        g.drawText ("x" + juce::String (verticalGain, 2), header.withTrimmedRight (72.0f),
                    juce::Justification::centredRight);

        g.setColour (isFrozen ? theme::accent2 : theme::textDim);
        g.drawText (isFrozen ? "FROZEN" : "TRIGGERED", header, juce::Justification::centredRight);

        const auto plot = area;

        g.setColour (theme::grid);
        g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

        for (int i = 1; i < 4; ++i)
        {
            const float x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        }

        if (count < 4)
            return;

        // Trigger on the first rising zero crossing in the first half of the buffer.
        int start = 0;
        const int searchLimit = count / 2;

        for (int i = 1; i < searchLimit; ++i)
        {
            if (left[(size_t) (i - 1)] <= 0.0f && left[(size_t) i] > 0.0f)
            {
                start = i;
                break;
            }
        }

        const int drawCount = juce::jmin (count - start, window);

        const auto yFor = [&] (float sample)
        {
            return plot.getCentreY()
                   - juce::jlimit (-1.0f, 1.0f, sample * verticalGain) * plot.getHeight() * 0.46f;
        };

        const auto buildPath = [&] (const std::vector<float>& data)
        {
            juce::Path path;

            for (int i = 0; i < drawCount; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) juce::jmax (1, drawCount - 1);
                const float y = yFor (data[(size_t) (start + i)]);

                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo (x, y);
            }

            return path;
        };

        g.setColour (theme::cold.withAlpha (0.55f));
        g.strokePath (buildPath (right), juce::PathStrokeType (1.1f));

        g.setColour (theme::accent.withAlpha (0.85f));
        g.strokePath (buildPath (left), juce::PathStrokeType (1.2f));
    }

private:
    std::vector<float> left, right;
    bool isFrozen = false;
    int windowSamples = 0;
    float verticalGain = 1.0f;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscilloscopeView)
};
