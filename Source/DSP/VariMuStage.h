#pragma once

#include "StageCommon.h"

namespace pentagon
{
class VariMuStage final : public StageBase
{
public:
    explicit VariMuStage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::variEnabled)),
          stageMix(apvts.getRawParameterValue(IDs::variMix)),
          sidechainHpf(apvts.getRawParameterValue(IDs::variSidechainHpf)),
          thresholdDb(apvts.getRawParameterValue(IDs::variThresholdDb)),
          attackMs(apvts.getRawParameterValue(IDs::variAttackMs)),
          recovery(apvts.getRawParameterValue(IDs::variRecovery)),
          makeupGainDb(apvts.getRawParameterValue(IDs::variMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::variSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::variSaturationMix)),
          saturationPlacement(apvts.getRawParameterValue(IDs::variSaturationPlacement))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.03);
        attackMsSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        stageMixSmoother.reset(sampleRate, 0.03);
        sidechainHpfSmoother.reset(sampleRate, 0.03);
        saturationMixSmoother.reset(sampleRate, 0.03);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        rmsState = 0.0f;
        detectorDriveState = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        thresholdDbSmoother.setTargetValue(thresholdDb->load() + macro.thresholdOffsetDb);
        attackMsSmoother.setTargetValue(attackMs->load() * macro.attackScale);
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        stageMixSmoother.setTargetValue(stageMix->load() * 0.01f);
        sidechainHpfSmoother.setTargetValue(sidechainHpf->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto saturation = juce::jlimit(0.0f, 0.85f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost + 0.03f);
        const auto satPlacement = static_cast<SaturationPlacement> (readChoiceValue(saturationPlacement));
        float blockPeakReductionDb = 0.0f;

        for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto attackSeconds = juce::jlimit(0.0001f, 0.25f, attackMsSmoother.getNextValue() * 0.001f);
            const auto releaseSeconds = getReleaseSeconds(readChoiceValue(recovery), envelopeDb) * macro.releaseScale;
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto stageWet = stageMixSmoother.getNextValue();
            const auto scHpf = sidechainHpfSmoother.getNextValue();
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            std::array<float, 2> dry {};
            float linkedSquare = 0.0f;

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(channel, sample);
                linkedSquare += dry[static_cast<size_t> (channel)] * dry[static_cast<size_t> (channel)];
            }

            linkedSquare /= static_cast<float> (numChannels);
            linkedSquare = std::pow(std::abs(processSidechainHpf(std::sqrt(linkedSquare), scHpf, effectiveSampleRate)), 2.0f);
            detectorDriveState = (0.92f * detectorDriveState) + (0.08f * linkedSquare);
            linkedSquare = (0.78f * linkedSquare) + (0.22f * std::pow(std::tanh(std::sqrt(detectorDriveState) * 1.8f), 2.0f));
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
                auto processed = dry[static_cast<size_t> (channel)];
                processed = applySaturationPlacement(processed, satPlacement, true, saturation, satMix, saturateVariMuSample);
                processed *= gain;
                processed = applySaturationPlacement(processed, satPlacement, false, saturation, satMix, saturateVariMuSample);
                processed *= makeup;
                block.setSample(channel, sample, applyStageMix(dry[static_cast<size_t> (channel)], processed, stageWet, mix));
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
    std::atomic<float>* stageMix {};
    std::atomic<float>* sidechainHpf {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* attackMs {};
    std::atomic<float>* recovery {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};
    std::atomic<float>* saturationPlacement {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> attackMsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stageMixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sidechainHpfSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float rmsState {};
    float detectorDriveState {};
};

} // namespace pentagon
