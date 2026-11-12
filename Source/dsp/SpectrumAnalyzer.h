#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

// Log-frequency FFT analyser. Audio is pushed in from the UI thread (the
// processor hands samples over through a lock-free FIFO), the transform is
// computed on the UI thread, so the audio callback stays cheap.
class SpectrumAnalyzer
{
public:
    static constexpr int numBins = 512;
    static constexpr int maxFftOrder = 14;
    static constexpr int maxFftSize = 1 << maxFftOrder;

    enum class Window { hann, blackmanHarris, flatTop };

    SpectrumAnalyzer();

    void prepare (double newSampleRate);
    void push (const float* left, const float* right, int numSamples) noexcept;
    void pushMono (const float* samples, int numSamples) noexcept;
    void update();
    void resetPeaks() noexcept;
    void resetAverage() noexcept;

    // FFT window size (2048 - 16384), window shape, display ballistics and
    // a dB/oct tilt.
    void setFftSize (int newSize);
    void setWindow (Window newWindow);
    void setTiming (float attack, float release, float peakDecay) noexcept;
    void setTilt (float dbPerOctave) noexcept { tiltDbPerOctave = dbPerOctave; }
    void setOctaveSmoothing (float octaves) noexcept
    {
        octaveSmoothing = juce::jlimit (0.0f, 1.0f, octaves);
    }

    int getFftSize() const noexcept { return fftSize; }
    Window getWindow() const noexcept { return windowType; }
    float getTilt() const noexcept { return tiltDbPerOctave; }
    float getOctaveSmoothing() const noexcept { return octaveSmoothing; }

    const std::array<float, numBins>& levels() const noexcept { return displayDb; }
    const std::array<float, numBins>& peaks()  const noexcept { return peakDb; }
    const std::array<float, numBins>& averageLevels() const noexcept { return averageDb; }

private:
    std::unique_ptr<juce::dsp::FFT> fft;

    int fftOrder = 12;
    int fftSize = 4096;

    std::array<float, 2 * maxFftSize> fftData {};
    std::array<float, maxFftSize> window {};
    std::array<float, maxFftSize> history {};

    std::array<float, numBins> displayDb {};
    std::array<float, numBins> peakDb {};
    std::array<float, numBins> averageDb {};

    std::array<int, numBins> binStart {};
    std::array<int, numBins> binEnd {};
    std::array<float, numBins> binHz {};

    float magnitudeScale = 1.0f;
    double sampleRate = 48000.0;
    bool averageInitialized = false;

    float attackCoefficient = 0.55f;
    float releaseCoefficient = 0.12f;
    float peakDecayDb = 0.35f;
    float tiltDbPerOctave = 0.0f;
    float octaveSmoothing = 0.0f;
    Window windowType = Window::hann;

    static constexpr float averageCoefficient = 0.08f;
};
