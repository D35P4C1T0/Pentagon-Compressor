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
    void moveStageToIndex(pentagon::StageType stage, int targetIndex);
    void setChainOrder(std::array<pentagon::StageType, pentagon::numStages> order);

    float getInputMeterDb() const noexcept;
    float getOutputMeterDb() const noexcept;
    float getStageMeterDb(pentagon::StageType stage) const noexcept;
    float getLimiterGainReductionDb() const noexcept;
    float getAutoGainCompensationDb() const noexcept;
    juce::String getOversamplingStatusText() const;

    juce::StringArray getFactoryPresetNames() const;
    void applyFactoryPreset(int index);
    int getCurrentProgramIndex() const noexcept;

    static constexpr int numUserPresetSlots = 4;
    juce::StringArray getUserPresetSlotNames() const;
    bool hasUserPresetSlot(int index) const;
    void saveUserPresetSlot(int index);
    void loadUserPresetSlot(int index);

    void captureComparisonSnapshot(bool slotA);
    bool hasComparisonSnapshot(bool slotA) const noexcept;
    void recallComparisonSnapshot(bool slotA);

    void setStageAudition(pentagon::StageType stage, pentagon::StageAuditionMode mode);
    pentagon::StageAuditionMode getStageAuditionMode(pentagon::StageType stage) const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void prepareOversampling(int maxChannels, int maxBlockSize);
    void updateLatencyForCurrentOversampling();
    int getRequestedOversamplingFactor() const noexcept;
    int getActiveOversamplingFactor() const noexcept;
    int getCurrentLatencySamples() const noexcept;
    int getLimiterLookaheadSamples() const noexcept;
    void applyPendingOversamplingMode();

    void updateDryLatencyCompensation(juce::AudioBuffer<float>& dryBuffer);
    void mixDryWetAndFinish(juce::AudioBuffer<float>& wetBuffer,
                            const juce::AudioBuffer<float>& dryBuffer,
                            float inputRms);
    void applyOutputStage(juce::AudioBuffer<float>& buffer);
    void publishMeters(float inputPeak, const juce::AudioBuffer<float>& outputBuffer);
    void syncStateProperties();
    void setPackedChainOrderInternal(uint32_t packedOrder, bool updateValueTree);
    void setParameterPlainValue(const juce::String& parameterID, float plainValue);
    juce::ValueTree createSnapshotState();
    void applySnapshotState(const juce::ValueTree& snapshot);
    juce::ValueTree getUserPresetContainer();
    const juce::ValueTree getUserPresetContainer() const;
    juce::ValueTree getUserPresetSlotNode(int index) const;
    void updateUserPresetSlot(int index, const juce::ValueTree& snapshot, const juce::String& name);
    static float computeWeightedLoudnessDb(const juce::AudioBuffer<float>& buffer) noexcept;
    float applySafety(float sample) const noexcept;

    APVTS parameters;
    pentagon::StageChainManager stageChain;

    std::atomic<uint32_t> packedChainOrder { pentagon::packChainOrder(pentagon::defaultChainOrder) };
    std::atomic<int> currentPresetIndex { 0 };
    std::array<std::atomic<float>, pentagon::numStages> stageMeterDb;
    std::atomic<float> inputMeterDb { -60.0f };
    std::atomic<float> outputMeterDb { -60.0f };
    std::atomic<float> limiterGainReductionDb { 0.0f };
    std::atomic<float> autoGainCompensationDb { 0.0f };

    std::atomic<float>* dryWetParam {};
    std::atomic<float>* outputGainParam {};
    std::atomic<float>* outputCeilingParam {};
    std::atomic<float>* autoGainEnabledParam {};
    std::atomic<float>* safetyEnabledParam {};
    std::atomic<float>* oversamplingParam {};
    std::atomic<float>* tweakModeParam {};
    std::atomic<float>* routingModeParam {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryWetSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputCeilingDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> autoGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> safetyMixSmoother;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling2x;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling4x;
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> dryDelayRingBuffer;
    juce::AudioBuffer<float> limiterLookaheadRingBuffer;
    int dryDelayWritePosition {};
    int limiterLookaheadWritePosition {};
    int currentLatencySamples {};
    int activeOversamplingMode { static_cast<int> (pentagon::OversamplingMode::x1) };
    int pendingOversamplingMode { static_cast<int> (pentagon::OversamplingMode::x1) };
    double currentSampleRate { 44100.0 };
    int maximumBlockSize {};
    int maximumChannels { 2 };
    float inputMeterStateDb { -60.0f };
    float outputMeterStateDb { -60.0f };
    float limiterGainState { 1.0f };
    float autoGainInputLoudnessDb { -24.0f };
    float autoGainOutputLoudnessDb { -24.0f };

    std::atomic<int> auditionStageIndex { -1 };
    std::atomic<int> auditionModeValue { static_cast<int> (pentagon::StageAuditionMode::off) };

    juce::ValueTree snapshotA;
    juce::ValueTree snapshotB;
    bool hasSnapshotA {};
    bool hasSnapshotB {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagonAudioProcessor)
};
