#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "Theme.h"

// Stereo volume-unit meter emulating the standard VU ballistics:
// 0 VU is the digital reference (default -18 dBFS, EBU), the scale is
// -20..+3 VU, and the needle takes 300 ms to reach 99% of a step
// (IEC 60268-17 / ANSI C16.5-1942). The 300 ms response is modelled with
// a one-pole whose time constant is 300 ms / ln(100) = 65.1 ms.
class VuMeterView : public juce::Component
{
public:
    VuMeterView() = default;

    void setZeroDbfs (float newZero) noexcept
    {
        zeroDbfs = juce::jlimit (-30.0f, -6.0f, newZero);
        repaint();
    }

    void setLevels (float rmsLeft, float rmsRight, float newZero,
                    double deltaSeconds) noexcept
    {
        setZeroDbfs (newZero);

        const auto rawVu = [this] (float rms)
        {
            const float db = 20.0f * std::log10 (juce::jmax (rms, 1.0e-6f));
            return db - zeroDbfs;
        };

        const float tau = 0.0651f; // 300 ms to 99%
        const float coeff = (float) (1.0 - std::exp (-juce::jmax (0.001, deltaSeconds) / tau));

        const float rawL = rawVu (rmsLeft);
        const float rawR = rawVu (rmsRight);

        displayL += coeff * (juce::jlimit (-20.0f, 3.0f, rawL) - displayL);
        displayR += coeff * (juce::jlimit (-20.0f, 3.0f, rawR) - displayR);

        numericL = rawL;
        numericR = rawR;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        theme::drawPanel (g, bounds);

        const float scale = juce::jlimit (0.80f, 1.30f, (float) getHeight() / 175.0f);
        auto area = bounds.reduced (10.0f * scale);

        auto header = area.removeFromTop (15.0f * scale);
        g.setFont (theme::labelFont (10.5f * scale));
        g.setColour (theme::textDim);
        g.drawText ("VU", header, juce::Justification::centredLeft);

        g.setFont (theme::labelFont (7.5f * scale));
        g.setColour (theme::textDim.withAlpha (0.9f));
        g.drawText ("0 VU = " + juce::String ((int) zeroDbfs) + " dBFS",
                    header, juce::Justification::centredRight);

        // Geometry: an upper arc around a pivot near the bottom.
        const float footerHeight = 13.0f * scale;
        const float pivotX = area.getCentreX();
        const float pivotY = area.getBottom() - footerHeight - 3.0f * scale;
        const float radius = juce::jmin (area.getWidth() * 0.477f,
                                         area.getHeight() * 0.88f);
        constexpr float startAngle = 2.7925f; // 160 deg
        constexpr float endAngle = 0.3491f;   // 20 deg

        const auto pointFor = [&] (float vu)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (vu + 20.0f) / 23.0f);
            const float angle = startAngle + (endAngle - startAngle) * t;
            return juce::Point<float> (pivotX + radius * std::cos (angle),
                                       pivotY - radius * std::sin (angle));
        };

        juce::Path arc;

        for (int i = 0; i <= 72; ++i)
        {
            const auto p = pointFor (-20.0f + 23.0f * (float) i / 72.0f);

            if (i == 0)
                arc.startNewSubPath (p);
            else
                arc.lineTo (p);
        }

        g.setColour (theme::gridStrong);
        g.strokePath (arc, juce::PathStrokeType (1.0f));

        const float marks[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f,
                                -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        const float labels[] = { -20.0f, -10.0f, -5.0f, -3.0f, 0.0f, 3.0f };

        for (const float mark : marks)
        {
            const auto p = pointFor (mark);
            const auto outward = (p - juce::Point<float> (pivotX, pivotY))
                                 / juce::jmax (1.0e-3f, radius);
            const bool zero = std::abs (mark) < 0.01f;

            g.setColour (zero ? theme::accent2 : theme::textDim.withAlpha (0.75f));
            g.drawLine ({ p, p + outward * 4.5f * scale }, zero ? 1.8f : 1.0f);
        }

        g.setFont (theme::labelFont (7.0f * scale));

        for (const float label : labels)
        {
            const auto p = pointFor (label);
            const auto outward = (p - juce::Point<float> (pivotX, pivotY))
                                 / juce::jmax (1.0e-3f, radius);
            const auto labelPoint = p + outward * 11.0f * scale;
            const bool zero = std::abs (label) < 0.01f;

            g.setColour (zero ? theme::accent2 : theme::textDim);
            g.drawText (label > 0.0f ? "+" + juce::String ((int) label)
                                     : juce::String ((int) label),
                        juce::Rectangle<float> (labelPoint.x - 9.0f * scale,
                                                labelPoint.y - 5.0f * scale,
                                                18.0f * scale, 10.0f * scale),
                        juce::Justification::centred);
        }

        const auto drawNeedle = [&] (float vu, juce::Colour colour)
        {
            const auto p = pointFor (vu);
            g.setColour (colour);
            g.drawLine ({ juce::Point<float> (pivotX, pivotY), p }, 1.8f * scale);
            g.fillEllipse (pivotX - 2.5f * scale, pivotY - 2.5f * scale,
                           5.0f * scale, 5.0f * scale);
        };

        drawNeedle (displayR, theme::cold);
        drawNeedle (displayL, theme::accent);

        const auto formatVu = [] (float value)
        {
            return (value >= 0.0f ? "+" : "") + juce::String (value, 1);
        };

        const auto footer = getLocalBounds().toFloat().removeFromBottom (footerHeight);

        g.setFont (theme::monoFont (8.5f * scale));
        g.setColour (theme::textDim);
        g.drawText ("L " + formatVu (numericL) + " VU",
                    footer.withTrimmedLeft (7.0f * scale), juce::Justification::centredLeft);
        g.drawText ("R " + formatVu (numericR) + " VU",
                    footer.withTrimmedRight (7.0f * scale), juce::Justification::centredRight);
    }

private:
    float zeroDbfs = -18.0f;
    float displayL = -20.0f, displayR = -20.0f;
    float numericL = -20.0f, numericR = -20.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeterView)
};
