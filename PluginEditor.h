#pragma once

#include <JuceHeader.h>
#include <array>
#include "PluginProcessor.h"

class AutoVoxEditor : public juce::AudioProcessorEditor,
                      private juce::Timer
{
public:
    explicit AutoVoxEditor(AutoVoxProcessor&);
    ~AutoVoxEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectStyle(int index);
    void drawMeterBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name,
                      float normalised, const juce::String& valueText);

    AutoVoxProcessor& processor;

    juce::Label titleLabel;
    std::array<juce::TextButton, kNumStyles> styleButtons;
    juce::Label intensityLabel;
    juce::Slider intensitySlider;
    juce::ToggleButton bypassButton;
    juce::Label readout;

    juce::Rectangle<int> meterArea;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> intensityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoVoxEditor)
};
