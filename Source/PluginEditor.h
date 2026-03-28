#pragma once

#include <array>
#include <memory>

#include <juce_gui_extra/juce_gui_extra.h>

#include "PluginProcessor.h"

class PentagonAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit PentagonAudioProcessorEditor(PentagonAudioProcessor&);
    ~PentagonAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class GlobalPanel;
    class StagePanel;

    PentagonAudioProcessor& processor;
    std::unique_ptr<GlobalPanel> globalPanel;
    std::array<std::unique_ptr<StagePanel>, pentagon::numStages> stagePanels;
    uint32_t lastOrderPacked {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagonAudioProcessorEditor)
};
