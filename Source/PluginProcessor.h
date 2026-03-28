#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "ParameterIds.h"
#include "StageProcessors.h"

class PentagonAudioProcessor final : public juce::AudioProcessor
{
public:
    using juce::AudioProcessor::processBlock;

    PentagonAudioProcessor();
    ~PentagonAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept;

    std::array<pentagon::StageType, pentagon::numStages> getChainOrder() const noexcept;
    uint32_t getPackedChainOrder() const noexcept;
    void moveStage(pentagon::StageType stage, int direction);
    void setChainOrder(std::array<pentagon::StageType, pentagon::numStages> order);

    float getInputMeterDb() const noexcept;
    float getOutputMeterDb() const noexcept;
    float getStageMeterDb(pentagon::StageType stage) const noexcept;

    juce::StringArray getFactoryPresetNames() const;
    void applyFactoryPreset(int index);
    int getCurrentProgramIndex() const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void prepareOversampling(int maxChannels, int maxBlockSize);
    void updateLatencyForCurrentOversampling();
    int getCurrentOversamplingFactor() const noexcept;
    int getCurrentLatencySamples() const noexcept;

    void updateDryLatencyCompensation(juce::AudioBuffer<float>& dryBuffer);
    void mixDryWetAndFinish(juce::AudioBuffer<float>& wetBuffer,
                            const juce::AudioBuffer<float>& dryBuffer,
                            float inputRms);
    void publishMeters(float inputPeak, const juce::AudioBuffer<float>& outputBuffer);
    void syncStateProperties();
    void setPackedChainOrderInternal(uint32_t packedOrder, bool updateValueTree);
    void setParameterPlainValue(const juce::String& parameterID, float plainValue);
    float applySafety(float sample) const noexcept;

    APVTS parameters;
    pentagon::StageChainManager stageChain;

    std::atomic<uint32_t> packedChainOrder { pentagon::packChainOrder(pentagon::defaultChainOrder) };
    std::atomic<int> currentPresetIndex { 0 };
    std::array<std::atomic<float>, pentagon::numStages> stageMeterDb;
    std::atomic<float> inputMeterDb { -60.0f };
    std::atomic<float> outputMeterDb { -60.0f };

    std::atomic<float>* dryWetParam {};
    std::atomic<float>* outputGainParam {};
    std::atomic<float>* autoGainEnabledParam {};
    std::atomic<float>* safetyEnabledParam {};
    std::atomic<float>* oversamplingParam {};
    std::atomic<float>* tweakModeParam {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryWetSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> autoGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> safetyMixSmoother;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling4x;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> dryDelayRingBuffer;
    int dryDelayWritePosition {};
    int currentLatencySamples {};
    double currentSampleRate { 44100.0 };
    int maximumBlockSize {};
    int maximumChannels { 2 };
    float inputMeterStateDb { -60.0f };
    float outputMeterStateDb { -60.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagonAudioProcessor)
};
