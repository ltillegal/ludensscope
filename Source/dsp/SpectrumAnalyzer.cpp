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
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    displayDb.fill (kFloorDb);
    peakDb.fill (kFloorDb);
    averageDb.fill (kFloorDb);
}

void SpectrumAnalyzer::setFftSize (int newSize)
{
    const int order = (int) std::lround (std::log2 ((double) newSize));

    if (newSize < 1024 || newSize > maxFftSize || (1 << order) != newSize)
        return;

    if (newSize == fftSize)
        return;

    fftOrder = order;
    fftSize = newSize;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    prepare (sampleRate);
}

void SpectrumAnalyzer::setWindow (Window newWindow)
{
    if (newWindow == windowType)
        return;

    windowType = newWindow;
    prepare (sampleRate);
}

void SpectrumAnalyzer::setTiming (float attack, float release, float peakDecay) noexcept
{
    attackCoefficient = juce::jlimit (0.01f, 0.99f, attack);
    releaseCoefficient = juce::jlimit (0.01f, 0.99f, release);
    peakDecayDb = juce::jlimit (0.0f, 2.0f, peakDecay);
}

void SpectrumAnalyzer::prepare (double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;

    double windowSum = 0.0;

    for (int i = 0; i < fftSize; ++i)
    {
        const double phase = 2.0 * kPi * (double) i / (double) fftSize;
        double value = 0.0;

        switch (windowType)
        {
            case Window::hann:
                value = 0.5 - 0.5 * std::cos (phase);
                break;

            case Window::blackmanHarris:
                value = 0.35875
                        - 0.48829 * std::cos (phase)
                        + 0.14128 * std::cos (2.0 * phase)
                        - 0.01168 * std::cos (3.0 * phase);
                break;

            case Window::flatTop:
                value = 0.21557895
                        - 0.41663158 * std::cos (phase)
                        + 0.277263158 * std::cos (2.0 * phase)
                        - 0.083578947 * std::cos (3.0 * phase)
                        + 0.006947368 * std::cos (4.0 * phase);
                break;
        }

        window[(size_t) i] = (float) value;
        windowSum += value;
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
        binHz[(size_t) i] = std::sqrt (f0 * f1) * (float) sampleRate;
    }

    history.fill (0.0f);
    displayDb.fill (kFloorDb);
    peakDb.fill (kFloorDb);
    averageDb.fill (kFloorDb);
    averageInitialized = false;
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

void SpectrumAnalyzer::pushMono (const float* samples, int numSamples) noexcept
{
    const int historySize = (int) history.size();
    const int n = std::min (numSamples, historySize);

    if (n <= 0)
        return;

    const int keep = historySize - n;

    if (keep > 0)
        std::memmove (history.data(), history.data() + n, sizeof (float) * (size_t) keep);

    std::memcpy (history.data() + keep, samples + (numSamples - n), sizeof (float) * (size_t) n);
}

void SpectrumAnalyzer::update()
{
    const int windows = juce::jlimit (1, 8, overlapFactor);
    const int hop = fftSize / windows;

    std::fill (powerSum.begin(), powerSum.begin() + fftSize / 2, 0.0f);

    for (int w = 0; w < windows; ++w)
    {
        const int start = (int) history.size() - fftSize - w * hop;

        std::fill (fftData.begin(), fftData.begin() + 2 * fftSize, 0.0f);

        for (int i = 0; i < fftSize; ++i)
            fftData[(size_t) i] = history[(size_t) (start + i)] * window[(size_t) i];

        fft->performFrequencyOnlyForwardTransform (fftData.data());

        for (int bin = 0; bin < fftSize / 2; ++bin)
            powerSum[(size_t) bin] += fftData[(size_t) bin] * fftData[(size_t) bin];
    }

    const float powerScale = 1.0f / (float) windows;

    const bool initializeAverage = ! averageInitialized;

    std::array<float, numBins> rawDb {};

    for (int i = 0; i < numBins; ++i)
    {
        float magnitude = 0.0f;

        for (int bin = binStart[(size_t) i]; bin < binEnd[(size_t) i]; ++bin)
            magnitude = std::max (magnitude, powerSum[(size_t) bin] * powerScale);

        magnitude = std::sqrt (magnitude);

        const float tilt = tiltDbPerOctave * std::log2 (binHz[(size_t) i] / 1000.0f);
        rawDb[(size_t) i] = juce::Decibels::gainToDecibels (magnitude * magnitudeScale, kFloorDb) + tilt;
    }

    std::array<float, numBins> targetDb = rawDb;

    if (octaveSmoothing > 0.0f)
    {
        const float binsPerOctave = (float) numBins
                                    / std::log2 (kMaxFrequency / kMinFrequency);
        const int radius = juce::jmax (1, (int) std::lround (binsPerOctave * octaveSmoothing * 0.5f));

        for (int i = 0; i < numBins; ++i)
        {
            const int first = juce::jmax (0, i - radius);
            const int last = juce::jmin (numBins - 1, i + radius);

            float sum = 0.0f;

            for (int j = first; j <= last; ++j)
                sum += rawDb[(size_t) j];

            targetDb[(size_t) i] = sum / (float) (last - first + 1);
        }
    }

    for (int i = 0; i < numBins; ++i)
    {
        const float db = targetDb[(size_t) i];

        float& smoothed = displayDb[(size_t) i];
        smoothed += (db > smoothed ? attackCoefficient : releaseCoefficient) * (db - smoothed);

        float& average = averageDb[(size_t) i];

        if (initializeAverage)
            average = db;
        else
            average += averageCoefficient * (db - average);

        float& held = peakDb[(size_t) i];
        held = std::max (held - peakDecayDb, db);
    }

    averageInitialized = true;
}

void SpectrumAnalyzer::resetPeaks() noexcept
{
    peakDb.fill (kFloorDb);
}

void SpectrumAnalyzer::resetAverage() noexcept
{
    averageDb.fill (kFloorDb);
    averageInitialized = false;
}
