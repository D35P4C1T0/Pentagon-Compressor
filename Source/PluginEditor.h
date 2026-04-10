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
    void setStageCollapsed(pentagon::StageType stage, bool collapsed);
    bool isStageCollapsed(pentagon::StageType stage) const noexcept;
    void beginStageDrag(pentagon::StageType stage);
    void updateStageDrag(pentagon::StageType stage, juce::Point<int> topLeft);
    void finishStageDrag(pentagon::StageType stage, juce::Point<int> dropPoint);

    class GlobalPanel;
    class StagePanel;

    PentagonAudioProcessor& pluginProcessor;
    std::unique_ptr<GlobalPanel> globalPanel;
    std::array<std::unique_ptr<StagePanel>, pentagon::numStages> stagePanels;
    std::array<bool, pentagon::numStages> collapsedStages {};
    std::array<juce::Rectangle<int>, pentagon::numStages> stageSlotBounds;
    pentagon::StageType draggingStage { pentagon::StageType::fet76 };
    bool isDraggingStage {};
    uint32_t lastOrderPacked {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PentagonAudioProcessorEditor)
};
