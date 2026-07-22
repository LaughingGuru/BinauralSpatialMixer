#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/BinaryData.h"

namespace
{
std::vector<std::byte> binaryDataToVector (const char* data, int size)
{
    const auto* begin = reinterpret_cast<const std::byte*> (data);
    return { begin, begin + size };
}
}

juce::WebBrowserComponent::Options
BinuaralMixAudioProcessorEditor::makeWebViewOptions (
    BinuaralMixAudioProcessor& ownerProcessor)
{
    auto options = juce::WebBrowserComponent::Options {}
       #if JUCE_WINDOWS
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
       #endif
        .withNativeIntegrationEnabled()
        .withEventListener (
            "spatialParameterChanged",
            [&ownerProcessor] (juce::var payload)
            {
                const auto* object = payload.getDynamicObject();

                if (object == nullptr)
                    return;

                const auto objectIndex =
                    static_cast<int> (object->getProperty ("objectIndex"));

                const auto parameterId =
                    object->getProperty ("parameter").toString();
                const auto value =
                    static_cast<float> (object->getProperty ("value"));

                ownerProcessor.setParameterFromWebView (objectIndex, parameterId, value);
            })
       #if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
        .withResourceProvider (
            [] (const juce::String& path)
                -> std::optional<juce::WebBrowserComponent::Resource>
            {
                if (path == "/" || path == "/appUI.html")
                {
                    return juce::WebBrowserComponent::Resource {
                        binaryDataToVector (
                            BinaryData::appUI_html,
                            BinaryData::appUI_htmlSize),
                        "text/html"
                    };
                }

                return std::nullopt;
            })
       #endif
        ;

    return options;
}

BinuaralMixAudioProcessorEditor::BinuaralMixAudioProcessorEditor (
    BinuaralMixAudioProcessor& ownerProcessor)
    : AudioProcessorEditor (&ownerProcessor),
      audioProcessor (ownerProcessor),
      webView (makeWebViewOptions (ownerProcessor))
{
    addAndMakeVisible (webView);
    setResizable (true, true);
    setResizeLimits (900, 560, 1800, 1100);
    setSize (1200, 750);

   #if JUCE_WEB_BROWSER_RESOURCE_PROVIDER_AVAILABLE
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #else
    const auto htmlFile =
        juce::File::getSpecialLocation (
            juce::File::currentApplicationFile)
            .getParentDirectory()
            .getChildFile ("Resources")
            .getChildFile ("appUI.html");
    webView.goToURL (juce::URL (htmlFile).toString (false));
    #endif

    startTimerHz (30);
}

BinuaralMixAudioProcessorEditor::~BinuaralMixAudioProcessorEditor() = default;

void BinuaralMixAudioProcessorEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void BinuaralMixAudioProcessorEditor::pushStateToWebView()
{
    webView.evaluateJavascript (
        "window.setSpatialState?.("
        + juce::JSON::toString (audioProcessor.getWebViewState(), true)
        + ");");
}

void BinuaralMixAudioProcessorEditor::pushAutomationStateToWebView()
{
    webView.evaluateJavascript (
        "window.updateAutomatedSpatialState?.("
        + juce::JSON::toString (audioProcessor.getWebViewState(), true)
        + ");");
}

void BinuaralMixAudioProcessorEditor::timerCallback()
{
    if (stateSyncTicksRemaining > 0)
    {
        pushStateToWebView();
        --stateSyncTicksRemaining;
    }

    // Mirror host automation into the custom web interface at 15 Hz. This is
    // frequent enough for smooth object motion without rebuilding the UI or
    // sending JavaScript on every audio/control update.
    if (++automationStateTick >= 2)
    {
        automationStateTick = 0;
        pushAutomationStateToWebView();
    }

    const auto leftRMS = audioProcessor.getLeftRMS();
    const auto rightRMS = audioProcessor.getRightRMS();

    juce::String javascript;
    javascript << "window.updateMasterVolume?.("
               << leftRMS << ", "
               << rightRMS << ");";

    webView.evaluateJavascript (javascript);
}
