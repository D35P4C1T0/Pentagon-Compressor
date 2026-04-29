#pragma once

#include <array>
#include <cstdint>

#include <juce_audio_processors/juce_audio_processors.h>

namespace pentagon
{
enum class StageType : uint8_t
{
    fet76 = 0,
    opto2a,
    vca160,
    variMu,
    tube670
};

constexpr int numStages = 5;
constexpr std::array<StageType, numStages> defaultChainOrder {
    StageType::fet76,
    StageType::opto2a,
    StageType::vca160,
    StageType::variMu,
    StageType::tube670
};

enum class OversamplingMode : int
{
    x1 = 0,
    x2,
    x4
};

enum class TweakMode : int
{
    clean = 0,
    balanced,
    aggressive,
    glue,
    vocal,
    loud
};

enum class StageAuditionMode : int
{
    off = 0,
    solo,
    delta
};

enum class RoutingMode : int
{
    serial = 0,
    parallel,
    hybrid
};

enum class SaturationPlacement : int
{
    post = 0,
    pre,
    both
};

struct MacroTuning
{
    float thresholdOffsetDb {};
    float attackScale { 1.0f };
    float releaseScale { 1.0f };
    float saturationBoost {};
};

inline MacroTuning getMacroTuning(const int tweakMode) noexcept
{
    switch (static_cast<TweakMode> (tweakMode))
    {
        case TweakMode::clean:      return {  2.0f, 1.15f, 1.10f, -0.06f };
        case TweakMode::balanced:   return {  0.0f, 1.00f, 1.00f,  0.00f };
        case TweakMode::aggressive: return { -3.0f, 0.75f, 0.85f,  0.10f };
        case TweakMode::glue:       return {  1.0f, 1.40f, 1.30f,  0.04f };
        case TweakMode::vocal:      return { -1.0f, 0.90f, 1.10f, -0.01f };
        case TweakMode::loud:       return { -2.0f, 0.82f, 0.72f,  0.12f };
    }

    return {};
}

inline constexpr const char* stageName(const StageType type) noexcept
{
    switch (type)
    {
        case StageType::fet76:   return "FET76";
        case StageType::opto2a:  return "OPTO2A";
        case StageType::vca160:  return "VCA160";
        case StageType::variMu:  return "VARIMU";
        case StageType::tube670: return "TUBE670";
    }

    return "UNKNOWN";
}

inline constexpr int toIndex(const StageType type) noexcept
{
    return static_cast<int> (type);
}

inline uint32_t packChainOrder(const std::array<StageType, numStages>& order) noexcept
{
    uint32_t packed = 0;

    for (size_t index = 0; index < defaultChainOrder.size(); ++index)
        packed |= (static_cast<uint32_t> (toIndex(order[index])) & 0x0fu) << (static_cast<uint32_t> (index) * 4u);

    return packed;
}

inline std::array<StageType, numStages> unpackChainOrder(const uint32_t packed) noexcept
{
    std::array<StageType, numStages> order {};

    for (size_t index = 0; index < defaultChainOrder.size(); ++index)
        order[index] = static_cast<StageType> ((packed >> (static_cast<uint32_t> (index) * 4u)) & 0x0fu);

    return order;
}

inline bool isValidChainOrder(const std::array<StageType, numStages>& order) noexcept
{
    std::array<bool, numStages> seen {};

    for (const auto stage : order)
    {
        const auto index = toIndex(stage);

        if (index < 0 || index >= numStages || seen[static_cast<size_t> (index)])
            return false;

        seen[static_cast<size_t> (index)] = true;
    }

    return true;
}

namespace IDs
{
    // This is the root APVTS node type used for host state serialization.
    inline constexpr auto stateType = "PentagonState";
    inline constexpr auto chainOrder = "chainOrderPacked";
    inline constexpr auto presetIndex = "presetIndex";

    inline constexpr auto dryWet = "global.dryWet";
    inline constexpr auto outputGainDb = "global.outputGainDb";
    inline constexpr auto outputCeilingDb = "global.outputCeilingDb";
    inline constexpr auto autoGainEnabled = "global.autoGainEnabled";
    inline constexpr auto safetyEnabled = "global.safetyEnabled";
    inline constexpr auto oversampling = "global.oversampling";
    inline constexpr auto tweakMode = "global.tweakMode";
    inline constexpr auto routingMode = "global.routingMode";

    inline constexpr auto fetEnabled = "fet.enabled";
    inline constexpr auto fetMix = "fet.mix";
    inline constexpr auto fetSidechainHpf = "fet.sidechainHpfHz";
    inline constexpr auto fetInput = "fet.inputGainDb";
    inline constexpr auto fetOutput = "fet.outputGainDb";
    inline constexpr auto fetAttackUs = "fet.attackUs";
    inline constexpr auto fetReleaseMs = "fet.releaseMs";
    inline constexpr auto fetThresholdDb = "fet.thresholdDb";
    inline constexpr auto fetRatio = "fet.ratio";
    inline constexpr auto fetSaturation = "fet.saturationMode";
    inline constexpr auto fetSaturationMix = "fet.saturationMix";
    inline constexpr auto fetSaturationPlacement = "fet.saturationPlacement";

    inline constexpr auto optoEnabled = "opto.enabled";
    inline constexpr auto optoMix = "opto.mix";
    inline constexpr auto optoSidechainHpf = "opto.sidechainHpfHz";
    inline constexpr auto optoPeakReduction = "opto.peakReduction";
    inline constexpr auto optoMode = "opto.mode";
    inline constexpr auto optoMakeupDb = "opto.makeupGainDb";
    inline constexpr auto optoHfEmphasis = "opto.hfEmphasis";
    inline constexpr auto optoSaturation = "opto.saturationMode";
    inline constexpr auto optoSaturationMix = "opto.saturationMix";
    inline constexpr auto optoSaturationPlacement = "opto.saturationPlacement";

    inline constexpr auto vcaEnabled = "vca.enabled";
    inline constexpr auto vcaMix = "vca.mix";
    inline constexpr auto vcaSidechainHpf = "vca.sidechainHpfHz";
    inline constexpr auto vcaThresholdDb = "vca.thresholdDb";
    inline constexpr auto vcaOvereasy = "vca.overeasy";
    inline constexpr auto vcaMakeupDb = "vca.makeupGainDb";
    inline constexpr auto vcaSaturation = "vca.saturationMode";
    inline constexpr auto vcaSaturationMix = "vca.saturationMix";
    inline constexpr auto vcaSaturationPlacement = "vca.saturationPlacement";

    inline constexpr auto variEnabled = "varimu.enabled";
    inline constexpr auto variMix = "varimu.mix";
    inline constexpr auto variSidechainHpf = "varimu.sidechainHpfHz";
    inline constexpr auto variThresholdDb = "varimu.thresholdDb";
    inline constexpr auto variAttackMs = "varimu.attackMs";
    inline constexpr auto variRecovery = "varimu.recovery";
    inline constexpr auto variMakeupDb = "varimu.makeupGainDb";
    inline constexpr auto variSaturation = "varimu.saturationMode";
    inline constexpr auto variSaturationMix = "varimu.saturationMix";
    inline constexpr auto variSaturationPlacement = "varimu.saturationPlacement";

    inline constexpr auto tubeEnabled = "tube670.enabled";
    inline constexpr auto tubeMix = "tube670.mix";
    inline constexpr auto tubeSidechainHpf = "tube670.sidechainHpfHz";
    inline constexpr auto tubeThresholdDb = "tube670.thresholdDb";
    inline constexpr auto tubeTimeConstant = "tube670.timeConstant";
    inline constexpr auto tubeMode = "tube670.mode";
    inline constexpr auto tubeMakeupDb = "tube670.makeupGainDb";
    inline constexpr auto tubeSaturation = "tube670.saturationMode";
    inline constexpr auto tubeSaturationMix = "tube670.saturationMix";
    inline constexpr auto tubeSaturationPlacement = "tube670.saturationPlacement";
}
} // namespace pentagon
