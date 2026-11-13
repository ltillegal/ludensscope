#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int snapshotMagic = 0x4c444e53; // 'LDNS'
    constexpr int snapshotVersion = 12;
}

LudensScopeProcessor::LudensScopeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    undoIntegrated = std::make_unique<float[]> (LoudnessMeter::integratedBlockCapacity);
    undoHistogram = std::make_unique<std::uint32_t[]> (LoudnessMeter::lraHistogramBins);
}

bool LudensScopeProcessor::supportsMonoBuses() const
{
    BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::mono());
    layout.outputBuses.add (juce::AudioChannelSet::mono());
    return isBusesLayoutSupported (layout);
}

bool LudensScopeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    const bool valid = input == juce::AudioChannelSet::mono()
                    || input == juce::AudioChannelSet::stereo();

    return valid && output == input;
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

std::shared_ptr<juce::AudioBuffer<float>> LudensScopeProcessor::copyReferenceAudio() const
{
    const juce::SpinLock::ScopedLockType guard (referenceLock);
    return referenceAudio;
}

void LudensScopeProcessor::restoreReferenceAudio (std::shared_ptr<juce::AudioBuffer<float>> audio,
                                                  float lufs, double loopStartSeconds,
                                                  double loopEndSeconds, double positionSeconds)
{
    const auto length = audio != nullptr ? (std::int64_t) audio->getNumSamples() : 0;
    auto keep = audio;

    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio = std::move (audio);
    }

    referenceSlots[(size_t) activeReferenceSlot.load()].audio = keep;
    referenceLength.store (length);
    referenceLufsValue.store (lufs);
    referencePlaying.store (false);
    referenceAudible.store (false);
    referenceLoopStart.store (0);
    referenceLoopEnd.store (length);

    rebuildReferenceWaveform();

    setReferenceLoopRange (loopStartSeconds, loopEndSeconds);
    setReferencePositionSeconds (positionSeconds);
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

    auto& slot = referenceSlots[(size_t) activeReferenceSlot.load()];
    slot.audio = newAudio;
    slot.lufs = lufs;

    rebuildReferenceWaveform();
}

void LudensScopeProcessor::rebuildReferenceWaveform()
{
    std::shared_ptr<juce::AudioBuffer<float>> audio;

    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        audio = referenceAudio;
    }

    referenceWaveformData.assign (referenceWaveformColumns * 2, 0.0f);

    if (audio != nullptr && audio->getNumSamples() > 0)
    {
        const int samples = audio->getNumSamples();
        const int channels = audio->getNumChannels();

        for (int c = 0; c < referenceWaveformColumns; ++c)
        {
            const int from = (int) ((juce::int64) c * samples / referenceWaveformColumns);
            const int to = (int) ((juce::int64) (c + 1) * samples / referenceWaveformColumns);

            float low = 0.0f;
            float high = 0.0f;

            for (int i = from; i < to; ++i)
            {
                const float value = channels > 1
                    ? 0.5f * (audio->getSample (0, i) + audio->getSample (1, i))
                    : audio->getSample (0, i);

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

    auto& slot = referenceSlots[(size_t) activeReferenceSlot.load()];
    slot.path.clear();
    slot.valid = false;
    slot.curve.fill (0.0f);
    slot.lufs = -100.0f;
    slot.loopStart = 0.0;
    slot.loopEnd = 0.0;
    slot.position = 0.0;
    slot.audio.reset();
    referenceGenerationValue.fetch_add (1);
}

void LudensScopeProcessor::setReferenceCurveInfo (const juce::String& path,
                                                  const float* curveDb, bool valid)
{
    auto& slot = referenceSlots[(size_t) activeReferenceSlot.load()];
    slot.path = path;
    slot.valid = valid && curveDb != nullptr;
    slot.lufs = referenceLufsValue.load (std::memory_order_relaxed);

    if (curveDb != nullptr)
        std::copy (curveDb, curveDb + 512, slot.curve.begin());

    referenceGenerationValue.fetch_add (1);
}

void LudensScopeProcessor::syncActiveReferenceSlot()
{
    auto& slot = referenceSlots[(size_t) activeReferenceSlot.load()];

    if (referenceLength.load() > 0)
    {
        const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        slot.lufs = referenceLufsValue.load (std::memory_order_relaxed);
        slot.loopStart = (double) referenceLoopStart.load (std::memory_order_relaxed) / rate;
        slot.loopEnd = (double) referenceLoopEnd.load (std::memory_order_relaxed) / rate;
        slot.position = (double) referencePosition.load (std::memory_order_relaxed) / rate;

        const juce::SpinLock::ScopedLockType guard (referenceLock);
        slot.audio = referenceAudio;
    }
}

void LudensScopeProcessor::switchReferenceSlot (int slot)
{
    slot = juce::jlimit (0, referenceSlotCount - 1, slot);

    if (slot == activeReferenceSlot.load())
        return;

    syncActiveReferenceSlot();
    activeReferenceSlot.store (slot);

    auto& next = referenceSlots[(size_t) slot];

    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio = next.audio;
    }

    const auto length = next.audio != nullptr
        ? (std::int64_t) next.audio->getNumSamples() : 0;

    referenceLength.store (length);
    referencePosition.store (0);
    referenceLoopStart.store (0);
    referenceLoopEnd.store (0);
    referencePlaying.store (false);
    referenceAudible.store (false);
    referenceLufsValue.store (next.lufs);

    if (length > 0)
    {
        setReferenceLoopRange (next.loopStart, next.loopEnd);
        setReferencePositionSeconds (next.position);
    }

    rebuildReferenceWaveform();
    referenceGenerationValue.fetch_add (1);
}

LudensScopeProcessor::ReferenceSettings LudensScopeProcessor::getReferenceSettings() const
{
    const auto& slot = referenceSlots[(size_t) activeReferenceSlot.load (std::memory_order_relaxed)];

    ReferenceSettings settings;
    settings.path = slot.path;
    settings.curve = slot.curve;
    settings.valid = slot.valid;
    settings.lufs = slot.lufs;
    settings.loopStart = slot.loopStart;
    settings.loopEnd = slot.loopEnd;
    settings.position = slot.position;

    const double rate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;

    if (referenceLength.load (std::memory_order_relaxed) > 0)
    {
        settings.lufs = referenceLufsValue.load (std::memory_order_relaxed);
        settings.loopStart = (double) referenceLoopStart.load (std::memory_order_relaxed) / rate;
        settings.loopEnd = (double) referenceLoopEnd.load (std::memory_order_relaxed) / rate;
        settings.position = (double) referencePosition.load (std::memory_order_relaxed) / rate;
    }

    return settings;
}

void LudensScopeProcessor::unloadReferenceAudio() noexcept
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
    referenceGenerationValue.fetch_add (1);
}

void LudensScopeProcessor::dropReferenceAudioCaches()
{
    syncActiveReferenceSlot();

    {
        const juce::SpinLock::ScopedLockType guard (referenceLock);
        referenceAudio.reset();
    }

    for (auto& slot : referenceSlots)
        slot.audio.reset();

    referenceLength.store (0);
    referencePosition.store (0);
    referenceLoopStart.store (0);
    referenceLoopEnd.store (0);
    referencePlaying.store (false);
    referenceAudible.store (false);
    referenceWaveformData.clear();
    referenceGenerationValue.fetch_add (1);
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

    if (buffer.getNumChannels() < 1 || numSamples <= 0)
        return;

    if (undoCaptureRequested.exchange (false, std::memory_order_relaxed))
    {
        loudness.captureState (undoIntegrated.get(), undoIntegratedCount, undoHistogram.get());
        undoClippedSamples = clippedSamples.load (std::memory_order_relaxed);
        undoAvailable.store (true, std::memory_order_relaxed);
    }

    if (undoRestoreRequested.exchange (false, std::memory_order_relaxed))
    {
        if (undoAvailable.load (std::memory_order_relaxed))
        {
            loudness.restoreState (undoIntegrated.get(), undoIntegratedCount, undoHistogram.get());
            clippedSamples.store (undoClippedSamples, std::memory_order_relaxed);
            undoAvailable.store (false, std::memory_order_relaxed);
        }
    }

    if (resetRequested.exchange (false, std::memory_order_relaxed))
    {
        loudness.reset();
        clippedSamples.store (0, std::memory_order_relaxed);
    }

    const bool stereo = buffer.getNumChannels() > 1;
    const float* left = buffer.getReadPointer (0);
    const float* right = stereo ? buffer.getReadPointer (1) : left;

    std::uint32_t blockClips = 0;

    for (int i = 0; i < numSamples; ++i)
    {
        if (std::abs (left[i]) >= 1.0f)  ++blockClips;
        if (stereo && std::abs (right[i]) >= 1.0f) ++blockClips;
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
    if (referenceMirror.load (std::memory_order_relaxed))
    {
        if (auto* playHead = getPlayHead())
        {
            if (const auto position = playHead->getPosition())
            {
                if (position->getIsPlaying())
                {
                    if (const auto time = position->getTimeInSeconds())
                    {
                        const auto length = referenceLength.load (std::memory_order_relaxed);

                        if (length > 0)
                        {
                            const auto sample = (std::int64_t) std::llround (*time * getSampleRate());
                            referencePosition.store (juce::jlimit ((std::int64_t) 0, length, sample),
                                                     std::memory_order_relaxed);
                        }
                    }
                }
            }
        }
    }

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
            auto* outR = stereo ? buffer.getWritePointer (1) : outL;

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
                        if (referenceLoop.load (std::memory_order_relaxed)
                            && ! referenceMirror.load (std::memory_order_relaxed))
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
                const float refMono = 0.5f * (refL + refR);
                outL[i] = outL[i] * dry
                        + (stereo ? refL : refMono) * referenceMix * referenceCurrentGain;

                if (stereo)
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
        auto* soloR = stereo ? buffer.getWritePointer (1) : soloL;

        for (int i = 0; i < numSamples; ++i)
        {
            soloL[i] = processBiquad (lowPass, soloLowPassMemory[0],
                                      processBiquad (highPass, soloHighPassMemory[0], soloL[i]));

            if (stereo)
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
    stream.writeInt (fftSettingsValue.overlapPreset);
    stream.writeInt (fftSettingsValue.noteGrid);

    stream.writeInt (viewSettingsValue.guidesEnabled);
    stream.writeInt (viewSettingsValue.guidePreset);
    stream.writeInt (viewSettingsValue.sideCurve);
    stream.writeInt (viewSettingsValue.peakMarkers);
    stream.writeInt (viewSettingsValue.difference);
    stream.writeInt (viewSettingsValue.spanIndex);
    stream.writeFloat (viewSettingsValue.rangeLow);
    stream.writeFloat (viewSettingsValue.rangeHigh);
    stream.writeFloat (viewSettingsValue.targetLufs);
    stream.writeInt (viewSettingsValue.waterfallHistory);
    stream.writeInt (viewSettingsValue.oscWindow);
    stream.writeFloat (viewSettingsValue.oscGain);
    stream.writeInt (viewSettingsValue.differenceSource);
    stream.writeString (viewSettingsValue.targetCurveName);
    stream.writeInt (viewSettingsValue.levelMatch);
    stream.writeInt (viewSettingsValue.mirror);
    stream.writeInt (viewSettingsValue.theme);
    stream.writeFloat (viewSettingsValue.waterfallRatio);

    syncActiveReferenceSlot();

    stream.writeInt (activeReferenceSlot.load());

    for (const auto& slot : referenceSlots)
    {
        stream.writeString (slot.path);
        stream.writeBool (slot.valid);

        for (const float value : slot.curve)
            stream.writeFloat (value);

        stream.writeFloat (slot.lufs);
        stream.writeDouble (slot.loopStart);
        stream.writeDouble (slot.loopEnd);
        stream.writeDouble (slot.position);
    }
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

    if (magic != snapshotMagic
        || (version < 1 || version > 12)
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

        if (version >= 4)
        {
            settings.overlapPreset = juce::jlimit (0, 2, stream.readInt());
            settings.noteGrid = stream.readInt() != 0 ? 1 : 0;
        }

        if (settings.fftSize != 2048 && settings.fftSize != 4096
            && settings.fftSize != 8192 && settings.fftSize != 16384)
        {
            settings.fftSize = 4096;
        }

        fftSettingsValue = settings;
    }

    if (version >= 5)
    {
        ViewSettings view;
        view.guidesEnabled = stream.readInt() != 0 ? 1 : 0;
        view.guidePreset = juce::jlimit (0, 15, stream.readInt());
        view.sideCurve = stream.readInt() != 0 ? 1 : 0;
        view.peakMarkers = stream.readInt() != 0 ? 1 : 0;
        view.difference = stream.readInt() != 0 ? 1 : 0;
        view.spanIndex = juce::jlimit (0, 5, stream.readInt());
        view.rangeLow = juce::jlimit (20.0f, 19801.0f, stream.readFloat());
        view.rangeHigh = juce::jlimit (view.rangeLow * 1.01f, 20000.0f, stream.readFloat());
        const float target = stream.readFloat();
        view.targetLufs = target < -90.0f ? -100.0f
                                          : juce::jlimit (-30.0f, -5.0f, target);
        view.waterfallHistory = stream.readInt();
        view.oscWindow = juce::jmax (0, stream.readInt());
        view.oscGain = juce::jlimit (0.25f, 8.0f, stream.readFloat());

        if (version >= 7)
            view.differenceSource = juce::jlimit (0, 2, stream.readInt());

        if (version >= 9)
            view.targetCurveName = stream.readString();

        if (version >= 10)
        {
            view.levelMatch = stream.readInt() != 0 ? 1 : 0;
            view.mirror = stream.readInt() != 0 ? 1 : 0;
        }

        if (version >= 11)
            view.theme = juce::jlimit (0, 6, stream.readInt());

        if (version >= 12)
            view.waterfallRatio = juce::jlimit (0.18f, 0.60f, stream.readFloat());

        if (view.waterfallHistory != 1 && view.waterfallHistory != 2
            && view.waterfallHistory != 4)
        {
            view.waterfallHistory = 2;
        }

        viewSettingsValue = view;
    }

    if (version >= 8)
    {
        const int slotCount = version >= 10 ? referenceSlotCount : 2;
        const int newActive = juce::jlimit (0, slotCount - 1, stream.readInt());
        std::array<ReferenceSlotInfo, referenceSlotCount> slots;

        for (int s = 0; s < slotCount; ++s)
        {
            auto& slot = slots[(size_t) s];
            slot.path = stream.readString();
            slot.valid = stream.readBool();

            for (int i = 0; i < 512; ++i)
                slot.curve[(size_t) i] = stream.readFloat();

            slot.lufs = stream.readFloat();
            slot.loopStart = stream.readDouble();
            slot.loopEnd = stream.readDouble();
            slot.position = stream.readDouble();
        }

        const int oldActive = activeReferenceSlot.load();
        const bool sameActivePath =
            slots[(size_t) newActive].path == referenceSlots[(size_t) oldActive].path;

        if (! sameActivePath)
            unloadReferenceAudio();

        referenceSlots = slots;
        activeReferenceSlot.store (newActive);
        referenceLufsValue.store (slots[(size_t) newActive].lufs);
        referenceGenerationValue.fetch_add (1);
    }
    else if (version >= 6)
    {
        ReferenceSlotInfo slot;
        slot.path = stream.readString();
        slot.valid = stream.readBool();

        for (int i = 0; i < 512; ++i)
            slot.curve[(size_t) i] = stream.readFloat();

        slot.lufs = stream.readFloat();
        slot.loopStart = stream.readDouble();
        slot.loopEnd = stream.readDouble();
        slot.position = stream.readDouble();

        const int oldActive = activeReferenceSlot.load();
        const bool samePath = slot.path == referenceSlots[(size_t) oldActive].path;

        if (! samePath)
            unloadReferenceAudio();

        referenceSlots[(size_t) oldActive] = slot;
        referenceLufsValue.store (slot.lufs);
        referenceGenerationValue.fetch_add (1);
    }

    clearUndo();
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
