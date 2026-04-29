#pragma once

#include "StageCommon.h"

namespace pentagon
{
class Opto2AStage final : public StageBase
{
public:
    explicit Opto2AStage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::optoEnabled)),
          stageMix(apvts.getRawParameterValue(IDs::optoMix)),
          sidechainHpf(apvts.getRawParameterValue(IDs::optoSidechainHpf)),
          peakReduction(apvts.getRawParameterValue(IDs::optoPeakReduction)),
          mode(apvts.getRawParameterValue(IDs::optoMode)),
          makeupGainDb(apvts.getRawParameterValue(IDs::optoMakeupDb)),
          hfEmphasis(apvts.getRawParameterValue(IDs::optoHfEmphasis)),
          saturationMode(apvts.getRawParameterValue(IDs::optoSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::optoSaturationMix)),
          saturationPlacement(apvts.getRawParameterValue(IDs::optoSaturationPlacement))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        peakReductionSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        hfEmphasisSmoother.reset(sampleRate, 0.03);
        stageMixSmoother.reset(sampleRate, 0.03);
        sidechainHpfSmoother.reset(sampleRate, 0.03);
        saturationMixSmoother.reset(sampleRate, 0.03);
    }

    void reset()
    {
        StageBase::reset();
        envelopeDb = 0.0f;
        highpassState = 0.0f;
        releaseMemory = 0.0f;
    }

    void process(juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
    {
        enabledMix.setTargetValue(enabled->load() > 0.5f ? 1.0f : 0.0f);
        peakReductionSmoother.setTargetValue(peakReduction->load());
        makeupDbSmoother.setTargetValue(makeupGainDb->load());
        hfEmphasisSmoother.setTargetValue(hfEmphasis->load());
        stageMixSmoother.setTargetValue(stageMix->load() * 0.01f);
        sidechainHpfSmoother.setTargetValue(sidechainHpf->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto ratio = readChoiceValue(mode) == 0 ? 3.0f : 10.0f;
        const auto saturation = juce::jlimit(0.0f, 0.75f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost);
        const auto satPlacement = static_cast<SaturationPlacement> (readChoiceValue(saturationPlacement));
        float blockPeakReductionDb = 0.0f;

        for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
        {
            const auto reductionControl = peakReductionSmoother.getNextValue();
            const auto threshold = juce::jmap(reductionControl, 0.0f, 100.0f, 0.0f, -45.0f) + macro.thresholdOffsetDb;
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto hf = hfEmphasisSmoother.getNextValue() * 0.01f;
            const auto stageWet = stageMixSmoother.getNextValue();
            const auto scHpf = sidechainHpfSmoother.getNextValue();
            const auto satMix = saturationMixSmoother.getNextValue();
            const auto mix = enabledMix.getNextValue();

            float linked = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(channel, sample);
                linked += std::abs(dry[static_cast<size_t> (channel)]);
            }

            linked /= static_cast<float> (numChannels);
            highpassState = (0.985f * highpassState) + (0.015f * linked);
            const auto hfComponent = linked - highpassState;
            const auto detector = juce::jmax(1.0e-6f, std::abs(processSidechainHpf(linked + (std::abs(hfComponent) * hf), scHpf, effectiveSampleRate)));
            const auto detectorDb = gainToDb(detector);
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, ratio, 6.0f);
            releaseMemory = juce::jmax(releaseMemory * 0.985f, envelopeDb);
            const auto releaseSeconds = juce::jmap(juce::jlimit(0.0f, 1.0f, releaseMemory / 18.0f), 0.0f, 1.0f, 0.15f, 1.15f) * macro.releaseScale;
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? 0.01f * macro.attackScale : releaseSeconds, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)];
                processed = applySaturationPlacement(processed, satPlacement, true, saturation, satMix, saturateOptoSample);
                processed *= gain;
                processed = applySaturationPlacement(processed, satPlacement, false, saturation, satMix, saturateOptoSample);
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
    std::atomic<float>* peakReduction {};
    std::atomic<float>* mode {};
    std::atomic<float>* makeupGainDb {};
    std::atomic<float>* hfEmphasis {};
    std::atomic<float>* saturationMode {};
    std::atomic<float>* saturationMix {};
    std::atomic<float>* saturationPlacement {};

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> peakReductionSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> makeupDbSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> hfEmphasisSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stageMixSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sidechainHpfSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> saturationMixSmoother;
    float envelopeDb {};
    float highpassState {};
    float releaseMemory {};
};

} // namespace pentagon
