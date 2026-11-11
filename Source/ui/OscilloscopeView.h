#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <vector>

#include "Theme.h"

class OscilloscopeView : public juce::Component
{
public:
    void setData (const float* leftSamples, const float* rightSamples,
                  int numSamples, bool frozen)
    {
        left.assign (leftSamples, leftSamples + numSamples);
        right.assign (rightSamples, rightSamples + numSamples);
        isFrozen = frozen;
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
        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("OSCILLOSCOPE", header, juce::Justification::centredLeft);

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

        const int count = (int) juce::jmin (left.size(), right.size());

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

        const int drawCount = juce::jmin (count - start, count / 2);

        const auto yFor = [&] (float sample)
        {
            return plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, sample) * plot.getHeight() * 0.46f;
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
};
