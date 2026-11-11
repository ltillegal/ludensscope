#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <vector>

#include "Theme.h"

class VectorScopeView : public juce::Component
{
public:
    void setData (const float* leftSamples, const float* rightSamples, int numSamples)
    {
        left.assign (leftSamples, leftSamples + numSamples);
        right.assign (rightSamples, rightSamples + numSamples);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = bounds.reduced (12.0f);

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("STEREO IMAGE", area.removeFromTop (14.0f), juce::Justification::centredLeft);

        const float side = juce::jmin (area.getWidth(), area.getHeight());
        const auto square = area.withSizeKeepingCentre (side, side);

        const float radius = side * 0.5f - 2.0f;
        const auto centre = square.getCentre();

        g.setColour (theme::grid);
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

        g.setColour (theme::gridStrong);
        g.drawVerticalLine ((int) centre.x, centre.y - radius, centre.y + radius);
        g.drawHorizontalLine ((int) centre.y, centre.x - radius, centre.x + radius);

        const float diagonal = radius * 0.7071f;
        g.setColour (theme::grid);
        g.drawLine (centre.x - diagonal, centre.y - diagonal,
                    centre.x + diagonal, centre.y + diagonal, 1.0f);
        g.drawLine (centre.x - diagonal, centre.y + diagonal,
                    centre.x + diagonal, centre.y - diagonal, 1.0f);

        const int count = (int) juce::jmin (left.size(), right.size());
        const int step = juce::jmax (1, count / 2000);

        g.setColour (theme::accent.withAlpha (0.30f));

        for (int i = 0; i < count; i += step)
        {
            const float sideValue = (left[(size_t) i] - right[(size_t) i]) * 0.7071f;
            const float midValue  = (left[(size_t) i] + right[(size_t) i]) * 0.7071f;

            const float x = centre.x + juce::jlimit (-1.0f, 1.0f, sideValue) * radius;
            const float y = centre.y - juce::jlimit (-1.0f, 1.0f, midValue) * radius;

            g.fillRect (x - 0.75f, y - 0.75f, 1.5f, 1.5f);
        }

        g.setFont (theme::labelFont (9.0f));
        g.setColour (theme::textDim);
        g.drawText ("L", juce::Rectangle<float> (centre.x - radius - 12.0f, centre.y - 6.0f, 9.0f, 12.0f),
                    juce::Justification::centredRight);
        g.drawText ("R", juce::Rectangle<float> (centre.x + radius + 3.0f, centre.y - 6.0f, 9.0f, 12.0f),
                    juce::Justification::centredLeft);
    }

private:
    std::vector<float> left, right;
};
