#include <cmath>
#include <iostream>

#include "../Source/PluginProcessor.h"

namespace
{
void expect(const bool condition, const char* message)
{
    if (! condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

float readParamPlainValue(juce::AudioProcessorValueTreeState& state, const juce::String& id)
{
    auto* parameter = state.getParameter(id);
    expect(parameter != nullptr, "parameter must exist");
    return parameter->convertFrom0to1(parameter->getValue());
}
} // namespace

int main()
{
    using namespace pentagon;

    const auto packedDefault = packChainOrder(defaultChainOrder);
    expect(isValidChainOrder(unpackChainOrder(packedDefault)), "default chain order must roundtrip");

    PentagonAudioProcessor processor;

    juce::AudioProcessor::BusesLayout monoLayout;
    monoLayout.inputBuses.add(juce::AudioChannelSet::mono());
    monoLayout.outputBuses.add(juce::AudioChannelSet::mono());

    juce::AudioProcessor::BusesLayout stereoLayout;
    stereoLayout.inputBuses.add(juce::AudioChannelSet::stereo());
    stereoLayout.outputBuses.add(juce::AudioChannelSet::stereo());

    expect(processor.isBusesLayoutSupported(monoLayout), "mono bus layout should be supported");
    expect(processor.isBusesLayoutSupported(stereoLayout), "stereo bus layout should be supported");

    processor.moveStageToIndex(StageType::tube670, 0);
    expect(processor.getChainOrder()[0] == StageType::tube670, "stage drag target should move into place");

    processor.applyFactoryPreset(2);
    processor.saveUserPresetSlot(0);
    expect(processor.hasUserPresetSlot(0), "user preset slot should be persisted");

    processor.captureComparisonSnapshot(true);
    expect(processor.hasComparisonSnapshot(true), "comparison snapshot A should exist");

    juce::MemoryBlock state;
    processor.getStateInformation(state);

    PentagonAudioProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int> (state.getSize()));

    expect(restored.getPackedChainOrder() == processor.getPackedChainOrder(), "chain order should survive state restore");
    expect(restored.hasUserPresetSlot(0), "user preset slot should survive state restore");
    expect(restored.getCurrentProgramIndex() == processor.getCurrentProgramIndex(), "preset index should survive state restore");

    restored.prepareToPlay(48000.0, 128);
    auto& stateTree = restored.getValueTreeState();
    stateTree.getParameter(IDs::oversampling)->setValueNotifyingHost(stateTree.getParameter(IDs::oversampling)->convertTo0to1(2.0f));

    juce::AudioBuffer<float> silence(2, 128);
    silence.clear();
    juce::MidiBuffer midi;
    restored.processBlock(silence, midi);
    expect(restored.getOversamplingStatusText().isNotEmpty(), "oversampling status text should be available");

    juce::AudioBuffer<float> audio(2, 128);
    audio.clear();
    audio.setSample(0, 0, 1.0f);
    audio.setSample(1, 0, 1.0f);
    restored.processBlock(audio, midi);

    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    {
        const auto* samples = audio.getReadPointer(channel);

        for (int sample = 0; sample < audio.getNumSamples(); ++sample)
            expect(std::isfinite(samples[sample]), "processed samples must remain finite");
    }

    expect(restored.getLatencySamples() >= 32, "reported latency should include finishing lookahead");
    expect(readParamPlainValue(stateTree, IDs::outputCeilingDb) <= 0.0f, "ceiling parameter should exist and be readable");

    std::cout << "Pentagon smoke tests passed\n";
    return 0;
}
