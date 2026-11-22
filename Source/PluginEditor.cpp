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
FixerEditor::FixerEditor (FixerProcessor& p, bool enabled)
    : AudioProcessorEditor (p), proc (p), playerEnabled (enabled), spectrum (p.cleaner),
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
      ceiling  (p.apvts, "ceiling",  "CEILING",   "Tetto del limiter (picco campione)"),
      wave (p.filePlayer)
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (spectrum);
    for (auto* k : { &mud, &harsh, &sens, &fizz, &fizzFreq, &deess, &vocal, &air, &warmth, &bassMono, &lowCut, &input, &output, &mix, &ceiling })
        addAndMakeVisible (k);
    for (auto* m : { &inMeter, &outMeter, &grMeter })
        addAndMakeVisible (m);

    presets.setTooltip ("Preset: quelli di fabbrica in alto, il tuo banco PERSONAL in fondo");
    presets.onChange = [this]
    {
        const int id = presets.getSelectedId();
        if (id >= userIdBase)     proc.loadUserPreset (id - userIdBase);
        else if (id > 0)          proc.setCurrentProgram (id - 1);
    };
    addAndMakeVisible (presets);
    rebuildPresetMenu();

    saveBtn.setTooltip ("Salva le impostazioni attuali nel banco PERSONAL, con un nome");
    saveBtn.onClick = [this] { savePreset(); };
    delBtn.setTooltip ("Elimina il preset PERSONAL selezionato");
    delBtn.onClick = [this] { deletePreset(); };
    addAndMakeVisible (saveBtn);
    addAndMakeVisible (delBtn);

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

    if (playerEnabled)
    {
        addAndMakeVisible (wave);
        addAndMakeVisible (loadBtn);
        addAndMakeVisible (playBtn);
        addAndMakeVisible (loopBtn);
        addAndMakeVisible (exportBtn);
        addAndMakeVisible (status);

        loadBtn.setTooltip ("Apri un brano (WAV, AIFF, FLAC, OGG). Puoi anche trascinarlo nella finestra");
        loadBtn.onClick = [this] { chooseFile(); };
        playBtn.setTooltip ("Ascolta il brano pulito. Con A/B senti l'originale");
        playBtn.onClick = [this] { if (proc.filePlayer.isPlaying()) proc.filePlayer.stop(); else proc.filePlayer.play(); };
        loopBtn.setClickingTogglesState (true);
        loopBtn.setToggleState (true, juce::dontSendNotification);
        loopBtn.getProperties().set ("ledColour", "ff3ee05c");
        loopBtn.setTooltip ("Ripete il brano alla fine");
        loopBtn.onClick = [this] { proc.filePlayer.setLooping (loopBtn.getToggleState()); };
        exportBtn.setTooltip ("Esporta il brano pulito in WAV a 24 bit, alla stessa frequenza del file");
        exportBtn.onClick = [this] { exportFile(); };
        exportBtn.setEnabled (false);
        status.setText (proc.filePlayer.isLoaded() ? "CARICATO: " + proc.filePlayer.getFile().getFileName()
                                                   : juce::String ("TRASCINA UN BRANO QUI, O PREMI LOAD"), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, C::printDim);
        status.setJustificationType (juce::Justification::centredLeft);

        setSize (1100, 744);
    }
    else
    {
        setSize (1100, 660);
    }
    startTimerHz (20);
}

FixerEditor::~FixerEditor()
{
    chooser.reset();
    setLookAndFeel (nullptr);
}

//==============================================================================
void WaveView::seek (const juce::MouseEvent& e)
{
    if (! player.isLoaded()) return;
    const float frac = juce::jlimit (0.0f, 1.0f, (float) e.x / (float) juce::jmax (1, getWidth()));
    player.setPosition ((double) frac * player.getLength());
}

void WaveView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    ui::paintRecess (g, r);
    const auto in = r.reduced (6.f, 6.f);
    const auto& ov = player.getOverview();
    if (! player.isLoaded() || ov.empty())
    {
        ui::paintPrinted (g, in, "NESSUN BRANO CARICATO", 12.f, juce::Justification::centred, C::printDim);
        return;
    }
    const double len = player.getLength();
    const float playX = in.getX() + in.getWidth() * (len > 0.0 ? (float) (player.getPosition() / len) : 0.0f);
    const float cy = in.getCentreY(), w = in.getWidth() / (float) ov.size();
    for (size_t i = 0; i < ov.size(); ++i)
    {
        const float x = in.getX() + (float) i * w;
        const float amp = juce::jmax (1.0f, ov[i] * in.getHeight() * 0.5f);
        g.setColour (x < playX ? C::print.withAlpha (.85f) : C::printDim.withAlpha (.45f));
        g.fillRect (x, cy - amp, juce::jmax (1.0f, w * .8f), amp * 2.0f);
    }
    g.setColour (C::ledGreen);
    g.fillRect (playX - 1.0f, in.getY(), 2.0f, in.getHeight());
}

bool FixerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! playerEnabled) return false;
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg")) return true;
    return false;
}

void FixerEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.hasFileExtension ("wav;aif;aiff;flac;ogg")) { loadFile (file); return; }
    }
}

void FixerEditor::rebuildPresetMenu()
{
    presets.clear (juce::dontSendNotification);
    const auto& list = asf::factoryPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        presets.addItem (list[(size_t) i].name, i + 1);

    presets.addSectionHeading ("PERSONAL");
    const auto& user = proc.getUserPresets();
    if (user.empty())
    {
        presets.addItem ("NESSUN PRESET: PREMI SAVE", userIdBase - 1);
        presets.setItemEnabled (userIdBase - 1, false);
    }
    for (int i = 0; i < (int) user.size(); ++i)
        presets.addItem (user[(size_t) i].name, userIdBase + i);

    syncPresetSelection();
}

void FixerEditor::syncPresetSelection()
{
    const int u = proc.getCurrentUserPreset();
    const int wanted = u >= 0 ? userIdBase + u : proc.getCurrentProgram() + 1;
    if (presets.getSelectedId() != wanted)
        presets.setSelectedId (wanted, juce::dontSendNotification);
}

void FixerEditor::savePreset()
{
    // il nome proposto è quello del preset selezionato, così SAVE lo aggiorna
    const int cur = proc.getCurrentUserPreset();
    const juce::String suggested = cur >= 0 ? proc.getUserPresets()[(size_t) cur].name : juce::String ("Il mio preset");

    auto* aw = new juce::AlertWindow ("Salva nel banco PERSONAL",
                                      "Scegli il nome. Se esiste già un preset con lo stesso nome, viene sostituito.",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", suggested, "Nome:");
    aw->addButton ("SALVA", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("ANNULLA", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<FixerEditor> safe (this);
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (safe == nullptr || result != 1) return;
        juce::String error;
        if (! safe->proc.saveUserPreset (aw->getTextEditorContents ("name"), error))
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", error);
            return;
        }
        safe->rebuildPresetMenu();
    }), true);
}

void FixerEditor::deletePreset()
{
    const int idx = proc.getCurrentUserPreset();
    if (idx < 0)
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Preset",
                                                "Seleziona un preset del banco PERSONAL da eliminare.");
        return;
    }
    const auto name = proc.getUserPresets()[(size_t) idx].name;
    juce::Component::SafePointer<FixerEditor> safe (this);
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Elimina preset",
                                        "Eliminare \"" + name + "\" dal banco PERSONAL? Non si può annullare.",
                                        "ELIMINA", "ANNULLA", this,
                                        juce::ModalCallbackFunction::create ([safe, idx] (int result)
    {
        if (safe == nullptr || result != 1) return;
        if (safe->proc.deleteUserPreset (idx)) safe->rebuildPresetMenu();
    }));
}

void FixerEditor::loadFile (const juce::File& f)
{
    juce::String err;
    if (proc.filePlayer.load (f, err))
        status.setText ("CARICATO: " + f.getFileName(), juce::dontSendNotification);
    else
        status.setText (err, juce::dontSendNotification);
}

void FixerEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Apri un brano", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) loadFile (f);
                          });
}

void FixerEditor::exportFile()
{
    if (! proc.filePlayer.isLoaded() || exporter.isBusy()) return;
    const auto src = proc.filePlayer.getFile();
    const auto suggested = src.getSiblingFile (src.getFileNameWithoutExtension() + " - AI Song Fixer.wav");
    chooser = std::make_unique<juce::FileChooser> ("Salva il brano pulito", suggested, "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                          | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, src] (const juce::FileChooser& fc)
                          {
                              const auto dest = fc.getResult();
                              if (dest == juce::File()) return;
                              juce::MemoryBlock state;
                              proc.getStateInformation (state);
                              exporter.start (src, dest.withFileExtension ("wav"), state);
                          });
}

void FixerEditor::timerCallback()
{
    inMeter.setLevel (juce::jmap (juce::Decibels::gainToDecibels (proc.inPeak.load(), -48.0f), -48.0f, 0.0f, 0.0f, 1.0f));
    outMeter.setLevel (juce::jmap (juce::Decibels::gainToDecibels (proc.outPeak.load(), -48.0f), -48.0f, 0.0f, 0.0f, 1.0f));
    grMeter.setLevel (juce::jlimit (0.0f, 1.0f, -proc.limiterGrDb.load() / 12.0f));

    shownLufsIn = proc.lufsIn.load(); shownLufsOut = proc.lufsOut.load();
    shownFair = proc.fairGainDb.load(); shownGr = proc.limiterGrDb.load();
    if (playerEnabled)
    {
        playBtn.setButtonText (proc.filePlayer.isPlaying() ? "STOP" : "PLAY");
        exportBtn.setEnabled (proc.filePlayer.isLoaded() && ! exporter.isBusy());
        wave.repaint();
        if (exporter.isBusy())
            status.setText ("ESPORTAZIONE " + juce::String (juce::roundToInt (exporter.getProgress() * 100.0f)) + " %", juce::dontSendNotification);
        switch (exporter.takeResult())
        {
            case asf::Exporter::Result::done:      status.setText ("SALVATO: " + exporter.getDestination().getFileName(), juce::dontSendNotification); break;
            case asf::Exporter::Result::failed:    status.setText ("ERRORE: " + exporter.getMessage(), juce::dontSendNotification); break;
            case asf::Exporter::Result::cancelled: status.setText ("ESPORTAZIONE ANNULLATA", juce::dontSendNotification); break;
            case asf::Exporter::Result::none:      break;
        }
    }
    syncPresetSelection();
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
        delBtn.setBounds (h.removeFromRight (46)); h.removeFromRight (4);
        saveBtn.setBounds (h.removeFromRight (56)); h.removeFromRight (6);
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

    if (playerEnabled)
    {
        transportArea = getLocalBounds().reduced (18, 0).withY (getHeight() - 124).withHeight (72);
        auto t = transportArea;
        auto top = t.removeFromTop (30);
        loadBtn.setBounds (top.removeFromLeft (80)); top.removeFromLeft (6);
        playBtn.setBounds (top.removeFromLeft (80)); top.removeFromLeft (6);
        loopBtn.setBounds (top.removeFromLeft (70)); top.removeFromLeft (12);
        exportBtn.setBounds (top.removeFromRight (130)); top.removeFromRight (12);
        status.setBounds (top);
        t.removeFromTop (6);
        wave.setBounds (t);
    }
}
