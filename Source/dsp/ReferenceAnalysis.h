#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "SpectrumAnalyzer.h"

struct ReferenceAnalysis
{
    bool ok = false;
    std::array<float, SpectrumAnalyzer::numBins> levels {};
};

// Decodes an audio file on the calling thread and returns its time-averaged
// spectrum on the same 512 log bins as the live analyzer. The optional
// shouldExit callback lets a background Thread abort early.
inline ReferenceAnalysis analyzeReferenceFile (const juce::File& file,
                                               std::function<bool()> shouldExit = {})
{
    ReferenceAnalysis result;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return result;

    SpectrumAnalyzer analyzer;
    analyzer.prepare (reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0);

    constexpr int blockSize = 8192;
    juce::AudioBuffer<float> buffer (2, blockSize);
    std::vector<float> mono ((size_t) blockSize);

    for (juce::int64 pos = 0; pos < reader->lengthInSamples; pos += blockSize)
    {
        if (shouldExit && shouldExit())
            return result;

        const int n = (int) juce::jmin ((juce::int64) blockSize,
                                        reader->lengthInSamples - pos);

        buffer.clear();
        reader->read (&buffer, 0, n, pos, true, reader->numChannels > 1);

        const float* left = buffer.getReadPointer (0);
        const float* right = reader->numChannels > 1 ? buffer.getReadPointer (1) : left;

        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = 0.5f * (left[i] + right[i]);

        analyzer.push (mono.data(), mono.data(), n);
        analyzer.update();
    }

    result.levels = analyzer.averageLevels();
    result.ok = true;
    return result;
}
