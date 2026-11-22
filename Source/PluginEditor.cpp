#include "PluginEditor.h"

namespace ui = mbstudio::ui;
namespace C = mbstudio::ui::colours;

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const char* id, const juce::String& name, const juce::String& tip)
    : title (name), param (s.getParameter (id))
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::thumbColourId, C::print);
    slider.setTooltip (tip);
    slider.setDoubleClickReturnValue (true, param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0);
    addAndMakeVisible (slider);
    att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    slider.onValueChange = [this] { repaint(); };
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (30);
    slider.setBounds (r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight())));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto value = r.removeFromBottom (14);
    auto name = r.removeFromBottom (16);
    ui::paintPrinted (g, name, title, 13.f, juce::Justification::centred);
    if (param != nullptr)
        ui::paintPrinted (g, value, param->getCurrentValueAsText(), 11.5f, juce::Justification::centred, C::printDim);
}

//==============================================================================
FixerEditor::FixerEditor (FixerProcessor& p)
    : AudioProcessorEditor (p), proc (p), spectrum (p.cleaner),
      mud      (p.apvts, "mud",      "MUD",       "Toglie il fango sui medio-bassi (120-600 Hz): risonanze e accumulo"),
      harsh    (p.apvts, "harsh",    "HARSH",     "Abbassa le risonanze aspre tra 1 e 12 kHz, solo dove sporgono"),
      sens     (p.apvts, "sens",     "SENS",      "Sensibilita' di MUD e HARSH: piu' alta = interviene su picchi piu' piccoli"),
      fizz     (p.apvts, "fizz",     "FIZZ",      "Riduce il velo metallico e il fruscio tipico dell'AI sopra FIZZ FREQ, e il side in alto"),
      fizzFreq (p.apvts, "fizzfreq", "FIZZ FREQ", "Da dove parte la pulizia del fizz"),
      deess    (p.apvts, "deess",    "DE-ESS",    "Smorza le sibilanti (4,5-10 kHz) quando dominano"),
      vocal    (p.apvts, "vocal",    "VOCAL",     "Presenza della voce al centro (2,6 kHz sul mid)"),
      air      (p.apvts, "air",      "AIR",       "Un po' d'aria sopra 10 kHz, dopo la pulizia"),
      warmth   (p.apvts, "warmth",   "WARMTH",    "Saturazione morbida tipo nastro"),
      bassMono (p.apvts, "bassmono", "BASS MONO", "Bassi in mono sotto la frequenza scelta (0 = spento)"),
      lowCut   (p.apvts, "lowcut",   "LOW CUT",   "Passa-alto 12 dB/ott contro rimbombi e subsonici"),
      input    (p.apvts, "input",    "INPUT",     "Guadagno in ingresso"),
      output   (p.apvts, "output",   "OUTPUT",    "Guadagno in uscita, prima del limiter"),
      mix      (p.apvts, "mix",      "MIX",       "Miscela tra originale e pulito"),
      ceiling  (p.apvts, "ceiling",  "CEILING",   "Tetto del limiter (picco campione)")
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (spectrum);
    for (auto* k : { &mud, &harsh, &sens, &fizz, &fizzFreq, &deess, &vocal, &air, &warmth, &bassMono, &lowCut, &input, &output, &mix, &ceiling })
        addAndMakeVisible (k);
    for (auto* m : { &inMeter, &outMeter, &grMeter })
        addAndMakeVisible (m);

    const auto& list = asf::factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        presets.addItem (list[(size_t) i].name, i + 1);
    presets.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presets.setTooltip ("Preset di partenza");
    presets.onChange = [this] { if (presets.getSelectedId() > 0) proc.setCurrentProgram (presets.getSelectedId() - 1); };
    addAndMakeVisible (presets);

    auto setupToggle = [this] (juce::TextButton& b, const char* id, const char* led, const juce::String& tip,
                               std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>& a)
    {
        b.setClickingTogglesState (true);
        b.getProperties().set ("ledColour", led);
        b.setTooltip (tip);
        addAndMakeVisible (b);
        a = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, id, b);
    };
    setupToggle (abBtn, "bypass", "ff3d8bff", "A/B - acceso: senti l'originale (A). Spento: senti il pulito (B)", abAtt);
    setupToggle (fairBtn, "fair", "ff3ee05c", "Confronto onesto: l'originale viene portato alla stessa loudness del pulito", fairAtt);
    setupToggle (deltaBtn, "delta", "ffffb020", "Ascolta solo quello che viene tolto", deltaAtt);
    setupToggle (limitBtn, "limit", "ff3ee05c", "Limiter di uscita", limitAtt);

    setSize (1000, 660);
    startTimerHz (20);
}

FixerEditor::~FixerEditor() { setLookAndFeel (nullptr); }

void FixerEditor::timerCallback()
{
    inMeter.setLevel (juce::jmap (juce::Decibels::gainToDecibels (proc.inPeak.load(), -48.0f), -48.0f, 0.0f, 0.0f, 1.0f));
    outMeter.setLevel (juce::jmap (juce::Decibels::gainToDecibels (proc.outPeak.load(), -48.0f), -48.0f, 0.0f, 0.0f, 1.0f));
    grMeter.setLevel (juce::jlimit (0.0f, 1.0f, -proc.limiterGrDb.load() / 12.0f));

    shownLufsIn = proc.lufsIn.load(); shownLufsOut = proc.lufsOut.load();
    shownFair = proc.fairGainDb.load(); shownGr = proc.limiterGrDb.load();
    if (presets.getSelectedId() != proc.getCurrentProgram() + 1)
        presets.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    repaint (lufsArea);
}

void FixerEditor::sectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    const auto a = area.toFloat();
    ui::paintEngravedLine (g, { a.getX(), a.getY() }, { a.getRight(), a.getY() });
    ui::paintChip (g, { a.getX(), a.getY() + 8, ui::chipWidth (title, 18.f), 18.f }, title);
}

void FixerEditor::paint (juce::Graphics& g)
{
    ui::paintChassis (g, getLocalBounds());
    ui::paintBrand (g, headerArea.toFloat().withRight ((float) presets.getX() - 12).withSizeKeepingCentre ((float) (presets.getX() - 12 - headerArea.getX()), 28)
                          .withX ((float) headerArea.getX()), "AI SONG FIXER");

    sectionTitle (g, cleanArea, "CLEAN");
    sectionTitle (g, toneArea, "TONE");
    sectionTitle (g, stereoArea, "STEREO");
    sectionTitle (g, outArea, "OUTPUT");
    sectionTitle (g, meterArea, "METERS");

    // etichette dei meter
    auto lab = [&] (juce::Component& m, const char* t)
    {
        ui::paintPrinted (g, m.getBounds().toFloat().withX ((float) meterArea.getX()).withWidth (40.f), t, 12.f);
    };
    lab (inMeter, "IN"); lab (outMeter, "OUT"); lab (grMeter, "LIMIT");

    // display LUFS
    ui::paintRecess (g, lufsArea.toFloat());
    auto r = lufsArea.toFloat().reduced (10, 6);
    auto row = [&] (const char* name, const juce::String& v, juce::Colour c)
    {
        auto line = r.removeFromTop (r.getHeight() / 3.0f);
        ui::paintPrinted (g, line.removeFromLeft (110), name, 12.f, juce::Justification::centredLeft, C::printDim);
        g.setColour (c); g.setFont (ui::Fonts::bold (17.f));
        g.drawText (v, line, juce::Justification::centredRight);
    };
    auto fmt = [] (float l) { return l <= -69.9f ? juce::String ("--.-") : juce::String (l, 1); };
    row ("LUFS-S ORIGINAL", fmt (shownLufsIn), C::printDim);
    row ("LUFS-S FIXED", fmt (shownLufsOut), C::print);
    row ("FAIR GAIN", (shownFair >= 0 ? "+" : "") + juce::String (shownFair, 1) + " dB", proc.apvts.getRawParameterValue ("fair")->load() > .5f ? C::ledGreen : C::printDim);

    // piede: stato e logo impresso
    const double rate = proc.preparedRate.load();
    const float latMs = (float) (proc.latency.load() * 1000.0 / rate);
    ui::paintPrinted (g, footerArea.toFloat().withWidth (520), "LATENCY " + juce::String (latMs, 1) + " MS   -   "
                      + juce::String (rate / 1000.0, 1) + " KHZ   -   PIEDMONT ELECTRONIC SOUND", 11.f,
                      juce::Justification::centredLeft, C::printDim);
    ui::paintLogo (g, footerArea.toFloat().withTrimmedLeft ((float) footerArea.getWidth() - 220), false, ui::Imprint::debossed,
                   juce::Justification::right);
}

void FixerEditor::resized()
{
    auto r = getLocalBounds().reduced (18, 0);
    headerArea = r.removeFromTop (58).withTrimmedTop (12).withTrimmedBottom (10);
    {
        auto h = headerArea;
        limitBtn.setBounds (h.removeFromRight (78)); h.removeFromRight (6);
        deltaBtn.setBounds (h.removeFromRight (82)); h.removeFromRight (6);
        fairBtn.setBounds (h.removeFromRight (74)); h.removeFromRight (6);
        abBtn.setBounds (h.removeFromRight (80)); h.removeFromRight (14);
        presets.setBounds (h.removeFromRight (170).reduced (0, 3));
    }

    spectrum.setBounds (r.removeFromTop (210));
    r.removeFromTop (12);

    const int knobW = 92, knobH = 118;
    auto row1 = r.removeFromTop (34 + knobH);
    cleanArea = row1.removeFromLeft (knobW * 6 + 20);
    row1.removeFromLeft (18);
    toneArea = row1;
    auto placeKnobs = [&] (juce::Rectangle<int> area, std::initializer_list<Knob*> ks)
    {
        auto a = area.withTrimmedTop (34);
        for (auto* k : ks) k->setBounds (a.removeFromLeft (knobW).withHeight (knobH));
    };
    placeKnobs (cleanArea, { &mud, &harsh, &sens, &fizz, &fizzFreq, &deess });
    placeKnobs (toneArea, { &vocal, &air, &warmth });

    r.removeFromTop (8);
    auto row2 = r.removeFromTop (34 + knobH);
    stereoArea = row2.removeFromLeft (knobW * 2 + 10);
    row2.removeFromLeft (18);
    outArea = row2.removeFromLeft (knobW * 4 + 10);
    row2.removeFromLeft (18);
    meterArea = row2;
    placeKnobs (stereoArea, { &bassMono, &lowCut });
    placeKnobs (outArea, { &input, &output, &mix, &ceiling });

    {
        auto m = meterArea.withTrimmedTop (36);
        auto meters = m.removeFromLeft (140);
        for (auto* lm : { &inMeter, &outMeter, &grMeter })
        {
            auto line = meters.removeFromTop (22);
            lm->setBounds (line.withTrimmedLeft (44).withSizeKeepingCentre (line.getWidth() - 44, 9));
            meters.removeFromTop (4);
        }
        m.removeFromLeft (12);
        lufsArea = m.withHeight (86);
    }

    footerArea = getLocalBounds().reduced (18, 0).removeFromBottom (40).withTrimmedBottom (8);
}
