#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>

#include "Theme.h"

class MeterPanel : public juce::Component
{
public:
    void setValues (float newMomentary, float newShortTerm, float newIntegrated,
                    float newTruePeak, float newSamplePeak, float newCorrelation,
                    float newTruePeakHold) noexcept
    {
        momentary = newMomentary;
        shortTerm = newShortTerm;
        integrated = newIntegrated;
        truePeak = newTruePeak;
        samplePeak = newSamplePeak;
        correlation = newCorrelation;
        truePeakHold = newTruePeakHold;
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (theme::panelLine);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

        auto area = bounds.reduced (14.0f);

        auto header = area.removeFromTop (16.0f);
        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("LOUDNESS", header, juce::Justification::centredLeft);
        g.drawText ("BS.1770-4", header, juce::Justification::centredRight);

        drawLoudnessRow (g, area.removeFromTop (44.0f), "MOMENTARY", momentary);
        drawLoudnessRow (g, area.removeFromTop (44.0f), "SHORT-TERM", shortTerm);
        drawLoudnessRow (g, area.removeFromTop (44.0f), "INTEGRATED", integrated);

        area.removeFromTop (8.0f);
        g.setColour (theme::panelLine);
        g.fillRect (area.removeFromTop (1.0f));
        area.removeFromTop (10.0f);

        g.setFont (theme::labelFont (10.0f));
        g.setColour (theme::textDim);
        g.drawText ("TRUE PEAK", area.removeFromTop (14.0f), juce::Justification::centredLeft);

        auto peakRow = area.removeFromTop (32.0f);
        g.setFont (theme::monoFont (27.0f));
        g.setColour (theme::peakColour (truePeak));
        g.drawText (juce::String (truePeak, 1),
                    peakRow.removeFromLeft (peakRow.getWidth() * 0.58f),
                    juce::Justification::centredLeft);
        g.setFont (theme::labelFont (11.0f));
        g.setColour (theme::textDim);
        g.drawText ("dBTP", peakRow, juce::Justification::bottomLeft);

        g.setFont (theme::monoFont (9.5f));
        g.setColour (theme::textDim);
        g.drawText ("hold " + juce::String (truePeakHold, 1),
                    area.removeFromTop (12.0f), juce::Justification::centredRight);

        auto sampleRow = area.removeFromTop (18.0f);
        g.setFont (theme::labelFont (10.0f));
        g.setColour (theme::textDim);
        g.drawText ("SAMPLE PEAK", sampleRow, juce::Justification::centredLeft);
        g.setFont (theme::monoFont (11.0f));
        g.setColour (theme::text);
        g.drawText (juce::String (samplePeak, 1) + " dBFS", sampleRow, juce::Justification::centredRight);

        area.removeFromTop (10.0f);
        g.setColour (theme::panelLine);
        g.fillRect (area.removeFromTop (1.0f));
        area.removeFromTop (10.0f);

        auto corrHeader = area.removeFromTop (14.0f);
        g.setFont (theme::labelFont (10.0f));
        g.setColour (theme::textDim);
        g.drawText ("CORRELATION", corrHeader, juce::Justification::centredLeft);
        g.setFont (theme::monoFont (11.0f));
        g.setColour (theme::text);
        g.drawText (juce::String (correlation, 2), corrHeader, juce::Justification::centredRight);

        auto bar = area.removeFromTop (8.0f);
        g.setColour (theme::grid);
        g.fillRoundedRectangle (bar, 4.0f);

        const float centreX = bar.getCentreX();
        const float halfWidth = bar.getWidth() * 0.5f;
        const float value = juce::jlimit (-1.0f, 1.0f, correlation);

        g.setColour (theme::gridStrong);
        g.drawVerticalLine ((int) centreX, bar.getY() - 2.0f, bar.getBottom() + 2.0f);

        juce::Rectangle<float> filled;

        if (value >= 0.0f)
            filled = { centreX, bar.getY(), halfWidth * value, bar.getHeight() };
        else
            filled = { centreX + halfWidth * value, bar.getY(), -halfWidth * value, bar.getHeight() };

        g.setColour (value < -0.1f ? theme::hot : theme::accent);
        g.fillRect (filled);
    }

private:
    void drawLoudnessRow (juce::Graphics& g, juce::Rectangle<float> row,
                          const juce::String& label, float value) const
    {
        auto textRow = row.removeFromTop (15.0f);

        g.setFont (theme::labelFont (10.0f));
        g.setColour (theme::text);
        g.drawText (label, textRow.removeFromLeft (textRow.getWidth() * 0.5f),
                    juce::Justification::centredLeft);

        g.setFont (theme::monoFont (12.0f));
        g.setColour (theme::text);
        g.drawText (juce::String (value, 1) + " LUFS", textRow, juce::Justification::centredRight);

        auto bar = row.removeFromTop (7.0f);
        g.setColour (theme::grid);
        g.fillRoundedRectangle (bar, 3.5f);

        const float t = juce::jlimit (0.0f, 1.0f, (value + 60.0f) / 60.0f);

        if (t > 0.002f)
        {
            const auto colour = theme::levelColour (value);
            auto filled = bar.withWidth (bar.getWidth() * t);

            g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.55f), bar.getX(), bar.getY(),
                                                     colour, bar.getRight(), bar.getY(), false));
            g.fillRoundedRectangle (filled, 3.5f);
        }
    }

    float momentary = -100.0f;
    float shortTerm = -100.0f;
    float integrated = -100.0f;
    float truePeak = -100.0f;
    float samplePeak = -100.0f;
    float correlation = 0.0f;
    float truePeakHold = -100.0f;
};
