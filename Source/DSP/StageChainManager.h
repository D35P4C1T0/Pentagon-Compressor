#pragma once

#include "Fet76Stage.h"
#include "Opto2AStage.h"
#include "Vca160Stage.h"
#include "VariMuStage.h"
#include "Tube670Stage.h"

namespace pentagon
{
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
        const auto maxOversampledBlockSize = spec.maximumBlockSize * 4;
        scratchBuffer.setSize(spec.numChannels, maxOversampledBlockSize, false, false, true);
        parallelInputBuffer.setSize(spec.numChannels, maxOversampledBlockSize, false, false, true);
        parallelSumBuffer.setSize(spec.numChannels, maxOversampledBlockSize, false, false, true);
        parallelWorkBuffer.setSize(spec.numChannels, maxOversampledBlockSize, false, false, true);
    }

    void reset()
    {
        fet76.reset();
        opto2a.reset();
        vca160.reset();
        variMu.reset();
        tube670.reset();
        scratchBuffer.clear();
        parallelInputBuffer.clear();
        parallelSumBuffer.clear();
        parallelWorkBuffer.clear();
    }

    void process(juce::dsp::AudioBlock<float>& block,
                 const uint32_t packedOrder,
                 const int tweakMode,
                 const int routingMode,
                 const double effectiveSampleRate,
                 const int auditionStageIndex = -1,
                 const StageAuditionMode auditionMode = StageAuditionMode::off)
    {
        // The packed order lets the UI reorder stages without touching the audio thread with locks.
        const auto macro = getMacroTuning(tweakMode);
        const auto order = unpackChainOrder(packedOrder);
        const auto auditionEnabled = auditionStageIndex >= 0 && auditionMode != StageAuditionMode::off;

        if (! auditionEnabled && static_cast<RoutingMode> (routingMode) == RoutingMode::parallel)
        {
            processParallel(block, order, macro, effectiveSampleRate);
            return;
        }

        if (! auditionEnabled && static_cast<RoutingMode> (routingMode) == RoutingMode::hybrid)
        {
            processHybrid(block, order, macro, effectiveSampleRate);
            return;
        }

        for (const auto stage : order)
        {
            if (auditionEnabled)
            {
                for (int channel = 0; channel < static_cast<int> (block.getNumChannels()); ++channel)
                    scratchBuffer.copyFrom(channel, 0, block.getChannelPointer(static_cast<size_t> (channel)), static_cast<int> (block.getNumSamples()));
            }

            processStage(stage, block, macro, effectiveSampleRate);

            if (! auditionEnabled || toIndex(stage) != auditionStageIndex)
                continue;

            if (auditionMode == StageAuditionMode::delta)
            {
                for (int channel = 0; channel < static_cast<int> (block.getNumChannels()); ++channel)
                {
                    auto* samples = block.getChannelPointer(static_cast<size_t> (channel));
                    const auto* before = scratchBuffer.getReadPointer(channel);

                    for (int sample = 0; sample < static_cast<int> (block.getNumSamples()); ++sample)
                        samples[sample] -= before[sample];
                }
            }

            break;
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
    void processStage(const StageType stage, juce::dsp::AudioBlock<float>& block, const MacroTuning& macro, const double effectiveSampleRate)
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

    void copyBlockToBuffer(juce::AudioBuffer<float>& destination, juce::dsp::AudioBlock<float>& source)
    {
        for (int channel = 0; channel < static_cast<int> (source.getNumChannels()); ++channel)
            destination.copyFrom(channel, 0, source.getChannelPointer(static_cast<size_t> (channel)), static_cast<int> (source.getNumSamples()));
    }

    void copyBufferToBlock(juce::dsp::AudioBlock<float>& destination, const juce::AudioBuffer<float>& source)
    {
        const auto numSamples = static_cast<int> (destination.getNumSamples());

        for (int channel = 0; channel < static_cast<int> (destination.getNumChannels()); ++channel)
            std::copy(source.getReadPointer(channel),
                      source.getReadPointer(channel) + numSamples,
                      destination.getChannelPointer(static_cast<size_t> (channel)));
    }

    void processParallel(juce::dsp::AudioBlock<float>& block,
                         const std::array<StageType, numStages>& order,
                         const MacroTuning& macro,
        const double effectiveSampleRate)
    {
        const auto numSamples = static_cast<int> (block.getNumSamples());
        copyBlockToBuffer(parallelInputBuffer, block);
        parallelSumBuffer.clear(0, numSamples);

        for (const auto stage : order)
        {
            for (int channel = 0; channel < parallelInputBuffer.getNumChannels(); ++channel)
                parallelWorkBuffer.copyFrom(channel, 0, parallelInputBuffer, channel, 0, numSamples);

            juce::dsp::AudioBlock<float> workBlock(parallelWorkBuffer);
            auto activeWorkBlock = workBlock.getSubBlock(0, static_cast<size_t> (numSamples));
            processStage(stage, activeWorkBlock, macro, effectiveSampleRate);

            for (int channel = 0; channel < parallelSumBuffer.getNumChannels(); ++channel)
                parallelSumBuffer.addFrom(channel, 0, parallelWorkBuffer, channel, 0, numSamples, 1.0f / static_cast<float> (numStages));
        }

        copyBufferToBlock(block, parallelSumBuffer);
    }

    void processHybrid(juce::dsp::AudioBlock<float>& block,
                       const std::array<StageType, numStages>& order,
                       const MacroTuning& macro,
        const double effectiveSampleRate)
    {
        const auto numSamples = static_cast<int> (block.getNumSamples());
        copyBlockToBuffer(parallelInputBuffer, block);
        parallelSumBuffer.clear(0, numSamples);

        for (int lane = 0; lane < 2; ++lane)
        {
            for (int channel = 0; channel < parallelInputBuffer.getNumChannels(); ++channel)
                parallelWorkBuffer.copyFrom(channel, 0, parallelInputBuffer, channel, 0, numSamples);

            juce::dsp::AudioBlock<float> workBlock(parallelWorkBuffer);
            auto activeWorkBlock = workBlock.getSubBlock(0, static_cast<size_t> (numSamples));
            processStage(order[static_cast<size_t> (lane)], activeWorkBlock, macro, effectiveSampleRate);

            for (int channel = 0; channel < parallelSumBuffer.getNumChannels(); ++channel)
                parallelSumBuffer.addFrom(channel, 0, parallelWorkBuffer, channel, 0, numSamples, 0.5f);
        }

        copyBufferToBlock(block, parallelSumBuffer);

        for (size_t index = 2; index < order.size(); ++index)
            processStage(order[index], block, macro, effectiveSampleRate);
    }

    Fet76Stage fet76;
    Opto2AStage opto2a;
    Vca160Stage vca160;
    VariMuStage variMu;
    Tube670Stage tube670;
    juce::AudioBuffer<float> scratchBuffer;
    juce::AudioBuffer<float> parallelInputBuffer;
    juce::AudioBuffer<float> parallelSumBuffer;
    juce::AudioBuffer<float> parallelWorkBuffer;
};
} // namespace pentagon
