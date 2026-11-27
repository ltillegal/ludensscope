#include "MatchProcessor.h"
#include "MatchEditor.h"

#include <cmath>

namespace
{
    constexpr const char* amountId = "amount";
    constexpr const char* mixId = "mix";
    constexpr const char* outputId = "output";

    // Eight log-spaced bands from 20 Hz to 20 kHz, edges in Hz.
    constexpr float bandEdges[LudensMatchProcessor::numBands + 1] = {
        20.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2500.0f, 6000.0f, 20000.0f
    };

    constexpr float maxCorrectionDb = 9.0f;
}

LudensMatchProcessor::LudensMatchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LudensMatch", makeLayout())
{
    amountParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (amountId));
    mixParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (mixId));
    outputParam = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter (outputId));

    fifoLeft.resize ((size_t) fifo.getTotalSize());
    fifoRight.resize ((size_t) fifo.getTotalSize());
}

juce::AudioProcessorValueTreeState::ParameterLayout LudensMatchProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { amountId, 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { mixId, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { outputId, 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));

    return layout;
}

float LudensMatchProcessor::bandCentreHz (int band) noexcept
{
    const int index = juce::jlimit (0, numBands - 1, band);
    return std::sqrt (bandEdges[index] * bandEdges[index + 1]);
}

void LudensMatchProcessor::prepareToPlay (double sampleRate, int)
{
    preparedRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    appliedVersion = -1;
    builtAmount = -1.0f;
    rebuildCoefficients (smoothedAmount);

    for (auto& filter : filters)
        filter.reset();

    smoothedAmount = amountParam != nullptr ? amountParam->get() * 0.01f : 1.0f;
    smoothedMix = mixParam != nullptr ? mixParam->get() * 0.01f : 1.0f;

    fifo.reset();
    std::fill (fifoLeft.begin(), fifoLeft.end(), 0.0f);
    std::fill (fifoRight.begin(), fifoRight.end(), 0.0f);
}

bool LudensMatchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (input != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void LudensMatchProcessor::setBandGains (const float* gainsDbIn, int count) noexcept
{
    const int n = juce::jmin (count, numBands);

    for (int i = 0; i < n; ++i)
        gainsDb[i].store (juce::jlimit (-maxCorrectionDb, maxCorrectionDb, gainsDbIn[i]),
                          std::memory_order_relaxed);

    gainsVersion.fetch_add (1, std::memory_order_release);
}

float LudensMatchProcessor::bandGain (int band) const noexcept
{
    return gainsDb[juce::jlimit (0, numBands - 1, band)].load (std::memory_order_relaxed);
}

void LudensMatchProcessor::rebuildCoefficients (float amountScale) noexcept
{
    builtAmount = amountScale;

    for (int i = 0; i < numBands; ++i)
    {
        const float gainDb = gainsDb[i].load (std::memory_order_relaxed) * amountScale;
        bandActive[(size_t) i] = std::abs (gainDb) >= 0.01f;
        const float frequency = bandCentreHz (i);
        const float bandwidth = bandEdges[i + 1] - bandEdges[i];
        const float q = juce::jlimit (0.4f, 4.0f, frequency / juce::jmax (1.0f, bandwidth));

        auto& f = filters[(size_t) i];

        if (! bandActive[(size_t) i])
        {
            f = Biquad {};
            continue;
        }

        const double pi = 3.14159265358979323846;
        const double A = std::pow (10.0, (double) gainDb / 40.0);
        const double w0 = 2.0 * pi * (double) frequency / preparedRate;
        const double alpha = std::sin (w0) / (2.0 * (double) q);
        const double cosw = std::cos (w0);
        const double a0 = 1.0 + alpha / A;

        f.b0 = (float) ((1.0 + alpha * A) / a0);
        f.b1 = (float) ((-2.0 * cosw) / a0);
        f.b2 = (float) ((1.0 - alpha * A) / a0);
        f.a1 = (float) ((-2.0 * cosw) / a0);
        f.a2 = (float) ((1.0 - alpha / A) / a0);
    }

    appliedVersion = gainsVersion.load (std::memory_order_acquire);
}

void LudensMatchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels < 1 || numSamples <= 0)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = numChannels > 1 ? buffer.getReadPointer (1) : left;

    // Feed the editor's analyser with the dry input.
    {
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

        if (size1 > 0)
        {
            std::memcpy (fifoLeft.data() + start1, left, sizeof (float) * (size_t) size1);
            std::memcpy (fifoRight.data() + start1, right, sizeof (float) * (size_t) size1);
        }

        if (size2 > 0)
        {
            std::memcpy (fifoLeft.data() + start2, left + size1, sizeof (float) * (size_t) size2);
            std::memcpy (fifoRight.data() + start2, right + size1, sizeof (float) * (size_t) size2);
        }

        fifo.finishedWrite (size1 + size2);
    }

    const float amountTarget = amountParam != nullptr ? amountParam->get() * 0.01f : 1.0f;
    const float mixTarget = mixParam != nullptr ? mixParam->get() * 0.01f : 1.0f;
    const float outputGain = juce::Decibels::decibelsToGain (outputParam != nullptr
                                                                 ? outputParam->get() : 0.0f,
                                                             0.0f);

    // Amount scales every band, so the coefficients are rebuilt when it moves
    // by more than a fraction of a percent - once per block, not per sample.
    const float smoothing = 1.0f - std::exp (-(float) numSamples / (float) (preparedRate * 0.02));
    smoothedAmount += smoothing * (amountTarget - smoothedAmount);
    smoothedMix += smoothing * (mixTarget - smoothedMix);

    if (gainsVersion.load (std::memory_order_acquire) != appliedVersion
        || std::abs (smoothedAmount - builtAmount) > 0.002f)
        rebuildCoefficients (smoothedAmount);

    auto* outL = buffer.getWritePointer (0);
    auto* outR = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float dryL = outL[i];
        const float dryR = outR != nullptr ? outR[i] : dryL;

        float wetL = dryL;
        float wetR = dryR;

        for (int band = 0; band < numBands; ++band)
        {
            if (! bandActive[(size_t) band])
                continue;

            auto& f = filters[(size_t) band];

            wetL = f.process (wetL, f.z1L, f.z2L);
            wetR = f.process (wetR, f.z1R, f.z2R);
        }

        outL[i] = (dryL + (wetL - dryL) * smoothedMix) * outputGain;

        if (outR != nullptr)
            outR[i] = (dryR + (wetR - dryR) * smoothedMix) * outputGain;
    }
}

int LudensMatchProcessor::readSamples (float* left, float* right, int maxFrames) noexcept
{
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (maxFrames, start1, size1, start2, size2);

    const int total = size1 + size2;

    if (total <= 0)
        return 0;

    auto copyOut = [] (float* destination, const std::vector<float>& source, int start, int count)
    {
        if (destination != nullptr && count > 0)
            std::memcpy (destination, source.data() + start, sizeof (float) * (size_t) count);
    };

    copyOut (left, fifoLeft, start1, size1);
    copyOut (left == nullptr ? nullptr : left + size1, fifoLeft, start2, size2);
    copyOut (right, fifoRight, start1, size1);
    copyOut (right == nullptr ? nullptr : right + size1, fifoRight, start2, size2);

    fifo.finishedRead (total);
    return total;
}

juce::AudioProcessorEditor* LudensMatchProcessor::createEditor()
{
    return new LudensMatchEditor (*this);
}

void LudensMatchProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LudensMatchProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LudensMatchProcessor();
}
