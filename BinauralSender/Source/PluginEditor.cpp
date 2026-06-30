#include "PluginEditor.h"

BinuaralSenderAudioProcessorEditor::BinuaralSenderAudioProcessorEditor (
    BinuaralSenderAudioProcessor& Newprocessor)
    : AudioProcessorEditor (&Newprocessor),
      audioProcessor (Newprocessor)
{
    titleLabel.setText ("Binuaral Sender", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    objectIdLabel.setText ("Object ID", juce::dontSendNotification);
    objectIdLabel.attachToComponent (&objectIdSlider, true);
    addAndMakeVisible (objectIdLabel);

    objectIdSlider.setSliderStyle (juce::Slider::IncDecButtons);
    objectIdSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 24);
    objectIdSlider.setRange (1, BinuaralTransport::maxObjects, 1);
    addAndMakeVisible (objectIdSlider);

    objectIdAttachment = std::make_unique<SliderAttachment> (
        audioProcessor.parameters,
        BinuaralSenderAudioProcessor::objectIdParameterId,
        objectIdSlider);
    objectIdSlider.onValueChange = [this]
    {
        audioProcessor.publishMetadataNow();
    };

    trackNameLabel.setText ("Track Name", juce::dontSendNotification);
    trackNameLabel.attachToComponent (&trackNameEditor, true);
    addAndMakeVisible (trackNameLabel);

    trackNameEditor.setText (audioProcessor.getTrackName(), juce::dontSendNotification);
    trackNameEditor.setSelectAllWhenFocused (true);
    trackNameEditor.onTextChange = [this]
    {
        audioProcessor.setTrackName (trackNameEditor.getText());
    };
    addAndMakeVisible (trackNameEditor);

    statusLabel.setText ("Publishing object metadata to the renderer.",
                         juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (statusLabel);

    setSize (420, 190);
}

void BinuaralSenderAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (24);

    titleLabel.setBounds (area.removeFromTop (36));
    area.removeFromTop (14);

    auto objectRow = area.removeFromTop (34);
    objectRow.removeFromLeft (100);
    objectIdSlider.setBounds (objectRow);

    area.removeFromTop (14);

    auto nameRow = area.removeFromTop (34);
    nameRow.removeFromLeft (100);
    trackNameEditor.setBounds (nameRow);

    area.removeFromTop (18);
    statusLabel.setBounds (area.removeFromTop (24));
}
