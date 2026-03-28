#pragma once

#include <array>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

#include "ParameterIds.h"

namespace pentagon
{
struct StagePrepareSpec
{
    double sampleRate {};
    int maximumBlockSize {};
    int numChannels {};
};

inline float dbToGain(const float db) noexcept
{
    return juce::Decibels::decibelsToGain(db, -120.0f);
}

inline float gainToDb(const float gain) noexcept
{
    return juce::Decibels::gainToDecibels(juce::jmax(1.0e-6f, gain), -120.0f);
}

inline float envelopeCoeff(const float timeSeconds, const double sampleRate) noexcept
{
    return std::exp(-1.0f / (juce::jmax(1.0e-4f, timeSeconds) * static_cast<float> (sampleRate)));
}

inline float computeReductionDb(const float detectorDb,
                                const float thresholdDb,
                                const float ratio,
                                const float kneeDb) noexcept
{
    // Shared static curve helper used by all five compressor models.
    const auto overDb = detectorDb - thresholdDb;

    if (kneeDb > 0.0f)
    {
        const auto halfKnee = kneeDb * 0.5f;

        if (overDb <= -halfKnee)
            return 0.0f;

        if (overDb < halfKnee)
        {
            const auto shifted = overDb + halfKnee;
            return (1.0f - (1.0f / ratio)) * shifted * shifted / (2.0f * kneeDb);
        }
    }

    return juce::jmax(0.0f, overDb) * (1.0f - (1.0f / ratio));
}

inline float saturationAmountFromMode(const int modeIndex) noexcept
{
    switch (modeIndex)
    {
        case 0:  return 0.0f;
        case 1:  return 0.08f;
        case 2:  return 0.18f;
        default: return 0.34f;
    }
}

inline float saturationBlend(const float dry, const float saturated, const float mix) noexcept
{
    // Each stage exposes both a saturation mode and a fine-grained wet mix.
    return dry + (juce::jlimit(0.0f, 1.0f, mix) * (saturated - dry));
}

inline float fetRatioFromChoice(const int choiceIndex) noexcept
{
    switch (choiceIndex)
    {
        case 0:  return 4.0f;
        case 1:  return 8.0f;
        case 2:  return 12.0f;
        default: return 20.0f;
    }
}

inline float saturateSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 5.5f);
    const auto shaped = std::tanh(sample * drive);
    const auto normaliser = std::tanh(drive);
    return shaped / juce::jmax(0.25f, normaliser);
}

inline int readChoiceValue(const std::atomic<float>* value) noexcept
{
    return static_cast<int> (std::lround(value->load()));
}

class StageBase
{
public:
    void prepare(const StagePrepareSpec& spec)
    {
        sampleRate = spec.sampleRate;
        numChannels = juce::jlimit(1, 2, spec.numChannels);
        enabledMix.reset(sampleRate, 0.012);
        meterReset();
    }

    void reset()
    {
        enabledMix.setCurrentAndTargetValue(0.0f);
        meterReset();
    }

    float getGainReductionDb() const noexcept
    {
        return meterGainReductionDb;
    }

protected:
    double sampleRate { 44100.0 };
    int numChannels { 2 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> enabledMix;
    float meterGainReductionDb {};

    void meterReset() noexcept
    {
        meterGainReductionDb = 0.0f;
    }

    void updateMeter(const float blockPeakReductionDb) noexcept
    {
        meterGainReductionDb = juce::jmax(blockPeakReductionDb, meterGainReductionDb * 0.88f);
    }
};

class Fet76Stage final : public StageBase
{
public:
    explicit Fet76Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::fetEnabled)),
          inputGain(apvts.getRawParameterValue(IDs::fetInput)),
          outputGain(apvts.getRawParameterValue(IDs::fetOutput)),
          attackUs(apvts.getRawParameterValue(IDs::fetAttackUs)),
          releaseMs(apvts.getRawParameterValue(IDs::fetReleaseMs)),
          thresholdDb(apvts.getRawParameterValue(IDs::fetThresholdDb)),
          ratio(apvts.getRawParameterValue(IDs::fetRatio)),
          saturationMode(apvts.getRawParameterValue(IDs::fetSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::fetSaturationMix))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        inputGainDbSmoother.reset(sampleRate, 0.02);
        outputGainDbSmoother.reset(sampleRate, 0.02);
        attackUsSmoother.reset(sampleRate, 0.02);
        releaseMsSmoother.reset(sampleRate, 0.02);
        thresholdDbSmoother.reset(sampleRate, 0.02);
        saturationMixSmoother.reset(sampleRate, 0.02);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        // Stage bypass is smoothed so enable toggles do not hard-step the signal.
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);

        inputGainDbSmoother.setTargetValue(inputGain->load());
        outputGainDbSmoother.setTargetValue(outputGain->load());
        attackUsSmoother.setTargetValue(attackUs->load() * macro.attackScale);
        releaseMsSmoother.setTargetValue(releaseMs->load() * macro.releaseScale);
        thresholdDbSmoother.setTargetValue(thresholdDb->load() + macro.thresholdOffsetDb);
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto ratioValue = fetRatioFromChoice(readChoiceValue(ratio));
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        float blockPeakReductionDb = 0.0f;

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto inGain = dbToGain(inputGainDbSmoother.getNextValue());
            const auto outGain = dbToGain(outputGainDbSmoother.getNextValue());
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto attackSeconds = juce::jlimit(20.0e-6f, 5.0e-3f, attackUsSmoother.getNextValue() * 1.0e-6f);
            const auto releaseSeconds = juce::jlimit(0.01f, 2.0f, releaseMsSmoother.getNextValue() * 0.001f);
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linkedPeak = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(static_cast<size_t> (channel), sample);
                linkedPeak = juce::jmax(linkedPeak, std::abs(dry[static_cast<size_t> (channel)] * inGain));
            }

            const auto detectorDb = gainToDb(linkedPeak);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, ratioValue, 2.0f);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? attackSeconds : releaseSeconds, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * inGain * gain;
                processed = saturationBlend(processed, saturateSample(processed, saturation), satMix);
                processed *= outGain;
                block.setSample(static_cast<size_t> (channel), sample, dry[static_cast<size_t> (channel)] + (mix * (processed - dry[static_cast<size_t> (channel)])));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    std::atomic<float>* enabled {};
    std::atomic<float>* inputGain {};
    std::atomic<float>* outputGain {};
    std::atomic<float>* attackUs {};
    std::atomic<float>* releaseMs {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* ratio {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> attackUsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> releaseMsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
};

class Opto2AStage final : public StageBase
{
public:
    explicit Opto2AStage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::optoEnabled)),
          peakReduction(apvts.getRawParameterValue(IDs::optoPeakReduction)),
          mode(apvts.getRawParameterValue(IDs::optoMode)),
          makeupGainDb(apvts.getRawParameterValue(IDs::optoMakeupDb)),
          hfEmphasis(apvts.getRawParameterValue(IDs::optoHfEmphasis)),
          saturationMode(apvts.getRawParameterValue(IDs::optoSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::optoSaturationMix))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        peakReductionSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        hfEmphasisSmoother.reset(sampleRate, 0.03);
        saturationMixSmoother.reset(sampleRate, 0.03);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        highpassState = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        peakReductionSmoother.setTargetValue(peakReduction->load());
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        hfEmphasisSmoother.setTargetValue(hfEmphasis->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto ratio = readChoiceValue(mode) == 0 ? 3.0f : 10.0f;
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        float blockPeakReductionDb = 0.0f;

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto reductionControl = peakReductionSmoother.getNextValue();
            const auto threshold = juce::jmap(reductionControl, 0.0f, 100.0f, 0.0f, -45.0f) + macro.thresholdOffsetDb;
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto hf = hfEmphasisSmoother.getNextValue() * 0.01f;
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linked = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(static_cast<size_t> (channel), sample);
                linked += std::abs(dry[static_cast<size_t> (channel)]);
            }

            linked /= static_cast<float> (numChannels);
            highpassState = (0.985f * highpassState) + (0.015f * linked);
            const auto hfComponent = linked - highpassState;
            const auto detector = juce::jmax(1.0e-6f, linked + (std::abs(hfComponent) * hf));
            const auto detectorDb = gainToDb(detector);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, ratio, 6.0f);
            const auto releaseSeconds = juce::jmap(juce::jlimit(0.0f, 1.0f, envelopeDb / 18.0f), 0.0f, 1.0f, 0.12f, 0.9f) * macro.releaseScale;
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? 0.01f * macro.attackScale : releaseSeconds, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * gain;
                processed = saturationBlend(processed, saturateSample(processed, saturation), satMix);
                processed *= makeup;
                block.setSample(static_cast<size_t> (channel), sample, dry[static_cast<size_t> (channel)] + (mix * (processed - dry[static_cast<size_t> (channel)])));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    std::atomic<float>* enabled {};
    std::atomic<float>* peakReduction {};
    std::atomic<float>* mode {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* hfEmphasis {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> peakReductionSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfEmphasisSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float highpassState {};
};

class Vca160Stage final : public StageBase
{
public:
    explicit Vca160Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::vcaEnabled)),
          thresholdDb(apvts.getRawParameterValue(IDs::vcaThresholdDb)),
          overeasy(apvts.getRawParameterValue(IDs::vcaOvereasy)),
          makeupGainDb(apvts.getRawParameterValue(IDs::vcaMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::vcaSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::vcaSaturationMix))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.02);
        makeupDbSmoother.reset(sampleRate, 0.02);
        saturationMixSmoother.reset(sampleRate, 0.02);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        thresholdDbSmoother.setTargetValue(thresholdDb->load() + macro.thresholdOffsetDb);
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto knee = overeasy->load() > 0.5f ? 8.0f : 1.0f;
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        float blockPeakReductionDb = 0.0f;

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linkedPeak = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(static_cast<size_t> (channel), sample);
                linkedPeak += dry[static_cast<size_t> (channel)] * dry[static_cast<size_t> (channel)];
            }

            linkedPeak = std::sqrt(linkedPeak / static_cast<float> (numChannels));

            const auto detectorDb = gainToDb(linkedPeak);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 4.0f, knee);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? 0.0012f * macro.attackScale : 0.10f * macro.releaseScale, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * gain;
                processed = saturationBlend(processed, saturateSample(processed, saturation), satMix);
                processed *= makeup;
                block.setSample(static_cast<size_t> (channel), sample, dry[static_cast<size_t> (channel)] + (mix * (processed - dry[static_cast<size_t> (channel)])));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    std::atomic<float>* enabled {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* overeasy {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
};

class VariMuStage final : public StageBase
{
public:
    explicit VariMuStage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::variEnabled)),
          thresholdDb(apvts.getRawParameterValue(IDs::variThresholdDb)),
          attackMs(apvts.getRawParameterValue(IDs::variAttackMs)),
          recovery(apvts.getRawParameterValue(IDs::variRecovery)),
          makeupGainDb(apvts.getRawParameterValue(IDs::variMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::variSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::variSaturationMix))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.03);
        attackMsSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        saturationMixSmoother.reset(sampleRate, 0.03);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        rmsState = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        thresholdDbSmoother.setTargetValue(thresholdDb->load() + macro.thresholdOffsetDb);
        attackMsSmoother.setTargetValue(attackMs->load() * macro.attackScale);
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto saturation = juce::jlimit(0.0f, 0.85f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost + 0.03f);
        float blockPeakReductionDb = 0.0f;

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto attackSeconds = juce::jlimit(0.0001f, 0.25f, attackMsSmoother.getNextValue() * 0.001f);
            const auto releaseSeconds = getReleaseSeconds(readChoiceValue(recovery), envelopeDb) * macro.releaseScale;
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            std::array<float, 2> dry {};
            float linkedSquare = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(static_cast<size_t> (channel), sample);
                linkedSquare += dry[static_cast<size_t> (channel)] * dry[static_cast<size_t> (channel)];
            }

            linkedSquare /= static_cast<float> (numChannels);
            const auto rmsCoeff = envelopeCoeff(0.02f, effectiveSampleRate);
            rmsState = (rmsCoeff * rmsState) + ((1.0f - rmsCoeff) * linkedSquare);

            const auto detectorDb = gainToDb(std::sqrt(rmsState));
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 2.5f, 10.0f);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? attackSeconds : releaseSeconds, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * gain;
                processed = saturationBlend(processed, saturateSample(processed, saturation), satMix);
                processed *= makeup;
                block.setSample(static_cast<size_t> (channel), sample, dry[static_cast<size_t> (channel)] + (mix * (processed - dry[static_cast<size_t> (channel)])));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    static float getReleaseSeconds(const int recoveryMode, const float currentReductionDb) noexcept
    {
        switch (recoveryMode)
        {
            case 0:  return 0.15f;
            case 1:  return 0.40f;
            case 2:  return 0.85f;
            default: return juce::jmap(juce::jlimit(0.0f, 1.0f, currentReductionDb / 18.0f), 0.0f, 1.0f, 0.30f, 1.40f);
        }
    }

    std::atomic<float>* enabled {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* attackMs {};
    std::atomic<float>* recovery {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> attackMsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float rmsState {};
};

class Tube670Stage final : public StageBase
{
public:
    explicit Tube670Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::tubeEnabled)),
          thresholdDb(apvts.getRawParameterValue(IDs::tubeThresholdDb)),
          timeConstant(apvts.getRawParameterValue(IDs::tubeTimeConstant)),
          mode(apvts.getRawParameterValue(IDs::tubeMode)),
          makeupGainDb(apvts.getRawParameterValue(IDs::tubeMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::tubeSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::tubeSaturationMix))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        saturationMixSmoother.reset(sampleRate, 0.03);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        envelopeDbMid = 0.0f;
        envelopeDbSide = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        thresholdDbSmoother.setTargetValue(thresholdDb->load() + macro.thresholdOffsetDb);
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto modeValue = readChoiceValue(mode);
        const auto timing = getTiming(readChoiceValue(timeConstant));
        const auto saturation = juce::jlimit(0.0f, 0.9f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost + 0.06f);
        float blockPeakReductionDb = 0.0f;
        constexpr auto invSqrt2 = 0.70710678118f;

        for (size_t sample = 0; sample < block.getNumSamples(); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            const auto applyGainCell = [threshold, &timing, macro, effectiveSampleRate](float inputSample, float& envelopeState) noexcept
            {
                const auto detectorDb = gainToDb(std::abs(inputSample));
                const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 6.0f, 6.0f);
                const auto attackSeconds = timing.first * macro.attackScale;
                const auto releaseSeconds = timing.second * macro.releaseScale;
                const auto coeff = envelopeCoeff(targetReductionDb > envelopeState ? attackSeconds : releaseSeconds, effectiveSampleRate);
                envelopeState = (coeff * envelopeState) + ((1.0f - coeff) * targetReductionDb);
                return dbToGain(-envelopeState);
            };

            if (modeValue == 1 && numChannels >= 2)
            {
                // The MS mode compresses encoded mid and side independently, then decodes back to LR.
                const auto leftDry = block.getSample(0, sample);
                const auto rightDry = block.getSample(1, sample);

                const auto mid = (leftDry + rightDry) * invSqrt2;
                const auto side = (leftDry - rightDry) * invSqrt2;

                auto processedMid = mid * applyGainCell(mid, envelopeDbMid);
                auto processedSide = side * applyGainCell(side, envelopeDbSide);

                processedMid = saturationBlend(processedMid, saturateSample(processedMid, saturation), satMix) * makeup;
                processedSide = saturationBlend(processedSide, saturateSample(processedSide, saturation), satMix) * makeup;

                const auto leftWet = (processedMid + processedSide) * invSqrt2;
                const auto rightWet = (processedMid - processedSide) * invSqrt2;

                block.setSample(0, sample, leftDry + (mix * (leftWet - leftDry)));
                block.setSample(1, sample, rightDry + (mix * (rightWet - rightDry)));

                blockPeakReductionDb = juce::jmax(blockPeakReductionDb, juce::jmax(envelopeDbMid, envelopeDbSide));
                continue;
            }

            float linkedPeak = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(static_cast<size_t> (channel), sample);
                linkedPeak = juce::jmax(linkedPeak, std::abs(dry[static_cast<size_t> (channel)]));
            }

            const auto detectorDb = gainToDb(linkedPeak);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 6.0f, 6.0f);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? timing.first * macro.attackScale : timing.second * macro.releaseScale, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * gain;
                processed = saturationBlend(processed, saturateSample(processed, saturation), satMix);
                processed *= makeup;
                block.setSample(static_cast<size_t> (channel), sample, dry[static_cast<size_t> (channel)] + (mix * (processed - dry[static_cast<size_t> (channel)])));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    static std::pair<float, float> getTiming(const int choice) noexcept
    {
        switch (choice)
        {
            case 0:  return { 0.0002f, 0.25f };
            case 1:  return { 0.0004f, 0.45f };
            case 2:  return { 0.0008f, 0.70f };
            case 3:  return { 0.0015f, 1.00f };
            case 4:  return { 0.0030f, 1.60f };
            default: return { 0.0060f, 2.40f };
        }
    }

    std::atomic<float>* enabled {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* timeConstant {};
    std::atomic<float>* mode {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float envelopeDbMid {};
    float envelopeDbSide {};
};

class StageChainManager
{
public:
    explicit StageChainManager(juce::AudioProcessorValueTreeState& apvts)
        : fet76(apvts),
          opto2a(apvts),
          vca160(apvts),
          variMu(apvts),
          tube670(apvts)
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        fet76.prepare(spec);
        opto2a.prepare(spec);
        vca160.prepare(spec);
        variMu.prepare(spec);
        tube670.prepare(spec);
    }

    void reset()
    {
        fet76.reset();
        opto2a.reset();
        vca160.reset();
        variMu.reset();
        tube670.reset();
    }

    void process(juce::dsp::AudioBlock<float>& block,
                 const uint32_t packedOrder,
                 const int tweakMode,
                 const double effectiveSampleRate)
    {
        // The packed order lets the UI reorder stages without touching the audio thread with locks.
        const auto macro = getMacroTuning(tweakMode);
        const auto order = unpackChainOrder(packedOrder);

        for (const auto stage : order)
        {
            switch (stage)
            {
                case StageType::fet76:   fet76.process(block, macro, effectiveSampleRate); break;
                case StageType::opto2a:  opto2a.process(block, macro, effectiveSampleRate); break;
                case StageType::vca160:  vca160.process(block, macro, effectiveSampleRate); break;
                case StageType::variMu:  variMu.process(block, macro, effectiveSampleRate); break;
                case StageType::tube670: tube670.process(block, macro, effectiveSampleRate); break;
            }
        }
    }

    std::array<float, numStages> getMeters() const noexcept
    {
        return {
            fet76.getGainReductionDb(),
            opto2a.getGainReductionDb(),
            vca160.getGainReductionDb(),
            variMu.getGainReductionDb(),
            tube670.getGainReductionDb()
        };
    }

private:
    Fet76Stage fet76;
    Opto2AStage opto2a;
    Vca160Stage vca160;
    VariMuStage variMu;
    Tube670Stage tube670;
};
} // namespace pentagon
