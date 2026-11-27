#include "MatchProcessor.h"
#include "MatchEditor.h"

namespace
{
    constexpr const char* amountId = "amount";
    constexpr const char* mixId = "mix";
    constexpr const char* outputId = "output";

    juce::AudioParameterFloat* asFloat (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return dynamic_cast<juce::AudioParameterFloat*> (state.getParameter (id));
    }
}

LudensMatchProcessor::LudensMatchProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LudensMatch", makeLayout())
{
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

void LudensMatchProcessor::prepareToPlay (double, int)
{
    fifo.reset();
    std::fill (fifoLeft.begin(), fifoLeft.end(), 0.0f);
    std::fill (fifoRight.begin(), fifoRight.end(), 0.0f);

    juce::ignoreUnused (asFloat (apvts, amountId), asFloat (apvts, mixId), asFloat (apvts, outputId));
}

bool LudensMatchProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (input != out)
        return false;

    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void LudensMatchProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < 1 || numSamples <= 0)
        return;

    const float* left = buffer.getReadPointer (0);
    const float* right = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : left;

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

    // Analysis only for now: the correction stage is the next milestone, so the
    // audio path stays dry.
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
