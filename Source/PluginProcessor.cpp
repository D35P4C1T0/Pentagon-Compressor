#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using namespace pentagon;

constexpr auto userPresetSlotsType = "UserPresetSlots";
constexpr auto userPresetSlotType = "UserPresetSlot";
constexpr auto userPresetSlotNameProperty = "name";
constexpr auto userPresetSlotSnapshotProperty = "snapshot";

juce::StringArray makeSaturationChoices(const std::initializer_list<const char*> names)
{
    juce::StringArray result;

    for (const auto* name : names)
        result.add(name);

    return result;
}

juce::StringArray makeFetRatioChoices()
{
    return { "4:1", "8:1", "12:1", "20:1" };
}

template <typename ParameterType, typename... Args>
std::unique_ptr<juce::RangedAudioParameter> makeParameter(Args&&... args)
{
    return std::make_unique<ParameterType>(std::forward<Args>(args)...);
}

float computeBufferRms(const juce::AudioBuffer<float>& buffer) noexcept
{
    double sumSquares = 0.0;
    int sampleCount = 0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const auto* samples = buffer.getReadPointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = static_cast<double> (samples[sample]);
            sumSquares += value * value;
        }

        sampleCount += buffer.getNumSamples();
    }

    if (sampleCount == 0)
        return 0.0f;

    return static_cast<float> (std::sqrt(sumSquares / static_cast<double> (sampleCount)));
}
} // namespace

PentagonAudioProcessor::PentagonAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, IDs::stateType, createParameterLayout()),
      stageChain(parameters)
{
    dryWetParam = parameters.getRawParameterValue(IDs::dryWet);
    outputGainParam = parameters.getRawParameterValue(IDs::outputGainDb);
    outputCeilingParam = parameters.getRawParameterValue(IDs::outputCeilingDb);
    autoGainEnabledParam = parameters.getRawParameterValue(IDs::autoGainEnabled);
    safetyEnabledParam = parameters.getRawParameterValue(IDs::safetyEnabled);
    oversamplingParam = parameters.getRawParameterValue(IDs::oversampling);
    tweakModeParam = parameters.getRawParameterValue(IDs::tweakMode);
    routingModeParam = parameters.getRawParameterValue(IDs::routingMode);

    for (auto& meter : stageMeterDb)
        meter.store(0.0f);

    syncStateProperties();
}

juce::AudioProcessorValueTreeState::ParameterLayout PentagonAudioProcessor::createParameterLayout()
{
    using Choice = juce::AudioParameterChoice;
    using Float = juce::AudioParameterFloat;
    using Bool = juce::AudioParameterBool;

    // One flat APVTS layout keeps automation/state handling simple across all five stages.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(50);

    params.push_back(makeParameter<Float>(IDs::dryWet, "Dry/Wet", juce::NormalisableRange<float>(0.0f, 1.0f), 0.71f));
    params.push_back(makeParameter<Float>(IDs::outputGainDb, "Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Float>(IDs::outputCeilingDb, "Ceiling", juce::NormalisableRange<float>(-18.0f, 0.0f, 0.01f), -0.8f));
    params.push_back(makeParameter<Bool>(IDs::autoGainEnabled, "Auto Gain", false));
    params.push_back(makeParameter<Bool>(IDs::safetyEnabled, "Safety", true));
    params.push_back(makeParameter<Choice>(IDs::oversampling, "Oversampling", juce::StringArray { "1x", "2x", "4x" }, 0));
    params.push_back(makeParameter<Choice>(IDs::tweakMode, "Tweak", juce::StringArray { "Clean", "Balanced", "Aggressive", "Glue", "Vocal", "Loud" }, 2));
    params.push_back(makeParameter<Choice>(IDs::routingMode, "Routing", juce::StringArray { "Serial", "Parallel", "Hybrid" }, 0));

    params.push_back(makeParameter<Bool>(IDs::fetEnabled, "FET76 On", false));
    params.push_back(makeParameter<Float>(IDs::fetMix, "FET76 Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Float>(IDs::fetSidechainHpf, "FET76 SC HPF", juce::NormalisableRange<float>(20.0f, 400.0f, 0.01f, 0.35f), 20.0f));
    params.push_back(makeParameter<Float>(IDs::fetInput, "FET76 Input", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Float>(IDs::fetOutput, "FET76 Output", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Float>(IDs::fetAttackUs, "FET76 Attack (us)", juce::NormalisableRange<float>(20.0f, 800.0f, 1.0f, 0.35f), 120.0f));
    params.push_back(makeParameter<Float>(IDs::fetReleaseMs, "FET76 Release (ms)", juce::NormalisableRange<float>(20.0f, 1200.0f, 1.0f, 0.35f), 220.0f));
    params.push_back(makeParameter<Float>(IDs::fetThresholdDb, "FET76 Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -24.0f));
    params.push_back(makeParameter<Choice>(IDs::fetRatio, "FET76 Ratio", makeFetRatioChoices(), 0));
    params.push_back(makeParameter<Choice>(IDs::fetSaturation, "FET76 Sat", makeSaturationChoices({ "Off", "Clean", "Warm", "Drive" }), 1));
    params.push_back(makeParameter<Float>(IDs::fetSaturationMix, "FET76 Sat Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Choice>(IDs::fetSaturationPlacement, "FET76 Sat Pos", juce::StringArray { "Post", "Pre", "Both" }, 0));

    params.push_back(makeParameter<Bool>(IDs::optoEnabled, "OPTO2A On", true));
    params.push_back(makeParameter<Float>(IDs::optoMix, "OPTO2A Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Float>(IDs::optoSidechainHpf, "OPTO2A SC HPF", juce::NormalisableRange<float>(20.0f, 400.0f, 0.01f, 0.35f), 20.0f));
    params.push_back(makeParameter<Float>(IDs::optoPeakReduction, "OPTO2A Peak Red", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 71.5f));
    params.push_back(makeParameter<Choice>(IDs::optoMode, "OPTO2A Mode", juce::StringArray { "COMP", "LIMIT" }, 0));
    params.push_back(makeParameter<Float>(IDs::optoMakeupDb, "OPTO2A Makeup", juce::NormalisableRange<float>(-12.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Float>(IDs::optoHfEmphasis, "OPTO2A HF Emp", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Choice>(IDs::optoSaturation, "OPTO2A Sat", makeSaturationChoices({ "Off", "Clean", "Warm", "Tube" }), 1));
    params.push_back(makeParameter<Float>(IDs::optoSaturationMix, "OPTO2A Sat Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Choice>(IDs::optoSaturationPlacement, "OPTO2A Sat Pos", juce::StringArray { "Post", "Pre", "Both" }, 0));

    params.push_back(makeParameter<Bool>(IDs::vcaEnabled, "VCA160 On", true));
    params.push_back(makeParameter<Float>(IDs::vcaMix, "VCA160 Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Float>(IDs::vcaSidechainHpf, "VCA160 SC HPF", juce::NormalisableRange<float>(20.0f, 400.0f, 0.01f, 0.35f), 20.0f));
    params.push_back(makeParameter<Float>(IDs::vcaThresholdDb, "VCA160 Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -28.6f));
    params.push_back(makeParameter<Bool>(IDs::vcaOvereasy, "VCA160 Overeasy", true));
    params.push_back(makeParameter<Float>(IDs::vcaMakeupDb, "VCA160 Makeup", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Choice>(IDs::vcaSaturation, "VCA160 Sat", makeSaturationChoices({ "Off", "Clean", "Punch", "Warm" }), 1));
    params.push_back(makeParameter<Float>(IDs::vcaSaturationMix, "VCA160 Sat Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Choice>(IDs::vcaSaturationPlacement, "VCA160 Sat Pos", juce::StringArray { "Post", "Pre", "Both" }, 0));

    params.push_back(makeParameter<Bool>(IDs::variEnabled, "VARIMU On", false));
    params.push_back(makeParameter<Float>(IDs::variMix, "VARIMU Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Float>(IDs::variSidechainHpf, "VARIMU SC HPF", juce::NormalisableRange<float>(20.0f, 400.0f, 0.01f, 0.35f), 20.0f));
    params.push_back(makeParameter<Float>(IDs::variThresholdDb, "VARIMU Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -22.0f));
    params.push_back(makeParameter<Float>(IDs::variAttackMs, "VARIMU Attack (ms)", juce::NormalisableRange<float>(0.1f, 100.0f, 0.01f, 0.3f), 45.0f));
    params.push_back(makeParameter<Choice>(IDs::variRecovery, "VARIMU Recovery", juce::StringArray { "Fast", "0.4s", "Slow", "Auto" }, 1));
    params.push_back(makeParameter<Float>(IDs::variMakeupDb, "VARIMU Makeup", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Choice>(IDs::variSaturation, "VARIMU Sat", makeSaturationChoices({ "Off", "Clean", "Warm", "Thick" }), 1));
    params.push_back(makeParameter<Float>(IDs::variSaturationMix, "VARIMU Sat Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Choice>(IDs::variSaturationPlacement, "VARIMU Sat Pos", juce::StringArray { "Post", "Pre", "Both" }, 0));

    params.push_back(makeParameter<Bool>(IDs::tubeEnabled, "TUBE670 On", false));
    params.push_back(makeParameter<Float>(IDs::tubeMix, "TUBE670 Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Float>(IDs::tubeSidechainHpf, "TUBE670 SC HPF", juce::NormalisableRange<float>(20.0f, 400.0f, 0.01f, 0.35f), 20.0f));
    params.push_back(makeParameter<Float>(IDs::tubeThresholdDb, "TUBE670 Threshold", juce::NormalisableRange<float>(-60.0f, 0.0f, 0.01f), -18.0f));
    params.push_back(makeParameter<Choice>(IDs::tubeTimeConstant, "TUBE670 Time Const", juce::StringArray { "1", "2", "3", "4", "5", "6" }, 1));
    params.push_back(makeParameter<Choice>(IDs::tubeMode, "TUBE670 Mode", juce::StringArray { "LR", "MS" }, 0));
    params.push_back(makeParameter<Float>(IDs::tubeMakeupDb, "TUBE670 Makeup", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(makeParameter<Choice>(IDs::tubeSaturation, "TUBE670 Sat", makeSaturationChoices({ "Off", "Clean", "Tube", "Heavy" }), 1));
    params.push_back(makeParameter<Float>(IDs::tubeSaturationMix, "TUBE670 Sat Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 0.01f), 100.0f));
    params.push_back(makeParameter<Choice>(IDs::tubeSaturationPlacement, "TUBE670 Sat Pos", juce::StringArray { "Post", "Pre", "Both" }, 0));

    return { params.begin(), params.end() };
}

const juce::String PentagonAudioProcessor::getName() const
{
    return "Pentagon";
}

bool PentagonAudioProcessor::acceptsMidi() const
{
    return false;
}

bool PentagonAudioProcessor::producesMidi() const
{
    return false;
}

bool PentagonAudioProcessor::isMidiEffect() const
{
    return false;
}

double PentagonAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int PentagonAudioProcessor::getNumPrograms()
{
    return getFactoryPresetNames().size();
}

int PentagonAudioProcessor::getCurrentProgram()
{
    return juce::jmax(0, getCurrentProgramIndex());
}

void PentagonAudioProcessor::setCurrentProgram(const int index)
{
    applyFactoryPreset(index);
}

const juce::String PentagonAudioProcessor::getProgramName(const int index)
{
    const auto names = getFactoryPresetNames();
    return juce::isPositiveAndBelow(index, names.size()) ? names[index] : juce::String {};
}

void PentagonAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void PentagonAudioProcessor::prepareOversampling(const int maxChannels, const int maxBlock)
{
    oversampling2x = std::make_unique<juce::dsp::Oversampling<float>>(static_cast<size_t> (maxChannels), 1,
                                                                       juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                                       true, true);
    oversampling4x = std::make_unique<juce::dsp::Oversampling<float>>(static_cast<size_t> (maxChannels), 2,
                                                                       juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                                       true, true);

    oversampling2x->reset();
    oversampling4x->reset();
    oversampling2x->initProcessing(static_cast<size_t> (maxBlock));
    oversampling4x->initProcessing(static_cast<size_t> (maxBlock));
}

void PentagonAudioProcessor::prepareToPlay(const double sampleRate, const int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maximumBlockSize = juce::jmax(1, samplesPerBlock);
    maximumChannels = juce::jlimit(1, 2, getTotalNumInputChannels());
    activeOversamplingMode = static_cast<int> (OversamplingMode::x1);
    pendingOversamplingMode = static_cast<int> (oversamplingParam->load());

    dryWetSmoother.reset(sampleRate, 0.02);
    outputGainDbSmoother.reset(sampleRate, 0.02);
    outputCeilingDbSmoother.reset(sampleRate, 0.05);
    autoGainDbSmoother.reset(sampleRate, 0.25);
    safetyMixSmoother.reset(sampleRate, 0.012);

    dryWetSmoother.setCurrentAndTargetValue(dryWetParam->load());
    outputGainDbSmoother.setCurrentAndTargetValue(outputGainParam->load());
    outputCeilingDbSmoother.setCurrentAndTargetValue(outputCeilingParam->load());
    autoGainDbSmoother.setCurrentAndTargetValue(0.0f);
    safetyMixSmoother.setCurrentAndTargetValue(safetyEnabledParam->load() > 0.5f ? 1.0f : 0.0f);

    dryBuffer.setSize(maximumChannels, maximumBlockSize, false, false, true);
    dryDelayRingBuffer.setSize(maximumChannels, maximumBlockSize + 2048, false, false, true);
    limiterLookaheadRingBuffer.setSize(maximumChannels, maximumBlockSize + getLimiterLookaheadSamples() + 32, false, false, true);
    dryDelayRingBuffer.clear();
    limiterLookaheadRingBuffer.clear();
    dryDelayWritePosition = 0;
    limiterLookaheadWritePosition = 0;
    limiterGainState = 1.0f;
    autoGainInputLoudnessDb = -24.0f;
    autoGainOutputLoudnessDb = -24.0f;

    prepareOversampling(maximumChannels, maximumBlockSize);

    stageChain.prepare({ sampleRate, samplesPerBlock, maximumChannels });
    stageChain.reset();

    if (pendingOversamplingMode != activeOversamplingMode)
        applyPendingOversamplingMode();

    updateLatencyForCurrentOversampling();
}

void PentagonAudioProcessor::releaseResources()
{
}

bool PentagonAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo();
}

int PentagonAudioProcessor::getRequestedOversamplingFactor() const noexcept
{
    switch (static_cast<OversamplingMode> (static_cast<int> (oversamplingParam->load())))
    {
        case OversamplingMode::x2: return 2;
        case OversamplingMode::x4: return 4;
        case OversamplingMode::x1:
        default:                   return 1;
    }
}

int PentagonAudioProcessor::getActiveOversamplingFactor() const noexcept
{
    switch (static_cast<OversamplingMode> (activeOversamplingMode))
    {
        case OversamplingMode::x2: return 2;
        case OversamplingMode::x4: return 4;
        case OversamplingMode::x1:
        default:                   return 1;
    }
}

int PentagonAudioProcessor::getCurrentLatencySamples() const noexcept
{
    if (const auto factor = getActiveOversamplingFactor(); factor == 2 && oversampling2x != nullptr)
        return static_cast<int>(std::ceil(oversampling2x->getLatencyInSamples()));
    else if (factor == 4 && oversampling4x != nullptr)
        return static_cast<int>(std::ceil(oversampling4x->getLatencyInSamples()));

    return 0;
}

int PentagonAudioProcessor::getLimiterLookaheadSamples() const noexcept
{
    return juce::jmax(32, static_cast<int> (std::round(currentSampleRate * 0.002)));
}

void PentagonAudioProcessor::applyPendingOversamplingMode()
{
    activeOversamplingMode = pendingOversamplingMode;
    currentLatencySamples = getCurrentLatencySamples();
    dryDelayRingBuffer.clear();
    dryDelayWritePosition = 0;

    if (oversampling2x != nullptr)
        oversampling2x->reset();

    if (oversampling4x != nullptr)
        oversampling4x->reset();

    stageChain.reset();
    limiterLookaheadRingBuffer.clear();
    limiterLookaheadWritePosition = 0;
    limiterGainState = 1.0f;
}

void PentagonAudioProcessor::updateLatencyForCurrentOversampling()
{
    pendingOversamplingMode = static_cast<int> (oversamplingParam->load());
    setLatencySamples(getCurrentLatencySamples() + getLimiterLookaheadSamples());
}

void PentagonAudioProcessor::updateDryLatencyCompensation(juce::AudioBuffer<float>& buffer)
{
    const auto delaySamples = currentLatencySamples;

    if (delaySamples <= 0)
        return;

    const auto ringSize = dryDelayRingBuffer.getNumSamples();

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto writePosition = dryDelayWritePosition;
        const auto readPosition = (writePosition + ringSize - delaySamples) % ringSize;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* ring = dryDelayRingBuffer.getWritePointer(channel);
            auto* dry = buffer.getWritePointer(channel);
            const auto delayed = ring[readPosition];
            ring[writePosition] = dry[sample];
            dry[sample] = delayed;
        }

        dryDelayWritePosition = (dryDelayWritePosition + 1) % ringSize;
    }
}

float PentagonAudioProcessor::applySafety(const float sample) const noexcept
{
    constexpr auto ceiling = 0.96f;

    if (std::abs(sample) <= ceiling)
        return sample;

    return std::tanh(sample / ceiling) * ceiling;
}

float PentagonAudioProcessor::computeWeightedLoudnessDb(const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto rms = computeBufferRms(buffer);
    float peak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));

    const auto weighted = juce::jmax(1.0e-6f, (rms * 0.82f) + (peak * 0.18f));
    return gainToDb(weighted);
}

void PentagonAudioProcessor::mixDryWetAndFinish(juce::AudioBuffer<float>& wetBuffer,
                                                const juce::AudioBuffer<float>& delayedDryBuffer,
                                                const float inputRms)
{
    dryWetSmoother.setTargetValue(dryWetParam->load());
    outputGainDbSmoother.setTargetValue(outputGainParam->load());
    outputCeilingDbSmoother.setTargetValue(outputCeilingParam->load());

    for (int sample = 0; sample < wetBuffer.getNumSamples(); ++sample)
    {
        const auto wetMix = dryWetSmoother.getNextValue();

        for (int channel = 0; channel < wetBuffer.getNumChannels(); ++channel)
        {
            const auto dry = delayedDryBuffer.getSample(channel, sample);
            const auto wet = wetBuffer.getSample(channel, sample);
            auto mixed = dry + (wetMix * (wet - dry));
            wetBuffer.setSample(channel, sample, mixed);
        }
    }

    const auto inputLoudnessDb = gainToDb(juce::jmax(1.0e-6f, inputRms));
    const auto outputLoudnessDb = computeWeightedLoudnessDb(wetBuffer);
    autoGainInputLoudnessDb = juce::jmap(0.07f, autoGainInputLoudnessDb, inputLoudnessDb);
    autoGainOutputLoudnessDb = juce::jmap(0.10f, autoGainOutputLoudnessDb, outputLoudnessDb);

    auto targetCompensationDb = 0.0f;

    if (autoGainEnabledParam->load() > 0.5f)
        targetCompensationDb = juce::jlimit(-12.0f, 12.0f, autoGainInputLoudnessDb - autoGainOutputLoudnessDb);

    autoGainDbSmoother.setTargetValue(targetCompensationDb);
    autoGainCompensationDb.store(targetCompensationDb);
}

void PentagonAudioProcessor::applyOutputStage(juce::AudioBuffer<float>& buffer)
{
    safetyMixSmoother.setTargetValue(safetyEnabledParam->load() > 0.5f ? 1.0f : 0.0f);

    const auto ringSize = limiterLookaheadRingBuffer.getNumSamples();
    const auto lookaheadSamples = juce::jmin(getLimiterLookaheadSamples(), ringSize - 1);
    auto blockLimiterReductionDb = 0.0f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto autoGain = dbToGain(autoGainDbSmoother.getNextValue());
        const auto outputGain = dbToGain(outputGainDbSmoother.getNextValue());
        const auto ceilingGain = dbToGain(outputCeilingDbSmoother.getNextValue());
        const auto safetyMix = safetyMixSmoother.getNextValue();

        auto detectorPeak = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* ring = limiterLookaheadRingBuffer.getWritePointer(channel);
            const auto shaped = buffer.getSample(channel, sample) * autoGain * outputGain;
            ring[limiterLookaheadWritePosition] = shaped;
            detectorPeak = juce::jmax(detectorPeak, std::abs(shaped));
        }

        const auto desiredGain = detectorPeak > ceilingGain ? juce::jlimit(0.02f, 1.0f, ceilingGain / detectorPeak) : 1.0f;
        const auto attackCoeff = std::exp(-1.0f / (0.00018f * static_cast<float> (currentSampleRate)));
        const auto releaseCoeff = std::exp(-1.0f / (0.085f * static_cast<float> (currentSampleRate)));
        const auto coeff = desiredGain < limiterGainState ? attackCoeff : releaseCoeff;
        limiterGainState = desiredGain + (coeff * (limiterGainState - desiredGain));

        const auto readPosition = (limiterLookaheadWritePosition + ringSize - lookaheadSamples) % ringSize;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            const auto* ring = limiterLookaheadRingBuffer.getReadPointer(channel);
            auto limited = ring[readPosition] * limiterGainState;
            const auto safe = applySafety(limited);
            limited += safetyMix * (safe - limited);
            buffer.setSample(channel, sample, limited);
        }

        limiterLookaheadWritePosition = (limiterLookaheadWritePosition + 1) % ringSize;
        blockLimiterReductionDb = juce::jmax(blockLimiterReductionDb, gainToDb(juce::jmax(1.0e-5f, limiterGainState)) * -1.0f);
    }

    limiterGainReductionDb.store(blockLimiterReductionDb);
}

void PentagonAudioProcessor::publishMeters(const float inputPeak, const juce::AudioBuffer<float>& outputBuffer)
{
    float outputPeak = 0.0f;

    for (int channel = 0; channel < outputBuffer.getNumChannels(); ++channel)
        outputPeak = juce::jmax(outputPeak, outputBuffer.getMagnitude(channel, 0, outputBuffer.getNumSamples()));

    const auto inputDb = juce::jmax(-60.0f, juce::Decibels::gainToDecibels(inputPeak, -60.0f));
    const auto outputDb = juce::jmax(-60.0f, juce::Decibels::gainToDecibels(outputPeak, -60.0f));

    inputMeterStateDb = inputDb > inputMeterStateDb ? inputDb : juce::jmap(0.18f, inputMeterStateDb, inputDb);
    outputMeterStateDb = outputDb > outputMeterStateDb ? outputDb : juce::jmap(0.18f, outputMeterStateDb, outputDb);

    inputMeterDb.store(inputMeterStateDb);
    outputMeterDb.store(outputMeterStateDb);

    const auto stageMeters = stageChain.getMeters();

    for (int index = 0; index < numStages; ++index)
        stageMeterDb[static_cast<size_t> (index)].store(stageMeters[static_cast<size_t> (index)]);
}

void PentagonAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (int channel = totalNumInputChannels; channel < totalNumOutputChannels; ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());

    if (buffer.getNumSamples() == 0)
        return;

    float inputPeak = 0.0f;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        // Keep a native-rate dry copy before the oversampled wet path is processed.
        dryBuffer.copyFrom(channel, 0, buffer, channel, 0, buffer.getNumSamples());
        inputPeak = juce::jmax(inputPeak, buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
    }

    const auto inputRms = computeBufferRms(dryBuffer);

    if (pendingOversamplingMode != activeOversamplingMode && inputPeak < 1.0e-3f)
        applyPendingOversamplingMode();

    updateLatencyForCurrentOversampling();

    const auto orderPacked = packedChainOrder.load(std::memory_order_acquire);
    const auto tweakMode = static_cast<int> (tweakModeParam->load());
    const auto oversamplingFactor = getActiveOversamplingFactor();
    const auto auditionStage = auditionStageIndex.load();
    const auto auditionMode = static_cast<StageAuditionMode> (auditionModeValue.load());

    if (oversamplingFactor == 1)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        stageChain.process(block, orderPacked, tweakMode, static_cast<int> (routingModeParam->load()), currentSampleRate, auditionStage, auditionMode);
    }
    else if (oversamplingFactor == 2 && oversampling2x != nullptr)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = oversampling2x->processSamplesUp(block);
        stageChain.process(oversampledBlock, orderPacked, tweakMode, static_cast<int> (routingModeParam->load()), currentSampleRate * 2.0, auditionStage, auditionMode);
        oversampling2x->processSamplesDown(block);
    }
    else if (oversampling4x != nullptr)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        auto oversampledBlock = oversampling4x->processSamplesUp(block);
        stageChain.process(oversampledBlock, orderPacked, tweakMode, static_cast<int> (routingModeParam->load()), currentSampleRate * 4.0, auditionStage, auditionMode);
        oversampling4x->processSamplesDown(block);
    }

    updateDryLatencyCompensation(dryBuffer);
    mixDryWetAndFinish(buffer, dryBuffer, inputRms);
    applyOutputStage(buffer);
    publishMeters(inputPeak, buffer);
}

bool PentagonAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* PentagonAudioProcessor::createEditor()
{
    return new PentagonAudioProcessorEditor(*this);
}

void PentagonAudioProcessor::syncStateProperties()
{
    parameters.state.setProperty(IDs::chainOrder, static_cast<int>(packedChainOrder.load()), nullptr);
    parameters.state.setProperty(IDs::presetIndex, currentPresetIndex.load(), nullptr);
}

void PentagonAudioProcessor::setPackedChainOrderInternal(const uint32_t packedOrder, const bool updateValueTree)
{
    const auto unpacked = unpackChainOrder(packedOrder);

    if (! isValidChainOrder(unpacked))
        return;

    packedChainOrder.store(packedOrder, std::memory_order_release);

    if (updateValueTree)
        parameters.state.setProperty(IDs::chainOrder, static_cast<int> (packedOrder), nullptr);
}

void PentagonAudioProcessor::moveStage(const StageType stage, const int direction)
{
    auto order = getChainOrder();
    const auto begin = order.begin();
    const auto end = order.end();
    const auto it = std::find(begin, end, stage);

    if (it == end)
        return;

    const auto index = static_cast<int>(std::distance(begin, it));
    const auto newIndex = juce::jlimit(0, numStages - 1, index + direction);

    if (newIndex == index)
        return;

    std::swap(order[static_cast<size_t> (index)], order[static_cast<size_t> (newIndex)]);
    setChainOrder(order);
}

void PentagonAudioProcessor::moveStageToIndex(const StageType stage, const int targetIndex)
{
    auto order = getChainOrder();
    const auto begin = order.begin();
    const auto end = order.end();
    const auto it = std::find(begin, end, stage);

    if (it == end)
        return;

    const auto currentIndex = static_cast<int> (std::distance(begin, it));
    const auto clampedTarget = juce::jlimit(0, numStages - 1, targetIndex);

    if (currentIndex == clampedTarget)
        return;

    const auto moved = *it;

    if (currentIndex < clampedTarget)
    {
        for (int index = currentIndex; index < clampedTarget; ++index)
            order[static_cast<size_t> (index)] = order[static_cast<size_t> (index + 1)];
    }
    else
    {
        for (int index = currentIndex; index > clampedTarget; --index)
            order[static_cast<size_t> (index)] = order[static_cast<size_t> (index - 1)];
    }

    order[static_cast<size_t> (clampedTarget)] = moved;
    setChainOrder(order);
}

void PentagonAudioProcessor::setChainOrder(std::array<StageType, numStages> order)
{
    if (! isValidChainOrder(order))
        order = defaultChainOrder;

    setPackedChainOrderInternal(packChainOrder(order), true);
}

std::array<StageType, numStages> PentagonAudioProcessor::getChainOrder() const noexcept
{
    return unpackChainOrder(packedChainOrder.load(std::memory_order_acquire));
}

uint32_t PentagonAudioProcessor::getPackedChainOrder() const noexcept
{
    return packedChainOrder.load(std::memory_order_acquire);
}

float PentagonAudioProcessor::getInputMeterDb() const noexcept
{
    return inputMeterDb.load();
}

float PentagonAudioProcessor::getOutputMeterDb() const noexcept
{
    return outputMeterDb.load();
}

float PentagonAudioProcessor::getStageMeterDb(const StageType stage) const noexcept
{
    return stageMeterDb[static_cast<size_t> (toIndex(stage))].load();
}

float PentagonAudioProcessor::getLimiterGainReductionDb() const noexcept
{
    return limiterGainReductionDb.load();
}

float PentagonAudioProcessor::getAutoGainCompensationDb() const noexcept
{
    return autoGainCompensationDb.load();
}

juce::String PentagonAudioProcessor::getOversamplingStatusText() const
{
    const auto modeToText = [] (const int mode)
    {
        switch (static_cast<OversamplingMode> (mode))
        {
            case OversamplingMode::x2: return juce::String("2x");
            case OversamplingMode::x4: return juce::String("4x");
            case OversamplingMode::x1:
            default:                   return juce::String("1x");
        }
    };

    if (pendingOversamplingMode != activeOversamplingMode)
        return modeToText(activeOversamplingMode) + " -> " + modeToText(pendingOversamplingMode) + " pending";

    return modeToText(activeOversamplingMode) + " active";
}

juce::AudioProcessorValueTreeState& PentagonAudioProcessor::getValueTreeState() noexcept
{
    return parameters;
}

juce::StringArray PentagonAudioProcessor::getFactoryPresetNames() const
{
    return { "VOICE RADIO", "DRUM SMASH", "GLUE BUS", "VOCAL LEVEL" };
}

void PentagonAudioProcessor::setParameterPlainValue(const juce::String& parameterID, const float plainValue)
{
    if (auto* parameter = parameters.getParameter(parameterID))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void PentagonAudioProcessor::applyFactoryPreset(const int index)
{
    const auto names = getFactoryPresetNames();
    const auto clampedIndex = juce::jlimit(0, names.size() - 1, index);

    currentPresetIndex.store(clampedIndex);
    parameters.state.setProperty(IDs::presetIndex, clampedIndex, nullptr);

    setParameterPlainValue(IDs::fetEnabled, 0.0f);
    setParameterPlainValue(IDs::optoEnabled, 0.0f);
    setParameterPlainValue(IDs::vcaEnabled, 0.0f);
    setParameterPlainValue(IDs::variEnabled, 0.0f);
    setParameterPlainValue(IDs::tubeEnabled, 0.0f);
    setParameterPlainValue(IDs::autoGainEnabled, 0.0f);
    setParameterPlainValue(IDs::outputCeilingDb, -0.8f);
    setParameterPlainValue(IDs::fetRatio, 0.0f);
    setParameterPlainValue(IDs::fetSaturationMix, 100.0f);
    setParameterPlainValue(IDs::optoSaturationMix, 100.0f);
    setParameterPlainValue(IDs::vcaSaturationMix, 100.0f);
    setParameterPlainValue(IDs::variSaturationMix, 100.0f);
    setParameterPlainValue(IDs::tubeSaturationMix, 100.0f);

    switch (clampedIndex)
    {
        case 0:
            setChainOrder(defaultChainOrder);
            setParameterPlainValue(IDs::dryWet, 0.71f);
            setParameterPlainValue(IDs::outputGainDb, 0.0f);
            setParameterPlainValue(IDs::outputCeilingDb, -0.8f);
            setParameterPlainValue(IDs::safetyEnabled, 1.0f);
            setParameterPlainValue(IDs::oversampling, 0.0f);
            setParameterPlainValue(IDs::tweakMode, 2.0f);

            setParameterPlainValue(IDs::fetEnabled, 0.0f);
            setParameterPlainValue(IDs::fetInput, 0.0f);
            setParameterPlainValue(IDs::fetOutput, 0.0f);
            setParameterPlainValue(IDs::fetAttackUs, 120.0f);
            setParameterPlainValue(IDs::fetReleaseMs, 220.0f);
            setParameterPlainValue(IDs::fetThresholdDb, -24.0f);
            setParameterPlainValue(IDs::fetRatio, 0.0f);
            setParameterPlainValue(IDs::fetSaturation, 1.0f);
            setParameterPlainValue(IDs::fetSaturationMix, 100.0f);

            setParameterPlainValue(IDs::optoEnabled, 1.0f);
            setParameterPlainValue(IDs::optoPeakReduction, 71.5f);
            setParameterPlainValue(IDs::optoMode, 0.0f);
            setParameterPlainValue(IDs::optoMakeupDb, 0.0f);
            setParameterPlainValue(IDs::optoHfEmphasis, 0.0f);
            setParameterPlainValue(IDs::optoSaturation, 1.0f);
            setParameterPlainValue(IDs::optoSaturationMix, 100.0f);

            setParameterPlainValue(IDs::vcaEnabled, 1.0f);
            setParameterPlainValue(IDs::vcaThresholdDb, -28.6f);
            setParameterPlainValue(IDs::vcaOvereasy, 1.0f);
            setParameterPlainValue(IDs::vcaMakeupDb, 0.0f);
            setParameterPlainValue(IDs::vcaSaturation, 1.0f);
            setParameterPlainValue(IDs::vcaSaturationMix, 100.0f);

            setParameterPlainValue(IDs::variEnabled, 0.0f);
            setParameterPlainValue(IDs::variThresholdDb, -22.0f);
            setParameterPlainValue(IDs::variAttackMs, 45.0f);
            setParameterPlainValue(IDs::variRecovery, 1.0f);
            setParameterPlainValue(IDs::variMakeupDb, 0.0f);
            setParameterPlainValue(IDs::variSaturation, 1.0f);
            setParameterPlainValue(IDs::variSaturationMix, 100.0f);

            setParameterPlainValue(IDs::tubeEnabled, 0.0f);
            setParameterPlainValue(IDs::tubeThresholdDb, -18.0f);
            setParameterPlainValue(IDs::tubeTimeConstant, 1.0f);
            setParameterPlainValue(IDs::tubeMode, 0.0f);
            setParameterPlainValue(IDs::tubeMakeupDb, 0.0f);
            setParameterPlainValue(IDs::tubeSaturation, 1.0f);
            setParameterPlainValue(IDs::tubeSaturationMix, 100.0f);
            break;

        case 1:
            setChainOrder({ StageType::fet76, StageType::vca160, StageType::opto2a, StageType::variMu, StageType::tube670 });
            setParameterPlainValue(IDs::dryWet, 0.92f);
            setParameterPlainValue(IDs::outputGainDb, -1.5f);
            setParameterPlainValue(IDs::outputCeilingDb, -1.1f);
            setParameterPlainValue(IDs::safetyEnabled, 1.0f);
            setParameterPlainValue(IDs::oversampling, 2.0f);
            setParameterPlainValue(IDs::tweakMode, 5.0f);

            setParameterPlainValue(IDs::fetEnabled, 1.0f);
            setParameterPlainValue(IDs::fetInput, 5.0f);
            setParameterPlainValue(IDs::fetOutput, -1.0f);
            setParameterPlainValue(IDs::fetAttackUs, 90.0f);
            setParameterPlainValue(IDs::fetReleaseMs, 120.0f);
            setParameterPlainValue(IDs::fetThresholdDb, -30.0f);
            setParameterPlainValue(IDs::fetRatio, 3.0f);
            setParameterPlainValue(IDs::fetSaturation, 3.0f);
            setParameterPlainValue(IDs::fetSaturationMix, 100.0f);

            setParameterPlainValue(IDs::optoEnabled, 0.0f);
            setParameterPlainValue(IDs::vcaEnabled, 1.0f);
            setParameterPlainValue(IDs::vcaThresholdDb, -34.0f);
            setParameterPlainValue(IDs::vcaOvereasy, 0.0f);
            setParameterPlainValue(IDs::vcaMakeupDb, 2.0f);
            setParameterPlainValue(IDs::vcaSaturation, 2.0f);
            setParameterPlainValue(IDs::vcaSaturationMix, 100.0f);

            setParameterPlainValue(IDs::variEnabled, 0.0f);
            setParameterPlainValue(IDs::tubeEnabled, 0.0f);
            break;

        case 2:
            setChainOrder({ StageType::variMu, StageType::opto2a, StageType::tube670, StageType::fet76, StageType::vca160 });
            setParameterPlainValue(IDs::dryWet, 0.68f);
            setParameterPlainValue(IDs::outputGainDb, 0.5f);
            setParameterPlainValue(IDs::outputCeilingDb, -0.8f);
            setParameterPlainValue(IDs::safetyEnabled, 1.0f);
            setParameterPlainValue(IDs::oversampling, 1.0f);
            setParameterPlainValue(IDs::tweakMode, 3.0f);

            setParameterPlainValue(IDs::variEnabled, 1.0f);
            setParameterPlainValue(IDs::variThresholdDb, -20.0f);
            setParameterPlainValue(IDs::variAttackMs, 55.0f);
            setParameterPlainValue(IDs::variRecovery, 3.0f);
            setParameterPlainValue(IDs::variMakeupDb, 1.0f);
            setParameterPlainValue(IDs::variSaturation, 2.0f);
            setParameterPlainValue(IDs::variSaturationMix, 100.0f);

            setParameterPlainValue(IDs::optoEnabled, 1.0f);
            setParameterPlainValue(IDs::optoPeakReduction, 55.0f);
            setParameterPlainValue(IDs::optoMode, 0.0f);
            setParameterPlainValue(IDs::optoMakeupDb, 0.5f);
            setParameterPlainValue(IDs::optoHfEmphasis, 12.0f);
            setParameterPlainValue(IDs::optoSaturation, 1.0f);
            setParameterPlainValue(IDs::optoSaturationMix, 100.0f);

            setParameterPlainValue(IDs::tubeEnabled, 1.0f);
            setParameterPlainValue(IDs::tubeThresholdDb, -24.0f);
            setParameterPlainValue(IDs::tubeTimeConstant, 2.0f);
            setParameterPlainValue(IDs::tubeMakeupDb, 1.5f);
            setParameterPlainValue(IDs::tubeSaturation, 2.0f);
            setParameterPlainValue(IDs::tubeSaturationMix, 100.0f);

            setParameterPlainValue(IDs::fetEnabled, 0.0f);
            setParameterPlainValue(IDs::vcaEnabled, 0.0f);
            break;

        default:
            setChainOrder({ StageType::opto2a, StageType::tube670, StageType::vca160, StageType::fet76, StageType::variMu });
            setParameterPlainValue(IDs::dryWet, 0.78f);
            setParameterPlainValue(IDs::outputGainDb, 0.0f);
            setParameterPlainValue(IDs::outputCeilingDb, -0.8f);
            setParameterPlainValue(IDs::safetyEnabled, 1.0f);
            setParameterPlainValue(IDs::oversampling, 1.0f);
            setParameterPlainValue(IDs::tweakMode, 4.0f);

            setParameterPlainValue(IDs::optoEnabled, 1.0f);
            setParameterPlainValue(IDs::optoPeakReduction, 63.0f);
            setParameterPlainValue(IDs::optoMode, 0.0f);
            setParameterPlainValue(IDs::optoMakeupDb, 1.0f);
            setParameterPlainValue(IDs::optoHfEmphasis, 8.0f);
            setParameterPlainValue(IDs::optoSaturation, 1.0f);
            setParameterPlainValue(IDs::optoSaturationMix, 100.0f);

            setParameterPlainValue(IDs::tubeEnabled, 1.0f);
            setParameterPlainValue(IDs::tubeThresholdDb, -19.0f);
            setParameterPlainValue(IDs::tubeTimeConstant, 1.0f);
            setParameterPlainValue(IDs::tubeMakeupDb, 1.0f);
            setParameterPlainValue(IDs::tubeSaturation, 1.0f);
            setParameterPlainValue(IDs::tubeSaturationMix, 100.0f);

            setParameterPlainValue(IDs::vcaEnabled, 1.0f);
            setParameterPlainValue(IDs::vcaThresholdDb, -26.0f);
            setParameterPlainValue(IDs::vcaOvereasy, 1.0f);
            setParameterPlainValue(IDs::vcaMakeupDb, 0.0f);
            setParameterPlainValue(IDs::vcaSaturation, 1.0f);
            setParameterPlainValue(IDs::vcaSaturationMix, 100.0f);

            setParameterPlainValue(IDs::fetEnabled, 0.0f);
            setParameterPlainValue(IDs::variEnabled, 0.0f);
            break;
    }
}

juce::ValueTree PentagonAudioProcessor::createSnapshotState()
{
    auto snapshot = parameters.copyState();

    for (int childIndex = snapshot.getNumChildren(); --childIndex >= 0;)
    {
        if (snapshot.getChild(childIndex).hasType(userPresetSlotsType))
            snapshot.removeChild(childIndex, nullptr);
    }

    snapshot.setProperty(IDs::chainOrder, static_cast<int> (packedChainOrder.load()), nullptr);
    snapshot.setProperty(IDs::presetIndex, currentPresetIndex.load(), nullptr);
    return snapshot;
}

juce::ValueTree PentagonAudioProcessor::getUserPresetContainer()
{
    for (int childIndex = 0; childIndex < parameters.state.getNumChildren(); ++childIndex)
    {
        auto child = parameters.state.getChild(childIndex);

        if (child.hasType(userPresetSlotsType))
            return child;
    }

    auto container = juce::ValueTree(userPresetSlotsType);

    for (int index = 0; index < numUserPresetSlots; ++index)
    {
        auto slot = juce::ValueTree(userPresetSlotType);
        slot.setProperty(userPresetSlotNameProperty, juce::String("SLOT ") + juce::String(index + 1), nullptr);
        slot.setProperty(userPresetSlotSnapshotProperty, juce::String(), nullptr);
        container.addChild(slot, -1, nullptr);
    }

    parameters.state.addChild(container, -1, nullptr);
    return parameters.state.getChild(parameters.state.getNumChildren() - 1);
}

const juce::ValueTree PentagonAudioProcessor::getUserPresetContainer() const
{
    for (int childIndex = 0; childIndex < parameters.state.getNumChildren(); ++childIndex)
    {
        const auto child = parameters.state.getChild(childIndex);

        if (child.hasType(userPresetSlotsType))
            return child;
    }

    return {};
}

juce::ValueTree PentagonAudioProcessor::getUserPresetSlotNode(const int index) const
{
    if (! juce::isPositiveAndBelow(index, numUserPresetSlots))
        return {};

    const auto container = getUserPresetContainer();

    if (! container.isValid() || index >= container.getNumChildren())
        return {};

    return container.getChild(index);
}

void PentagonAudioProcessor::updateUserPresetSlot(const int index, const juce::ValueTree& snapshot, const juce::String& name)
{
    auto container = getUserPresetContainer();

    if (! juce::isPositiveAndBelow(index, juce::jmin(numUserPresetSlots, container.getNumChildren())))
        return;

    auto slot = container.getChild(index);
    slot.setProperty(userPresetSlotNameProperty, name, nullptr);
    slot.setProperty(userPresetSlotSnapshotProperty, snapshot.toXmlString(), nullptr);
}

juce::StringArray PentagonAudioProcessor::getUserPresetSlotNames() const
{
    juce::StringArray names;
    const auto container = getUserPresetContainer();

    for (int index = 0; index < numUserPresetSlots; ++index)
    {
        auto name = juce::String("SLOT ") + juce::String(index + 1);

        if (container.isValid() && index < container.getNumChildren())
            name = container.getChild(index).getProperty(userPresetSlotNameProperty, name).toString();

        names.add(name);
    }

    return names;
}

bool PentagonAudioProcessor::hasUserPresetSlot(const int index) const
{
    const auto slot = getUserPresetSlotNode(index);
    return slot.isValid() && slot.getProperty(userPresetSlotSnapshotProperty).toString().isNotEmpty();
}

void PentagonAudioProcessor::applySnapshotState(const juce::ValueTree& snapshot)
{
    if (! snapshot.isValid())
        return;

    auto restored = snapshot.createCopy();

    for (int childIndex = restored.getNumChildren(); --childIndex >= 0;)
    {
        if (restored.getChild(childIndex).hasType(userPresetSlotsType))
            restored.removeChild(childIndex, nullptr);
    }

    const auto userPresets = getUserPresetContainer();

    if (userPresets.isValid())
        restored.addChild(userPresets.createCopy(), -1, nullptr);

    parameters.replaceState(restored);

    auto restoredPackedOrder = static_cast<uint32_t> (static_cast<int> (parameters.state.getProperty(IDs::chainOrder, static_cast<int> (packChainOrder(defaultChainOrder)))));
    const auto unpacked = unpackChainOrder(restoredPackedOrder);

    if (! isValidChainOrder(unpacked))
        restoredPackedOrder = packChainOrder(defaultChainOrder);

    setPackedChainOrderInternal(restoredPackedOrder, true);
    currentPresetIndex.store(static_cast<int> (parameters.state.getProperty(IDs::presetIndex, -1)));
}

void PentagonAudioProcessor::saveUserPresetSlot(const int index)
{
    if (! juce::isPositiveAndBelow(index, numUserPresetSlots))
        return;

    const auto snapshot = createSnapshotState();
    const auto factoryNames = getFactoryPresetNames();
    const auto currentIndex = currentPresetIndex.load();
    const auto defaultName = juce::isPositiveAndBelow(currentIndex, factoryNames.size())
        ? factoryNames[currentIndex] + " USER"
        : juce::String("SLOT ") + juce::String(index + 1);

    updateUserPresetSlot(index, snapshot, defaultName);
}

void PentagonAudioProcessor::loadUserPresetSlot(const int index)
{
    const auto slot = getUserPresetSlotNode(index);

    if (! slot.isValid())
        return;

    const auto xmlText = slot.getProperty(userPresetSlotSnapshotProperty).toString();

    if (xmlText.isEmpty())
        return;

    if (auto xml = juce::parseXML(xmlText))
        applySnapshotState(juce::ValueTree::fromXml(*xml));
}

void PentagonAudioProcessor::captureComparisonSnapshot(const bool slotA)
{
    if (slotA)
    {
        snapshotA = createSnapshotState();
        hasSnapshotA = true;
    }
    else
    {
        snapshotB = createSnapshotState();
        hasSnapshotB = true;
    }
}

bool PentagonAudioProcessor::hasComparisonSnapshot(const bool slotA) const noexcept
{
    return slotA ? hasSnapshotA : hasSnapshotB;
}

void PentagonAudioProcessor::recallComparisonSnapshot(const bool slotA)
{
    if (slotA && hasSnapshotA)
        applySnapshotState(snapshotA);
    else if (! slotA && hasSnapshotB)
        applySnapshotState(snapshotB);
}

void PentagonAudioProcessor::setStageAudition(const StageType stage, const StageAuditionMode mode)
{
    if (mode == StageAuditionMode::off)
    {
        auditionStageIndex.store(-1);
        auditionModeValue.store(static_cast<int> (StageAuditionMode::off));
        return;
    }

    auditionStageIndex.store(toIndex(stage));
    auditionModeValue.store(static_cast<int> (mode));
}

StageAuditionMode PentagonAudioProcessor::getStageAuditionMode(const StageType stage) const noexcept
{
    if (auditionStageIndex.load() != toIndex(stage))
        return StageAuditionMode::off;

    return static_cast<StageAuditionMode> (auditionModeValue.load());
}

int PentagonAudioProcessor::getCurrentProgramIndex() const noexcept
{
    return currentPresetIndex.load();
}

void PentagonAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty(IDs::chainOrder, static_cast<int> (packedChainOrder.load()), nullptr);
    state.setProperty(IDs::presetIndex, currentPresetIndex.load(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void PentagonAudioProcessor::setStateInformation(const void* data, const int sizeInBytes)
{
    if (auto xmlState = getXmlFromBinary(data, sizeInBytes))
    {
        const auto tree = juce::ValueTree::fromXml(*xmlState);

        if (tree.isValid())
            parameters.replaceState(tree);
    }

    getUserPresetContainer();
    applySnapshotState(parameters.copyState());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PentagonAudioProcessor();
}
