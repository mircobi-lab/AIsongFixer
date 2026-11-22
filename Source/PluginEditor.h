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

// forma d'onda del brano: clic o trascinamento per cambiare punto di ascolto
class WaveView : public juce::Component
{
public:
    explicit WaveView (asf::FilePlayer& p) : player (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { seek (e); }
    void mouseDrag (const juce::MouseEvent& e) override { seek (e); }
private:
    void seek (const juce::MouseEvent&);
    asf::FilePlayer& player;
};

class FixerEditor : public juce::AudioProcessorEditor, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    // playerEnabled: true solo nello Standalone, dove si può caricare ed esportare un file
    FixerEditor (FixerProcessor&, bool playerEnabled);
    ~FixerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void timerCallback() override;
    void sectionTitle (juce::Graphics&, juce::Rectangle<int> area, const juce::String& title);
    void loadFile (const juce::File&);
    void rebuildPresetMenu();
    void syncPresetSelection();
    void savePreset();
    void deletePreset();

    // id del menu: 1..N preset di fabbrica, userIdBase+i preset del banco Personal
    static constexpr int userIdBase = 1000;
    void chooseFile();
    void exportFile();

    FixerProcessor& proc;
    bool playerEnabled;
    mbstudio::ui::HardwareLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    SpectrumView spectrum;
    juce::ComboBox presets;
    juce::TextButton abBtn { "A/B" }, fairBtn { "FAIR" }, deltaBtn { "DELTA" }, limitBtn { "LIMIT" };
    juce::TextButton saveBtn { "SAVE" }, delBtn { "DEL" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> abAtt, fairAtt, deltaAtt, limitAtt;

    Knob mud, harsh, sens, fizz, fizzFreq, deess, vocal, air, warmth, bassMono, lowCut, input, output, mix, ceiling;
    mbstudio::ui::LedMeter inMeter { 16 }, outMeter { 16 }, grMeter { 16, mbstudio::ui::colours::ledAmber };

    // lettore (solo Standalone)
    WaveView wave;
    juce::TextButton loadBtn { "LOAD" }, playBtn { "PLAY" }, loopBtn { "LOOP" }, exportBtn { "EXPORT WAV" };
    juce::Label status;
    asf::Exporter exporter;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> headerArea, cleanArea, toneArea, stereoArea, outArea, meterArea, lufsArea, footerArea, transportArea;
    float shownLufsIn = -70, shownLufsOut = -70, shownFair = 0, shownGr = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixerEditor)
};
