#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <vector>

#include "GuidePresets.h"
#include "Theme.h"

class SpectrumView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    SpectrumView()
    {
        setOpaque (true);
        setBufferedToImage (true);
        setTooltip ("Wheel: zoom. Double-click: reset. Shift/Alt-click: solo a guide zone.");
    }

    static constexpr int numBins = 512;
    static constexpr int snapshotCount = 4;
    static constexpr float minFrequency = 20.0f;
    static constexpr float maxFrequency = 20000.0f;

    void setData (const float* levelsDb, const float* peaksDb) noexcept
    {
        std::copy (levelsDb, levelsDb + numBins, levels.begin());
        std::copy (peaksDb,  peaksDb  + numBins, peaks.begin());
    }

    void setReference (const float* referenceDb, bool active,
                       const float* low = nullptr, const float* high = nullptr) noexcept
    {
        if (referenceDb != nullptr)
            std::copy (referenceDb, referenceDb + numBins, reference.begin());

        hasReferenceBand = active && low != nullptr && high != nullptr;

        if (hasReferenceBand)
        {
            std::copy (low, low + numBins, referenceLow.begin());
            std::copy (high, high + numBins, referenceHigh.begin());
        }

        hasReference = active;
        repaint();
    }

    void setMatchSuggestions (const juce::StringArray& lines, bool show)
    {
        matchLines = lines;
        showMatch = show;
        repaint();
    }

    void setShowMatch (bool show)
    {
        showMatch = show;
        repaint();
    }

    void setRange (float lowHz, float highHz) noexcept
    {
        rangeLow  = std::clamp (lowHz, minFrequency, maxFrequency / 1.01f);
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

    void setSoloZone (int zone) noexcept
    {
        soloZone = zone;
        repaint();
    }

    std::function<void (int)> onGuideSolo;

    void setSnapshots (const std::array<std::array<float, numBins>, snapshotCount>& data,
                       const std::array<bool, snapshotCount>& present,
                       int selected) noexcept
    {
        snapshots = data;
        snapshotPresent = present;
        selectedSnapshot = juce::jlimit (0, snapshotCount - 1, selected);
        repaint();
    }

    void setPeakMarkers (bool shouldShow) noexcept
    {
        showPeakMarkers = shouldShow;
        repaint();
    }

    void setDifferenceShading (bool shouldShow) noexcept
    {
        showDifference = shouldShow;
        repaint();
    }

    void setDifferenceSource (int source) noexcept
    {
        differenceSource = juce::jlimit (0, 2, source);
        repaint();
    }

    void setNoteGrid (bool shouldShow) noexcept
    {
        showNoteGrid = shouldShow;
        repaint();
    }

    void setSideData (const float* levelsDb, const float* peaksDb) noexcept
    {
        std::copy (levelsDb, levelsDb + numBins, sideLevels.begin());
        std::copy (peaksDb, peaksDb + numBins, sidePeaks.begin());
        hasSide = true;
    }

    void setShowSide (bool shouldShow) noexcept
    {
        showSide = shouldShow;
        repaint();
    }

    bool hasReferenceCurve() const noexcept { return hasReference; }

    void setReferencePlaceholder (const juce::String& text)
    {
        referencePlaceholder = text;
        repaint();
    }

    void setUiScale (float scale) noexcept
    {
        const float next = juce::jlimit (0.75f, 1.5f, scale);

        if (std::abs (next - uiScale) < 0.001f)
            return;

        uiScale = next;
        updateSnapshotAreas();
        repaint();
    }

    float getUiScale() const noexcept { return uiScale; }

    void resized() override { updateSnapshotAreas(); }

    void setGainStaging (bool enabled, float zeroDbfs) noexcept
    {
        gainStaging = enabled;
        vuZeroDbfs = zeroDbfs;
    }

    void setTargetCurve (const float* curveDb, bool active, const juce::String& name)
    {
        if (curveDb != nullptr)
            std::copy (curveDb, curveDb + numBins, targetCurve.begin());

        hasTargetCurve = active && curveDb != nullptr;
        targetCurveName = name;
        repaint();
    }

    std::function<void (int)> onSnapshotSelect;

    // Emitted when the user zooms with the wheel; the editor applies the range
    // to the waterfall as well so both views stay aligned.
    std::function<void (float, float)> onRangeChange;
    std::function<void()> onRangeReset;

    void mouseMove (const juce::MouseEvent& e) override
    {
        cursorActive = true;
        cursorPosition = e.position;

        bool overSnapshot = false;

        for (int i = 0; i < snapshotCount; ++i)
        {
            if (snapshotLetterAreas[(size_t) i].contains (e.position))
            {
                overSnapshot = true;
                setTooltip ("Snapshot " + juce::String::charToString (
                    (juce::juce_wchar) ('A' + i)) + ": click to show it on the spectrum");
                break;
            }
        }

        if (overSnapshot)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        else if (plotArea().contains (e.position))
        {
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            setTooltip ("Wheel: zoom. Double-click: reset. Shift/Alt-click: solo a guide zone.");
        }
        else
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            setTooltip (juce::String());
        }

        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        cursorActive = false;
        setMouseCursor (juce::MouseCursor::NormalCursor);
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

        const bool guideModifier = e.mods.isShiftDown() || e.mods.isAltDown();

        auto triggerGuide = [this, guideModifier] (juce::Point<float> pos)
        {
            if (! guideModifier || ! showGuides || ! plotArea().contains (pos))
                return;

            const auto plot = plotArea();
            const float fraction = juce::jlimit (0.0f, 1.0f,
                                                 (pos.x - plot.getX()) / plot.getWidth());
            const float hz = frequencyForFraction (fraction);

            for (size_t i = 0; i < guideZones.size(); ++i)
            {
                if (hz >= guideZones[i].low && hz <= guideZones[i].high)
                {
                    if (onGuideSolo)
                        onGuideSolo ((int) i);

                    return;
                }
            }
        };

        triggerGuide (e.position);
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        theme::drawPanel (g, bounds);

        auto area = bounds.reduced (12.0f);
        auto titleRow = area.removeFromTop (16.0f);

        g.setFont (theme::detailFont (10.5f, uiScale));
        g.setColour (theme::textDim);
        g.drawText ("SPECTRUM  " + rangeText(), titleRow, juce::Justification::centredLeft);

        // Snapshot slots A-D, scaled with the rest of the UI.
        const float snapScale = uiScale;
        const juce::Rectangle<float> snapArea (titleRow.getRight() - 44.0f * snapScale
                                                   - 112.0f * snapScale,
                                               titleRow.getY(), 108.0f * snapScale,
                                               titleRow.getHeight());
        g.setFont (theme::labelFont (8.5f * snapScale));
        g.setColour (theme::textDim);
        g.drawText ("SNAP", snapArea.withWidth (32.0f * snapScale),
                    juce::Justification::centredLeft);

        for (int i = 0; i < snapshotCount; ++i)
        {
            const auto& letter = snapshotLetterAreas[(size_t) i];

            if (i == selectedSnapshot)
            {
                g.setColour (snapshotColour (i).withAlpha (0.18f));
                g.fillRoundedRectangle (letter.expanded (1.0f, 0.0f), 3.0f);
                g.setColour (snapshotColour (i).withAlpha (0.75f));
                g.drawRoundedRectangle (letter.expanded (1.0f, 0.0f), 3.0f, 1.0f);
            }

            g.setFont (theme::labelFont (9.0f * snapScale));
            g.setColour (snapshotPresent[(size_t) i] ? snapshotColour (i)
                                                     : theme::textDim.withAlpha (0.45f));
            g.drawText (juce::String::charToString ((juce::juce_wchar) ('A' + i)),
                        letter, juce::Justification::centred);
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

        g.setFont (theme::detailMonoFont (9.0f, uiScale));

        // Note grid: octave C markers for tonal checks.
        if (showNoteGrid)
        {
            static const float noteHz[] = { 32.703f, 65.406f, 130.813f, 261.626f,
                                            523.251f, 1046.502f, 2093.005f, 4186.009f };

            g.setColour (theme::gridStrong.withAlpha (0.55f));

            for (const float hz : noteHz)
            {
                if (hz < rangeLow || hz > rangeHigh)
                    continue;

                const float x = xFor (fractionForHz (hz));
                g.drawVerticalLine ((int) std::round (x), plot.getY(), plot.getBottom());
            }
        }

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
                const bool soloed = (int) zoneIndex == soloZone;

                g.setColour ((soloed ? theme::accent
                                     : (zoneIndex % 2 == 0 ? theme::cold : theme::accent))
                                 .withAlpha (soloed ? 0.10f : 0.035f));
                g.fillRect (juce::Rectangle<float> (x0, plot.getY(), x1 - x0, plot.getHeight()));

                g.setColour (soloed ? theme::accent.withAlpha (0.8f)
                                    : theme::panelLine.withAlpha (0.7f));
                g.drawVerticalLine ((int) std::round (x0), plot.getY(), plot.getBottom());

                if (soloed)
                    g.drawVerticalLine ((int) std::round (x1), plot.getY(), plot.getBottom());

                g.setFont (theme::detailFont (9.0f, uiScale));
                g.setColour (soloed ? theme::accent : theme::textDim.withAlpha (0.9f));
                g.drawText (zone.name,
                            juce::Rectangle<float> (x0, plot.getBottom() - 12.0f, x1 - x0, 11.0f),
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

        if (showSide && hasSide)
        {
            juce::Path sidePath;

            for (int s = 0; s < steps; ++s)
            {
                const float fraction = (float) s / (float) (steps - 1);
                const float x = xFor (fraction);
                const float y = yFor (levelAt (sideLevels, fraction));

                if (s == 0) sidePath.startNewSubPath (x, y);
                else        sidePath.lineTo (x, y);
            }

            g.setColour (theme::cold.withAlpha (0.75f));
            g.strokePath (sidePath, juce::PathStrokeType (1.1f));
        }

        if (showMatch && ! matchLines.isEmpty())
        {
            const int count = juce::jmin (4, matchLines.size());
            const juce::Rectangle<float> box (plot.getX() + 8.0f, plot.getY() + 26.0f,
                                              230.0f, 14.0f + (float) count * 11.0f);
            g.setColour (theme::bg.withAlpha (0.62f));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (theme::panelLine);
            g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);

            g.setFont (theme::detailFont (8.0f, uiScale));
            g.setColour (theme::accent2);
            g.drawText ("REF MATCH  (mix vs reference)", box.reduced (7.0f, 0.0f).removeFromTop (13.0f),
                        juce::Justification::centredLeft);

            g.setFont (theme::detailFont (8.0f, uiScale));

            for (int i = 0; i < count; ++i)
            {
                g.setColour (theme::text);
                g.drawText (matchLines[i],
                            box.reduced (7.0f, 0.0f).withTrimmedTop (13.0f + (float) i * 11.0f)
                               .removeFromTop (11.0f),
                            juce::Justification::centredLeft);
            }
        }

        if (hasReference)
        {
            if (hasReferenceBand)
            {
                juce::Path band;

                for (int s = 0; s < steps; ++s)
                {
                    const float fraction = (float) s / (float) (steps - 1);
                    const float x = xFor (fraction);
                    const float y = yFor (levelAt (referenceHigh, fraction));

                    if (s == 0) band.startNewSubPath (x, y);
                    else        band.lineTo (x, y);
                }

                for (int s = steps - 1; s >= 0; --s)
                {
                    const float fraction = (float) s / (float) (steps - 1);
                    band.lineTo (xFor (fraction), yFor (levelAt (referenceLow, fraction)));
                }

                band.closeSubPath();
                g.setColour (theme::text.withAlpha (0.055f));
                g.fillPath (band);
            }

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
        else if (referencePlaceholder.isNotEmpty())
        {
            const juce::Rectangle<float> hint (plot.getRight() - 250.0f, plot.getY() + 6.0f,
                                               250.0f, 18.0f);
            g.setColour (theme::panel.withAlpha (0.55f));
            g.fillRoundedRectangle (hint, 5.0f);
            g.setColour (theme::textDim);
            g.setFont (theme::detailFont (8.5f, uiScale));
            g.drawText (referencePlaceholder, hint, juce::Justification::centred);
        }

        if (hasTargetCurve)
        {
            juce::Path targetPath;

            for (int s = 0; s < steps; ++s)
            {
                const float fraction = (float) s / (float) (steps - 1);
                const float x = xFor (fraction);
                const float y = yFor (levelAt (targetCurve, fraction));

                if (s == 0) targetPath.startNewSubPath (x, y);
                else        targetPath.lineTo (x, y);
            }

            const float dashes[] = { 3.0f, 3.0f };
            juce::Path dashedTarget;
            juce::PathStrokeType (1.2f).createDashedStroke (dashedTarget, targetPath, dashes, 2);

            g.setColour (theme::accent2.withAlpha (0.9f));
            g.strokePath (dashedTarget, juce::PathStrokeType (1.2f));

            const juce::Rectangle<float> chip (plot.getCentreX() - 90.0f, plot.getY() + 6.0f,
                                               180.0f, 16.0f);
            g.setColour (theme::panel.withAlpha (0.85f));
            g.fillRoundedRectangle (chip, 4.0f);
            g.setColour (theme::accent2);
            g.setFont (theme::detailFont (8.5f, uiScale));
            g.drawText ("TARGET  " + targetCurveName.substring (0, 24), chip,
                        juce::Justification::centred);
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

        // Difference shading: A is the selected snapshot (or the live curve),
        // B depends on the selected source: next snapshot, REF FILE or live.
        if (showDifference)
        {
            const bool haveSnapshot = snapshotPresent[(size_t) selectedSnapshot];
            const float* curveB = nullptr;

            if (differenceSource == 1 && hasReference)
            {
                curveB = reference.data();
            }
            else if (differenceSource == 2)
            {
                curveB = levels.data();
            }
            else
            {
                if (haveSnapshot)
                {
                    for (int slot = 0; slot < snapshotCount; ++slot)
                    {
                        if (slot != selectedSnapshot && snapshotPresent[(size_t) slot])
                        {
                            curveB = snapshots[(size_t) slot].data();
                            break;
                        }
                    }
                }

                if (curveB == nullptr)
                    curveB = levels.data();
            }

            const int shadingSteps = juce::jmax (2, (int) std::ceil (plot.getWidth()));

            for (int s = 0; s < shadingSteps; ++s)
            {
                const float fraction = (float) s / (float) (shadingSteps - 1);
                const float x = xFor (fraction);
                const int bin = juce::jlimit (0, numBins - 1,
                                              (int) std::lround (fraction * (numBins - 1)));
                const float dbA = haveSnapshot
                    ? snapshots[(size_t) selectedSnapshot][(size_t) bin]
                    : levels[(size_t) bin];
                const float dbB = curveB[bin];

                const float yA = yFor (dbA);
                const float yB = yFor (dbB);

                g.setColour ((dbA >= dbB ? theme::accent : theme::hot).withAlpha (0.10f));
                g.fillRect (juce::Rectangle<float> (x, juce::jmin (yA, yB), 1.0f,
                                                    std::abs (yA - yB)));
            }
        }

        // Automatic markers on the strongest spectral peaks.
        if (showPeakMarkers)
        {
            struct Peak { int bin; float db; };

            std::vector<Peak> peaks;

            for (int i = 8; i < numBins - 8; ++i)
            {
                const float db = levels[(size_t) i];

                if (db < -84.0f)
                    continue;

                if (db >= levels[(size_t) (i - 1)] && db > levels[(size_t) (i + 1)])
                    peaks.push_back ({ i, db });
            }

            std::sort (peaks.begin(), peaks.end(),
                       [] (const Peak& a, const Peak& b) { return a.db > b.db; });

            std::vector<float> usedX;
            int drawn = 0;

            for (const auto& peak : peaks)
            {
                if (drawn >= 3)
                    break;

                const float fraction = (float) peak.bin / (float) (numBins - 1);
                const float hz = minFrequency * std::pow (maxFrequency / minFrequency, fraction);

                if (hz < rangeLow || hz > rangeHigh)
                    continue;

                const float x = xFor (fractionForHz (hz));

                bool crowded = false;

                for (const float other : usedX)
                {
                    if (std::abs (other - x) < 48.0f)
                        crowded = true;
                }

                if (crowded)
                    continue;

                usedX.push_back (x);
                ++drawn;

                const float y = yFor (peak.db);

                g.setColour (theme::accent2);
                g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);

                const juce::String label = formatHzShort (hz);
                const float labelY = (y - 18.0f < plot.getY() + 28.0f) ? y + 6.0f : y - 18.0f;

                g.setColour (theme::bg.withAlpha (0.7f));
                g.fillRoundedRectangle (x - 18.0f, labelY, 36.0f, 12.0f, 3.0f);

                g.setColour (theme::accent2);
                g.setFont (theme::detailMonoFont (8.5f, uiScale));
                g.drawText (label, juce::Rectangle<float> (x - 18.0f, labelY, 36.0f, 12.0f),
                            juce::Justification::centred);
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

                const float ps = uiScale;
                const float pillWidth = 74.0f * ps;
                const float pillHeight = 16.0f * ps;
                const float pillGap = 4.0f * ps;

                float x = plot.getX() + 8.0f * ps;
                const float y = plot.getY() + 6.0f * ps;

                for (int band = 0; band < 3; ++band)
                {
                    const juce::Rectangle<float> pill (x, y, pillWidth, pillHeight);

                    g.setColour (theme::bg.withAlpha (0.62f));
                    g.fillRoundedRectangle (pill, 7.0f * ps);
                    g.setColour (theme::panelLine.withAlpha (0.9f));
                    g.drawRoundedRectangle (pill, 7.0f * ps, 1.0f);

                    g.setColour (colours[band]);
                    g.fillEllipse (pill.getX() + 7.0f * ps, pill.getCentreY() - 1.5f * ps,
                                   3.0f * ps, 3.0f * ps);

                    g.setFont (theme::detailFont (8.5f, uiScale));
                    g.setColour (theme::textDim);
                    g.drawText (labels[band], pill.withTrimmedLeft (14.0f * ps),
                                juce::Justification::centredLeft);

                    g.setFont (theme::detailMonoFont (9.0f, uiScale));
                    g.setColour (theme::text);
                    g.drawText (juce::String (juce::roundToInt (100.0f * bandPower[band] / total)) + "%",
                                pill.withTrimmedRight (7.0f * ps), juce::Justification::centredRight);

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

            // Q of the nearest spectral peak, from its -3 dB width.
            float qValue = 0.0f;

            if (levelDb > -80.0f)
            {
                const int cursorBin = juce::jlimit (0, numBins - 1,
                    (int) std::lround (fraction * (float) (numBins - 1)));
                int peakBin = -1;

                for (int i = juce::jmax (1, cursorBin - 24);
                     i <= juce::jmin (numBins - 2, cursorBin + 24); ++i)
                {
                    if (levels[(size_t) i] >= levels[(size_t) (i - 1)]
                        && levels[(size_t) i] > levels[(size_t) (i + 1)]
                        && levels[(size_t) i] > -84.0f)
                    {
                        if (peakBin < 0 || levels[(size_t) i] > levels[(size_t) peakBin])
                            peakBin = i;
                    }
                }

                if (peakBin >= 0 && levels[(size_t) peakBin] - levelDb < 8.0f)
                {
                    const float threshold = levels[(size_t) peakBin] - 3.0f;
                    int left = peakBin;
                    int right = peakBin;

                    while (left > 0 && levels[(size_t) (left - 1)] > threshold)
                        --left;

                    while (right < numBins - 1 && levels[(size_t) (right + 1)] > threshold)
                        ++right;

                    const auto hzForBin = [] (int bin)
                    {
                        return minFrequency * std::pow (maxFrequency / minFrequency,
                                                        (float) bin / (float) (numBins - 1));
                    };

                    const float bandwidth = hzForBin (right) - hzForBin (left);
                    const float peakHz = hzForBin (peakBin);

                    if (bandwidth > 0.0f)
                        qValue = peakHz / bandwidth;
                }
            }

            juce::String line2 = "peak " + juce::String (peakLevelDb, 1) + " dBFS";

            if (qValue > 0.0f)
                line2 += "   Q " + juce::String (qValue, 1);

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
            const juce::Rectangle<float> box (boxX, plot.getY() + 48.0f * uiScale + 4.0f,
                                              boxWidth, boxHeight);

            g.setColour (theme::bg.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 7.0f);
            g.setColour (theme::text.withAlpha (0.14f));
            g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);

            auto textArea = box.reduced (7.0f, 4.0f);

            g.setFont (theme::detailMonoFont (10.0f, uiScale));
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

    // Hit areas must be valid before the first paint, otherwise the first
    // click on a SNAP letter is swallowed.
    void updateSnapshotAreas()
    {
        const auto titleRow = getLocalBounds().toFloat()
                                  .reduced (12.0f)
                                  .removeFromTop (16.0f);
        const float s = uiScale;
        const float snapX = titleRow.getRight() - (44.0f + 112.0f) * s;

        for (int i = 0; i < snapshotCount; ++i)
        {
            snapshotLetterAreas[(size_t) i] = juce::Rectangle<float> (
                snapX + 36.0f * s + (float) i * 18.0f * s,
                titleRow.getY() + 2.0f * s,
                15.0f * s,
                titleRow.getHeight() - 4.0f * s);
        }
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
            default: return theme::accent; // SNAP D follows the palette like A-C
        }
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
    std::array<float, numBins> targetCurve {};
    std::array<float, numBins> sideLevels {};
    std::array<float, numBins> sidePeaks {};
    bool hasSide = false;
    bool showSide = false;

    std::array<std::array<float, numBins>, snapshotCount> snapshots {};
    std::array<bool, snapshotCount> snapshotPresent {};
    int selectedSnapshot = 0;
    std::array<juce::Rectangle<float>, snapshotCount> snapshotLetterAreas {};

    bool hasReference = false;
    juce::String referencePlaceholder;
    bool hasTargetCurve = false;
    juce::String targetCurveName;
    bool cursorActive = false;
    bool showGuides = false;
    std::vector<GuideZone> guideZones;
    int soloZone = -1;

    bool gainStaging = false;
    float vuZeroDbfs = -18.0f;
    bool hasReferenceBand = false;
    std::array<float, numBins> referenceLow {};
    std::array<float, numBins> referenceHigh {};
    bool showMatch = false;
    juce::StringArray matchLines;
    float uiScale = 1.0f;
    bool showPeakMarkers = true;
    bool showDifference = false;
    int differenceSource = 0;
    bool showNoteGrid = false;

    juce::Point<float> cursorPosition;

    float rangeLow = minFrequency;
    float rangeHigh = maxFrequency;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumView)
};
