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

            const int base = windowSamples > 0 ? windowSamples : count;
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

        theme::drawPanel (g, bounds);

        auto area = bounds.reduced (12.0f);

        auto header = area.removeFromTop (14.0f);

        const int count = (int) juce::jmin (left.size(), right.size());
        const int window = windowSamples > 0
            ? juce::jmin (windowSamples, count)
            : count;

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
        const int columns = juce::jlimit (2, (int) juce::jmax (2.0f, plot.getWidth()), drawCount);

        const auto yFor = [&] (float sample)
        {
            return plot.getCentreY()
                   - juce::jlimit (-1.0f, 1.0f, sample * verticalGain) * plot.getHeight() * 0.46f;
        };

        // Min/max envelope per pixel column: at long time windows this draws
        // the waveform shape instead of a sparse, aliased polyline.
        const auto drawChannel = [&] (const std::vector<float>& data, juce::Colour colour)
        {
            std::vector<float> tops ((size_t) columns), bottoms ((size_t) columns);

            for (int c = 0; c < columns; ++c)
            {
                const int from = start + (int) ((juce::int64) c * drawCount / columns);
                int to = start + (int) ((juce::int64) (c + 1) * drawCount / columns);

                if (to <= from)
                    to = from + 1;

                float lo = data[(size_t) from];
                float hi = lo;

                for (int i = from + 1; i < to && i < start + drawCount; ++i)
                {
                    lo = juce::jmin (lo, data[(size_t) i]);
                    hi = juce::jmax (hi, data[(size_t) i]);
                }

                tops[(size_t) c] = hi;
                bottoms[(size_t) c] = lo;
            }

            const auto xAt = [&] (int c)
            {
                return plot.getX() + plot.getWidth() * (float) c / (float) (columns - 1);
            };

            juce::Path envelope;
            envelope.startNewSubPath (xAt (0), yFor (tops[0]));

            for (int c = 1; c < columns; ++c)
                envelope.lineTo (xAt (c), yFor (tops[(size_t) c]));

            for (int c = columns - 1; c >= 0; --c)
                envelope.lineTo (xAt (c), yFor (bottoms[(size_t) c]));

            envelope.closeSubPath();

            g.setGradientFill (juce::ColourGradient::vertical (colour.withAlpha (0.16f), plot.getY(),
                                                               colour.withAlpha (0.01f), plot.getBottom()));
            g.fillPath (envelope);

            g.setColour (colour.withAlpha (0.85f));
            g.strokePath (envelope, juce::PathStrokeType (1.0f));
        };

        drawChannel (right, theme::cold);
        drawChannel (left, theme::accent);
    }

private:
    std::vector<float> left, right;
    bool isFrozen = false;
    int windowSamples = 0;
    float verticalGain = 1.0f;
    double sampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscilloscopeView)
};
