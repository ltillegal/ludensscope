#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include "Theme.h"

class SpectrumView : public juce::Component
{
public:
    SpectrumView() = default;

    static constexpr int numBins = 512;
    static constexpr float minFrequency = 20.0f;
    static constexpr float maxFrequency = 20000.0f;

    void setData (const float* levelsDb, const float* peaksDb) noexcept
    {
        std::copy (levelsDb, levelsDb + numBins, levels.begin());
        std::copy (peaksDb,  peaksDb  + numBins, peaks.begin());
    }

    void setReference (const float* referenceDb, bool active) noexcept
    {
        if (referenceDb != nullptr)
            std::copy (referenceDb, referenceDb + numBins, reference.begin());

        hasReference = active;
        repaint();
    }

    void setRange (float lowHz, float highHz) noexcept
    {
        rangeLow  = std::clamp (lowHz, minFrequency, maxFrequency);
        rangeHigh = std::clamp (highHz, rangeLow * 1.01f, maxFrequency);
        repaint();
    }

    void setGuides (bool shouldShow) noexcept
    {
        showGuides = shouldShow;
        repaint();
    }

    // Emitted when the user zooms with the wheel; the editor applies the range
    // to the waterfall as well so both views stay aligned.
    std::function<void (float, float)> onRangeChange;
    std::function<void()> onRangeReset;

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

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        const auto plot = plotArea();

        if (! plot.contains (e.position) || std::abs (wheel.deltaY) < 1.0e-4f)
            return;

        const float fraction = juce::jlimit (0.0f, 1.0f,
                                             (e.position.x - plot.getX()) / plot.getWidth());
        const float hz = frequencyForFraction (fraction);
        const float octaves = std::log2 (rangeHigh / rangeLow);
        const float newOctaves = juce::jlimit (0.5f, 9.966f,
                                               octaves * std::pow (2.0f, -wheel.deltaY * 0.35f));
        const float logSpan = std::log (2.0f) * newOctaves;

        float low = hz / std::exp (fraction * logSpan);
        float high = low * std::exp (logSpan);

        if (low < minFrequency)
        {
            high *= minFrequency / low;
            low = minFrequency;
        }

        if (high > maxFrequency)
        {
            low *= maxFrequency / high;
            high = maxFrequency;
        }

        if (onRangeChange)
            onRangeChange (low, high);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (plotArea().contains (e.position) && onRangeReset)
            onRangeReset();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        theme::drawPanel (g, bounds);

        auto area = bounds.reduced (12.0f);
        auto titleRow = area.removeFromTop (16.0f);

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("SPECTRUM  " + rangeText(), titleRow, juce::Justification::centredLeft);

        if (hasReference)
        {
            g.setColour (theme::cold);
            g.drawText ("REF", titleRow.withTrimmedRight (44.0f),
                        juce::Justification::centredRight);
            g.setColour (theme::textDim);
        }

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

        g.setFont (theme::monoFont (9.0f));

        // Placement guides: where the fundamental of each element usually sits.
        if (showGuides)
        {
            struct Zone { const char* name; float low; float high; };

            static const Zone zones[] = {
                { "SUB",   20.0f,    60.0f },
                { "KICK",  60.0f,   120.0f },
                { "BASS", 120.0f,   250.0f },
                { "SNARE",250.0f,   500.0f },
                { "VOX",  500.0f,  2000.0f },
                { "PRES",2000.0f,  6000.0f },
                { "AIR", 6000.0f, 20000.0f },
            };

            for (int i = 0; i < 7; ++i)
            {
                const float low = juce::jmax (zones[i].low, rangeLow);
                const float high = juce::jmin (zones[i].high, rangeHigh);

                if (high <= low)
                    continue;

                const float x0 = xFor (fractionForHz (low));
                const float x1 = xFor (fractionForHz (high));

                g.setColour ((i % 2 == 0 ? theme::cold : theme::accent).withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));

                g.setColour (theme::panelLine.withAlpha (0.7f));
                g.drawVerticalLine ((int) std::round (x0), plot.getY(), plot.getBottom());

                g.setFont (theme::labelFont (7.5f));
                g.setColour (theme::textDim.withAlpha (0.9f));
                g.drawText (zones[i].name,
                            juce::Rectangle<float> (x0, plot.getBottom() - 11.0f, x1 - x0, 10.0f),
                            juce::Justification::centred);
            }
        }

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

        const float multipliers[] = { 1.0f, 2.0f, 5.0f };

        for (int decade = 1; decade <= 5; ++decade)
        {
            const float base = std::pow (10.0f, (float) decade);

            for (const float multiplier : multipliers)
            {
                const float hz = multiplier * base;

                if (hz < rangeLow * 0.999f || hz > rangeHigh * 1.001f)
                    continue;

                const float x = xFor (fractionForHz (hz));

                g.setColour (theme::grid);
                g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

                g.setColour (theme::textDim);
                g.drawText (formatHzShort (hz),
                            juce::Rectangle<float> (x - 16.0f, plot.getBottom() + 1.0f, 32.0f, 12.0f),
                            juce::Justification::centred);
            }
        }

        const int steps = juce::jmax (2, (int) std::ceil (plot.getWidth()));

        juce::Path curve, filled;

        for (int s = 0; s < steps; ++s)
        {
            const float fraction = (float) s / (float) (steps - 1);
            const float x = xFor (fraction);
            const float y = yFor (levelAt (levels, fraction));

            if (s == 0)
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

        g.setGradientFill (juce::ColourGradient::vertical (theme::accent.withAlpha (0.30f), plot.getY(),
                                                           theme::accent.withAlpha (0.01f), plot.getBottom()));
        g.fillPath (filled);

        g.setColour (theme::accent.withAlpha (0.08f));
        g.strokePath (curve, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (theme::accent.withAlpha (0.18f));
        g.strokePath (curve, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        g.setColour (theme::accent);
        g.strokePath (curve, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path peakPath;

        for (int s = 0; s < steps; ++s)
        {
            const float fraction = (float) s / (float) (steps - 1);
            const float x = xFor (fraction);
            const float y = yFor (levelAt (peaks, fraction));

            if (s == 0) peakPath.startNewSubPath (x, y);
            else        peakPath.lineTo (x, y);
        }

        g.setColour (theme::accent2.withAlpha (0.55f));
        g.strokePath (peakPath, juce::PathStrokeType (1.0f));

        if (hasReference)
        {
            juce::Path referencePath;

            for (int s = 0; s < steps; ++s)
            {
                const float fraction = (float) s / (float) (steps - 1);
                const float x = xFor (fraction);
                const float y = yFor (levelAt (reference, fraction));

                if (s == 0) referencePath.startNewSubPath (x, y);
                else        referencePath.lineTo (x, y);
            }

            const float dashes[] = { 5.0f, 4.0f };
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, referencePath, dashes, 2);

            g.setColour (theme::cold.withAlpha (0.8f));
            g.strokePath (dashed, juce::PathStrokeType (1.0f));
        }

        // Signature HUD: rough low / mid / high energy balance.
        {
            float bandPower[3] = {};

            for (int i = 0; i < numBins; ++i)
            {
                const float hz = minFrequency * std::pow (maxFrequency / minFrequency,
                                                          (float) i / (float) (numBins - 1));
                const float power = std::pow (10.0f, levels[(size_t) i] / 10.0f) * hz;
                const int band = hz < 250.0f ? 0 : (hz < 4000.0f ? 1 : 2);

                bandPower[band] += power;
            }

            const float total = bandPower[0] + bandPower[1] + bandPower[2];

            if (total > 1.0e-9f)
            {
                const char* labels[] = { "LOW", "MID", "HIGH" };
                const juce::Colour colours[] = { theme::cold, theme::accent, theme::accent2 };

                constexpr float pillWidth = 74.0f;
                constexpr float pillHeight = 16.0f;
                constexpr float pillGap = 4.0f;

                float x = plot.getX() + 8.0f;
                const float y = plot.getY() + 6.0f;

                for (int band = 0; band < 3; ++band)
                {
                    const juce::Rectangle<float> pill (x, y, pillWidth, pillHeight);

                    g.setColour (theme::bg.withAlpha (0.62f));
                    g.fillRoundedRectangle (pill, 7.0f);
                    g.setColour (theme::panelLine.withAlpha (0.9f));
                    g.drawRoundedRectangle (pill, 7.0f, 1.0f);

                    g.setColour (colours[band]);
                    g.fillEllipse (pill.getX() + 7.0f, pill.getCentreY() - 1.5f, 3.0f, 3.0f);

                    g.setFont (theme::labelFont (8.5f));
                    g.setColour (theme::textDim);
                    g.drawText (labels[band], pill.withTrimmedLeft (14.0f),
                                juce::Justification::centredLeft);

                    g.setFont (theme::monoFont (9.0f));
                    g.setColour (theme::text);
                    g.drawText (juce::String (juce::roundToInt (100.0f * bandPower[band] / total)) + "%",
                                pill.withTrimmedRight (7.0f), juce::Justification::centredRight);

                    x += pillWidth + pillGap;
                }
            }
        }

        if (cursorActive && plot.contains (cursorPosition))
        {
            const float fraction = juce::jlimit (0.0f, 1.0f,
                                                 (cursorPosition.x - plot.getX()) / plot.getWidth());
            const float hz = frequencyForFraction (fraction);

            const float levelDb = levelAt (levels, fraction);
            const float peakLevelDb = levelAt (peaks, fraction);
            const float referenceLevelDb = levelAt (reference, fraction);

            const float x = xFor (fraction);
            const float y = yFor (levelDb);

            g.setColour (theme::text.withAlpha (0.45f));
            g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());

            g.setColour (theme::accent);
            g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);

            const int midi = (int) std::lround (69.0 + 12.0 * std::log2 (hz / 440.0));
            const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B" };
            const juce::String note = juce::String (noteNames[((midi % 12) + 12) % 12])
                                      + juce::String (midi / 12 - 1);

            const juce::String line1 = formatHzLong (hz) + "  " + note + "   "
                                       + juce::String (levelDb, 1) + " dBFS";
            const juce::String line2 = "peak " + juce::String (peakLevelDb, 1) + " dBFS";

            const float difference = levelDb - referenceLevelDb;
            const juce::String line3 = "ref " + juce::String (referenceLevelDb, 1)
                                       + "   \xce\x94 " + (difference >= 0.0f ? "+" : "")
                                       + juce::String (difference, 1) + " dB";

            constexpr float boxWidth = 196.0f;
            const float boxHeight = hasReference ? 47.0f : 34.0f;

            const float rawX = x > plot.getCentreX() ? x - boxWidth - 10.0f : x + 10.0f;
            const float boxX = juce::jlimit (plot.getX() + 4.0f,
                                             plot.getRight() - boxWidth - 4.0f, rawX);
            const juce::Rectangle<float> box (boxX, plot.getY() + 30.0f, boxWidth, boxHeight);

            g.setColour (theme::bg.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 7.0f);
            g.setColour (theme::text.withAlpha (0.14f));
            g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);

            auto textArea = box.reduced (7.0f, 4.0f);

            g.setFont (theme::monoFont (10.0f));
            g.setColour (theme::text);
            g.drawText (line1, textArea.removeFromTop (12.0f), juce::Justification::centredLeft);

            g.setColour (theme::textDim);
            g.drawText (line2, textArea.removeFromTop (12.0f), juce::Justification::centredLeft);

            if (hasReference)
            {
                g.setColour (theme::cold);
                g.drawText (line3, textArea.removeFromTop (12.0f),
                            juce::Justification::centredLeft);
            }
        }
    }

private:
    juce::Rectangle<float> plotArea() const
    {
        return getLocalBounds().toFloat()
            .reduced (12.0f)
            .withTrimmedTop (16.0f)
            .withTrimmedBottom (14.0f)
            .withTrimmedRight (30.0f);
    }

    float frequencyForFraction (float fraction) const noexcept
    {
        return rangeLow * std::pow (rangeHigh / rangeLow,
                                    juce::jlimit (0.0f, 1.0f, fraction));
    }

    float fractionForHz (float hz) const noexcept
    {
        return std::log (hz / rangeLow) / std::log (rangeHigh / rangeLow);
    }

    float levelAt (const std::array<float, numBins>& data, float fraction) const noexcept
    {
        const float hz = frequencyForFraction (fraction);
        const float binF = std::log (hz / minFrequency)
                           / std::log (maxFrequency / minFrequency)
                           * (float) (numBins - 1);

        const int b0 = juce::jlimit (0, numBins - 1, (int) binF);
        const int b1 = juce::jmin (b0 + 1, numBins - 1);
        const float mix = juce::jlimit (0.0f, 1.0f, binF - (float) b0);

        return data[(size_t) b0] + mix * (data[(size_t) b1] - data[(size_t) b0]);
    }

    static juce::String formatHzShort (float hz)
    {
        if (hz >= 1000.0f)
        {
            const float k = hz / 1000.0f;
            return (k == std::floor (k) ? juce::String ((int) k) : juce::String (k, 1)) + "k";
        }

        return juce::String ((int) std::lround (hz));
    }

    static juce::String formatHzLong (float hz)
    {
        if (hz >= 1000.0f)
        {
            const float k = hz / 1000.0f;
            return (k == std::floor (k) ? juce::String ((int) k)
                                        : juce::String (k, k >= 10.0f ? 1 : 2)) + " kHz";
        }

        return juce::String ((int) std::lround (hz)) + " Hz";
    }

    juce::String rangeText() const
    {
        const float octaves = std::log2 (rangeHigh / rangeLow);

        juce::String oct;

        if (octaves >= 9.9f)
            oct = "10 oct";
        else if (octaves < 2.0f)
            oct = juce::String (octaves, 1) + " oct";
        else
            oct = juce::String (juce::roundToInt (octaves)) + " oct";

        return formatHzLong (rangeLow) + " - " + formatHzLong (rangeHigh) + "  \xc2\xb7  " + oct;
    }

    std::array<float, numBins> levels {};
    std::array<float, numBins> peaks {};
    std::array<float, numBins> reference {};

    bool hasReference = false;
    bool cursorActive = false;
    bool showGuides = false;
    juce::Point<float> cursorPosition;

    float rangeLow = minFrequency;
    float rangeHigh = maxFrequency;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
