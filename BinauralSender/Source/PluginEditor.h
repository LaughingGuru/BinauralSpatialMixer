#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BinuaralSenderAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BinuaralSenderAudioProcessorEditor (BinuaralSenderAudioProcessor&);
    ~BinuaralSenderAudioProcessorEditor() override = default;

    void resized() override;

private:
    BinuaralSenderAudioProcessor& audioProcessor;

    juce::Label titleLabel;
    juce::Label objectIdLabel;
    juce::Slider objectIdSlider;
    juce::Label trackNameLabel;
    juce::TextEditor trackNameEditor;
    juce::Label statusLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> objectIdAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinuaralSenderAudioProcessorEditor)
};
