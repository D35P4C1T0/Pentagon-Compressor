#pragma once

#include "StageCommon.h"

namespace pentagon
{
class Vca160Stage final : public StageBase
{
public:
    explicit Vca160Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::vcaEnabled)),
          stageMix(apvts.getRawParameterValue(IDs::vcaMix)),
          sidechainHpf(apvts.getRawParameterValue(IDs::vcaSidechainHpf)),
          thresholdDb(apvts.getRawParameterValue(IDs::vcaThresholdDb)),
          overeasy(apvts.getRawParameterValue(IDs::vcaOvereasy)),
          makeupGainDb(apvts.getRawParameterValue(IDs::vcaMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::vcaSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::vcaSaturationMix)),
          saturationPlacement(apvts.getRawParameterValue(IDs::vcaSaturationPlacement))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.02);
        makeupDbSmoother.reset(sampleRate, 0.02);
        stageMixSmoother.reset(sampleRate, 0.02);
        sidechainHpfSmoother.reset(sampleRate, 0.02);
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
        stageMixSmoother.setTargetValue(stageMix->load() * 0.01f);
        sidechainHpfSmoother.setTargetValue(sidechainHpf->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto knee = overeasy->load() > 0.5f ? 8.0f : 1.0f;
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        const auto satPlacement = static_cast<SaturationPlacement> (readChoiceValue(saturationPlacement));
        float blockPeakReductionDb = 0.0f;

        for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto stageWet = stageMixSmoother.getNextValue();
            const auto scHpf = sidechainHpfSmoother.getNextValue();
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linkedPeak = 0.0f;
            float linkedRms = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(channel, sample);
                const auto sampleValue = dry[static_cast<size_t> (channel)];
                linkedPeak = juce::jmax(linkedPeak, std::abs(sampleValue));
                linkedRms += sampleValue * sampleValue;
            }

            linkedRms = std::sqrt(linkedRms / static_cast<float> (numChannels));
            const auto detector = std::abs(processSidechainHpf(juce::jmap(overeasy->load() > 0.5f ? 0.35f : 0.62f, linkedRms, linkedPeak), scHpf, effectiveSampleRate));

            const auto detectorDb = gainToDb(detector);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 4.0f, knee);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? 0.0012f * macro.attackScale : 0.10f * macro.releaseScale, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)];
                processed = applySaturationPlacement(processed, satPlacement, true, saturation, satMix, saturateVcaSample);
                processed *= gain;
                processed = applySaturationPlacement(processed, satPlacement, false, saturation, satMix, saturateVcaSample);
                processed *= makeup;
                block.setSample(channel, sample, applyStageMix(dry[static_cast<size_t> (channel)], processed, stageWet, mix));
            }
        }

        updateMeter(blockPeakReductionDb);
    }

private:
    std::atomic<float>* enabled {};
    std::atomic<float>* stageMix {};
    std::atomic<float>* sidechainHpf {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* overeasy {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};
    std::atomic<float>* saturationPlacement {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stageMixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sidechainHpfSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
};

} // namespace pentagon
