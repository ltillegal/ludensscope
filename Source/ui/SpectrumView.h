#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "GuidePresets.h"
#include "Theme.h"

class SpectrumView : public juce::Component
{
public:
    SpectrumView() = default;

    static constexpr int numBins = 512;
    static constexpr int snapshotCount = 4;
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

    void setGuideZones (const std::vector<GuideZone>& zones)
    {
        guideZones = zones;
        repaint();
    }

    void setSnapshots (const std::array<std::array<float, numBins>, snapshotCount>& data,
                       const std::array<bool, snapshotCount>& present,
                       int selected) noexcept
    {
        snapshots = data;
        snapshotPresent = present;
        selectedSnapshot = juce::jlimit (0, snapshotCount - 1, selected);
        repaint();
    }

    void setTransport (bool enabled, bool playing, bool looping, bool audible,
                       double positionSeconds, double lengthSeconds) noexcept
    {
        transportEnabled = enabled;
        transportPlaying = playing;
        transportLooping = looping;
        transportAudible = audible;
        transportPosition = positionSeconds;
        transportLength = lengthSeconds;
    }

    std::function<void()> onPlayToggle;
    std::function<void()> onLoopToggle;
    std::function<void()> onAbToggle;
    std::function<void (int)> onSnapshotSelect;

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

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < snapshotCount; ++i)
        {
            if (snapshotLetterAreas[(size_t) i].contains (e.position))
            {
                if (onSnapshotSelect)
                    onSnapshotSelect (i);

                return;
            }
        }

        if (! transportEnabled)
            return;

        if (playArea.contains (e.position))
        {
            if (onPlayToggle)
                onPlayToggle();
        }
        else if (loopArea.contains (e.position))
        {
            if (onLoopToggle)
                onLoopToggle();
        }
        else if (abArea.contains (e.position))
        {
            if (onAbToggle)
                onAbToggle();
        }
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

        // Snapshot slots A-D.
        const juce::Rectangle<float> snapArea (titleRow.getRight() - 44.0f - 112.0f,
                                               titleRow.getY(), 108.0f, titleRow.getHeight());
        g.setFont (theme::labelFont (8.5f));
        g.setColour (theme::textDim);
        g.drawText ("SNAP", snapArea.withWidth (32.0f), juce::Justification::centredLeft);

        for (int i = 0; i < snapshotCount; ++i)
        {
            const juce::Rectangle<float> letter (snapArea.getX() + 36.0f + (float) i * 18.0f,
                                                 titleRow.getY() + 2.0f, 15.0f,
                                                 titleRow.getHeight() - 4.0f);
            snapshotLetterAreas[(size_t) i] = letter;

            if (i == selectedSnapshot)
            {
                g.setColour (snapshotColour (i).withAlpha (0.18f));
                g.fillRoundedRectangle (letter.expanded (1.0f, 0.0f), 3.0f);
                g.setColour (snapshotColour (i).withAlpha (0.75f));
                g.drawRoundedRectangle (letter.expanded (1.0f, 0.0f), 3.0f, 1.0f);
            }

            g.setFont (theme::labelFont (9.0f));
            g.setColour (snapshotPresent[(size_t) i] ? snapshotColour (i)
                                                     : theme::textDim.withAlpha (0.45f));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                        letter, juce::Justification::centred);
        }

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
            for (size_t zoneIndex = 0; zoneIndex < guideZones.size(); ++zoneIndex)
            {
                const auto& zone = guideZones[zoneIndex];

                const float low = juce::jmax (zone.low, rangeLow);
                const float high = juce::jmin (zone.high, rangeHigh);

                if (high <= low)
                    continue;

                const float x0 = xFor (fractionForHz (low));
                const float x1 = xFor (fractionForHz (high));

                g.setColour ((zoneIndex % 2 == 0 ? theme::cold : theme::accent).withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));

                g.setColour (theme::panelLine.withAlpha (0.7f));
                g.drawVerticalLine ((int) std::round (x0), plot.getY(), plot.getBottom());

                g.setFont (theme::labelFont (7.5f));
                g.setColour (theme::textDim.withAlpha (0.9f));
                g.drawText (zone.name,
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

            g.setColour (theme::text.withAlpha (0.65f));
            g.strokePath (dashed, juce::PathStrokeType (1.0f));
        }

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int slot = 0; slot < snapshotCount; ++slot)
            {
                if (! snapshotPresent[(size_t) slot])
                    continue;

                const bool selected = slot == selectedSnapshot;

                if ((pass == 0) == selected)
                    continue;

                juce::Path path;

                for (int s = 0; s < steps; ++s)
                {
                    const float fraction = (float) s / (float) (steps - 1);
                    const float x = xFor (fraction);
                    const float y = yFor (levelAt (snapshots[(size_t) slot], fraction));

                    if (s == 0) path.startNewSubPath (x, y);
                    else        path.lineTo (x, y);
                }

                const float dashes[] = { 4.0f, 4.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, path, dashes, 2);

                g.setColour (snapshotColour (slot).withAlpha (selected ? 0.95f : 0.45f));
                g.strokePath (dashed, juce::PathStrokeType (selected ? 1.3f : 0.9f));
            }
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

        // Reference transport, only when a reference is loaded.
        if (transportEnabled)
        {
            const juce::Rectangle<float> bar (plot.getRight() - 224.0f, plot.getY() + 6.0f,
                                              216.0f, 18.0f);

            g.setColour (theme::bg.withAlpha (0.66f));
            g.fillRoundedRectangle (bar, 9.0f);
            g.setColour (theme::panelLine);
            g.drawRoundedRectangle (bar, 9.0f, 1.0f);

            playArea = { bar.getX() + 4.0f, bar.getY(), 24.0f, bar.getHeight() };
            loopArea = { bar.getX() + 34.0f, bar.getY(), 44.0f, bar.getHeight() };
            abArea = { bar.getX() + 86.0f, bar.getY(), 42.0f, bar.getHeight() };

            g.setColour (transportPlaying ? theme::accent2 : theme::accent);

            if (transportPlaying)
            {
                g.fillRect (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 3.0f,
                            6.0f, 6.0f);
            }
            else
            {
                juce::Path triangle;
                triangle.addTriangle (playArea.getCentreX() - 3.0f, playArea.getCentreY() - 4.0f,
                                      playArea.getCentreX() - 3.0f, playArea.getCentreY() + 4.0f,
                                      playArea.getCentreX() + 4.0f, playArea.getCentreY());
                g.fillPath (triangle);
            }

            g.setFont (theme::labelFont (8.5f));
            g.setColour (transportLooping ? theme::accent2 : theme::textDim);
            g.drawText ("LOOP", loopArea, juce::Justification::centred);

            g.setColour (transportAudible ? theme::accent : theme::textDim);
            g.drawText (transportAudible ? "REF" : "MIX", abArea, juce::Justification::centred);

            g.setFont (theme::monoFont (9.0f));
            g.setColour (theme::textDim);
            g.drawText (formatTime (transportPosition) + " / " + formatTime (transportLength),
                        bar.withTrimmedLeft (134.0f).withTrimmedRight (8.0f),
                        juce::Justification::centredRight);
        }

        if (cursorActive && plot.contains (cursorPosition))
        {
            const float fraction = juce::jlimit (0.0f, 1.0f,
                                                 (cursorPosition.x - plot.getX()) / plot.getWidth());
            const float hz = frequencyForFraction (fraction);

            const float levelDb = levelAt (levels, fraction);
            const float peakLevelDb = levelAt (peaks, fraction);
            const float referenceLevelDb = levelAt (reference, fraction);

            const bool hasSelectedSnapshot = snapshotPresent[(size_t) selectedSnapshot];
            const float snapshotLevelDb = hasSelectedSnapshot
                ? levelAt (snapshots[(size_t) selectedSnapshot], fraction)
                : 0.0f;

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

            const float snapshotDifference = levelDb - snapshotLevelDb;
            const juce::String snapshotLine =
                juce::String::charToString ((juce::juce_wchar) ('A' + selectedSnapshot))
                + " " + juce::String (snapshotLevelDb, 1)
                + "   \xce\x94 " + (snapshotDifference >= 0.0f ? "+" : "")
                + juce::String (snapshotDifference, 1) + " dB";

            constexpr float boxWidth = 196.0f;
            const float boxHeight = 34.0f
                                    + (hasReference ? 12.0f : 0.0f)
                                    + (hasSelectedSnapshot ? 12.0f : 0.0f);

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
                g.setColour (theme::text.withAlpha (0.8f));
                g.drawText (line3, textArea.removeFromTop (12.0f),
                            juce::Justification::centredLeft);
            }

            if (hasSelectedSnapshot)
            {
                g.setColour (snapshotColour (selectedSnapshot));
                g.drawText (snapshotLine, textArea.removeFromTop (12.0f),
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

    static juce::Colour snapshotColour (int slot)
    {
        switch (slot)
        {
            case 0:  return theme::cold;
            case 1:  return theme::accent2;
            case 2:  return theme::hot;
            default: return juce::Colour (0xffb18cff);
        }
    }

    static juce::String formatTime (double seconds)
    {
        const int total = juce::jmax (0, (int) seconds);
        return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
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

    std::array<std::array<float, numBins>, snapshotCount> snapshots {};
    std::array<bool, snapshotCount> snapshotPresent {};
    int selectedSnapshot = 0;
    std::array<juce::Rectangle<float>, snapshotCount> snapshotLetterAreas {};

    bool hasReference = false;
    bool cursorActive = false;
    bool showGuides = false;
    std::vector<GuideZone> guideZones;

    bool transportEnabled = false;
    bool transportPlaying = false;
    bool transportLooping = true;
    bool transportAudible = false;
    double transportPosition = 0.0;
    double transportLength = 0.0;

    juce::Rectangle<float> playArea, loopArea, abArea;

    juce::Point<float> cursorPosition;

    float rangeLow = minFrequency;
    float rangeHigh = maxFrequency;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
