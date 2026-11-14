#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

#include "Theme.h"
#include "dsp/LoudnessMeter.h"

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

    void setMomentaryLufs (float lufs) noexcept { momentaryLufs = lufs; }

    void setGainStaging (bool enabled, float zeroDbfs) noexcept
    {
        gainStaging = enabled;
        vuZeroDbfs = zeroDbfs;
    }

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    }

    int getWindowSamples() const noexcept { return windowSamples; }
    float getVerticalGain() const noexcept { return verticalGain; }

    void setWindowSamples (int samples) noexcept
    {
        windowSamples = juce::jlimit (0, 1 << 20, samples);
        repaint();
    }

    void setVerticalGain (float gain) noexcept
    {
        verticalGain = juce::jlimit (0.25f, 8.0f, gain);
        repaint();
    }

    std::function<void()> onSettingsChanged;

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

        if (onSettingsChanged)
            onSettingsChanged();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        windowSamples = 0;
        verticalGain = 1.0f;
        repaint();

        if (onSettingsChanged)
            onSettingsChanged();
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
        const juce::String windowText = milliseconds >= 1000.0
            ? juce::String (milliseconds / 1000.0, 2) + " s"
            : juce::String (milliseconds, milliseconds < 10.0 ? 2 : 1) + " ms";

        g.setFont (theme::labelFont (10.5f));
        g.setColour (theme::textDim);
        g.drawText ("OSCILLOSCOPE  " + windowText, header, juce::Justification::centredLeft);

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

        if (gainStaging)
        {
            const float levels[] = { 0.0f, -6.0f, -12.0f, vuZeroDbfs };

            for (const float level : levels)
            {
                const float amplitude = juce::Decibels::decibelsToGain (level);
                const float y = yFor (amplitude);
                const bool zero = std::abs (level - vuZeroDbfs) < 0.01f;

                g.setColour (zero ? theme::accent2.withAlpha (0.60f)
                                  : theme::textDim.withAlpha (0.25f));
                g.drawHorizontalLine ((int) std::round (y), plot.getX(), plot.getRight());

                g.setFont (theme::labelFont (7.0f));
                g.setColour (zero ? theme::accent2 : theme::textDim);
                g.drawText (zero ? "0 VU" : juce::String ((int) level) + " dB",
                            juce::Rectangle<float> (plot.getX() + 4.0f, y - 9.0f,
                                                    56.0f, 9.0f),
                            juce::Justification::centredLeft);
            }
        }

        // Min/max envelope per pixel column: at long time windows this draws
        // the waveform shape instead of a sparse, aliased polyline.
        const auto drawChannel = [&] (const std::vector<float>& data, juce::Colour colour)
        {
            auto& tops = envelopeTops;
            auto& bottoms = envelopeBottoms;
            tops.resize ((size_t) columns);
            bottoms.resize ((size_t) columns);

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

        if (drawCount >= 8)
        {
            double sumSquares = 0.0;
            float peak = 0.0f;

            for (int i = 0; i < drawCount; ++i)
            {
                const float l = left[(size_t) (start + i)];
                const float r = right[(size_t) (start + i)];

                sumSquares += (double) l * l + (double) r * r;
                peak = juce::jmax (peak, juce::jmax (std::abs (l), std::abs (r)));
            }

            const float rms = (float) std::sqrt (sumSquares / (double) (2 * drawCount));
            const float peakDb = 20.0f * std::log10 (juce::jmax (peak, 1.0e-5f));
            const float rmsDb = 20.0f * std::log10 (juce::jmax (rms, 1.0e-5f));

            const float lufs = momentaryLufs;

            const juce::Rectangle<float> stats (plot.getX() + 8.0f, plot.getY() + 6.0f, 360.0f, 18.0f);

            g.setColour (theme::bg.withAlpha (0.66f));
            g.fillRoundedRectangle (stats, 9.0f);
            g.setColour (theme::panelLine);
            g.drawRoundedRectangle (stats, 9.0f, 1.0f);

            g.setFont (theme::labelFont (8.5f));
            g.setColour (theme::textDim);
            g.drawText ("PEAK", stats.withTrimmedLeft (9.0f).withWidth (28.0f),
                        juce::Justification::centredLeft);
            g.drawText ("RMS", stats.withTrimmedLeft (84.0f).withWidth (26.0f),
                        juce::Justification::centredLeft);
            g.drawText ("CREST", stats.withTrimmedLeft (160.0f).withWidth (34.0f),
                        juce::Justification::centredLeft);

            g.setFont (theme::monoFont (9.5f));
            g.setColour (theme::accent);
            g.drawText (juce::String (peakDb, 1), stats.withTrimmedLeft (36.0f).withWidth (42.0f),
                        juce::Justification::centredLeft);
            g.setColour (theme::cold);
            g.drawText (juce::String (rmsDb, 1), stats.withTrimmedLeft (110.0f).withWidth (42.0f),
                        juce::Justification::centredLeft);
            g.setColour (theme::accent2);
            g.drawText (juce::String (peakDb - rmsDb, 1) + " dB",
                        stats.withTrimmedLeft (194.0f).withWidth (86.0f),
                        juce::Justification::centredLeft);

            g.setFont (theme::labelFont (8.5f));
            g.setColour (theme::textDim);
            g.drawText ("LUFS", stats.withTrimmedLeft (250.0f).withWidth (30.0f),
                        juce::Justification::centredLeft);

            g.setFont (theme::monoFont (9.5f));
            g.setColour (theme::text);
            g.drawText (lufs > -70.0f ? juce::String (lufs, 1) : juce::String ("--"),
                        stats.withTrimmedLeft (282.0f).withWidth (64.0f),
                        juce::Justification::centredLeft);
        }
    }

private:
    std::vector<float> left, right;
    bool isFrozen = false;
    int windowSamples = 0;
    float verticalGain = 1.0f;
    double sampleRate = 48000.0;

    std::vector<float> envelopeTops, envelopeBottoms;
    float momentaryLufs = -100.0f;
    bool gainStaging = false;
    float vuZeroDbfs = -18.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscilloscopeView)
};
