#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr auto defaultTrackName = "Track";
}

BinuaralSenderAudioProcessor::BinuaralSenderAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateTreeId, createParameterLayout())
#else
    : parameters (*this, nullptr, stateTreeId, createParameterLayout())
#endif
{
    if (! parameters.state.hasProperty (trackNamePropertyId))
        parameters.state.setProperty (trackNamePropertyId, defaultTrackName, nullptr);

    ensureSenderInstanceId();
}

juce::AudioProcessorValueTreeState::ParameterLayout
BinuaralSenderAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { objectIdParameterId, 1 },
        "Object ID",
        1,
        BinuaralTransport::maxObjects,
        1));

    return layout;
}

void BinuaralSenderAudioProcessor::prepareToPlay (double sampleRate, int)
{
    juce::ignoreUnused (sampleRate);
    lastObjectId = getObjectId();
    publishMetadataNow();
    transportNeedsRefresh.store (false);
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BinuaralSenderAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    return input == output
        && (input == juce::AudioChannelSet::mono()
            || input == juce::AudioChannelSet::stereo());
}
#endif

void BinuaralSenderAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (buffer);

    const auto currentObjectId = getObjectId();

    if (currentObjectId != lastObjectId || transportNeedsRefresh.exchange (false))
    {
        lastObjectId = currentObjectId;
        publishMetadataNow();
    }
}

juce::AudioProcessorEditor* BinuaralSenderAudioProcessor::createEditor()
{
    return new BinuaralSenderAudioProcessorEditor (*this);
}

int BinuaralSenderAudioProcessor::getObjectId() const
{
    if (const auto* parameter = parameters.getRawParameterValue (objectIdParameterId))
        return static_cast<int> (std::lround (parameter->load()));

    return 1;
}

juce::String BinuaralSenderAudioProcessor::getTrackName() const
{
    return parameters.state.getProperty (trackNamePropertyId, defaultTrackName).toString();
}

juce::String BinuaralSenderAudioProcessor::getInstanceId() const
{
    return parameters.state.getProperty (instanceIdPropertyId, {}).toString();
}

void BinuaralSenderAudioProcessor::setTrackName (const juce::String& name)
{
    parameters.state.setProperty (trackNamePropertyId,
                                  name.substring (0, BinuaralTransport::maxTrackNameBytes - 1),
                                  nullptr);
    transportNeedsRefresh.store (true);
    publishMetadataNow();
}

void BinuaralSenderAudioProcessor::ensureSenderInstanceId()
{
    if (parameters.state.getProperty (instanceIdPropertyId).toString().isEmpty())
        parameters.state.setProperty (instanceIdPropertyId, juce::Uuid().toString(), nullptr);
}

void BinuaralSenderAudioProcessor::publishMetadataNow()
{
    ensureSenderInstanceId();
    sender.prepare (getInstanceId(), getObjectId());
    sender.setTrackName (getTrackName());
    sender.writeMetadata();
}

void BinuaralSenderAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    ensureSenderInstanceId();

    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BinuaralSenderAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            ensureSenderInstanceId();
            transportNeedsRefresh.store (true);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BinuaralSenderAudioProcessor();
}
