#include "PluginEditor.h"

#include <limits>
#include <vector>

namespace
{
using APVTS = juce::AudioProcessorValueTreeState;
using SliderAttachment = APVTS::SliderAttachment;
using ButtonAttachment = APVTS::ButtonAttachment;
using ComboBoxAttachment = APVTS::ComboBoxAttachment;

struct RetroPalette
{
    static juce::Colour background() noexcept   { return juce::Colour::fromRGB(10, 13, 10); }
    static juce::Colour panel() noexcept        { return juce::Colour::fromRGB(16, 21, 16); }
    static juce::Colour border() noexcept       { return juce::Colour::fromRGB(199, 171, 84); }
    static juce::Colour text() noexcept         { return juce::Colour::fromRGB(85, 223, 118); }
    static juce::Colour meter() noexcept        { return juce::Colour::fromRGB(114, 241, 143); }
    static juce::Colour meterBg() noexcept      { return juce::Colour::fromRGB(22, 30, 22); }
};

juce::Font retroFont(const float height, const bool bold = false)
{
    return { juce::FontOptions("Courier New", height, bold ? juce::Font::bold : juce::Font::plain) };
}

void styleSlider(juce::Slider& slider)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 18);
    slider.setColour(juce::Slider::backgroundColourId, RetroPalette::meterBg());
    slider.setColour(juce::Slider::trackColourId, RetroPalette::meter());
    slider.setColour(juce::Slider::thumbColourId, RetroPalette::border());
    slider.setColour(juce::Slider::textBoxOutlineColourId, RetroPalette::border());
    slider.setColour(juce::Slider::textBoxTextColourId, RetroPalette::text());
    slider.setColour(juce::Slider::textBoxBackgroundColourId, RetroPalette::panel());
}

void styleCombo(juce::ComboBox& combo)
{
    combo.setColour(juce::ComboBox::backgroundColourId, RetroPalette::panel());
    combo.setColour(juce::ComboBox::outlineColourId, RetroPalette::border());
    combo.setColour(juce::ComboBox::textColourId, RetroPalette::text());
    combo.setColour(juce::ComboBox::arrowColourId, RetroPalette::border());
}

void styleButton(juce::Button& button)
{
    button.setColour(juce::TextButton::buttonColourId, RetroPalette::panel());
    button.setColour(juce::TextButton::buttonOnColourId, RetroPalette::border().darker(0.3f));
    button.setColour(juce::TextButton::textColourOffId, RetroPalette::text());
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
}

class MeterBar final : public juce::Component
{
public:
    explicit MeterBar(juce::String name) : title(std::move(name)) {}

    void setValue(const float normalised, const juce::String& valueText)
    {
        level = juce::jlimit(0.0f, 1.0f, normalised);
        text = valueText;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().reduced(2);
        g.setColour(RetroPalette::panel());
        g.fillRect(area);
        g.setColour(RetroPalette::border());
        g.drawRect(area, 1);

        auto meterArea = area.reduced(6, 8);
        meterArea.removeFromTop(14);
        meterArea.removeFromBottom(16);

        g.setColour(RetroPalette::meterBg());
        g.fillRect(meterArea);
        g.setColour(RetroPalette::border().withAlpha(0.12f));
        g.drawRect(meterArea, 1);

        const auto lineY = static_cast<float> (meterArea.getCentreY());
        const auto lineStartX = static_cast<float> (meterArea.getX() + 4);
        const auto lineEndX = static_cast<float> (meterArea.getRight() - 4);
        const auto activeX = juce::jmap(level, 0.0f, 1.0f, lineStartX, lineEndX);

        g.setColour(RetroPalette::border().withAlpha(0.2f));
        g.drawLine(lineStartX, lineY, lineEndX, lineY, 2.0f);

        g.setColour(RetroPalette::border().withAlpha(0.18f));

        for (int tick = 1; tick < 8; ++tick)
        {
            const auto x = juce::jmap(static_cast<float> (tick), 0.0f, 8.0f, lineStartX, lineEndX);
            g.drawVerticalLine(static_cast<int> (std::round(x)), meterArea.getY() + 4.0f, meterArea.getBottom() - 4.0f);
        }

        g.setColour(RetroPalette::meter());
        g.drawLine(lineStartX, lineY, activeX, lineY, 3.0f);
        g.fillEllipse(activeX - 4.0f, lineY - 4.0f, 8.0f, 8.0f);

        g.setColour(RetroPalette::text());
        g.setFont(retroFont(13.0f, true));
        g.drawText(title, area.removeFromTop(16), juce::Justification::centredLeft);
        g.setFont(retroFont(12.0f));
        g.drawText(text, area.removeFromBottom(14), juce::Justification::centredRight);
    }

private:
    juce::String title;
    juce::String text { "-inf dB" };
    float level {};
};

class ControlRow : public juce::Component
{
public:
    void paint(juce::Graphics& g) override
    {
        g.setColour(RetroPalette::border().withAlpha(0.15f));
        g.drawLine(0.0f, static_cast<float> (getHeight() - 1), static_cast<float> (getWidth()), static_cast<float> (getHeight() - 1));
    }
};

class SliderRow final : public ControlRow
{
public:
    SliderRow(APVTS& state, const juce::String& labelText, const juce::String& parameterID)
        : attachment(state, parameterID, slider)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setFont(retroFont(13.0f, true));
        label.setColour(juce::Label::textColourId, RetroPalette::text());
        styleSlider(slider);

        addAndMakeVisible(label);
        addAndMakeVisible(slider);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 2);
        label.setBounds(area.removeFromLeft(96));
        slider.setBounds(area);
    }

private:
    juce::Label label;
    juce::Slider slider;
    SliderAttachment attachment;
};

class ToggleRow final : public ControlRow
{
public:
    ToggleRow(APVTS& state, const juce::String& labelText, const juce::String& parameterID)
        : attachment(state, parameterID, button)
    {
        button.setButtonText(labelText);
        button.setColour(juce::ToggleButton::textColourId, RetroPalette::text());
        button.setClickingTogglesState(true);
        button.setRadioGroupId(0);
        addAndMakeVisible(button);
    }

    void resized() override
    {
        button.setBounds(getLocalBounds().reduced(4, 2));
    }

private:
    juce::ToggleButton button;
    ButtonAttachment attachment;
};

class ChoiceRow final : public ControlRow
{
public:
    ChoiceRow(APVTS& state, const juce::String& labelText, const juce::String& parameterID)
    {
        label.setText(labelText, juce::dontSendNotification);
        label.setFont(retroFont(13.0f, true));
        label.setColour(juce::Label::textColourId, RetroPalette::text());
        styleCombo(combo);

        if (const auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterID)))
        {
            for (int index = 0; index < parameter->choices.size(); ++index)
                combo.addItem(parameter->choices[index], index + 1);
        }

        addAndMakeVisible(label);
        addAndMakeVisible(combo);
        attachment = std::make_unique<ComboBoxAttachment>(state, parameterID, combo);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 2);
        label.setBounds(area.removeFromLeft(96));
        combo.setBounds(area);
    }

private:
    juce::Label label;
    juce::ComboBox combo;
    std::unique_ptr<ComboBoxAttachment> attachment;
};
} // namespace

class PentagonAudioProcessorEditor::GlobalPanel final : public juce::Component
{
public:
    explicit GlobalPanel(PentagonAudioProcessor& processorToUse)
        : processor(processorToUse),
          dryWet(processor.getValueTreeState(), "DRY/WET", pentagon::IDs::dryWet),
          output(processor.getValueTreeState(), "OUTPUT", pentagon::IDs::outputGainDb),
          ceiling(processor.getValueTreeState(), "CEILING", pentagon::IDs::outputCeilingDb),
          autoGain(processor.getValueTreeState(), "AUTO GAIN", pentagon::IDs::autoGainEnabled),
          safety(processor.getValueTreeState(), "LIMIT", pentagon::IDs::safetyEnabled),
          oversampling(processor.getValueTreeState(), "OS", pentagon::IDs::oversampling),
          tweak(processor.getValueTreeState(), "TWEAK", pentagon::IDs::tweakMode),
          inputMeter("IN"),
          outputMeter("OUT"),
          matchMeter("MATCH"),
          limitMeter("LIMIT")
    {
        addAndMakeVisible(dryWet);
        addAndMakeVisible(output);
        addAndMakeVisible(ceiling);
        addAndMakeVisible(autoGain);
        addAndMakeVisible(safety);
        addAndMakeVisible(oversampling);
        addAndMakeVisible(tweak);
        addAndMakeVisible(inputMeter);
        addAndMakeVisible(outputMeter);
        addAndMakeVisible(matchMeter);
        addAndMakeVisible(limitMeter);
        addAndMakeVisible(presetLabel);
        addAndMakeVisible(presetBox);
        addAndMakeVisible(userSlotLabel);
        addAndMakeVisible(userSlotBox);
        addAndMakeVisible(loadSlotButton);
        addAndMakeVisible(saveSlotButton);
        addAndMakeVisible(storeAButton);
        addAndMakeVisible(recallAButton);
        addAndMakeVisible(storeBButton);
        addAndMakeVisible(recallBButton);
        addAndMakeVisible(osStatusLabel);

        presetLabel.setText("PRESET", juce::dontSendNotification);
        presetLabel.setFont(retroFont(13.0f, true));
        presetLabel.setColour(juce::Label::textColourId, RetroPalette::text());
        styleCombo(presetBox);

        userSlotLabel.setText("USER", juce::dontSendNotification);
        userSlotLabel.setFont(retroFont(13.0f, true));
        userSlotLabel.setColour(juce::Label::textColourId, RetroPalette::text());
        styleCombo(userSlotBox);

        osStatusLabel.setFont(retroFont(12.0f));
        osStatusLabel.setColour(juce::Label::textColourId, RetroPalette::border());
        osStatusLabel.setJustificationType(juce::Justification::centredLeft);

        for (auto* button : { &loadSlotButton, &saveSlotButton, &storeAButton, &recallAButton, &storeBButton, &recallBButton })
            styleButton(*button);

        loadSlotButton.setButtonText("LOAD SLOT");
        saveSlotButton.setButtonText("SAVE SLOT");
        storeAButton.setButtonText("STORE A");
        recallAButton.setButtonText("LOAD A");
        storeBButton.setButtonText("STORE B");
        recallBButton.setButtonText("LOAD B");

        const auto presetNames = processor.getFactoryPresetNames();

        for (int index = 0; index < presetNames.size(); ++index)
            presetBox.addItem(presetNames[index], index + 1);

        refreshUserSlots();

        presetBox.onChange = [this]
        {
            const auto selected = presetBox.getSelectedId() - 1;

            if (selected >= 0)
                processor.applyFactoryPreset(selected);
        };

        loadSlotButton.onClick = [this]
        {
            const auto slot = userSlotBox.getSelectedId() - 1;

            if (slot >= 0)
                processor.loadUserPresetSlot(slot);
        };

        saveSlotButton.onClick = [this]
        {
            const auto slot = userSlotBox.getSelectedId() - 1;

            if (slot >= 0)
            {
                processor.saveUserPresetSlot(slot);
                refreshUserSlots();
            }
        };

        storeAButton.onClick = [this] { processor.captureComparisonSnapshot(true); };
        recallAButton.onClick = [this] { processor.recallComparisonSnapshot(true); };
        storeBButton.onClick = [this] { processor.captureComparisonSnapshot(false); };
        recallBButton.onClick = [this] { processor.recallComparisonSnapshot(false); };

        syncFromProcessor();
    }

    void refreshUserSlots()
    {
        userSlotBox.clear(juce::dontSendNotification);
        const auto slotNames = processor.getUserPresetSlotNames();

        for (int index = 0; index < slotNames.size(); ++index)
            userSlotBox.addItem(slotNames[index], index + 1);

        userSlotBox.setSelectedId(1, juce::dontSendNotification);
    }

    void syncFromProcessor()
    {
        // Meter repainting is timer-driven so the editor never pulls data directly from the audio thread.
        const auto presetIndex = processor.getCurrentProgramIndex();
        presetBox.setSelectedId(presetIndex >= 0 ? presetIndex + 1 : 0, juce::dontSendNotification);

        const auto inputDb = processor.getInputMeterDb();
        const auto outputDb = processor.getOutputMeterDb();
        const auto matchDb = processor.getAutoGainCompensationDb();
        const auto limitDb = processor.getLimiterGainReductionDb();
        inputMeter.setValue(juce::jmap(inputDb, -60.0f, 0.0f, 0.0f, 1.0f), juce::String(inputDb, 1) + " dB");
        outputMeter.setValue(juce::jmap(outputDb, -60.0f, 0.0f, 0.0f, 1.0f), juce::String(outputDb, 1) + " dB");
        matchMeter.setValue(juce::jmap(matchDb, -12.0f, 12.0f, 0.0f, 1.0f), juce::String(matchDb, 1) + " dB");
        limitMeter.setValue(juce::jlimit(0.0f, 1.0f, limitDb / 12.0f), juce::String(limitDb, 1) + " dB");
        osStatusLabel.setText(processor.getOversamplingStatusText(), juce::dontSendNotification);
        recallAButton.setEnabled(processor.hasComparisonSnapshot(true));
        recallBButton.setEnabled(processor.hasComparisonSnapshot(false));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(RetroPalette::panel());
        g.setColour(RetroPalette::border());
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.2f);
        g.setColour(RetroPalette::text());
        g.setFont(retroFont(16.0f, true));
        g.drawText("[ GLOBAL ]", 12, 8, 160, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(10);
        area.removeFromTop(28);

        auto topRow = area.removeFromTop(34);
        presetLabel.setBounds(topRow.removeFromLeft(70));
        presetBox.setBounds(topRow.removeFromLeft(220));
        topRow.removeFromLeft(12);
        userSlotLabel.setBounds(topRow.removeFromLeft(54));
        userSlotBox.setBounds(topRow.removeFromLeft(190));
        topRow.removeFromLeft(8);
        loadSlotButton.setBounds(topRow.removeFromLeft(92));
        topRow.removeFromLeft(6);
        saveSlotButton.setBounds(topRow.removeFromLeft(92));
        topRow.removeFromLeft(8);
        inputMeter.setBounds(topRow.removeFromLeft(150));
        topRow.removeFromLeft(8);
        outputMeter.setBounds(topRow.removeFromLeft(150));

        area.removeFromTop(8);

        auto firstControlRow = area.removeFromTop(30);
        dryWet.setBounds(firstControlRow.removeFromLeft(getWidth() / 3));
        firstControlRow.removeFromLeft(8);
        output.setBounds(firstControlRow.removeFromLeft(getWidth() / 3 - 10));
        firstControlRow.removeFromLeft(8);
        ceiling.setBounds(firstControlRow);

        area.removeFromTop(4);

        auto secondControlRow = area.removeFromTop(30);
        autoGain.setBounds(secondControlRow.removeFromLeft(140));
        secondControlRow.removeFromLeft(8);
        safety.setBounds(secondControlRow.removeFromLeft(120));
        secondControlRow.removeFromLeft(8);
        oversampling.setBounds(secondControlRow.removeFromLeft(220));
        secondControlRow.removeFromLeft(8);
        tweak.setBounds(secondControlRow.removeFromLeft(220));

        area.removeFromTop(6);

        auto thirdControlRow = area.removeFromTop(34);
        storeAButton.setBounds(thirdControlRow.removeFromLeft(92));
        thirdControlRow.removeFromLeft(6);
        recallAButton.setBounds(thirdControlRow.removeFromLeft(92));
        thirdControlRow.removeFromLeft(12);
        storeBButton.setBounds(thirdControlRow.removeFromLeft(92));
        thirdControlRow.removeFromLeft(6);
        recallBButton.setBounds(thirdControlRow.removeFromLeft(92));
        thirdControlRow.removeFromLeft(12);
        matchMeter.setBounds(thirdControlRow.removeFromLeft(150));
        thirdControlRow.removeFromLeft(8);
        limitMeter.setBounds(thirdControlRow.removeFromLeft(150));

        area.removeFromTop(4);
        osStatusLabel.setBounds(area.removeFromTop(20));
    }

private:
    PentagonAudioProcessor& processor;
    SliderRow dryWet;
    SliderRow output;
    SliderRow ceiling;
    ToggleRow autoGain;
    ToggleRow safety;
    ChoiceRow oversampling;
    ChoiceRow tweak;
    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::Label userSlotLabel;
    juce::ComboBox userSlotBox;
    juce::TextButton loadSlotButton;
    juce::TextButton saveSlotButton;
    juce::TextButton storeAButton;
    juce::TextButton recallAButton;
    juce::TextButton storeBButton;
    juce::TextButton recallBButton;
    juce::Label osStatusLabel;
    MeterBar inputMeter;
    MeterBar outputMeter;
    MeterBar matchMeter;
    MeterBar limitMeter;
};

class PentagonAudioProcessorEditor::StagePanel final : public juce::Component
{
public:
    StagePanel(PentagonAudioProcessorEditor& ownerToUse, PentagonAudioProcessor& processorToUse, const pentagon::StageType typeToUse)
        : owner(ownerToUse),
          processor(processorToUse),
          stage(typeToUse),
          grMeter("GR")
    {
        styleButton(moveLeftButton);
        styleButton(moveRightButton);
        styleButton(collapseButton);
        styleButton(soloButton);
        styleButton(deltaButton);

        moveLeftButton.setButtonText("<");
        moveRightButton.setButtonText(">");
        collapseButton.setButtonText("^");
        soloButton.setButtonText("SOLO");
        deltaButton.setButtonText("DELTA");
        moveLeftButton.onClick = [this] { processor.moveStage(stage, -1); };
        moveRightButton.onClick = [this] { processor.moveStage(stage, 1); };
        collapseButton.onClick = [this]
        {
            owner.setStageCollapsed(stage, ! owner.isStageCollapsed(stage));
            collapseButton.setButtonText(owner.isStageCollapsed(stage) ? "v" : "^");
        };
        soloButton.onClick = [this]
        {
            const auto currentMode = processor.getStageAuditionMode(stage);
            processor.setStageAudition(stage, currentMode == pentagon::StageAuditionMode::solo ? pentagon::StageAuditionMode::off : pentagon::StageAuditionMode::solo);
        };
        deltaButton.onClick = [this]
        {
            const auto currentMode = processor.getStageAuditionMode(stage);
            processor.setStageAudition(stage, currentMode == pentagon::StageAuditionMode::delta ? pentagon::StageAuditionMode::off : pentagon::StageAuditionMode::delta);
        };

        addAndMakeVisible(moveLeftButton);
        addAndMakeVisible(moveRightButton);
        addAndMakeVisible(collapseButton);
        addAndMakeVisible(soloButton);
        addAndMakeVisible(deltaButton);
        addAndMakeVisible(grMeter);

        buildRows();
    }

    void syncFromProcessor()
    {
        const auto grDb = processor.getStageMeterDb(stage);
        grMeter.setValue(juce::jlimit(0.0f, 1.0f, grDb / 24.0f), juce::String(grDb, 1) + " dB");
        collapseButton.setButtonText(owner.isStageCollapsed(stage) ? "v" : "^");

        const auto auditionMode = processor.getStageAuditionMode(stage);
        soloButton.setColour(juce::TextButton::buttonColourId, auditionMode == pentagon::StageAuditionMode::solo ? RetroPalette::border().brighter(0.2f) : RetroPalette::panel());
        deltaButton.setColour(juce::TextButton::buttonColourId, auditionMode == pentagon::StageAuditionMode::delta ? RetroPalette::border().brighter(0.2f) : RetroPalette::panel());

        const auto collapsed = owner.isStageCollapsed(stage);

        for (auto& row : rows)
            row->setVisible(! collapsed);
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(RetroPalette::panel());
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
        g.setColour(RetroPalette::border());
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f, 1.2f);
        g.setColour(RetroPalette::text());
        g.setFont(retroFont(15.0f, true));
        g.drawText(juce::String("[ ") + pentagon::stageName(stage) + " ]", 10, 8, getWidth() - 90, 20, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto header = area.removeFromTop(26);
        moveRightButton.setBounds(header.removeFromRight(28));
        moveLeftButton.setBounds(header.removeFromRight(28));
        header.removeFromRight(6);
        collapseButton.setBounds(header.removeFromRight(28));
        header.removeFromRight(6);
        deltaButton.setBounds(header.removeFromRight(58));
        header.removeFromRight(6);
        soloButton.setBounds(header.removeFromRight(52));

        area.removeFromTop(4);
        auto meterArea = area.removeFromBottom(40);
        grMeter.setBounds(meterArea);

        if (owner.isStageCollapsed(stage))
            return;

        for (auto& row : rows)
        {
            row->setBounds(area.removeFromTop(24));
            area.removeFromTop(1);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        dragOffset = event.getPosition();
        owner.beginStageDrag(stage);
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        owner.updateStageDrag(stage, getBounds().getPosition() + event.getPosition() - dragOffset);
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        owner.finishStageDrag(stage, localPointToGlobal(event.getPosition()));
    }

private:
    void addSlider(const juce::String& label, const juce::String& parameterID)
    {
        auto row = std::make_unique<SliderRow>(processor.getValueTreeState(), label, parameterID);
        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    void addToggle(const juce::String& label, const juce::String& parameterID)
    {
        auto row = std::make_unique<ToggleRow>(processor.getValueTreeState(), label, parameterID);
        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    void addChoice(const juce::String& label, const juce::String& parameterID)
    {
        auto row = std::make_unique<ChoiceRow>(processor.getValueTreeState(), label, parameterID);
        addAndMakeVisible(*row);
        rows.push_back(std::move(row));
    }

    void buildRows()
    {
        using namespace pentagon;

        // Card contents are stage-specific, but all controls are built from the same small row widgets.
        switch (stage)
        {
            case StageType::fet76:
                addToggle("ON", IDs::fetEnabled);
                addSlider("INPUT", IDs::fetInput);
                addSlider("OUTPUT", IDs::fetOutput);
                addSlider("ATTACK", IDs::fetAttackUs);
                addSlider("RELEASE", IDs::fetReleaseMs);
                addSlider("THRESH", IDs::fetThresholdDb);
                addChoice("RATIO", IDs::fetRatio);
                addChoice("SAT", IDs::fetSaturation);
                addSlider("SAT MIX", IDs::fetSaturationMix);
                break;

            case StageType::opto2a:
                addToggle("ON", IDs::optoEnabled);
                addSlider("PEAK RED", IDs::optoPeakReduction);
                addChoice("MODE", IDs::optoMode);
                addSlider("MAKEUP", IDs::optoMakeupDb);
                addSlider("HF EMP", IDs::optoHfEmphasis);
                addChoice("SAT", IDs::optoSaturation);
                addSlider("SAT MIX", IDs::optoSaturationMix);
                break;

            case StageType::vca160:
                addToggle("ON", IDs::vcaEnabled);
                addSlider("THRESH", IDs::vcaThresholdDb);
                addToggle("OVEREASY", IDs::vcaOvereasy);
                addSlider("MAKEUP", IDs::vcaMakeupDb);
                addChoice("SAT", IDs::vcaSaturation);
                addSlider("SAT MIX", IDs::vcaSaturationMix);
                break;

            case StageType::variMu:
                addToggle("ON", IDs::variEnabled);
                addSlider("THRESH", IDs::variThresholdDb);
                addSlider("ATTACK", IDs::variAttackMs);
                addChoice("RECOVER", IDs::variRecovery);
                addSlider("MAKEUP", IDs::variMakeupDb);
                addChoice("SAT", IDs::variSaturation);
                addSlider("SAT MIX", IDs::variSaturationMix);
                break;

            case StageType::tube670:
                addToggle("ON", IDs::tubeEnabled);
                addSlider("THRESH", IDs::tubeThresholdDb);
                addChoice("TIME", IDs::tubeTimeConstant);
                addChoice("MODE", IDs::tubeMode);
                addSlider("MAKEUP", IDs::tubeMakeupDb);
                addChoice("SAT", IDs::tubeSaturation);
                addSlider("SAT MIX", IDs::tubeSaturationMix);
                break;
        }
    }

    PentagonAudioProcessorEditor& owner;
    PentagonAudioProcessor& processor;
    pentagon::StageType stage;
    juce::TextButton moveLeftButton;
    juce::TextButton moveRightButton;
    juce::TextButton collapseButton;
    juce::TextButton soloButton;
    juce::TextButton deltaButton;
    MeterBar grMeter;
    std::vector<std::unique_ptr<juce::Component>> rows;
    juce::Point<int> dragOffset;
};

PentagonAudioProcessorEditor::PentagonAudioProcessorEditor(PentagonAudioProcessor& processorToUse)
    : AudioProcessorEditor(&processorToUse),
      pluginProcessor(processorToUse),
      globalPanel(std::make_unique<GlobalPanel>(processorToUse)),
      lastOrderPacked(pluginProcessor.getPackedChainOrder())
{
    setOpaque(true);
    addAndMakeVisible(*globalPanel);

    const std::array<pentagon::StageType, pentagon::numStages> types {
        pentagon::StageType::fet76,
        pentagon::StageType::opto2a,
        pentagon::StageType::vca160,
        pentagon::StageType::variMu,
        pentagon::StageType::tube670
    };

    for (int index = 0; index < pentagon::numStages; ++index)
    {
        stagePanels[static_cast<size_t> (index)] = std::make_unique<StagePanel>(*this, pluginProcessor, types[static_cast<size_t> (index)]);
        addAndMakeVisible(*stagePanels[static_cast<size_t> (index)]);
    }

    setResizable(true, true);
    setResizeLimits(980, 820, 1760, 1280);
    setSize(1360, 980);

    startTimerHz(30);
}

PentagonAudioProcessorEditor::~PentagonAudioProcessorEditor() = default;

void PentagonAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(RetroPalette::background());

    g.setColour(RetroPalette::text());
    g.setFont(retroFont(24.0f, true));
    g.drawText("Pentagon", 20, 10, 220, 28, juce::Justification::centredLeft);

    g.setFont(retroFont(13.0f));
    g.drawText("five-stage serial compressor | reorderable chain | VST3", 20, 38, getWidth() - 40, 18, juce::Justification::centredLeft);
}

void PentagonAudioProcessorEditor::resized()
{
    if (globalPanel == nullptr)
        return;

    for (const auto& panel : stagePanels)
    {
        if (panel == nullptr)
            return;
    }

    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(58);

    globalPanel->setBounds(area.removeFromTop(200));
    area.removeFromTop(14);

    const auto orderedStages = pluginProcessor.getChainOrder();
    constexpr int columns = 3;
    const auto rowHeight = (area.getHeight() - 12) / 2;
    const auto columnWidth = (area.getWidth() - 16) / columns;

    for (int index = 0; index < pentagon::numStages; ++index)
    {
        const auto row = index / columns;
        const auto column = index % columns;
        auto bounds = juce::Rectangle<int>(
            area.getX() + (column * (columnWidth + 8)),
            area.getY() + (row * (rowHeight + 12)),
            columnWidth,
            rowHeight);

        stageSlotBounds[static_cast<size_t> (index)] = bounds;

        const auto stageType = orderedStages[static_cast<size_t> (index)];
        if (! isDraggingStage || draggingStage != stageType)
            stagePanels[static_cast<size_t> (pentagon::toIndex(stageType))]->setBounds(bounds);
    }
}

void PentagonAudioProcessorEditor::timerCallback()
{
    globalPanel->syncFromProcessor();

    for (auto& panel : stagePanels)
    {
        if (panel != nullptr)
            panel->syncFromProcessor();
    }

    const auto packedOrder = pluginProcessor.getPackedChainOrder();

    if (packedOrder != lastOrderPacked)
    {
        lastOrderPacked = packedOrder;
        resized();
        repaint();
    }
}

void PentagonAudioProcessorEditor::setStageCollapsed(const pentagon::StageType stage, const bool collapsed)
{
    collapsedStages[static_cast<size_t> (pentagon::toIndex(stage))] = collapsed;
    resized();
    repaint();
}

bool PentagonAudioProcessorEditor::isStageCollapsed(const pentagon::StageType stage) const noexcept
{
    return collapsedStages[static_cast<size_t> (pentagon::toIndex(stage))];
}

void PentagonAudioProcessorEditor::beginStageDrag(const pentagon::StageType stage)
{
    draggingStage = stage;
    isDraggingStage = true;
    if (auto* panel = stagePanels[static_cast<size_t> (pentagon::toIndex(stage))].get())
        panel->toFront(false);
}

void PentagonAudioProcessorEditor::updateStageDrag(const pentagon::StageType stage, const juce::Point<int> topLeft)
{
    if (! isDraggingStage || draggingStage != stage)
        return;

    if (auto* panel = stagePanels[static_cast<size_t> (pentagon::toIndex(stage))].get())
        panel->setTopLeftPosition(topLeft);
}

void PentagonAudioProcessorEditor::finishStageDrag(const pentagon::StageType stage, const juce::Point<int> dropPoint)
{
    if (! isDraggingStage || draggingStage != stage)
        return;

    isDraggingStage = false;
    const auto localDropPoint = getLocalPoint(nullptr, dropPoint);
    auto bestIndex = 0;
    auto bestDistance = std::numeric_limits<int>::max();

    for (int index = 0; index < pentagon::numStages; ++index)
    {
        const auto distance = stageSlotBounds[static_cast<size_t> (index)].getCentre().getDistanceFrom(localDropPoint);

        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestIndex = index;
        }
    }

    pluginProcessor.moveStageToIndex(stage, bestIndex);
    resized();
    repaint();
}
