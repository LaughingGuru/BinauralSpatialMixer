#pragma once

#include <JuceHeader.h>
#include "SharedObjectAudioTransport.h"

struct MYSOFA_EASY;

class BinuaralMixAudioProcessor final : public juce::AudioProcessor,
                                        private juce::Thread
{
public:
    BinuaralMixAudioProcessor();
    ~BinuaralMixAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void setParameterFromWebView (int objectIndex, const juce::String& parameterId, float value);
    
    float getLeftRMS() const
    {
        return leftRMS.load();
    }

    float getRightRMS() const
    {
        return rightRMS.load();
    }

    float getObjectRMS (size_t objectIndex) const
    {
        if (objectIndex >= objectRMS.size())
            return -100.0f;

        return objectRMS[objectIndex].load();
    }
    
    juce::AudioProcessorValueTreeState parameters;

    static constexpr auto azimuthParameterId = "azimuth";
    static constexpr auto distanceParameterId = "distance";
    static constexpr auto elevationParameterId = "elevation";
    static constexpr auto sizeParameterId = "size";
    static constexpr size_t numSpatialObjects = 4;

private:
    
    struct PendingImpulseResponse
    {
        juce::AudioBuffer<float> left;
        juce::AudioBuffer<float> right;
        int lengthInSamples = 0;
    };
    
    struct SpatialObject
    {
        std::atomic<bool> enable { false };
        
        std::atomic<float> azimuth { 0.0f };
        std::atomic<float> elevation { 0.0f };
        std::atomic<float> distance { 1.0f };
        std::atomic<float> size { 1.0f };
        
        BinuaralTransport::Receiver receiver;
        BinuaralTransport::ReceivedBlock receivedBlock;
        bool senderOpen = false;
        
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        juce::AudioTransportSource transportSource;
        
        juce::AudioBuffer<float> fileBuffer;
        juce::AudioBuffer<float> monoBuffer;
        
        juce::AudioBuffer<float> leftBufferA, rightBufferA;
        
        juce::AudioBuffer<float> leftBufferB, rightBufferB;
        
        // Current Renderer
        juce::dsp::Convolution leftConvolutionA;
        juce::dsp::Convolution rightConvolutionA;
        
        // Next Renderer
        juce::dsp::Convolution leftConvolutionB;
        juce::dsp::Convolution rightConvolutionB;
        
        
        juce::AudioBuffer<float> wetBuffer;
        juce::dsp::Reverb reverb;
        juce::dsp::DryWetMixer<float> dryWetMixer;
        
        juce::dsp::StateVariableTPTFilter<float> leftDistanceLPF;
        juce::dsp::StateVariableTPTFilter<float> rightDistanceLPF;

        juce::dsp::StateVariableTPTFilter<float> wetLeftLPF;
        juce::dsp::StateVariableTPTFilter<float> wetRightLPF;
        
        bool usingConvolverA = true;
        std::atomic<bool> crossfading { false };
        
        float crossfadePosition = 1.0f;
        float crossfadeIncrement = 0.0f;
        
        juce::SpinLock pendingImpulseLock;
        std::unique_ptr<PendingImpulseResponse> pendingImpulse;
        std::atomic<bool> binauralIrReady { false };
        
    };
    
    std::array<SpatialObject, numSpatialObjects> objects;
    
    std::atomic<float> leftRMS {-100.0f};
    std::atomic<float> rightRMS {-100.0f};
    std::array<std::atomic<float>, numSpatialObjects> objectRMS {};

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::String getObjectParameterId (size_t objectIndex, const juce::String& parameterId);

    void run() override;
    void updateObjectsFromParameters();
    void setAutomatableObjectParameter (int objectIndex, const juce::String& parameterId, float value);
    void consumePendingImpulseResponse();
    void prepareImpulseResponse (SpatialObject& object,
                                 float azimuthDegrees,
                                 float elevationDegrees,
                                 float distanceMetres,
                                 float size,
                                 int filterLength,
                                 MYSOFA_EASY* sofa);
    
    juce::AudioFormatManager formatManager;
    
    bool loadAudioFile(SpatialObject& object,
                       const juce::File& file);

    std::atomic<double> currentSampleRate { 44100.0 };
    std::atomic<int> currentIrLength { 0 };

    int maximumBlockSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinuaralMixAudioProcessor)
};
