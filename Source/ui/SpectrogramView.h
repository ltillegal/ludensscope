#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "Theme.h"

// Scrolling spectrogram. Rows are written into a ring buffer image, so no
// pixel copying is needed per frame; the newest row is drawn at the bottom.
class SpectrogramView : public juce::Component
{
public:
    SpectrogramView()
    {
        setOpaque (true);
        setBufferedToImage (true);

        for (int i = 0; i < paletteSize; ++i)
            palette[(size_t) i] = heat ((float) i / (float) (paletteSize - 1));

        paletteTheme = theme::getTheme();
    }

    void push (const float* levelsDb, int numBins)
    {
        if (! image.isValid() || numBins <= 0)
            return;

        // Two editor ticks per row by default: the panel covers less history
        // than the raw 30 Hz scroll, which is easier to read.
        if ((pushCounter++ % pushDivider) != 0)
            return;

        juce::Image::BitmapData data (image, juce::Image::BitmapData::writeOnly);

        if ((int) rowT.size() != width)
        {
            rowT.assign ((size_t) width, 0.0f);
            blurredT.assign ((size_t) width, 0.0f);
            smoothT.assign ((size_t) width, 0.0f);
            hasPreviousRow = false;
        }

        for (int x = 0; x < width; ++x)
        {
            const float fraction = (float) x / (float) juce::jmax (1, width - 1);
            const float db = levelAt (levelsDb, numBins, fraction);
            rowT[(size_t) x] = juce::jlimit (0.0f, 1.0f,
                                             (db - minDb) / (maxDb - minDb));
        }

        // A 3-tap horizontal blur removes the hard steps between log bins;
        // blending with the previous row rounds off the vertical block edges.
        for (int x = 0; x < width; ++x)
        {
            const int left = juce::jmax (0, x - 1);
            const int right = juce::jmin (width - 1, x + 1);

            blurredT[(size_t) x] = 0.25f * rowT[(size_t) left]
                                 + 0.50f * rowT[(size_t) x]
                                 + 0.25f * rowT[(size_t) right];
        }

        for (int x = 0; x < width; ++x)
        {
            float t = blurredT[(size_t) x];

            if (hasPreviousRow)
                t = 0.70f * t + 0.30f * smoothT[(size_t) x];

            smoothT[(size_t) x] = t;
            data.setPixelColour (x, writeRow,
                                 palette[(size_t) (int) std::lround (t * (float) (paletteSize - 1))]);
        }

        hasPreviousRow = true;
        writeRow = (writeRow + 1) % height;
        repaint();
    }

    void setRange (float lowHz, float highHz)
    {
        if (std::abs (lowHz - rangeLow) < 0.5f && std::abs (highHz - rangeHigh) < 0.5f)
            return;

        rangeLow = lowHz;
        rangeHigh = highHz;
        clear();
    }

    void setPushDivider (int divider)
    {
        pushDivider = juce::jlimit (1, 8, divider);
        pushCounter = 0;
        repaint();
    }

    void setUiScale (float scale) noexcept
    {
        const float next = juce::jlimit (0.75f, 1.5f, scale);

        if (std::abs (next - uiScale) < 0.001f)
            return;

        uiScale = next;

        // The image covers the plot area, which itself depends on the scale.
        if (getWidth() > 0 && getHeight() > 0)
            resized();

        repaint();
    }

    float getUiScale() const noexcept { return uiScale; }

    // How much time the visible panel covers when only every `divider`-th
    // editor tick becomes a row. Uses the current panel height, so menu labels
    // match what the waterfall actually shows.
    double historySecondsFor (int divider) const noexcept
    {
        return height > 0 ? (double) height * (double) juce::jlimit (1, 8, divider) / 30.0
                          : 0.0;
    }

    void clear()
    {
        if (image.isValid())
            image.clear (image.getBounds(), theme::bg);

        std::fill (smoothT.begin(), smoothT.end(), 0.0f);
        hasPreviousRow = false;
        writeRow = 0;
        repaint();
    }

    void resized() override
    {
        const auto plot = plotBounds().toNearestInt();

        if (plot.getWidth() != width || plot.getHeight() != height)
        {
            const auto oldImage = image;
            const int oldWidth = width;
            const int oldHeight = height;
            const int oldWriteRow = writeRow;

            width = juce::jmax (1, plot.getWidth());
            height = juce::jmax (1, plot.getHeight());

            image = juce::Image (juce::Image::ARGB, width, height, true);
            image.clear (image.getBounds(), theme::bg);

            if (oldImage.isValid() && oldWidth > 0 && oldHeight > 0)
            {
                juce::Graphics g (image);
                g.drawImage (oldImage, 0, 0, width, height, 0, 0, oldWidth, oldHeight);
            }

            writeRow = oldHeight > 0
                           ? juce::jlimit (0, height - 1,
                                           (int) std::lround ((double) oldWriteRow
                                                              * (double) height
                                                              / (double) oldHeight))
                           : 0;

            rowT.assign ((size_t) width, 0.0f);
            blurredT.assign ((size_t) width, 0.0f);
            smoothT.assign ((size_t) width, 0.0f);
            hasPreviousRow = false;
        }
    }

    void paint (juce::Graphics& g) override
    {
        refreshThemePalette();

        const auto bounds = getLocalBounds().toFloat();

        theme::drawPanel (g, bounds);

        auto area = bounds.reduced (12.0f * uiScale);
        auto titleRow = area.removeFromTop (14.0f * uiScale);

        g.setFont (theme::detailFont (10.5f, uiScale));
        g.setColour (theme::textDim);
        auto titleLeft = titleRow.removeFromLeft (titleRow.getWidth() * 0.55f);
        g.drawFittedText ("WATERFALL", titleLeft.toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.65f);

        g.setFont (theme::detailFont (9.0f, uiScale));
        g.drawFittedText ("older \xe2\x86\x91  " + juce::String (historySeconds(), 1) + " s",
                          titleRow.toNearestInt(), juce::Justification::centredRight, 1, 0.6f);

        const auto plot = plotBounds();

        if (! image.isValid())
            return;

        // The waterfall is a block inside a rounded panel, so its own corners
        // are rounded too - otherwise it reads as a square pasted on.
        juce::Graphics::ScopedSaveState plotState (g);
        juce::Path plotClip;
        plotClip.addRoundedRectangle (plot, 6.0f * uiScale);
        g.reduceClipRegion (plotClip);

        const int oldestRows = height - writeRow;

        if (oldestRows > 0)
            g.drawImage (image, (int) plot.getX(), (int) plot.getY(),
                         width, oldestRows, 0, writeRow, width, oldestRows);

        if (writeRow > 0)
            g.drawImage (image, (int) plot.getX(), (int) plot.getY() + oldestRows,
                         width, writeRow, 0, 0, width, writeRow);

        // dB colour scale in the margin that the spectrum uses for its labels.
        const float legendX = plot.getRight() + 5.0f;
        const juce::Rectangle<float> legend (legendX, plot.getY(), 6.0f, plot.getHeight());

        juce::ColourGradient scale (heat (1.0f), legendX, plot.getY(),
                                    heat (0.0f), legendX, plot.getBottom(), false);
        g.setGradientFill (scale);
        g.fillRoundedRectangle (legend.withWidth (legend.getWidth()), 3.0f * uiScale);

        g.setFont (theme::detailMonoFont (8.0f, uiScale));
        g.setColour (theme::textDim);
        const float legendScale = uiScale;
        g.drawText ("0", juce::Rectangle<float> (legendX + 9.0f * legendScale, plot.getY(),
                                                 18.0f * legendScale, 10.0f * legendScale),
                    juce::Justification::centredLeft);
        g.drawText ("-96", juce::Rectangle<float> (legendX + 9.0f * legendScale,
                                                    plot.getBottom() - 10.0f * legendScale,
                                                    18.0f * legendScale, 10.0f * legendScale),
                    juce::Justification::centredLeft);
    }

private:
    // Linear interpolation between log bins, matching the spectrum view, so
    // adjacent waterfall pixels no longer snap to the same FFT bin.
    float levelAt (const float* levelsDb, int numBins, float fraction) const noexcept
    {
        const float hz = rangeLow * std::pow (rangeHigh / rangeLow,
                                             juce::jlimit (0.0f, 1.0f, fraction));
        const float binF = std::log (hz / 20.0f) / std::log (1000.0f)
                           * (float) (numBins - 1);
        const int b0 = juce::jlimit (0, numBins - 1, (int) binF);
        const int b1 = juce::jmin (b0 + 1, numBins - 1);
        const float mix = juce::jlimit (0.0f, 1.0f, binF - (float) b0);

        return levelsDb[(size_t) b0]
               + mix * (levelsDb[(size_t) b1] - levelsDb[(size_t) b0]);
    }

    double historySeconds() const noexcept
    {
        return historySecondsFor (pushDivider);
    }

    juce::Rectangle<float> plotBounds() const
    {
        return getLocalBounds().toFloat()
            .reduced (12.0f * uiScale)
            .withTrimmedTop (16.0f * uiScale)
            .withTrimmedRight (30.0f * uiScale);
    }

    static juce::Colour heat (float t)
    {
        static constexpr int numStops = 7;

        const juce::Colour stops[numStops] = {
            theme::bg,
            theme::panel,
            theme::gridStrong,
            theme::accent,
            theme::accent2,
            theme::hot,
            theme::hot.brighter (0.35f),
        };

        static const float positions[numStops] = {
            0.0f, 0.35f, 0.55f, 0.70f, 0.85f, 0.95f, 1.0f,
        };

        for (int i = 1; i < numStops; ++i)
        {
            if (t <= positions[i])
            {
                const float span = positions[i] - positions[i - 1];
                const float f = span > 0.0f ? (t - positions[i - 1]) / span : 0.0f;

                return stops[i - 1].interpolatedWith (stops[i], f);
            }
        }

        return stops[numStops - 1];
    }

    static constexpr int paletteSize = 256;
    static constexpr float minDb = -96.0f;
    static constexpr float maxDb = 0.0f;

    std::array<juce::Colour, paletteSize> palette {};

    void refreshThemePalette()
    {
        if (paletteTheme == theme::getTheme())
            return;

        for (int i = 0; i < paletteSize; ++i)
            palette[(size_t) i] = heat ((float) i / (float) (paletteSize - 1));

        paletteTheme = theme::getTheme();
    }

    juce::Image image;
    std::vector<float> rowT;
    std::vector<float> blurredT;
    std::vector<float> smoothT;
    bool hasPreviousRow = false;

    int width = 0;
    int height = 0;
    int writeRow = 0;
    int pushCounter = 0;
    int pushDivider = 2;
    int paletteTheme = -1;

    float uiScale = 1.0f;
    float rangeLow = 20.0f;
    float rangeHigh = 20000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrogramView)
};
