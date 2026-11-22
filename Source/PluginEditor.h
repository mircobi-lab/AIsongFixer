#pragma once
#include "PluginProcessor.h"
#include "SpectrumView.h"
#include "MBStudioLookAndFeel.h"

// manopola con nome stampato sotto e valore in grigio
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& name, const juce::String& tip);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;
private:
    juce::String title;
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class FixerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit FixerEditor (FixerProcessor&);
    ~FixerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void sectionTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);

    FixerProcessor& proc;
    mbstudio::ui::HardwareLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    SpectrumView spectrum;
    juce::ComboBox presets;
    juce::TextButton abBtn { "A/B" }, fairBtn { "FAIR" }, deltaBtn { "DELTA" }, limitBtn { "LIMIT" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> abAtt, fairAtt, deltaAtt, limitAtt;

    Knob mud, harsh, sens, fizz, fizzFreq, deess, vocal, air, warmth, bassMono, lowCut, input, output, mix, ceiling;
    mbstudio::ui::LedMeter inMeter { 16 }, outMeter { 16 }, grMeter { 16, mbstudio::ui::colours::ledAmber };

    juce::Rectangle<int> headerArea, cleanArea, toneArea, stereoArea, outArea, meterArea, lufsArea, footerArea;
    float shownLufsIn = -70, shownLufsOut = -70, shownFair = 0, shownGr = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixerEditor)
};
