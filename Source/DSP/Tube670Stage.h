#pragma once

#include "StageCommon.h"

namespace pentagon
{
class Tube670Stage final : public StageBase
{
public:
    explicit Tube670Stage(juce::AudioProcessorValueTreeState& apvts)
        : enabled(apvts.getRawParameterValue(IDs::tubeEnabled)),
          stageMix(apvts.getRawParameterValue(IDs::tubeMix)),
          sidechainHpf(apvts.getRawParameterValue(IDs::tubeSidechainHpf)),
          thresholdDb(apvts.getRawParameterValue(IDs::tubeThresholdDb)),
          timeConstant(apvts.getRawParameterValue(IDs::tubeTimeConstant)),
          mode(apvts.getRawParameterValue(IDs::tubeMode)),
          makeupGainDb(apvts.getRawParameterValue(IDs::tubeMakeupDb)),
          saturationMode(apvts.getRawParameterValue(IDs::tubeSaturation)),
          saturationMix(apvts.getRawParameterValue(IDs::tubeSaturationMix)),
          saturationPlacement(apvts.getRawParameterValue(IDs::tubeSaturationPlacement))
    {
    }

    void prepare(const StagePrepareSpec& spec)
    {
        StageBase::prepare(spec);
        thresholdDbSmoother.reset(sampleRate, 0.03);
        makeupDbSmoother.reset(sampleRate, 0.03);
        stageMixSmoother.reset(sampleRate, 0.03);
        sidechainHpfSmoother.reset(sampleRate, 0.03);
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
        stageMixSmoother.setTargetValue(stageMix->load() * 0.01f);
        sidechainHpfSmoother.setTargetValue(sidechainHpf->load());
        saturationMixSmoother.setTargetValue(saturationMix->load() * 0.01f);

        const auto modeValue = readChoiceValue(mode);
        const auto timing = getTiming(readChoiceValue(timeConstant));
        const auto saturation = juce::jlimit(0.0f, 0.9f, saturationAmountFromMode(readChoiceValue(saturationMode)) + macro.saturationBoost + 0.06f);
        const auto satPlacement = static_cast<SaturationPlacement> (readChoiceValue(saturationPlacement));
        float blockPeakReductionDb = 0.0f;
        constexpr auto invSqrt2 = 0.70710678118f;

        for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
        {
            const auto threshold = thresholdDbSmoother.getNextValue();
            const auto makeup = dbToGain(makeupDbSmoother.getNextValue());
            const auto stageWet = stageMixSmoother.getNextValue();
            const auto scHpf = sidechainHpfSmoother.getNextValue();
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

                auto processedMid = applySaturationPlacement(mid, satPlacement, true, saturation, satMix, saturateTubeSample);
                auto processedSide = applySaturationPlacement(side, satPlacement, true, saturation, satMix, saturateTubeSample);

                processedMid *= applyGainCell(mid, envelopeDbMid);
                processedSide *= applyGainCell(side, envelopeDbSide);

                processedMid = applySaturationPlacement(processedMid, satPlacement, false, saturation, satMix, saturateTubeSample) * makeup;
                processedSide = applySaturationPlacement(processedSide, satPlacement, false, saturation, satMix, saturateTubeSample) * makeup;

                const auto leftWet = (processedMid + processedSide) * invSqrt2;
                const auto rightWet = (processedMid - processedSide) * invSqrt2;

                block.setSample(0, sample, applyStageMix(leftDry, leftWet, stageWet, mix));
                block.setSample(1, sample, applyStageMix(rightDry, rightWet, stageWet, mix));

                blockPeakReductionDb = juce::jmax(blockPeakReductionDb, juce::jmax(envelopeDbMid, envelopeDbSide));
                continue;
            }

            float linkedPeak = 0.0f;
            std::array<float, 2> dry {};

            for (int channel = 0; channel < numChannels; ++channel)
            {
                dry[static_cast<size_t> (channel)] = block.getSample(channel, sample);
                linkedPeak = juce::jmax(linkedPeak, std::abs(dry[static_cast<size_t> (channel)]));
            }

            const auto detectorDb = gainToDb(std::abs(processSidechainHpf(linkedPeak, scHpf, effectiveSampleRate)));
            const auto targetReductionDb = computeReductionDb(detectorDb, threshold, 6.0f, 6.0f);
            const auto coeff = envelopeCoeff(targetReductionDb > envelopeDb ? timing.first * macro.attackScale : timing.second * macro.releaseScale, effectiveSampleRate);
            envelopeDb = (coeff * envelopeDb) + ((1.0f - coeff) * targetReductionDb);
            blockPeakReductionDb = juce::jmax(blockPeakReductionDb, envelopeDb);

            const auto gain = dbToGain(-envelopeDb);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                auto processed = dry[static_cast<size_t> (channel)];
                processed = applySaturationPlacement(processed, satPlacement, true, saturation, satMix, saturateTubeSample);
                processed *= gain;
                processed = applySaturationPlacement(processed, satPlacement, false, saturation, satMix, saturateTubeSample);
                processed *= makeup;
                block.setSample(channel, sample, applyStageMix(dry[static_cast<size_t> (channel)], processed, stageWet, mix));
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
    std::atomic<float>* stageMix {};
    std::atomic<float>* sidechainHpf {};
    std::atomic<float>* thresholdDb {};
    std::atomic<float>* timeConstant {};
    std::atomic<float>* mode {};
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
    float envelopeDbMid {};
    float envelopeDbSide {};
};

} // namespace pentagon
