#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class BinuaralMixAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    explicit BinuaralMixAudioProcessorEditor (BinuaralMixAudioProcessor&);
    ~BinuaralMixAudioProcessorEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    void pushStateToWebView();
    void pushAutomationStateToWebView();

    static juce::WebBrowserComponent::Options makeWebViewOptions (
        BinuaralMixAudioProcessor&);

    BinuaralMixAudioProcessor& audioProcessor;
    juce::WebBrowserComponent webView;
    int stateSyncTicksRemaining = 10;
    int automationStateTick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (
        BinuaralMixAudioProcessorEditor)
};
