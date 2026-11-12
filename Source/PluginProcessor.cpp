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

    referenceMix = 0.0f;
    referenceCurrentGain = 1.0f;
    referencePlaying.store (false);
    referenceAudible.store (false);
}

void LudensScopeProcessor::setReferenceAudio (std::unique_ptr<juce::AudioBuffer<float>> audio,
                                              float lufs)
{
    std::shared_ptr<juce::AudioBuffer<float>> newAudio = std::move (audio);
    const auto length = newAudio != nullptr ? (std::int64_t) newAudio->getNumSamples() : 0;

    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio = newAudio;
    }

    referenceLength.store (length);
    referencePosition.store (0);
    referenceLufsValue.store (lufs);
    referencePlaying.store (false);
    referenceAudible.store (false);
}

void LudensScopeProcessor::clearReferenceAudio()
{
    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio.reset();
    }

    referenceLength.store (0);
    referencePosition.store (0);
    referencePlaying.store (false);
    referenceAudible.store (false);
}

void LudensScopeProcessor::setReferencePlaying (bool shouldPlay) noexcept
{
    if (shouldPlay
        && referenceLength.load (std::memory_order_relaxed) > 0
        && referencePosition.load (std::memory_order_relaxed)
               >= referenceLength.load (std::memory_order_relaxed))
    {
        referencePosition.store (0, std::memory_order_relaxed);
    }

    referencePlaying.store (shouldPlay, std::memory_order_relaxed);

    if (! shouldPlay)
        referenceAudible.store (false, std::memory_order_relaxed);
}

void LudensScopeProcessor::setReferenceAudible (bool shouldBeAudible) noexcept
{
    referenceAudible.store (shouldBeAudible, std::memory_order_relaxed);
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

    // Reference player / A-B. Analysis above always sees the dry input; only
    // the outgoing audio is replaced while the reference is audible.
    std::shared_ptr<juce::AudioBuffer<float>> audio;

    {
        const juce::SpinLock::ScopedTryLockType guard (referenceLock);

        if (guard.isLocked())
            audio = referenceAudio;
    }

    if (audio == nullptr || audio->getNumSamples() == 0)
        return;

    const bool playing = referencePlaying.load (std::memory_order_relaxed);
    const bool audible = referenceAudible.load (std::memory_order_relaxed);

    if (! playing && ! audible && referenceMix < 1.0e-4f)
        return;

    const auto length = (std::int64_t) audio->getNumSamples();
    const int refChannels = audio->getNumChannels();

    const float gainTarget = juce::Decibels::decibelsToGain (referenceGainDb.load());
    const float rate = (float) juce::jmax (1.0, getSampleRate());
    const float mixStep = 1.0f / (rate * 0.02f);

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

    auto pos = referencePosition.load (std::memory_order_relaxed);
    bool stillPlaying = playing;
    float targetMix = (audible && playing) ? 1.0f : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        referenceMix += juce::jlimit (-mixStep, mixStep, targetMix - referenceMix);
        referenceCurrentGain += juce::jlimit (-mixStep, mixStep,
                                              gainTarget - referenceCurrentGain);

        float refL = 0.0f;
        float refR = 0.0f;

        if (stillPlaying && length > 0)
        {
            const auto p = (int) (pos % length);
            refL = audio->getSample (0, p);
            refR = refChannels > 1 ? audio->getSample (1, p) : refL;

            if (++pos >= length)
            {
                if (referenceLoop.load (std::memory_order_relaxed))
                {
                    pos = 0;
                }
                else
                {
                    pos = length;
                    stillPlaying = false;
                    referencePlaying.store (false, std::memory_order_relaxed);
                    referenceAudible.store (false, std::memory_order_relaxed);
                    targetMix = 0.0f;
                }
            }
        }

        const float dry = 1.0f - referenceMix;
        outL[i] = outL[i] * dry + refL * referenceMix * referenceCurrentGain;

        if (buffer.getNumChannels() > 1)
            outR[i] = outR[i] * dry + refR * referenceMix * referenceCurrentGain;
    }

    referencePosition.store (pos, std::memory_order_relaxed);
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
