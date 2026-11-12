#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int snapshotMagic = 0x4c444e53; // 'LDNS'
    constexpr int snapshotVersion = 3;
}

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
    referenceLoopStart.store (0);
    referenceLoopEnd.store (length);
    referenceLufsValue.store (lufs);
    referencePlaying.store (false);
    referenceAudible.store (false);

    referenceWaveformData.assign (referenceWaveformColumns * 2, 0.0f);

    if (newAudio != nullptr && newAudio->getNumSamples() > 0)
    {
        const int samples = newAudio->getNumSamples();
        const int channels = newAudio->getNumChannels();

        for (int c = 0; c < referenceWaveformColumns; ++c)
        {
            const int from = (int) ((juce::int64) c * samples / referenceWaveformColumns);
            const int to = (int) ((juce::int64) (c + 1) * samples / referenceWaveformColumns);

            float low = 0.0f;
            float high = 0.0f;

            for (int i = from; i < to; ++i)
            {
                const float value = channels > 1
                    ? 0.5f * (newAudio->getSample (0, i) + newAudio->getSample (1, i))
                    : newAudio->getSample (0, i);

                low = std::min (low, value);
                high = std::max (high, value);
            }

            referenceWaveformData[(size_t) (c * 2)] = low;
            referenceWaveformData[(size_t) (c * 2 + 1)] = high;
        }
    }
}

void LudensScopeProcessor::clearReferenceAudio()
{
    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio.reset();
    }

    referenceLength.store (0);
    referencePosition.store (0);
    referenceLoopStart.store (0);
    referenceLoopEnd.store (0);
    referencePlaying.store (false);
    referenceAudible.store (false);
    referenceWaveformData.clear();
}

void LudensScopeProcessor::setReferencePositionSeconds (double seconds) noexcept
{
    const auto length = referenceLength.load (std::memory_order_relaxed);

    if (length <= 0)
        return;

    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const auto position = (std::int64_t) std::llround (
        juce::jlimit (0.0, (double) length, seconds * rate));

    referencePosition.store (juce::jlimit ((std::int64_t) 0, length, position),
                             std::memory_order_relaxed);
}

void LudensScopeProcessor::setReferenceLoopRange (double startSeconds, double endSeconds) noexcept
{
    const auto length = referenceLength.load (std::memory_order_relaxed);

    if (length <= 0)
        return;

    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    auto start = (std::int64_t) std::llround (
        juce::jlimit (0.0, (double) length, startSeconds * rate));
    auto end = (std::int64_t) std::llround (
        juce::jlimit (0.0, (double) length, endSeconds * rate));

    if (end <= start)
    {
        start = 0;
        end = length;
    }

    referenceLoopStart.store (start, std::memory_order_relaxed);
    referenceLoopEnd.store (end, std::memory_order_relaxed);

    const auto position = referencePosition.load (std::memory_order_relaxed);

    if (position < start || position >= end)
        referencePosition.store (start, std::memory_order_relaxed);
}

double LudensScopeProcessor::referenceLoopStartSeconds() const noexcept
{
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    return (double) referenceLoopStart.load() / rate;
}

double LudensScopeProcessor::referenceLoopEndSeconds() const noexcept
{
    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    return (double) referenceLoopEnd.load() / rate;
}

void LudensScopeProcessor::setReferencePlaying (bool shouldPlay) noexcept
{
    if (shouldPlay)
    {
        const auto length = referenceLength.load (std::memory_order_relaxed);

        if (length > 0)
        {
            const auto start = referenceLoopStart.load (std::memory_order_relaxed);
            const auto end = referenceLoopEnd.load (std::memory_order_relaxed);

            auto position = referencePosition.load (std::memory_order_relaxed);

            if (position >= length || (end > start && (position < start || position >= end)))
                position = end > start ? start : 0;

            referencePosition.store (position, std::memory_order_relaxed);
        }
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

    if (audio != nullptr && audio->getNumSamples() > 0)
    {
        const bool playing = referencePlaying.load (std::memory_order_relaxed);
        const bool audible = referenceAudible.load (std::memory_order_relaxed);

        if (playing || audible || referenceMix >= 1.0e-4f)
        {
            const auto length = (std::int64_t) audio->getNumSamples();
            const int refChannels = audio->getNumChannels();

            const float gainTarget = juce::Decibels::decibelsToGain (referenceGainDb.load());
            const float rate = (float) juce::jmax (1.0, getSampleRate());
            const float mixStep = 1.0f / (rate * 0.02f);

            auto* outL = buffer.getWritePointer (0);
            auto* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : outL;

            const auto loopStart = referenceLoopStart.load (std::memory_order_relaxed);
            const auto loopEnd = referenceLoopEnd.load (std::memory_order_relaxed);
            const auto effectiveEnd = loopEnd > loopStart ? loopEnd : length;

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

                    if (++pos >= effectiveEnd)
                    {
                        if (referenceLoop.load (std::memory_order_relaxed))
                        {
                            pos = effectiveEnd == loopEnd ? loopStart : 0;
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
    }

    // Band solo: a high-pass + low-pass pair on the outgoing audio only, so
    // the analysers above always see the untouched mix.
    if (bandSoloActive.load (std::memory_order_relaxed))
    {
        if (soloResetRequested.exchange (false, std::memory_order_relaxed))
        {
            for (auto& memory : soloHighPassMemory) memory = {};
            for (auto& memory : soloLowPassMemory) memory = {};
        }

        const BiquadCoefficients highPass { soloHighPass[0].load(), soloHighPass[1].load(),
                                            soloHighPass[2].load(), soloHighPass[3].load(),
                                            soloHighPass[4].load() };
        const BiquadCoefficients lowPass { soloLowPass[0].load(), soloLowPass[1].load(),
                                           soloLowPass[2].load(), soloLowPass[3].load(),
                                           soloLowPass[4].load() };

        auto* soloL = buffer.getWritePointer (0);
        auto* soloR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : soloL;

        for (int i = 0; i < numSamples; ++i)
        {
            soloL[i] = processBiquad (lowPass, soloLowPassMemory[0],
                                      processBiquad (highPass, soloHighPassMemory[0], soloL[i]));

            if (buffer.getNumChannels() > 1)
                soloR[i] = processBiquad (lowPass, soloLowPassMemory[1],
                                          processBiquad (highPass, soloHighPassMemory[1], soloR[i]));
        }
    }
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

void LudensScopeProcessor::setSnapshot (int slot, const float* levelsDb)
{
    if (slot < 0 || slot >= snapshotSlots || levelsDb == nullptr)
        return;

    std::copy (levelsDb, levelsDb + snapshotBins, snapshots[(size_t) slot].begin());
    snapshotFilled[(size_t) slot] = true;
    snapshotGenerationValue.fetch_add (1);
}

bool LudensScopeProcessor::isSnapshotFilled (int slot) const noexcept
{
    return slot >= 0 && slot < snapshotSlots && snapshotFilled[(size_t) slot];
}

const float* LudensScopeProcessor::snapshotLevels (int slot) const noexcept
{
    if (slot < 0 || slot >= snapshotSlots)
        return nullptr;

    return snapshots[(size_t) slot].data();
}

void LudensScopeProcessor::clearSnapshots() noexcept
{
    std::fill (snapshotFilled.begin(), snapshotFilled.end(), false);
    selectedSnapshotValue.store (0);
    snapshotGenerationValue.fetch_add (1);
}

void LudensScopeProcessor::clearSnapshot (int slot) noexcept
{
    if (slot < 0 || slot >= snapshotSlots)
        return;

    snapshotFilled[(size_t) slot] = false;
    snapshotGenerationValue.fetch_add (1);
}

int LudensScopeProcessor::selectedSnapshot() const noexcept
{
    return selectedSnapshotValue.load();
}

void LudensScopeProcessor::setSelectedSnapshot (int slot) noexcept
{
    selectedSnapshotValue.store (juce::jlimit (0, snapshotSlots - 1, slot));
}

int LudensScopeProcessor::snapshotGeneration() const noexcept
{
    return snapshotGenerationValue.load();
}

void LudensScopeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);

    stream.writeInt (snapshotMagic);
    stream.writeInt (snapshotVersion);
    stream.writeInt (snapshotSlots);
    stream.writeInt (snapshotBins);
    stream.writeInt (selectedSnapshotValue.load());

    for (int slot = 0; slot < snapshotSlots; ++slot)
    {
        const bool filled = snapshotFilled[(size_t) slot];
        stream.writeBool (filled);

        if (filled)
            stream.write (snapshots[(size_t) slot].data(),
                          sizeof (float) * (size_t) snapshotBins);
    }

    stream.writeInt (fftSettingsValue.fftSize);
    stream.writeInt (fftSettingsValue.timingPreset);
    stream.writeInt (fftSettingsValue.windowType);
    stream.writeFloat (fftSettingsValue.tiltDbPerOctave);
    stream.writeInt (fftSettingsValue.smoothingPreset);
}

void LudensScopeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    clearSnapshots();

    if (data == nullptr || sizeInBytes < 20)
        return;

    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    const int magic = stream.readInt();
    const int version = stream.readInt();
    const int slots = stream.readInt();
    const int bins = stream.readInt();
    const int selected = stream.readInt();

    if (magic != snapshotMagic || (version != 1 && version != 2 && version != 3)
        || slots != snapshotSlots || bins != snapshotBins)
        return;

    for (int slot = 0; slot < slots; ++slot)
    {
        if (! stream.readBool())
            continue;

        stream.read (snapshots[(size_t) slot].data(),
                     (int) (sizeof (float) * (size_t) bins));
        snapshotFilled[(size_t) slot] = true;
    }

    selectedSnapshotValue.store (juce::jlimit (0, snapshotSlots - 1, selected));

    if (version >= 2)
    {
        FftSettings settings;
        settings.fftSize = stream.readInt();
        settings.timingPreset = juce::jlimit (0, 2, stream.readInt());
        settings.windowType = juce::jlimit (0, 2, stream.readInt());
        settings.tiltDbPerOctave = juce::jlimit (0.0f, 6.0f, stream.readFloat());

        if (version >= 3)
            settings.smoothingPreset = juce::jlimit (0, 3, stream.readInt());

        if (settings.fftSize != 2048 && settings.fftSize != 4096
            && settings.fftSize != 8192 && settings.fftSize != 16384)
        {
            settings.fftSize = 4096;
        }

        fftSettingsValue = settings;
    }

    snapshotGenerationValue.fetch_add (1);
}

LudensScopeProcessor::BiquadCoefficients LudensScopeProcessor::makeHighPass (
    double sampleRate, double frequency, double q) noexcept
{
    BiquadCoefficients coefficients;

    const double w0 = 2.0 * juce::MathConstants<double>::pi
                      * juce::jlimit (10.0, sampleRate * 0.45, frequency) / sampleRate;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    coefficients.b0 = (float) ((1.0 + cosw) * 0.5 / a0);
    coefficients.b1 = (float) (-(1.0 + cosw) / a0);
    coefficients.b2 = coefficients.b0;
    coefficients.a1 = (float) (-2.0 * cosw / a0);
    coefficients.a2 = (float) ((1.0 - alpha) / a0);
    return coefficients;
}

LudensScopeProcessor::BiquadCoefficients LudensScopeProcessor::makeLowPass (
    double sampleRate, double frequency, double q) noexcept
{
    BiquadCoefficients coefficients;

    const double w0 = 2.0 * juce::MathConstants<double>::pi
                      * juce::jlimit (10.0, sampleRate * 0.45, frequency) / sampleRate;
    const double cosw = std::cos (w0);
    const double alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    coefficients.b0 = (float) ((1.0 - cosw) * 0.5 / a0);
    coefficients.b1 = (float) ((1.0 - cosw) / a0);
    coefficients.b2 = coefficients.b0;
    coefficients.a1 = (float) (-2.0 * cosw / a0);
    coefficients.a2 = (float) ((1.0 - alpha) / a0);
    return coefficients;
}

float LudensScopeProcessor::processBiquad (const BiquadCoefficients& coefficients,
                                           BiquadMemory& memory, float sample) noexcept
{
    const float output = coefficients.b0 * sample + memory.z1;
    memory.z1 = coefficients.b1 * sample - coefficients.a1 * output + memory.z2;
    memory.z2 = coefficients.b2 * sample - coefficients.a2 * output;
    return output;
}

void LudensScopeProcessor::setBandSolo (bool active, float lowHz, float highHz)
{
    if (! active)
    {
        bandSoloActive.store (false, std::memory_order_relaxed);
        return;
    }

    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
    const double low = juce::jlimit (20.0, sampleRate * 0.45, (double) lowHz);
    const double high = juce::jlimit (low * 1.01, sampleRate * 0.45, (double) highHz);

    const auto highPass = makeHighPass (sampleRate, low, 0.7071);
    const auto lowPass = makeLowPass (sampleRate, high, 0.7071);

    const float highPassValues[5] = { highPass.b0, highPass.b1, highPass.b2,
                                      highPass.a1, highPass.a2 };
    const float lowPassValues[5] = { lowPass.b0, lowPass.b1, lowPass.b2,
                                     lowPass.a1, lowPass.a2 };

    for (int i = 0; i < 5; ++i)
    {
        soloHighPass[i].store (highPassValues[i], std::memory_order_relaxed);
        soloLowPass[i].store (lowPassValues[i], std::memory_order_relaxed);
    }

    soloResetRequested.store (true, std::memory_order_relaxed);
    bandSoloActive.store (true, std::memory_order_relaxed);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LudensScopeProcessor();
}
