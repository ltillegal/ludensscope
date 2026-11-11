#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

LudensScopeProcessor::LudensScopeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool LudensScopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void LudensScopeProcessor::prepareToPlay (double sampleRate, int)
{
    loudness.prepare (sampleRate);
    fifo.reset();
    fifoLeft.fill (0.0f);
    fifoRight.fill (0.0f);
}

void LudensScopeProcessor::releaseResources()
{
}

void LudensScopeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused (midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2 || numSamples <= 0)
        return;

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        loudness.reset();
        clippedSamples.store (0, std::memory_order_relaxed);
    }

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getReadPointer (1);

    std::uint32_t blockClips = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (std::abs (left[i]) >= 1.0f)  ++blockClips;
        if (std::abs (right[i]) >= 1.0f) ++blockClips;
    }

    if (blockClips > 0)
        clippedSamples.fetch_add (blockClips, std::memory_order_relaxed);

    loudness.process (left, right, numSamples);

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0)
    {
        std::memcpy (fifoLeft.data()  + start1, left,  sizeof (float) * (size_t) size1);
        std::memcpy (fifoRight.data() + start1, right, sizeof (float) * (size_t) size1);
    }

    if (size2 > 0)
    {
        std::memcpy (fifoLeft.data()  + start2, left  + size1, sizeof (float) * (size_t) size2);
        std::memcpy (fifoRight.data() + start2, right + size1, sizeof (float) * (size_t) size2);
    }

    fifo.finishedWrite (size1 + size2);
}

int LudensScopeProcessor::readSamples (float* left, float* right, int maxFrames) noexcept
{
    if (maxFrames <= 0)
        return 0;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    fifo.prepareToRead (maxFrames, start1, size1, start2, size2);

    const int total = size1 + size2;

    if (total <= 0)
        return 0;

    if (size1 > 0)
    {
        std::memcpy (left,  fifoLeft.data()  + start1, sizeof (float) * (size_t) size1);
        std::memcpy (right, fifoRight.data() + start1, sizeof (float) * (size_t) size1);
    }

    if (size2 > 0)
    {
        std::memcpy (left  + size1, fifoLeft.data()  + start2, sizeof (float) * (size_t) size2);
        std::memcpy (right + size1, fifoRight.data() + start2, sizeof (float) * (size_t) size2);
    }

    fifo.finishedRead (total);

    return total;
}

juce::AudioProcessorEditor* LudensScopeProcessor::createEditor()
{
    return new LudensScopeEditor (*this);
}

void LudensScopeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // No parameters, no presets: the state is intentionally empty.
    destData.reset();
}

void LudensScopeProcessor::setStateInformation (const void*, int)
{
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LudensScopeProcessor();
}
