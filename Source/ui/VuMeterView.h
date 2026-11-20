#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "Theme.h"

// Stereo volume-unit meter following IEC 60268-17 / ANSI C16.5:
// - the pointer deflection is proportional to voltage, so the dB scale is
//   non-linear (0 VU sits at ~68.5% of the -20..+3 arc)
// - ballistics: critically damped second-order system in the voltage domain
//   (zeta = 1.0, omega_n = 13.3 rad/s) plus 80 ms input smoothing by default,
//   ≈500 ms to 99% of a step, not a one-pole in dB
// - 0 VU is the digital reference (default -18 dBFS, EBU)
class VuMeterView : public juce::Component
{
public:
    VuMeterView()
    {
        setOpaque (true);
        setBufferedToImage (true);
    }

    void setUiScale (float scale) noexcept
    {
        const float newScale = juce::jlimit (0.75f, 1.5f, scale);

        if (std::abs (newScale - uiScale) < 0.001f)
            return;

        uiScale = newScale;
        repaint();
    }

    // Puts both needles back on their mechanical rest stop (left, -20 VU).
    void setRest() noexcept
    {
        displayL = displayR = -20.0f;
        voltageL = voltageR = 0.1f;
        velocityL = velocityR = 0.0f;
        powerL = powerR = 0.0f;
        numericL = numericR = -20.0f;
        repaint();
    }

    void setZeroDbfs (float newZero) noexcept
    {
        zeroDbfs = juce::jlimit (-30.0f, -6.0f, newZero);
        repaint();
    }

    void setInputSmoothingMs (int ms) noexcept
    {
        const int next = (ms == 40 || ms == 150) ? ms : 80;

        if (next == inputSmoothingMs)
            return;

        inputSmoothingMs = next;
    }

    int getInputSmoothingMs() const noexcept { return inputSmoothingMs; }
    float getDisplayLevelL() const noexcept { return displayL; }
    float getDisplayLevelR() const noexcept { return displayR; }
    float getNumericLevelL() const noexcept { return numericL; }
    float getNumericLevelR() const noexcept { return numericR; }

    void setLevels (float rmsLeft, float rmsRight, float newZero,
                    double deltaSeconds) noexcept
    {
        setZeroDbfs (newZero);

        // Smooth in the power domain, not in dB. A host writes silent blocks
        // into the same FIFO as audible ones; a dB smoother dives to -100 VU
        // on every silent tick and pins the needle to its rest stop while the
        // digits jump around. Power averaging treats those gaps as silence
        // instead of as -100 VU steps.
        const auto rawPower = [] (float rms)
        {
            return juce::jmax (rms * rms, 1.0e-12f);
        };

        const double smoothingSeconds = 0.001 * (double) inputSmoothingMs;
        const float levelCoeff = (float) (1.0 - std::exp (-juce::jmax (0.001, deltaSeconds) / smoothingSeconds));
        powerL += levelCoeff * (rawPower (rmsLeft) - powerL);
        powerR += levelCoeff * (rawPower (rmsRight) - powerR);

        const auto rawVu = [this] (float power)
        {
            return 10.0f * std::log10 (power) - zeroDbfs;
        };

        const float rawL = rawVu (powerL);
        const float rawR = rawVu (powerR);

        // VU ballistics in the voltage domain: critically damped, ~500 ms to
        // 99% of a step, no overshoot.
        constexpr float zeta = 1.0f;
        constexpr float omegaN = 13.3f; // rad/s -> ~500 ms settling at zeta 1
        const float dt = juce::jlimit (0.001f, 0.1f, (float) deltaSeconds);

        const auto targetVoltage = [] (float vu)
        {
            return std::pow (10.0f, juce::jlimit (-20.0f, 3.0f, vu) / 20.0f);
        };

        const auto step = [&] (float& v, float& velocity, float target)
        {
            velocity += (omegaN * omegaN * (target - v)
                         - 2.0f * zeta * omegaN * velocity) * dt;
            v += velocity * dt;
            v = juce::jlimit (0.07f, 1.6f, v); // mechanical stops

            // Don't let velocity wind up against a stop: that would keep the
            // needle pinned there long after the signal comes back.
            if ((v <= 0.07f && velocity < 0.0f) || (v >= 1.6f && velocity > 0.0f))
                velocity = 0.0f;
        };

        step (voltageL, velocityL, targetVoltage (rawL));
        step (voltageR, velocityR, targetVoltage (rawR));

        displayL = 20.0f * std::log10 (juce::jmax (voltageL, 1.0e-6f));
        displayR = 20.0f * std::log10 (juce::jmax (voltageR, 1.0e-6f));

        // Show exactly what the needle indicates. The input smoother alone was
        // too fast next to the 500 ms ballistics: the digits jumped around
        // while the pointer barely moved, which looked like a broken meter.
        numericL = displayL;
        numericR = displayR;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        theme::drawPanel (g, bounds);

        const float scale = juce::jlimit (0.72f, 1.25f, (float) getHeight() / 165.0f) * uiScale;
        auto area = bounds.reduced (8.0f * scale);

        auto header = area.removeFromTop (13.0f * scale);
        g.setFont (theme::labelFont (10.5f * scale));
        g.setColour (theme::textDim);
        g.drawText ("VU", header, juce::Justification::centredLeft);

        g.setFont (theme::labelFont (7.5f * scale));
        g.setColour (theme::textDim.withAlpha (0.9f));
        g.drawText ("0 VU = " + juce::String ((int) zeroDbfs) + " dBFS",
                    header, juce::Justification::centredRight);

        const float gap = 7.0f * scale;
        auto leftFace = area.removeFromLeft ((area.getWidth() - gap) * 0.5f);
        area.removeFromLeft (gap);
        auto rightFace = area;

        drawFace (g, leftFace, displayL, numericL, "L", theme::accent, scale);
        drawFace (g, rightFace, displayR, numericR, "R", theme::cold, scale);
    }

private:
    void drawFace (juce::Graphics& g, juce::Rectangle<float> face, float vu,
                   float numeric, const juce::String& label,
                   juce::Colour needleColour, float scale)
    {
        const float fs = juce::jlimit (0.9f, 1.3f, face.getWidth() / 190.0f);

        g.setColour (theme::bg.withAlpha (0.62f));
        g.fillRoundedRectangle (face, 4.0f * scale);
        g.setColour (theme::panelLine);
        g.drawRoundedRectangle (face.reduced (0.5f), 4.0f * scale, 1.0f);

        auto inner = face.reduced (7.0f * scale);
        auto top = inner.removeFromTop (11.0f * scale);

        g.setFont (theme::labelFont (8.0f * fs));
        g.setColour (theme::textDim);
        g.drawText ("VU", top.removeFromLeft (18.0f * fs),
                    juce::Justification::centredLeft);
        g.drawText (label, top, juce::Justification::centredLeft);
        g.drawText (numeric > -99.0f
                        ? (numeric >= 0.0f ? "+" : "") + juce::String (numeric, 1)
                        : juce::String ("--"),
                    top, juce::Justification::centredRight);

        const float pivotX = inner.getCentreX();
        const float pivotY = inner.getBottom() - 3.0f * scale;
        const float radius = juce::jmin (inner.getWidth() * 0.455f,
                                         inner.getHeight() * 0.90f);
        constexpr float startAngle = 2.7925f; // 160 deg
        constexpr float endAngle = 0.3491f;   // 20 deg

        // VU deflection is proportional to voltage, not dB: the classic scale
        // is non-linear, with 0 VU at ~70% of the arc (IEC 60268-17 / ANSI C16.5).
        const auto pointFor = [&] (float value)
        {
            const float v = std::pow (10.0f, juce::jlimit (-20.0f, 3.0f, value) / 20.0f);
            const float t = juce::jlimit (0.0f, 1.0f, (v - 0.1f) / (1.41253754f - 0.1f));
            const float angle = startAngle + (endAngle - startAngle) * t;
            return juce::Point<float> (pivotX + radius * std::cos (angle),
                                       pivotY - radius * std::sin (angle));
        };

        juce::Path arc, arcHot;

        for (int i = 0; i <= 96; ++i)
        {
            const float value = -20.0f + 23.0f * (float) i / 96.0f;
            const auto p = pointFor (value);

            if (value <= 0.001f)
            {
                if (arc.isEmpty()) arc.startNewSubPath (p);
                else              arc.lineTo (p);
            }

            if (value >= -0.001f)
            {
                if (arcHot.isEmpty()) arcHot.startNewSubPath (p);
                else                 arcHot.lineTo (p);
            }
        }

        g.setColour (theme::gridStrong);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
        g.setColour (theme::hot);
        g.strokePath (arcHot, juce::PathStrokeType (1.6f));

        const float marks[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f,
                                -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };
        const float labels[] = { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f,
                                 -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f };

        for (const float mark : marks)
        {
            const auto p = pointFor (mark);
            const auto outward = (p - juce::Point<float> (pivotX, pivotY))
                                 / juce::jmax (1.0e-3f, radius);
            const bool over = mark >= -0.001f;
            const bool zero = std::abs (mark) < 0.01f;

            g.setColour (over ? theme::hot : theme::textDim.withAlpha (0.75f));
            g.drawLine ({ p, p + outward * 4.0f * scale },
                        zero ? 1.8f : (over ? 1.2f : 1.0f));
        }

        g.setFont (theme::labelFont (8.2f * fs));

        for (const float labelValue : labels)
        {
            const auto p = pointFor (labelValue);
            const auto outward = (p - juce::Point<float> (pivotX, pivotY))
                                 / juce::jmax (1.0e-3f, radius);
            const auto labelPoint = p - outward * 11.0f * scale;
            const bool over = labelValue >= -0.001f;

            g.setColour (over ? theme::hot : theme::textDim);
            g.drawText (labelValue > 0.0f ? "+" + juce::String ((int) labelValue)
                                          : juce::String ((int) labelValue),
                        juce::Rectangle<float> (labelPoint.x - 12.0f * fs,
                                                labelPoint.y - 5.0f * fs,
                                                24.0f * fs, 10.0f * fs),
                        juce::Justification::centred);
        }

        g.setFont (theme::labelFont (6.0f * fs));
        g.setColour (theme::textDim.withAlpha (0.55f));
        g.drawText ("LUDENS", face.removeFromBottom (9.0f * fs),
                    juce::Justification::centred);

        const auto needlePoint = pointFor (vu);
        g.setColour (needleColour);
        g.drawLine ({ juce::Point<float> (pivotX, pivotY), needlePoint }, 1.8f * scale);
        g.fillEllipse (pivotX - 2.4f * scale, pivotY - 2.4f * scale,
                       4.8f * scale, 4.8f * scale);
    }

private:
    float uiScale = 1.0f;
    int inputSmoothingMs = 80;
    float powerL = 0.0f, powerR = 0.0f;
    float zeroDbfs = -18.0f;
    float displayL = -20.0f, displayR = -20.0f;
    float voltageL = 0.1f, voltageR = 0.1f;   // -20 VU at rest
    float velocityL = 0.0f, velocityR = 0.0f;
    float numericL = -20.0f, numericR = -20.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeterView)
};
