#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <algorithm>
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
    bool truncated = false;
    bool cancelled = false;
    std::array<float, SpectrumAnalyzer::numBins> levels {};
    std::array<float, SpectrumAnalyzer::numBins> levelsLow {};
    std::array<float, SpectrumAnalyzer::numBins> levelsMid {};
    std::array<float, SpectrumAnalyzer::numBins> levelsHigh {};
    std::unique_ptr<juce::AudioBuffer<float>> audio;
    double sampleRate = 0.0;
    float lufs = -100.0f;
};

inline constexpr juce::int64 maxReferenceBytes = 256ll << 20; // per slot

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
    const auto maxSamplesByTime = (juce::int64) (rate * 60.0 * 10.0);
    const auto maxSamplesByBytes = maxReferenceBytes
                                 / (juce::int64) (2 * sizeof (float));
    const auto maxSamples = juce::jmin (maxSamplesByTime, maxSamplesByBytes);
    const int totalSamples = (int) juce::jmin (reader->lengthInSamples, maxSamples);

    if (totalSamples <= 0)
        return result;

    result.truncated = reader->lengthInSamples > (juce::int64) totalSamples;
    result.sampleRate = rate;

    result.audio = std::make_unique<juce::AudioBuffer<float>> (2, totalSamples);

    if (result.audio == nullptr || result.audio->getNumSamples() <= 0
        || result.audio->getWritePointer (0) == nullptr)
    {
        result.audio.reset();
        return result;
    }

    auto analyzer = std::make_unique<SpectrumAnalyzer>();
    analyzer->prepare (rate);

    auto loudness = std::make_unique<LoudnessMeter>();
    loudness->prepare (rate);

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> mono ((size_t) blockSize);

    const int sourceChannels = juce::jlimit (1, 2, (int) reader->numChannels);

    std::array<double, SpectrumAnalyzer::numBins> powerSum {};
    int levelCount = 0;
    const auto skipSamples = juce::jmin ((juce::int64) totalSamples / 2,
                                      juce::jmax ((juce::int64) analyzer->getFftSize(),
                                                  (juce::int64) rate));
    std::vector<float> levelHistory;
    levelHistory.reserve ((size_t) juce::jmax (1, (int) (totalSamples / blockSize + 1))
                          * (size_t) SpectrumAnalyzer::numBins);

    for (juce::int64 pos = 0; pos < totalSamples; pos += blockSize)
    {
        if (shouldExit && shouldExit())
        {
            result.cancelled = true;
            return result;
        }

        const int n = (int) juce::jmin ((juce::int64) blockSize, totalSamples - pos);

        buffer.clear();
        reader->read (&buffer, 0, n, pos, true, sourceChannels > 1);

        for (int channel = 0; channel < 2; ++channel)
            result.audio->copyFrom (channel, (int) pos, buffer,
                                    juce::jmin (channel, sourceChannels - 1), 0, n);

        const float* left = buffer.getReadPointer (0);
        const float* right = buffer.getReadPointer (sourceChannels > 1 ? 1 : 0);

        if (sourceChannels == 1)
            loudness->processMono (left, n);
        else
            loudness->process (left, right, n);

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = 0.5f * (left[i] + right[i]);

        analyzer->push (mono.data(), mono.data(), n);
        analyzer->update();

        // Mean power after the analyser has warmed up, not the live EMA.
        if (pos >= skipSamples)
        {
            const auto& levels = analyzer->levels();

            for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
            {
                powerSum[(size_t) i] += std::pow (10.0, (double) levels[(size_t) i] / 10.0);
                levelHistory.push_back (levels[(size_t) i]);
            }

            ++levelCount;
        }
    }

    if (levelCount > 0)
    {
        for (int i = 0; i < SpectrumAnalyzer::numBins; ++i)
        {
            const double meanPower = powerSum[(size_t) i] / (double) levelCount;
            result.levels[(size_t) i] = (float) (10.0 * std::log10 (
                juce::jmax (meanPower, 1.0e-12)));
        }
    }
    else
    {
        result.levels.fill (-100.0f);
    }

    if (levelCount > 0)
    {
        std::vector<float> column ((size_t) levelCount);

        for (int bin = 0; bin < SpectrumAnalyzer::numBins; ++bin)
        {
            for (int block = 0; block < levelCount; ++block)
                column[(size_t) block] = levelHistory[(size_t) block
                                                      * (size_t) SpectrumAnalyzer::numBins
                                                      + (size_t) bin];

            const auto p10 = column.begin() + (std::ptrdiff_t) (0.10 * (levelCount - 1));
            const auto p90 = column.begin() + (std::ptrdiff_t) (0.90 * (levelCount - 1));
            std::nth_element (column.begin(), p10, column.end());
            const float low = *p10;
            std::nth_element (column.begin(), p90, column.end());
            const float high = *p90;

            std::nth_element (column.begin(),
                              column.begin() + (std::ptrdiff_t) (0.50 * (levelCount - 1)),
                              column.end());

            result.levelsLow[(size_t) bin] = low;
            result.levelsMid[(size_t) bin] = column[(size_t) (0.50 * (levelCount - 1))];
            result.levelsHigh[(size_t) bin] = high;
        }
    }
    else
    {
        result.levelsLow.fill (-100.0f);
        result.levelsMid.fill (-100.0f);
        result.levelsHigh.fill (-100.0f);
    }

    result.lufs = loudness->integratedLufs();
    result.ok = true;
    return result;
}

// Linear resampling of a decoded reference to the plugin's sample rate.
inline std::unique_ptr<juce::AudioBuffer<float>> resampleReference (
    juce::AudioBuffer<float>& source, double sourceRate, double targetRate,
    std::function<bool()> shouldExit = {}, bool* truncated = nullptr)
{
    if (sourceRate <= 0.0 || targetRate <= 0.0
        || std::abs (sourceRate - targetRate) < 0.5
        || source.getNumSamples() == 0)
    {
        return std::make_unique<juce::AudioBuffer<float>> (source);
    }

    // Anti-alias before decimation: Lagrange interpolation does not filter.
    if (targetRate < sourceRate * 0.999)
    {
        if (shouldExit && shouldExit())
            return nullptr;

        const auto cutoff = juce::jmin (0.45 * targetRate, sourceRate * 0.49);
        auto coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (
            sourceRate, cutoff);

        juce::dsp::AudioBlock<float> block (source);

        for (size_t channel = 0; channel < (size_t) source.getNumChannels(); ++channel)
        {
            juce::dsp::IIR::Filter<float> filter (coefficients);
            juce::dsp::ProcessSpec spec;
            spec.sampleRate = sourceRate;
            spec.maximumBlockSize = (juce::uint32) source.getNumSamples();
            spec.numChannels = 1;
            filter.prepare (spec);
            filter.reset();

            auto channelBlock = block.getSingleChannelBlock (channel);
            juce::dsp::ProcessContextReplacing<float> context (channelBlock);
            filter.process (context);
        }
    }

    if (shouldExit && shouldExit())
        return nullptr;

    const double ratio = sourceRate / targetRate;
    const int expectedOutput = (int) std::lround ((double) source.getNumSamples() / ratio);
    const int maxOutputByBytes = (int) (maxReferenceBytes / (juce::int64) (2 * sizeof (float)));
    const int outputSamples = juce::jmax (1, juce::jmin (expectedOutput, maxOutputByBytes));

    if (truncated != nullptr)
        *truncated = expectedOutput > maxOutputByBytes;

    auto result = std::make_unique<juce::AudioBuffer<float>> (2, outputSamples);

    for (int channel = 0; channel < 2; ++channel)
    {
        if (shouldExit && shouldExit())
            return nullptr;

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
