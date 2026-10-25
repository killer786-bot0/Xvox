#include "PluginEditor.h"

namespace
{
const juce::Colour kBg { 0xff15161a };
const juce::Colour kAccent { 0xffff7a1a };
const juce::Colour kPanel { 0xff24262c };
const juce::Colour kText { 0xffe8e8ea };
} // namespace

AutoVoxEditor::AutoVoxEditor(AutoVoxProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(540, 400);
    setResizable(true, true);
    setResizeLimits(360, 270, 1200, 900);
    getConstrainer()->setFixedAspectRatio(1.35);

    titleLabel.setText("AutoVox", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(26.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(titleLabel);

    const juce::StringArray names { "Romantic", "Sad", "Hip Hop", "Trap", "Drill" };
    for (int i = 0; i < kNumStyles; ++i)
    {
        auto& b = styleButtons[(size_t) i];
        b.setButtonText(names[i]);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(1);
        b.setColour(juce::TextButton::buttonOnColourId, kAccent);
        b.setColour(juce::TextButton::textColourOffId, kText);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { selectStyle(i); };
        addAndMakeVisible(b);
    }

    intensityLabel.setText("Intensity", juce::dontSendNotification);
    intensityLabel.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(intensityLabel);

    intensitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 20);
    intensitySlider.setNumDecimalPlacesToDisplay(0);
    intensitySlider.setTextValueSuffix(" %");
    intensitySlider.setColour(juce::Slider::thumbColourId, kAccent);
    addAndMakeVisible(intensitySlider);

    bypassButton.setButtonText("Bypass");
    bypassButton.setColour(juce::ToggleButton::textColourId, kText);
    addAndMakeVisible(bypassButton);

    readout.setColour(juce::Label::textColourId, kText);
    readout.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(readout);

    intensityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, "intensity", intensitySlider);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, "bypass", bypassButton);

    startTimerHz(25);
}

AutoVoxEditor::~AutoVoxEditor()
{
    stopTimer();
}

void AutoVoxEditor::selectStyle(int index)
{
    if (auto* param = processor.apvts.getParameter("style"))
        param->setValueNotifyingHost(param->convertTo0to1((float) index));
}

void AutoVoxEditor::timerCallback()
{
    const int current = juce::jlimit(0, kNumStyles - 1,
                                     (int) std::round(processor.apvts.getRawParameterValue("style")->load()));
    for (int i = 0; i < kNumStyles; ++i)
        styleButtons[(size_t) i].setToggleState(i == current, juce::dontSendNotification);

    const float level = processor.analysis.levelDb.load();
    const float centroid = processor.analysis.centroidHz.load();
    const float sib = processor.analysis.sibilance.load();
    const float pitch = processor.analysis.pitchRangeSt.load();

    readout.setText(juce::String::formatted("level %.1f dB   brightness %.1f kHz   sibilance %d%%   pitch spread %.1f st",
                                            level, centroid / 1000.0f, juce::roundToInt(sib * 100.0f), pitch),
                    juce::dontSendNotification);

    repaint(meterArea);
}

void AutoVoxEditor::drawMeterBar(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name,
                                 float normalised, const juce::String& valueText)
{
    auto labelArea = area.removeFromLeft(110);
    g.setColour(kText);
    g.setFont(14.0f);
    g.drawText(name, labelArea, juce::Justification::centredLeft);

    auto bar = area.reduced(4, 6);
    g.setColour(kPanel);
    g.fillRoundedRectangle(bar.toFloat(), 4.0f);

    const float fill = juce::jlimit(0.0f, 1.0f, normalised);
    g.setColour(kAccent);
    g.fillRoundedRectangle(bar.toFloat().withWidth(bar.getWidth() * fill), 4.0f);

    g.setColour(kText.withAlpha(0.7f));
    g.setFont(13.0f);
    g.drawText(valueText, bar, juce::Justification::centredRight);
}

void AutoVoxEditor::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    g.setColour(kPanel);
    g.fillRoundedRectangle(meterArea.toFloat().expanded(4.0f), 8.0f);

    auto area = meterArea.reduced(10, 8);
    const float level = processor.analysis.levelDb.load();
    const float centroid = processor.analysis.centroidHz.load();
    const float sib = processor.analysis.sibilance.load();

    drawMeterBar(g, area.removeFromTop(30), "Loudness",
                 (level + 60.0f) / 60.0f, juce::String::formatted("%.1f dB", level));
    drawMeterBar(g, area.removeFromTop(30), "Brightness",
                 std::log2(juce::jlimit(200.0f, 12000.0f, centroid) / 200.0f) / std::log2(60.0f),
                 juce::String::formatted("%.0f Hz", centroid));
    drawMeterBar(g, area.removeFromTop(30), "Sibilance",
                 sib / 0.5f, juce::String::formatted("%d%%", juce::roundToInt(sib * 100.0f)));
}

void AutoVoxEditor::resized()
{
    auto area = getLocalBounds().reduced(16);

    titleLabel.setBounds(area.removeFromTop(36));

    auto styleRow = area.removeFromTop(44);
    const int bw = styleRow.getWidth() / kNumStyles;
    for (auto& b : styleButtons)
        b.setBounds(styleRow.removeFromLeft(bw).reduced(3));

    area.removeFromTop(10);

    auto intensityRow = area.removeFromTop(36);
    intensityLabel.setBounds(intensityRow.removeFromLeft(90));
    intensitySlider.setBounds(intensityRow);

    area.removeFromTop(8);
    bypassButton.setBounds(area.removeFromTop(28));

    area.removeFromTop(8);
    meterArea = area.removeFromTop(110);

    readout.setBounds(area.removeFromTop(24));
}
