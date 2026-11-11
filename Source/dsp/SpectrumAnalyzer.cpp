#include "SpectrumAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    constexpr float kMinFrequency = 20.0f;
    constexpr float kMaxFrequency = 20000.0f;
    constexpr float kFloorDb = -120.0f;
}

SpectrumAnalyzer::SpectrumAnalyzer()
{
    // Periodic Hann window.
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) fftSize));

    displayDb.fill (kFloorDb);
    peakDb.fill (kFloorDb);
}

void SpectrumAnalyzer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    double windowSum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) fftSize));
        windowSum += (double) window[(size_t) i];
    }

    // JUCE's forward FFT is unnormalised, so a sine of amplitude A produces a
    // single-sided peak magnitude of A * windowSum / 2. Scale by 2 / windowSum
    // to make the display read dBFS.
    magnitudeScale = (float) (2.0 / std::max (1.0, windowSum));

    const float minRatio = kMinFrequency / (float) sampleRate;
    const float maxRatio = kMaxFrequency / (float) sampleRate;
    const float logRange = std::log (maxRatio / minRatio);

    for (int i = 0; i < numBins; ++i)
    {
        const float f0 = minRatio * std::exp (logRange * (float) i / (float) numBins);
        const float f1 = minRatio * std::exp (logRange * (float) (i + 1) / (float) numBins);

        int b0 = (int) std::floor (f0 * (float) fftSize);
        int b1 = (int) std::ceil  (f1 * (float) fftSize);

        b0 = juce::jlimit (1, fftSize / 2 - 1, b0);
        b1 = juce::jlimit (b0 + 1, fftSize / 2, b1);

        binStart[(size_t) i] = b0;
        binEnd[(size_t) i] = b1;
    }

    history.fill (0.0f);
    displayDb.fill (kFloorDb);
    peakDb.fill (kFloorDb);
}

void SpectrumAnalyzer::push (const float* left, const float* right, int numSamples) noexcept
{
    const int historySize = (int) history.size();
    const int n = std::min (numSamples, historySize);

    if (n <= 0)
        return;

    const int keep = historySize - n;

    if (keep > 0)
        std::memmove (history.data(), history.data() + n, sizeof (float) * (size_t) keep);

    for (int i = 0; i < n; ++i)
        history[(size_t) (keep + i)] = 0.5f * (left[i] + right[i]);
}

void SpectrumAnalyzer::update()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);

    const int offset = (int) history.size() - fftSize;

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = history[(size_t) (offset + i)] * window[(size_t) i];

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int i = 0; i < numBins; ++i)
    {
        float magnitude = 0.0f;

        for (int bin = binStart[(size_t) i]; bin < binEnd[(size_t) i]; ++bin)
            magnitude = std::max (magnitude, fftData[(size_t) bin]);

        const float db = juce::Decibels::gainToDecibels (magnitude * magnitudeScale, kFloorDb);

        float& smoothed = displayDb[(size_t) i];
        smoothed += (db > smoothed ? 0.55f : 0.12f) * (db - smoothed);

        float& held = peakDb[(size_t) i];
        held = std::max (held - 0.35f, db);
    }
}

void SpectrumAnalyzer::resetPeaks() noexcept
{
    peakDb.fill (kFloorDb);
}
