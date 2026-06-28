#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../JuceLibraryCode/BinaryData.h"

extern "C"
{
    #include "mysofa.h"
}

#include <cmath>
#include <limits>

namespace
{
constexpr auto parameterTreeId = "BinuaralMixParameters";
constexpr float minimumAudibleDistance = 1.0f;
constexpr float outputCeiling = 0.95f;

float wrapAzimuthDegrees (float azimuth)
{
    while (azimuth > 180.0f)
        azimuth -= 360.0f;

    while (azimuth < -180.0f)
        azimuth += 360.0f;

    return azimuth;
}
}

BinuaralMixAudioProcessor::BinuaralMixAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput ("Input", juce::AudioChannelSet::discreteChannels (8), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("SOFA HRIR Loader"),
      parameters (*this, nullptr, parameterTreeId, createParameterLayout())
{
    formatManager.registerBasicFormats();
    
}

BinuaralMixAudioProcessor::~BinuaralMixAudioProcessor()
{
    releaseResources();
}

juce::AudioProcessorValueTreeState::ParameterLayout
BinuaralMixAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const std::array<float, numSpatialObjects> defaultAzimuths
    {{
        0.0f,
        47.0f,
        94.0f,
        141.0f
    }};

    const std::array<float, numSpatialObjects> defaultDistances
    {{
        1.0f,
        2.5f,
        3.0f,
        3.5f
    }};

    const std::array<float, numSpatialObjects> defaultElevations
    {{
        0.0f,
        10.0f,
        -30.0f,
        20.0f
    }};

    for (size_t index = 0; index < numSpatialObjects; ++index)
    {
        const auto objectName = juce::String ("Object ") + juce::String (static_cast<int> (index + 1));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getObjectParameterId (index, azimuthParameterId), 1 },
            objectName + " Azimuth",
            juce::NormalisableRange<float> { -180.0f, 180.0f, 0.1f },
            defaultAzimuths[index],
            juce::AudioParameterFloatAttributes().withLabel ("deg")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getObjectParameterId (index, distanceParameterId), 1 },
            objectName + " Distance",
            juce::NormalisableRange<float> { 0.2f, 10.0f, 0.01f, 0.45f },
            defaultDistances[index],
            juce::AudioParameterFloatAttributes().withLabel ("m")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getObjectParameterId (index, elevationParameterId), 1 },
            objectName + " Elevation",
            juce::NormalisableRange<float> { -40.0f, 90.0f, 0.1f },
            defaultElevations[index],
            juce::AudioParameterFloatAttributes().withLabel ("deg")));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { getObjectParameterId (index, sizeParameterId), 1 },
            objectName + " Size",
            juce::NormalisableRange<float> { 1.0f, 5.0f, 0.1f },
            1.0f,
            juce::AudioParameterFloatAttributes().withLabel ("x")));
    }

    return layout;
}

juce::String BinuaralMixAudioProcessor::getObjectParameterId (
    size_t objectIndex,
    const juce::String& parameterId)
{
    return "object"
        + juce::String (static_cast<int> (objectIndex + 1))
        + "_"
        + parameterId;
}

const juce::String BinuaralMixAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BinuaralMixAudioProcessor::loadAudioFile(SpatialObject& object, const juce::File& file)
{
    auto* reader = formatManager.createReaderFor(file);

    if (reader == nullptr)
        return false;

    auto newSource =
        std::make_unique<juce::AudioFormatReaderSource>(
            reader,
            true);
    
    newSource->setLooping(true);

    object.transportSource.setSource(
        newSource.get(),
        0,
        nullptr,
        reader->sampleRate);

    object.readerSource = std::move(newSource);

    object.transportSource.start();

    return true;
}

bool BinuaralMixAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BinuaralMixAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BinuaralMixAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BinuaralMixAudioProcessor::getTailLengthSeconds() const
{
    return currentIrLength.load() / juce::jmax (1.0, currentSampleRate.load());
}

int BinuaralMixAudioProcessor::getNumPrograms() { return 1; }
int BinuaralMixAudioProcessor::getCurrentProgram() { return 0; }
void BinuaralMixAudioProcessor::setCurrentProgram (int) {}
const juce::String BinuaralMixAudioProcessor::getProgramName (int) { return {}; }
void BinuaralMixAudioProcessor::changeProgramName (int, const juce::String&) {}

void BinuaralMixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    stopThread (2000);

    currentSampleRate.store (sampleRate);
    maximumBlockSize = juce::jmax (1, samplesPerBlock);
 
    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (maximumBlockSize),
        1
    };
    
    for (size_t index = 0; index < objects.size(); ++index)
    {
        auto& object = objects[index];
        objectRMS[index].store (-100.0f);

        //Stereo Input
        object.fileBuffer.setSize(2, maximumBlockSize);
        
        //HRTF Processing Buffers are mono.
        object.monoBuffer.setSize(1, maximumBlockSize);
        object.leftBufferA.setSize(1, maximumBlockSize);
        object.rightBufferA.setSize(1, maximumBlockSize);
        object.leftBufferB.setSize(1, maximumBlockSize);
        object.rightBufferB.setSize(1, maximumBlockSize);
        
        object.fileBuffer.clear();
        object.monoBuffer.clear();
        object.leftBufferA.clear();
        object.rightBufferA.clear();
        object.leftBufferB.clear();
        object.rightBufferB.clear();
        
        // Preparing Covolution
        object.leftConvolutionA.prepare (spec);
        object.rightConvolutionA.prepare (spec);
        object.leftConvolutionB.prepare (spec);
        object.rightConvolutionB.prepare (spec);
        
        // Preparing Reverb
        object.wetBuffer.setSize(2, maximumBlockSize);
        object.reverb.prepare(spec);
        
        juce::dsp::Reverb::Parameters params;
        
        params.roomSize = 0.7f;
        params.damping = 0.5f;
        params.width = 1.0f;
        params.freezeMode = 0.0f;
        params.wetLevel = 1.0f;
        params.dryLevel = 0.0f;
        
        object.reverb.setParameters(params);
        
        
        object.leftConvolutionA.reset();
        object.rightConvolutionA.reset();
        object.leftConvolutionB.reset();
        object.rightConvolutionB.reset();
        
        object.leftDistanceLPF.prepare (spec);
        object.rightDistanceLPF.prepare (spec);
        object.wetLeftLPF.prepare (spec);
        object.wetRightLPF.prepare (spec);
        
        object.wetLeftLPF.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        object.wetRightLPF.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        
        object.leftDistanceLPF.setType (
                    juce::dsp::StateVariableTPTFilterType::lowpass);

        object.rightDistanceLPF.setType (
                    juce::dsp::StateVariableTPTFilterType::lowpass);

        object.leftDistanceLPF.reset();
        object.rightDistanceLPF.reset();
        object.wetLeftLPF.reset();
        object.wetRightLPF.reset();

        object.usingConvolverA = true;
        object.crossfading.store (false);
        object.crossfadePosition = 1.0f;
        object.crossfadeIncrement = 0.0f;
        object.binauralIrReady.store (false);
        object.enable.store (index == 0);

        {
            const juce::SpinLock::ScopedLockType lock (
                object.pendingImpulseLock);

            object.pendingImpulse.reset();
        }
    }

    for (size_t index = 0; index < objects.size(); ++index)
        objects[index].senderOpen =
            objects[index].receiver.open (static_cast<int> (index + 1));

    currentIrLength.store (0);
    updateObjectsFromParameters();
    startThread();
}

void BinuaralMixAudioProcessor::releaseResources()
{
    signalThreadShouldExit();
    stopThread(3000);
    
    for (auto& object : objects)
    {
        object.transportSource.stop();
        object.transportSource.releaseResources();
        object.transportSource.setSource(nullptr);
        object.readerSource.reset();
        
        object.pendingImpulse.reset();
    }
}

bool BinuaralMixAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() != 1)
        return false;

    if (layouts.getMainInputChannelSet().size() != 8)
        return false;

    return true;
}

void BinuaralMixAudioProcessor::consumePendingImpulseResponse()
{
    const auto sampleRate = currentSampleRate.load();
    
    for (auto& object : objects) {
    std::unique_ptr<PendingImpulseResponse> update;

        if (object.pendingImpulseLock.tryEnter())
        {
            update = std::move (object.pendingImpulse);
            object.pendingImpulseLock.exit();
        }
        
        if (update == nullptr)
            continue;
    
        
        if (object.usingConvolverA)
        {
            object.leftConvolutionB.loadImpulseResponse (
                                                         std::move (update->left),
                                                         sampleRate,
                                                         juce::dsp::Convolution::Stereo::no,
                                                         juce::dsp::Convolution::Trim::no,
                                                         juce::dsp::Convolution::Normalise::no);
            
            object.rightConvolutionB.loadImpulseResponse (
                                                          std::move (update->right),
                                                          sampleRate,
                                                          juce::dsp::Convolution::Stereo::no,
                                                          juce::dsp::Convolution::Trim::no,
                                                          juce::dsp::Convolution::Normalise::no);
        }
        else {
            object.leftConvolutionA.loadImpulseResponse (
                                                         std::move (update->left),
                                                         sampleRate,
                                                         juce::dsp::Convolution::Stereo::no,
                                                         juce::dsp::Convolution::Trim::no,
                                                         juce::dsp::Convolution::Normalise::no);
            
            object.rightConvolutionA.loadImpulseResponse (
                                                          std::move (update->right),
                                                          sampleRate,
                                                          juce::dsp::Convolution::Stereo::no,
                                                          juce::dsp::Convolution::Trim::no,
                                                          juce::dsp::Convolution::Normalise::no);
            
        }
        
        currentIrLength.store (update->lengthInSamples);
        
        constexpr float crossfadeTimeSeconds = 0.05f;
        object.crossfadePosition = 0.0f;
        
        object.crossfadeIncrement = 1.0f/ (crossfadeTimeSeconds * static_cast<float>(sampleRate));
        
        object.crossfading.store(true);
        
        object.binauralIrReady.store (true);
    }
}

void BinuaralMixAudioProcessor::processBlock (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    updateObjectsFromParameters();
    consumePendingImpulseResponse();

    const auto numSamples = buffer.getNumSamples();

    for (int offset = 0;
         offset < numSamples;
         offset += maximumBlockSize)
    {
        const auto blockSize =
            juce::jmin (maximumBlockSize, numSamples - offset);

        for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
        {
            auto& object = objects[objectIndex];

            if (! object.senderOpen)
                object.senderOpen =
                    object.receiver.open (static_cast<int> (objectIndex + 1));

            if (object.senderOpen)
                object.receiver.readLatest (object.receivedBlock);

            object.monoBuffer.clear (0, 0, blockSize);

            if (! object.enable.load())
            {
                objectRMS[objectIndex].store (-100.0f);
                continue;
            }

            const auto leftInputChannel = static_cast<int> (objectIndex * 2);
            const auto rightInputChannel = leftInputChannel + 1;
            const auto inputChannelCount = getTotalNumInputChannels();

            if (leftInputChannel >= inputChannelCount)
            {
                objectRMS[objectIndex].store (-100.0f);
                continue;
            }

            if (rightInputChannel < inputChannelCount)
            {
                object.monoBuffer.addFrom (
                    0, 0,
                    buffer, leftInputChannel, offset,
                    blockSize,
                    0.5f);

                object.monoBuffer.addFrom (
                    0, 0,
                    buffer, rightInputChannel, offset,
                    blockSize,
                    0.5f);
            }
            else
            {
                object.monoBuffer.copyFrom (
                    0, 0,
                    buffer, leftInputChannel, offset,
                    blockSize);
            }
        }

        buffer.clear (0, offset, blockSize);
        buffer.clear (1, offset, blockSize);

        for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
        {
            auto& object = objects[objectIndex];

            if (! object.enable.load())
                continue;

            // Until the first HRTF is ready, add dry mono audio.
            if (! object.binauralIrReady.load())
            {
                const auto dryObjectRMS =
                    object.monoBuffer.getRMSLevel (0, 0, blockSize);

                objectRMS[objectIndex].store (
                    juce::Decibels::gainToDecibels (dryObjectRMS, -100.0f));

                buffer.addFrom (
                    0, offset,
                    object.monoBuffer, 0, 0,
                    blockSize);

                buffer.addFrom (
                    1, offset,
                    object.monoBuffer, 0, 0,
                    blockSize);

                continue;
            }

            // Copy this object's mono input into both convolver pairs.
            object.leftBufferA.copyFrom (
                0, 0, object.monoBuffer, 0, 0, blockSize);

            object.rightBufferA.copyFrom (
                0, 0, object.monoBuffer, 0, 0, blockSize);

            object.leftBufferB.copyFrom (
                0, 0, object.monoBuffer, 0, 0, blockSize);

            object.rightBufferB.copyFrom (
                0, 0, object.monoBuffer, 0, 0, blockSize);

            auto leftBlockA =
                juce::dsp::AudioBlock<float> (object.leftBufferA)
                    .getSubBlock (
                        0, static_cast<size_t> (blockSize));

            auto rightBlockA =
                juce::dsp::AudioBlock<float> (object.rightBufferA)
                    .getSubBlock (
                        0, static_cast<size_t> (blockSize));

            auto leftBlockB =
                juce::dsp::AudioBlock<float> (object.leftBufferB)
                    .getSubBlock (
                        0, static_cast<size_t> (blockSize));

            auto rightBlockB =
                juce::dsp::AudioBlock<float> (object.rightBufferB)
                    .getSubBlock (
                        0, static_cast<size_t> (blockSize));

            object.leftConvolutionA.process (
                juce::dsp::ProcessContextReplacing<float> (
                    leftBlockA));

            object.rightConvolutionA.process (
                juce::dsp::ProcessContextReplacing<float> (
                    rightBlockA));

            object.leftConvolutionB.process (
                juce::dsp::ProcessContextReplacing<float> (
                    leftBlockB));

            object.rightConvolutionB.process (
                juce::dsp::ProcessContextReplacing<float> (
                    rightBlockB));

            float fadeA = 0.0f;
            float fadeB = 0.0f;

            if (object.crossfading.load())
            {
                /*
                    usingConvolverA describes the current convolver.
                    The other convolver contains the newly loaded IR.
                */
                if (object.usingConvolverA)
                {
                    // A -> B
                    fadeA = 1.0f - object.crossfadePosition;
                    fadeB = object.crossfadePosition;
                }
                else
                {
                    // B -> A
                    fadeA = object.crossfadePosition;
                    fadeB = 1.0f - object.crossfadePosition;
                }
            }
            else if (object.usingConvolverA)
            {
                fadeA = 1.0f;
                fadeB = 0.0f;
            }
            else
            {
                fadeA = 0.0f;
                fadeB = 1.0f;
            }

            /*
                Store the crossfaded result in leftBufferA and
                rightBufferA. These become this object's final
                stereo working buffers.
            */
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto left =
                    object.leftBufferA.getSample (0, sample) * fadeA
                    + object.leftBufferB.getSample (0, sample) * fadeB;

                const auto right =
                    object.rightBufferA.getSample (0, sample) * fadeA
                    + object.rightBufferB.getSample (0, sample) * fadeB;

                object.leftBufferA.setSample (0, sample, left);
                object.rightBufferA.setSample (0, sample, right);
            }

            if (object.crossfading.load())
            {
                object.crossfadePosition +=
                    object.crossfadeIncrement
                    * static_cast<float> (blockSize);

                if (object.crossfadePosition >= 1.0f)
                {
                    object.crossfadePosition = 1.0f;
                    object.usingConvolverA =
                        ! object.usingConvolverA;

                    object.crossfading.store (false);
                }
            }

            // Apply distance filtering to this object only.
            const auto distance =
                juce::jlimit (
                    0.2f,
                    10.0f,
                    object.distance.load());
            
            //Apply Comb Filtering for early reflections
            

            // Apply Reverb on this object
            const float wetHrtfAmount = 0.45f; // 0 = mono wet, 1 = Full HRTF wet
            
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto hrtfLeft = object.leftBufferA.getSample(0, sample);
                
                const auto hrtfRight = object.rightBufferA.getSample(0, sample);
                
                const auto mono = 0.5f * (hrtfLeft + hrtfRight);
                
                const auto wetLeft = mono + (hrtfLeft - mono) * wetHrtfAmount;
                
                const auto wetRight = mono + (hrtfRight - mono) * wetHrtfAmount;
                
                object.wetBuffer.setSample(0, sample, wetLeft);
                object.wetBuffer.setSample(1, sample, wetRight);
            };
            
            const auto wetCutoff = juce::jmap (distance, 1.0f, 10.0f, 9000.0f, 2500.0f);
            object.wetLeftLPF.setCutoffFrequency(wetCutoff);
            object.wetRightLPF.setCutoffFrequency(wetCutoff);
            
            auto wetLeftBlock = juce::dsp::AudioBlock<float> (object.wetBuffer).getSingleChannelBlock(0).getSubBlock(0, static_cast<size_t> (blockSize));
            
            auto wetRightBlock = juce::dsp::AudioBlock<float> (object.wetBuffer).getSingleChannelBlock(1).getSubBlock(0, static_cast<size_t> (blockSize));
            
            object.wetLeftLPF.process (juce::dsp::ProcessContextReplacing<float>(wetLeftBlock));
            
            object.wetRightLPF.process (juce::dsp::ProcessContextReplacing<float>(wetRightBlock));
            
            
            
            // Reverb for Dry Signal
            auto wetBlock = juce::dsp::AudioBlock<float>(object.wetBuffer).getSubBlock(0, static_cast<size_t> (blockSize));
            object.reverb.process(juce::dsp::ProcessContextReplacing<float>(wetBlock));
            
            const auto wetAmount = juce::jmap (distance, 1.0f, 10.0f, 0.05f, 0.45f);
            const auto dryAmount = juce::jmap (distance, 1.0f, 10.0f, 1.0f, 0.85f);
            
            object.leftBufferA.applyGain(0, blockSize, dryAmount);
            object.rightBufferA.applyGain(0, blockSize, dryAmount);
            
            // Gain reduction on Wet Reverb
            const float reverbGain = juce::jmap(distance, 1.0f, 10.0f, 0.8f, 0.6f);
            
            object.leftBufferA.addFrom(0, 0, object.wetBuffer, 0, 0, blockSize, wetAmount * reverbGain);
            object.rightBufferA.addFrom(0, 0, object.wetBuffer, 1, 0, blockSize, wetAmount * reverbGain);
            
            
            // Apply Cutoff (LPF)
            const auto cutoff =
                juce::jmap (
                    distance,
                    1.0f,
                    10.0f,
                    12000.0f,
                    1800.0f);

            object.leftDistanceLPF.setCutoffFrequency (cutoff);
            object.rightDistanceLPF.setCutoffFrequency (cutoff);

            object.leftDistanceLPF.process (
                juce::dsp::ProcessContextReplacing<float> (
                    leftBlockA));

            object.rightDistanceLPF.process (
                juce::dsp::ProcessContextReplacing<float> (
                    rightBlockA));

            const auto objectLeftRMS =
                object.leftBufferA.getRMSLevel (0, 0, blockSize);
            const auto objectRightRMS =
                object.rightBufferA.getRMSLevel (0, 0, blockSize);
            const auto objectRMSGain =
                juce::jmax (objectLeftRMS, objectRightRMS);

            objectRMS[objectIndex].store (
                juce::Decibels::gainToDecibels (objectRMSGain, -100.0f));

            // Sum this spatialized object into the stereo output.
            buffer.addFrom (
                0, offset,
                object.leftBufferA, 0, 0,
                blockSize);

            buffer.addFrom (
                1, offset,
                object.rightBufferA, 0, 0,
                blockSize);
        }
        
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto left = buffer.getSample (0, sample);
        auto right = buffer.getSample (1, sample);

        if (! std::isfinite (left))
            left = 0.0f;

        if (! std::isfinite (right))
            right = 0.0f;

        const auto peak = juce::jmax (std::abs (left), std::abs (right));

        if (peak > outputCeiling)
        {
            const auto gain = outputCeiling / peak;
            left *= gain;
            right *= gain;
        }

        buffer.setSample (0, sample, left);
        buffer.setSample (1, sample, right);
    }
    
    const float left = buffer.getRMSLevel(0, 0, buffer.getNumSamples());
    
    const float right = buffer.getRMSLevel(1, 0, buffer.getNumSamples());
    
    leftRMS.store( juce::Decibels::gainToDecibels(left, -100.0f));
    rightRMS.store(juce::Decibels::gainToDecibels(right, -100.0f));
    
}

void BinuaralMixAudioProcessor::run()
{
    int filterLength = 0;
    int error = MYSOFA_OK;
    const auto sampleRate = static_cast<float> (currentSampleRate.load());
    
    auto* sofa = mysofa_open_data( BinaryData::MIT_KEMAR_normal_pinna_sofa,
        BinaryData::MIT_KEMAR_normal_pinna_sofaSize,
        sampleRate,
        &filterLength,
        &error);
    
    if (sofa == nullptr || error != MYSOFA_OK)
    {
        DBG ("Unable to open SOFA file");
    }

    std::array<bool, 4> hasPreviousPosition {};
    std::array<float, 4> previousAzimuth {};
    std::array<float, 4> previousElevation {};
    std::array<float, 4> previousDistance {};
    std::array<float, 4> previousSize {};
    

    while (! threadShouldExit())
    {
        for (size_t index = 0; index < objects.size(); ++index)
        {
            auto& object = objects[index];

            if (! object.enable.load())
                continue;

            const auto azimuth = object.azimuth.load();
            const auto elevation = object.elevation.load();
            const auto distance = object.distance.load();
            const auto size = object.size.load();

            const auto changed =
                ! hasPreviousPosition[index]
                || std::abs (
                    azimuth - previousAzimuth[index]) >= 0.05f
                || std::abs (
                    elevation - previousElevation[index]) >= 0.05f
                || std::abs (
                    distance - previousDistance[index]) >= 0.005f
                || std::abs (
                    size - previousSize[index]) >= 0.01f;

            if (! changed)
                continue;

            prepareImpulseResponse (
                object,
                azimuth,
                elevation,
                distance,
                size,
                filterLength,
                sofa);

            previousAzimuth[index] = azimuth;
            previousElevation[index] = elevation;
            previousDistance[index] = distance;
            previousSize[index] = size;
            hasPreviousPosition[index] = true;
        }

        wait (5);
    }

    mysofa_close (sofa);
}

void BinuaralMixAudioProcessor::prepareImpulseResponse (BinuaralMixAudioProcessor::SpatialObject& object,
    float azimuthDegrees,
    float elevationDegrees,
    float distanceMetres,
    float size,
    int filterLength,
    MYSOFA_EASY* sofa)
{
    const auto sampleRate = static_cast<float> (currentSampleRate.load());
    
    auto applyFractionalDelay = [] (std::vector<float>& ir, float frac) {
        if (frac <= 0.0f)
            return;
        std::vector<float> delayed (ir.size(), 0.0f);
        
        delayed[0] = ir[0] * (1.0f - frac);
        
        for (size_t i = 1; i < ir.size(); ++i)
            delayed[i] = ir[i] * (1.0f - frac) + ir[i-1] * frac;
        ir = std::move (delayed);
    };

    struct SpreadFilter
    {
        std::vector<float> left;
        std::vector<float> right;
        int leftDelaySamples = 0;
        int rightDelaySamples = 0;
        float gain = 1.0f;
    };

    const auto limitedSize = juce::jlimit (1.0f, 5.0f, size);
    const auto spreadDegrees =
        juce::jmap (limitedSize, 1.0f, 5.0f, 0.0f, 30.0f);

    struct SpreadOffset
    {
        float azimuthOffsetDegrees = 0.0f;
        float elevationOffsetDegrees = 0.0f;
        float gain = 1.0f;
    };

    std::vector<SpreadOffset> offsets;

    if (spreadDegrees < 0.5f)
    {
        offsets.push_back ({ 0.0f, 0.0f, 1.0f });
    }
    else
    {
        const auto elevationSpread = spreadDegrees;

        offsets.push_back ({ -spreadDegrees, -elevationSpread, 1.0f });
        offsets.push_back ({  spreadDegrees, -elevationSpread, 1.0f });
        offsets.push_back ({ -spreadDegrees,  elevationSpread, 1.0f });
        offsets.push_back ({  spreadDegrees,  elevationSpread, 1.0f });
    }

    float totalSpreadGain = 0.0f;

    for (const auto& offset : offsets)
        totalSpreadGain += offset.gain;

    if (totalSpreadGain > 0.0f)
        for (auto& offset : offsets)
            offset.gain /= totalSpreadGain;

    std::vector<SpreadFilter> spreadFilters;
    spreadFilters.reserve (offsets.size());

    int maxLeftDelaySamples = 0;
    int maxRightDelaySamples = 0;

    for (const auto& offset : offsets)
    {
        const auto spreadAzimuthDegrees =
            wrapAzimuthDegrees (azimuthDegrees + offset.azimuthOffsetDegrees);
        const auto spreadElevationDegrees =
            juce::jlimit (-40.0f, 90.0f, elevationDegrees + offset.elevationOffsetDegrees);
        const auto azimuth = juce::degreesToRadians (spreadAzimuthDegrees);
        const auto elevation = juce::degreesToRadians (spreadElevationDegrees);
        const auto horizontalDistance = distanceMetres * std::cos (elevation);

        // SOFA Cartesian coordinates are +X front, +Y left, +Z up.
        // The UI uses positive azimuth to the listener's right, hence the -Y.
        const auto x = horizontalDistance * std::cos (azimuth);
        const auto y = -horizontalDistance * std::sin (azimuth);
        const auto z = distanceMetres * std::sin (elevation);

        SpreadFilter spreadFilter {
            std::vector<float> (static_cast<size_t> (filterLength)),
            std::vector<float> (static_cast<size_t> (filterLength))
        };

        float leftDelaySeconds = 0.0f;
        float rightDelaySeconds = 0.0f;

        mysofa_getfilter_float (sofa,
                                x,
                                y,
                                z,
                                spreadFilter.left.data(),
                                spreadFilter.right.data(),
                                &leftDelaySeconds,
                                &rightDelaySeconds);

        const auto leftDelaySamples = leftDelaySeconds * sampleRate;
        const auto rightDelaySamples = rightDelaySeconds * sampleRate;

        spreadFilter.leftDelaySamples =
            juce::jmax (0, static_cast<int> (std::floor (leftDelaySamples)));
        spreadFilter.rightDelaySamples =
            juce::jmax (0, static_cast<int> (std::floor (rightDelaySamples)));
        spreadFilter.gain = offset.gain;

        applyFractionalDelay (
            spreadFilter.left,
            leftDelaySamples - static_cast<float> (spreadFilter.leftDelaySamples));
        applyFractionalDelay (
            spreadFilter.right,
            rightDelaySamples - static_cast<float> (spreadFilter.rightDelaySamples));

        maxLeftDelaySamples = juce::jmax (maxLeftDelaySamples, spreadFilter.leftDelaySamples);
        maxRightDelaySamples = juce::jmax (maxRightDelaySamples, spreadFilter.rightDelaySamples);
        spreadFilters.push_back (std::move (spreadFilter));
    }

    
    // Comb Filtering to mimic Early Reflections
    struct CombReflection {
        float delayMs;
        float gain;
    };
    
    const std::array<CombReflection, 4> reflections
    {{
        {7.0f, 0.22f}, // reflection after 7ms, 22% gain
        {13.0f, 0.16f}, // reflection after 13ms, 16% gain
        {21.0f, 0.11f},
        {32.0f, 0.07f}
    }};
    
    const auto maxReflectionDelaySamples = juce::roundToInt(32.0f * 0.001f * sampleRate);
    
    const auto totalLength =
    filterLength + juce::jmax (maxLeftDelaySamples, maxRightDelaySamples) + maxReflectionDelaySamples + 1;
    
    
    auto update = std::make_unique<PendingImpulseResponse>();
    update->left.setSize (1, totalLength);
    update->right.setSize (1, totalLength);
    update->left.clear();
    update->right.clear();
    update->lengthInSamples = totalLength;

    for (const auto& spreadFilter : spreadFilters)
    {
        update->left.addFrom (
            0,
            spreadFilter.leftDelaySamples,
            spreadFilter.left.data(),
            filterLength,
            spreadFilter.gain);
        update->right.addFrom (
            0,
            spreadFilter.rightDelaySamples,
            spreadFilter.right.data(),
            filterLength,
            spreadFilter.gain);

        // Early reflection taps.
        for (const auto& reflection : reflections)
        {
            const auto reflectionDelaySamples = juce::roundToInt(reflection.delayMs * 0.001f * sampleRate);

            update->left.addFrom (
                0,
                spreadFilter.leftDelaySamples + reflectionDelaySamples,
                spreadFilter.left.data(),
                filterLength,
                reflection.gain * spreadFilter.gain);
            update->right.addFrom (
                0,
                spreadFilter.rightDelaySamples + reflectionDelaySamples,
                spreadFilter.right.data(),
                filterLength,
                reflection.gain * spreadFilter.gain);
        }
    }

    const auto distanceGain = std::pow (minimumAudibleDistance / juce::jmax (minimumAudibleDistance, distanceMetres), 0.45f);
    
    update->left.applyGain (distanceGain);
    update->right.applyGain (distanceGain);

    const juce::SpinLock::ScopedLockType lock (object.pendingImpulseLock);
    object.pendingImpulse = std::move (update);
}

bool BinuaralMixAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BinuaralMixAudioProcessor::createEditor()
{
    return new BinuaralMixAudioProcessorEditor (*this);
}

void BinuaralMixAudioProcessor::updateObjectsFromParameters()
{
    for (size_t index = 0; index < objects.size(); ++index)
    {
        auto& object = objects[index];

        if (auto* value = parameters.getRawParameterValue (
                getObjectParameterId (index, azimuthParameterId)))
            object.azimuth.store (value->load());

        if (auto* value = parameters.getRawParameterValue (
                getObjectParameterId (index, distanceParameterId)))
            object.distance.store (value->load());

        if (auto* value = parameters.getRawParameterValue (
                getObjectParameterId (index, elevationParameterId)))
            object.elevation.store (value->load());

        if (auto* value = parameters.getRawParameterValue (
                getObjectParameterId (index, sizeParameterId)))
            object.size.store (value->load());
    }
}

void BinuaralMixAudioProcessor::setAutomatableObjectParameter (
    int objectIndex,
    const juce::String& parameterId,
    float value)
{
    if (! juce::isPositiveAndBelow (objectIndex, static_cast<int> (objects.size())))
        return;

    const auto index = static_cast<size_t> (objectIndex);
    const auto fullParameterId = getObjectParameterId (index, parameterId);

    auto* parameter = parameters.getParameter (fullParameterId);

    if (parameter == nullptr)
        return;

    const auto normalisedValue = parameter->convertTo0to1 (value);

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalisedValue);
    parameter->endChangeGesture();
}

void BinuaralMixAudioProcessor::setParameterFromWebView (int objectIndex,
    const juce::String& parameterId,
    float value)
{
    DBG("Object " + juce::String(objectIndex)
        + " Parameter " + parameterId
        + " Value " + juce::String(value));
    
   if (! juce::isPositiveAndBelow(objectIndex, static_cast<int> (objects.size())))
       return;
    
    auto& object = objects[static_cast<size_t> (objectIndex)];
    
    if (parameterId == "azimuth")
        setAutomatableObjectParameter (
            objectIndex,
            azimuthParameterId,
            juce::jlimit (-180.0f, 180.0f, value));
    else if (parameterId == "elevation")
        setAutomatableObjectParameter (
            objectIndex,
            elevationParameterId,
            juce::jlimit (-40.0f, 90.0f, value));
    else if (parameterId == "distance")
        setAutomatableObjectParameter (
            objectIndex,
            distanceParameterId,
            juce::jlimit (0.2f, 10.0f, value));
    else if (parameterId == "size")
        setAutomatableObjectParameter (
            objectIndex,
            sizeParameterId,
            juce::jlimit (0.1f, 5.0f, value));
    else if (parameterId == "enabled")
        object.enable.store (value > 0.5f);
    
    updateObjectsFromParameters();

    if (parameterId == "size")
        object.size.store (juce::jlimit (0.1f, 5.0f, value));
}

void BinuaralMixAudioProcessor::getStateInformation (
    juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BinuaralMixAudioProcessor::setStateInformation (
    const void* data,
    int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            updateObjectsFromParameters();
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BinuaralMixAudioProcessor();
}
