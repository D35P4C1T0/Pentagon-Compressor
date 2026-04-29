#pragma once

#include "StageCommon.h"

namespace pentagon
{
class Fet76Stage final : public StageBase
{
public:
    explicit Fet76Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::fetEnabled)),
          stageMix(apvts.getRawParameterValue(IDs::fetMix)),
          sidechainHpf(apvts.getRawParameterValue(IDs::fetSidechainHpf)),
          inputGain(apvts.getRawParameterValue(IDs::fetInput)),
          outputGain(apvts.getRawParameterValue(IDs::fetOutput)),
          attackUs(apvts.getRawParameterValue(IDs::fetAttackUs)),
          releaseMs(apvts.getRawParameterValue(IDs::fetReleaseMs)),
          thresholdDb(apvts.getRawParameterValue(IDs::fetThresholdDb)),
          ratio(apvts.getRawParameterValue(IDs::fetRatio)),
          saturationMode(apvts.getRawParameterValue(IDs::fetSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::fetSaturationMix)),
          saturationPlacement(apvts.getRawParameterValue(IDs::fetSaturationPlacement))
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
        stageMixSmoother.reset(sampleRate, 0.02);
        sidechainHpfSmoother.reset(sampleRate, 0.02);
        saturationMixSmoother.reset(sampleRate, 0.02);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        detectorState = 0.0f;
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
        stageMixSmoother.setTargetValue(stageMix->load() * 0.01f);
        sidechainHpfSmoother.setTargetValue(sidechainHpf->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto ratioValue = fetRatioFromChoice(readChoiceValue(ratio));
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        const auto satPlacement = static_cast<SaturationPlacement> (readChoiceValue(saturationPlacement));
        float blockPeakReductionDb = 0.0f;

        for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
        {
            const auto inGain = dbToGain(inputGainDbSmoother.getNextValue());
            const auto outGain = dbToGain(outputGainDbSmoother.getNextValue());
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto stageWet = stageMixSmoother.getNextValue();
            const auto scHpf = sidechainHpfSmoother.getNextValue();
            const auto attackSeconds = juce::jlimit(20.0e-6f, 5.0e-3f, attackUsSmoother.getNextValue() * 1.0e-6f);
            const auto releaseSeconds = juce::jlimit(0.01f, 2.0f, releaseMsSmoother.getNextValue() * 0.001f);
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linkedPeak = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(channel, sample);
                linkedPeak = juce::jmax(linkedPeak, std::abs(dry[static_cast<size_t> (channel)] * inGain));
            }

            const auto edge = std::abs(linkedPeak - detectorState);
            detectorState = (0.94f * detectorState) + (0.06f * linkedPeak);
            const auto detector = std::abs(processSidechainHpf(linkedPeak + (edge * 0.35f), scHpf, effectiveSampleRate));
            const auto detectorDb = gainToDb(detector);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, ratioValue, 2.0f);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? attackSeconds : releaseSeconds, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)] * inGain;
                processed = applySaturationPlacement(processed, satPlacement, true, saturation, satMix, saturateFetSample);
                processed *= gain;
                processed = applySaturationPlacement(processed, satPlacement, false, saturation, satMix, saturateFetSample);
                processed *= outGain;
                block.setSample(channel, sample, applyStageMix(dry[static_cast<size_t> (channel)], processed, stageWet, mix));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    std::atomic<float>* enabled {};
    std::atomic<float>* stageMix {};
    std::atomic<float>* sidechainHpf {};
    std::atomic<float>* inputGain {};
    std::atomic<float>* outputGain {};
    std::atomic<float>* attackUs {};
    std::atomic<float>* releaseMs {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* ratio {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};
    std::atomic<float>* saturationPlacement {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> inputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputGainDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> attackUsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> releaseMsSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stageMixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sidechainHpfSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float detectorState {};
};

} // namespace pentagon
