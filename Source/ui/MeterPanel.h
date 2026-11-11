#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

#include "Theme.h"

class MeterPanel : public juce::Component
{
public:
    MeterPanel() = default;

    void setValues (float newMomentary, float newShortTerm, float newIntegrated,
                    float newTruePeak, float newSamplePeak, float newCorrelation,
                    float newTruePeakHold, float newLra, float newPlr,
                    std::uint64_t newClipSamples, int newTruePeakOvers) noexcept
    {
        momentary = newMomentary;
        shortTerm = newShortTerm;
        integrated = newIntegrated;
        truePeak = newTruePeak;
        samplePeak = newSamplePeak;
        correlation = newCorrelation;
        truePeakHold = newTruePeakHold;
        lra = newLra;
        plr = newPlr;
        clipSamples = newClipSamples;
        truePeakOvers = newTruePeakOvers;
    }

    // Measured integrated loudness target in LUFS; values below -90 disable it.
    void setTarget (float lufs) noexcept
    {
        targetEnabled = lufs > -90.0f;
        targetLufs = targetEnabled ? lufs : -14.0f;
    }

    std::function<void (float)> onTargetChange;

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

        g.setFont (theme::labelFont (9.0f));
        g.drawText ("BS.1770-4", header.withTrimmedLeft (62.0f),
                    juce::Justification::centredLeft);

        auto targetRect = header.removeFromRight (86.0f);
        targetArea = targetRect.toNearestInt();

        g.setFont (theme::labelFont (10.0f));
        g.setColour (targetEnabled ? theme::accent2 : theme::textDim);
        g.drawText (targetEnabled ? "TGT " + juce::String (targetLufs, 1) : "TARGET",
                    targetRect.withTrimmedRight (12.0f), juce::Justification::centredRight);

        auto chevron = targetRect.removeFromRight (12.0f).withSizeKeepingCentre (7.0f, 4.0f);
        juce::Path chevronPath;
        chevronPath.addTriangle (chevron.getX(), chevron.getY(),
                                 chevron.getRight(), chevron.getY(),
                                 chevron.getCentreX(), chevron.getBottom());
        g.fillPath (chevronPath);

        drawLoudnessRow (g, area.removeFromTop (44.0f), "MOMENTARY", momentary, false);
        drawLoudnessRow (g, area.removeFromTop (44.0f), "SHORT-TERM", shortTerm, false);
        drawLoudnessRow (g, area.removeFromTop (44.0f), "INTEGRATED", integrated, true);

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

        area.removeFromTop (6.0f);
        drawInfoRow (g, area.removeFromTop (16.0f), "LRA",
                     lra >= 0.0f ? juce::String (lra, 1) + " LU" : juce::String ("--"),
                     theme::text);
        drawInfoRow (g, area.removeFromTop (16.0f), "PLR",
                     plr > -99.0f ? juce::String (plr, 1) + " dB" : juce::String ("--"),
                     theme::text);
        drawInfoRow (g, area.removeFromTop (16.0f), "CLIP",
                     juce::String ((juce::int64) clipSamples) + " smp / "
                         + juce::String (truePeakOvers) + " TP",
                     (clipSamples > 0 || truePeakOvers > 0) ? theme::hot : theme::textDim);

        area.removeFromTop (8.0f);
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

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! targetArea.contains (e.getPosition()))
            return;

        juce::PopupMenu menu;
        menu.addItem (1, "-14.0 LUFS  (streaming)");
        menu.addItem (2, "-16.0 LUFS  (podcast)");
        menu.addItem (3, "-18.0 LUFS");
        menu.addItem (4, "-23.0 LUFS  (EBU R128)");
        menu.addSeparator();
        menu.addItem (5, "Target: off");

        juce::Component::SafePointer<MeterPanel> safe (this);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safe] (int result)
                            {
                                if (safe == nullptr || result <= 0)
                                    return;

                                const float values[] = { -14.0f, -16.0f, -18.0f, -23.0f, -100.0f };
                                const float chosen = values[juce::jlimit (0, 4, result - 1)];

                                safe->setTarget (chosen);

                                if (safe->onTargetChange)
                                    safe->onTargetChange (chosen);

                                safe->repaint();
                            });
    }

private:
    void drawLoudnessRow (juce::Graphics& g, juce::Rectangle<float> row,
                          const juce::String& label, float value, bool withTarget) const
    {
        auto textRow = row.removeFromTop (15.0f);

        g.setFont (theme::labelFont (10.0f));
        g.setColour (theme::text);
        g.drawText (label, textRow.removeFromLeft (textRow.getWidth() * 0.5f),
                    juce::Justification::centredLeft);

        const bool inTarget = withTarget && targetEnabled
                              && std::abs (value - targetLufs) <= 0.5f;

        g.setFont (theme::monoFont (12.0f));
        g.setColour (inTarget ? theme::accent : theme::text);
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

        if (withTarget && targetEnabled)
        {
            const float targetT = juce::jlimit (0.0f, 1.0f, (targetLufs + 60.0f) / 60.0f);
            const float x = bar.getX() + bar.getWidth() * targetT;

            g.setColour (theme::text);
            g.fillRect (juce::Rectangle<float> (x - 0.75f, bar.getY() - 2.0f,
                                                1.5f, bar.getHeight() + 4.0f));

            const float delta = value - targetLufs;
            const juce::String deltaText = (delta >= 0.0f ? "+" : "")
                                           + juce::String (delta, 1) + " LU";

            g.setFont (theme::monoFont (9.0f));
            g.setColour (inTarget ? theme::accent : theme::textDim);
            g.drawText (deltaText, row.removeFromTop (10.0f),
                        juce::Justification::centredRight);
        }
    }

    void drawInfoRow (juce::Graphics& g, juce::Rectangle<float> row,
                      const juce::String& label, const juce::String& value,
                      juce::Colour valueColour) const
    {
        g.setFont (theme::labelFont (9.5f));
        g.setColour (theme::textDim);
        g.drawText (label, row, juce::Justification::centredLeft);

        g.setFont (theme::monoFont (10.5f));
        g.setColour (valueColour);
        g.drawText (value, row, juce::Justification::centredRight);
    }

    float momentary = -100.0f;
    float shortTerm = -100.0f;
    float integrated = -100.0f;
    float truePeak = -100.0f;
    float samplePeak = -100.0f;
    float correlation = 0.0f;
    float truePeakHold = -100.0f;
    float lra = -1.0f;
    float plr = -100.0f;
    std::uint64_t clipSamples = 0;
    int truePeakOvers = 0;

    bool targetEnabled = true;
    float targetLufs = -14.0f;

    juce::Rectangle<int> targetArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};
