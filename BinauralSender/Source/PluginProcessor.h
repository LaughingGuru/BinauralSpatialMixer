#pragma once

#include <JuceHeader.h>
#include "SharedObjectAudioTransport.h"

class BinuaralSenderAudioProcessor final : public juce::AudioProcessor
{
public:
    BinuaralSenderAudioProcessor();
    ~BinuaralSenderAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    static constexpr auto objectIdParameterId = "objectId";
    static constexpr auto stateTreeId = "BinuaralSenderState";
    static constexpr auto trackNamePropertyId = "trackName";
    static constexpr auto instanceIdPropertyId = "instanceId";

    juce::String getTrackName() const;
    void setTrackName (const juce::String& name);
    juce::String getInstanceId() const;
    void publishMetadataNow();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    int getObjectId() const;
    void ensureSenderInstanceId();

    BinuaralTransport::Sender sender;
    std::atomic<bool> transportNeedsRefresh { true };
    int lastObjectId = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinuaralSenderAudioProcessor)
};
