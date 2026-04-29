#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <juce_dsp/juce_dsp.h>

#include "../ParameterIds.h"

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

inline float saturateFetSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 7.0f);
    const auto asym = sample + (0.12f * amount);
    const auto clipped = std::tanh(asym * drive);
    return clipped / juce::jmax(0.3f, std::tanh(drive));
}

inline float saturateOptoSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 3.0f);
    return (sample * drive) / (1.0f + (std::abs(sample) * drive));
}

inline float saturateVcaSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 4.5f);
    const auto cubic = (sample * drive) - (0.18f * amount * std::pow(sample * drive, 3.0f));
    return juce::jlimit(-1.25f, 1.25f, cubic);
}

inline float saturateVariMuSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 4.0f);
    const auto biased = sample + (0.08f * amount);
    return std::tanh(biased * drive) * (1.0f - (0.08f * amount));
}

inline float saturateTubeSample(const float sample, const float amount) noexcept
{
    if (amount <= 0.0f)
        return sample;

    const auto drive = 1.0f + (amount * 5.8f);
    const auto biased = sample + (0.16f * amount);
    const auto soft = std::tanh(biased * drive);
    const auto folded = soft - (0.05f * amount * std::sin(soft * juce::MathConstants<float>::pi));
    return folded / juce::jmax(0.32f, std::tanh(drive));
}

template <typename Saturator>
inline float applySaturationPlacement(float sample,
                                      const SaturationPlacement placement,
                                      const bool preSlot,
                                      const float amount,
                                      const float mix,
                                      Saturator&& saturator) noexcept
{
    if (placement == SaturationPlacement::both || (preSlot && placement == SaturationPlacement::pre) || (! preSlot && placement == SaturationPlacement::post))
        return saturationBlend(sample, saturator(sample, amount), mix);

    return sample;
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
        sidechainState = 0.0f;
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
    float sidechainState {};

    float processSidechainHpf(const float detector, const float cutoffHz, const double effectiveSampleRate) noexcept
    {
        if (cutoffHz <= 20.01f)
            return detector;

        const auto coeff = std::exp(-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float> (effectiveSampleRate));
        sidechainState = (coeff * sidechainState) + ((1.0f - coeff) * detector);
        return detector - sidechainState;
    }

    static float applyStageMix(const float dry, const float processed, const float stageMix, const float enabled) noexcept
    {
        const auto wet = dry + (juce::jlimit(0.0f, 1.0f, stageMix) * (processed - dry));
        return dry + (enabled * (wet - dry));
    }

    void meterReset() noexcept
    {
        meterGainReductionDb = 0.0f;
    }

    void updateMeter(const float blockPeakReductionDb) noexcept
    {
        meterGainReductionDb = juce::jmax(blockPeakReductionDb, meterGainReductionDb * 0.88f);
    }
};

} // namespace pentagon
