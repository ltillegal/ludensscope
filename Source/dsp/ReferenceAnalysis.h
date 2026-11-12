#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

#include "LoudnessMeter.h"
#include "SpectrumAnalyzer.h"

struct ReferenceAnalysis
{
    bool ok = false;
    std::array<float, SpectrumAnalyzer::numBins> levels {};
    std::unique_ptr<juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;
    float lufs = -100.0f;
};

// Decodes an audio file on the calling thread, returning its time-averaged
// spectrum, its BS.1770 integrated loudness and the audio itself (stereo, at
// the file's sample rate, capped at ten minutes).
inline ReferenceAnalysis analyzeReferenceFile (const juce::File& file,
                                               std::function<bool()> shouldExit = {})
{
    ReferenceAnalysis result;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return result;

    const double rate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const auto maxSamples = (juce::int64) (rate * 60.0 * 10.0);
    const int totalSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);

    result.audio = std::make_unique<juce::AudioBuffer<float>> (2, juce::jmax (1, totalSamples));
    result.sampleRate = rate;

    SpectrumAnalyzer analyzer;
    analyzer.prepare (rate);

    LoudnessMeter loudness;
    loudness.prepare (rate);

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> mono ((size_t) blockSize);

    const int sourceChannels = juce::jlimit (1, 2, (int) reader->numChannels);

    for (juce::int64 pos = 0; pos < totalSamples; pos += blockSize)
    {
        if (shouldExit && shouldExit())
            return result;

        const int n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos);

        buffer.clear();
        reader->read (&buffer, 0, n, pos, true, sourceChannels > 1);

        for (int channel = 0; channel < 2; ++channel)
            result.audio->copyFrom (channel, (int) pos, buffer,
                                    juce::jmin (channel, sourceChannels - 1), 0, n);

        const float* left = buffer.getReadPointer (0);
        const float* right = buffer.getReadPointer (sourceChannels > 1 ? 1 : 0);

        loudness.process (left, right, n);

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = 0.5f * (left[i] + right[i]);

        analyzer.push (mono.data(), mono.data(), n);
        analyzer.update();
    }

    result.levels = analyzer.averageLevels();
    result.lufs = loudness.integratedLufs();
    result.ok = true;
    return result;
}

// Linear resampling of a decoded reference to the plugin's sample rate.
inline std::unique_ptr<juce::AudioBuffer<float>> resampleReference (
    const juce::AudioBuffer<float>& source, double sourceRate, double targetRate)
{
    if (sourceRate <= 0.0 || targetRate <= 0.0
        || std::abs (sourceRate - targetRate) < 0.5
        || source.getNumSamples() == 0)
    {
        return std::make_unique<juce::AudioBuffer<float>> (source);
    }

    const double ratio = sourceRate / targetRate;
    const int outputSamples = juce::jmax (1, (int) std::lround ((double) source.getNumSamples() / ratio));

    auto result = std::make_unique<juce::AudioBuffer<float>> (2, outputSamples);

    for (int channel = 0; channel < 2; ++channel)
    {
        const int sourceChannel = juce::jmin (channel, source.getNumChannels() - 1);

        juce::LagrangeInterpolator interpolator;
        interpolator.reset();
        interpolator.process (ratio,
                              source.getReadPointer (sourceChannel),
                              result->getWritePointer (channel),
                              outputSamples);
    }

    return result;
}
