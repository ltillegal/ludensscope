#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

// Log-frequency FFT analyser. Audio is pushed in from the UI thread (the
// processor hands samples over through a lock-free FIFO), the transform is
// computed on the UI thread, so the audio callback stays cheap.
class SpectrumAnalyzer
{
public:
    static constexpr int numBins = 512;
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;

    SpectrumAnalyzer();

    void prepare (double newSampleRate);
    void push (const float* left, const float* right, int numSamples) noexcept;
    void update();
    void resetPeaks() noexcept;

    const std::array<float, numBins>& levels() const noexcept { return displayDb; }
    const std::array<float, numBins>& peaks()  const noexcept { return peakDb; }
    const std::array<float, numBins>& averageLevels() const noexcept { return averageDb; }

    void resetAverage() noexcept;

private:
    juce::dsp::FFT fft { fftOrder };

    std::array<float, 2 * fftSize> fftData {};
    std::array<float, fftSize> window {};
    std::array<float, 2 * fftSize> history {};

    std::array<float, numBins> displayDb {};
    std::array<float, numBins> peakDb {};
    std::array<float, numBins> averageDb {};

    std::array<int, numBins> binStart {};
    std::array<int, numBins> binEnd {};

    float magnitudeScale = 1.0f;
    double sampleRate = 48000.0;
    bool averageInitialized = false;

    static constexpr float averageCoefficient = 0.08f;
};
