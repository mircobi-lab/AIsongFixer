#include "PluginProcessor.h"
#include <algorithm>
#include "PluginEditor.h"

namespace asf
{
const std::vector<Preset>& factoryPresets()
{
    //                           mud  harsh sens  fizz  fizzF   deess vocal air   bassM warmth lowCut
    static const std::vector<Preset> list {
        { "SUNO FULL MIX",       0.35f, 0.45f, 0.5f, 0.40f, 11000, 0.30f, 0.25f, 0.25f, 120, 0.15f, 25 },
        { "SUNO GENTLE",         0.20f, 0.25f, 0.4f, 0.25f, 12000, 0.20f, 0.15f, 0.15f, 100, 0.10f, 20 },
        { "SUNO STRONG",         0.50f, 0.65f, 0.6f, 0.60f, 10000, 0.45f, 0.30f, 0.30f, 140, 0.20f, 30 },
        { "UDIO",                0.30f, 0.40f, 0.5f, 0.30f, 13000, 0.30f, 0.20f, 0.30f, 120, 0.15f, 25 },
        { "VOCAL STEM",          0.40f, 0.50f, 0.5f, 0.45f, 10000, 0.50f, 0.35f, 0.20f,   0, 0.10f, 80 },
        { "INSTRUMENTAL STEM",   0.35f, 0.35f, 0.5f, 0.40f, 11000, 0.00f, 0.00f, 0.25f, 120, 0.15f, 25 },
        { "FLAT",                0.00f, 0.00f, 0.5f, 0.00f, 12000, 0.00f, 0.00f, 0.00f,   0, 0.00f, 10 },
    };
    return list;
}
}

using APVTS = juce::AudioProcessorValueTreeState;

//==============================================================================
// Banco Personal
namespace
{
    // parametri salvati in un preset: pulizia e tono (come i preset di fabbrica)
    constexpr const char* const presetParamIds[] { "mud", "harsh", "sens", "fizz", "fizzfreq", "deess",
                                                   "vocal", "air", "bassmono", "warmth", "lowcut" };
    constexpr const char* const presetTag = "AISONGFIXER_PRESET";

    juce::File defaultPresetDir()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MBStudio91").getChildFile ("AI Song Fixer").getChildFile ("Presets");
    }

    // solo lettere, cifre, spazi, trattino e underscore: il nome diventa anche il nome del file
    juce::String cleanPresetName (const juce::String& raw)
    {
        juce::String out;
        for (auto ch : raw.trim())
        {
            if (juce::CharacterFunctions::isLetterOrDigit (ch) || ch == ' ' || ch == '-' || ch == '_')
                out += juce::String::charToString (ch);
            else if (out.isNotEmpty() && ! out.endsWith ("_"))
                out += "_";
        }
        return out.trim().substring (0, 40);
    }
}

APVTS::ParameterLayout FixerProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + " %"; };
    auto hz = [] (float v, int) { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (juce::roundToInt (v)) + " Hz"; };
    auto db = [] (float v, int) { return String (v, 1) + " dB"; };
    auto add = [&] (const char* id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    {
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def,
                                                             AudioParameterFloatAttributes().withStringFromValueFunction (f)));
    };
    auto hzRange = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };

    const auto& d = asf::factoryPresets().front();
    add ("input",    "Input",      { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    add ("lowcut",   "Low Cut",    hzRange (10.0f, 120.0f), d.lowCut, hz);
    add ("mud",      "Mud",        { 0.0f, 1.0f }, d.mud, pct);
    add ("harsh",    "Harsh",      { 0.0f, 1.0f }, d.harsh, pct);
    add ("sens",     "Sensitivity",{ 0.0f, 1.0f }, d.sens, pct);
    add ("fizz",     "Fizz",       { 0.0f, 1.0f }, d.fizz, pct);
    add ("fizzfreq", "Fizz Freq",  hzRange (6000.0f, 18000.0f), d.fizzFreq, hz);
    add ("deess",    "De-Ess",     { 0.0f, 1.0f }, d.deess, pct);
    add ("vocal",    "Vocal",      { 0.0f, 1.0f }, d.vocal, pct);
    add ("air",      "Air",        { 0.0f, 1.0f }, d.air, pct);
    {
        NormalisableRange<float> r (0.0f, 250.0f, 1.0f);
        ps.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "bassmono", 1 }, "Bass Mono", r, d.bassMono,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? String ("OFF") : String (juce::roundToInt (v)) + " Hz"; })));
    }
    add ("warmth",   "Warmth",     { 0.0f, 1.0f }, d.warmth, pct);
    add ("output",   "Output",     { -12.0f, 12.0f, 0.1f }, 0.0f, db);
    add ("mix",      "Mix",        { 0.0f, 1.0f }, 1.0f, pct);
    add ("ceiling",  "Ceiling",    { -3.0f, 0.0f, 0.1f }, -1.0f, db);
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "limit", 1 }, "Limiter", true));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "A/B Original", false));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "fair", 1 }, "Fair Compare", true));
    ps.push_back (std::make_unique<AudioParameterBool> (ParameterID { "delta", 1 }, "Delta", false));
    return { ps.begin(), ps.end() };
}

FixerProcessor::FixerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AISONGFIXER", createLayout())
{
    presetDir = defaultPresetDir();
    refreshUserPresets();
}

bool FixerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void FixerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    preparedRate.store (sampleRate);
    cleaner.prepare (sampleRate);
    filePlayer.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    dryMeter.prepare (sampleRate);
    wetMeter.prepare (sampleRate);
    preDelay.prepare (cleaner.getLatency());
    const int total = cleaner.getLatency() + limiter.getLatency();
    rawDelay.prepare (total);
    latency.store (total);
    setLatencySamples (total);

    hp.reset(); hpFreq = -1;
    for (auto* b : { &raw, &pre, &preD, &rawD, &work })
        b->setSize (2, chunk, false, false, true);

    for (auto* s : { &inGain, &outGain, &mix, &bypassMix, &fairLin })
        s->reset (sampleRate, 0.03);
    fairLin.reset (sampleRate, 0.3);
    inGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("input")));
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p ("output")));
    mix.setCurrentAndTargetValue (p ("mix"));
    bypassMix.setCurrentAndTargetValue (p ("bypass") > 0.5f ? 1.0f : 0.0f);
    fairLin.setCurrentAndTargetValue (1.0f);
}

void FixerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = getTotalNumInputChannels();
    const int total = buffer.getNumSamples();
    if (numCh == 0 || total == 0) return;
    for (int c = numCh; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, total);

    // Standalone con un brano caricato: il file prende il posto dell'ingresso audio
    if (juce::JUCEApplicationBase::isStandaloneApp() && filePlayer.isLoaded())
    {
        buffer.clear();
        filePlayer.render (buffer);
    }

    asf::CleanSettings s;
    s.mud = p ("mud"); s.harsh = p ("harsh"); s.sens = p ("sens"); s.fizz = p ("fizz");
    s.fizzFreq = p ("fizzfreq"); s.deess = p ("deess"); s.vocal = p ("vocal"); s.air = p ("air");
    s.bassMono = p ("bassmono");

    const float lc = p ("lowcut");
    if (std::abs (lc - hpFreq) > 0.01f) { hp.setHighPass (sr, lc); hpFreq = lc; }

    inGain.setTargetValue (juce::Decibels::decibelsToGain (p ("input")));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (p ("output")));
    mix.setTargetValue (p ("mix"));
    const bool bypass = p ("bypass") > 0.5f, fair = p ("fair") > 0.5f, delta = p ("delta") > 0.5f, limit = p ("limit") > 0.5f;
    bypassMix.setTargetValue (bypass ? 1.0f : 0.0f);
    const float warmth = p ("warmth");
    const float drive = 1.0f + 2.0f * warmth;
    const float ceiling = juce::Decibels::decibelsToGain (p ("ceiling"));

    float peakIn = 0, peakOut = 0, minGr = 1;

    for (int start = 0; start < total; start += chunk)
    {
        const int n = std::min (chunk, total - start);
        float* bl = buffer.getWritePointer (0, start);
        float* br = buffer.getWritePointer (numCh > 1 ? 1 : 0, start);

        float* rl = raw.getWritePointer (0); float* rr = raw.getWritePointer (1);
        float* xl = work.getWritePointer (0); float* xr = work.getWritePointer (1);
        float* pl = pre.getWritePointer (0); float* pr = pre.getWritePointer (1);
        float* dl = preD.getWritePointer (0); float* dr = preD.getWritePointer (1);
        float* ol = rawD.getWritePointer (0); float* orr = rawD.getWritePointer (1);

        for (int i = 0; i < n; ++i)
        {
            rl[i] = bl[i]; rr[i] = br[i];
            peakIn = std::max (peakIn, std::max (std::abs (rl[i]), std::abs (rr[i])));
            const float g = inGain.getNextValue();
            const float x0 = hp.run (rl[i] * g, 0), x1 = hp.run (rr[i] * g, 1);
            xl[i] = pl[i] = x0; xr[i] = pr[i] = x1;
        }
        dryMeter.process (rl, rr, n);

        cleaner.process (xl, xr, n, s);
        preDelay.process (pl, pr, dl, dr, n);

        for (int i = 0; i < n; ++i)
        {
            float l, r;
            if (delta)
            {
                l = dl[i] - xl[i]; r = dr[i] - xr[i];               // quello che la pulizia toglie
            }
            else
            {
                const float m = mix.getNextValue();
                l = m * xl[i] + (1.0f - m) * dl[i];
                r = m * xr[i] + (1.0f - m) * dr[i];
                if (warmth > 0.001f)
                {
                    l += warmth * (std::tanh (drive * l) / drive - l);
                    r += warmth * (std::tanh (drive * r) / drive - r);
                }
                const float og = outGain.getNextValue();
                l *= og; r *= og;
            }
            xl[i] = l; xr[i] = r;
        }

        limiter.process (xl, xr, n, ceiling, limit && ! delta);
        minGr = std::min (minGr, limiter.getLastMinGain());
        wetMeter.process (xl, xr, n);

        rawDelay.process (rl, rr, ol, orr, n);

        // FAIR: l'originale viene portato alla stessa loudness del pulito, il pulito non si tocca
        float fairDb = 0;
        if (fair)
        {
            const float li = dryMeter.shortTerm(), lo = wetMeter.shortTerm();
            if (li > -60.0f && lo > -60.0f) fairDb = juce::jlimit (-12.0f, 12.0f, lo - li);
        }
        fairLin.setTargetValue (juce::Decibels::decibelsToGain (fairDb));
        fairGainDb.store (fairDb);

        for (int i = 0; i < n; ++i)
        {
            const float b = bypassMix.getNextValue();
            const float fg = fairLin.getNextValue();
            float l = xl[i] + b * (ol[i] * fg - xl[i]);
            float r = xr[i] + b * (orr[i] * fg - xr[i]);
            bl[i] = l;
            if (numCh > 1) br[i] = r;
            else bl[i] = 0.5f * (l + r);
            peakOut = std::max (peakOut, std::max (std::abs (l), std::abs (r)));
        }
    }

    inPeak.store (std::max (peakIn, inPeak.load() * 0.85f));
    outPeak.store (std::max (peakOut, outPeak.load() * 0.85f));
    lufsIn.store (dryMeter.shortTerm());
    lufsOut.store (wetMeter.shortTerm());
    limiterGrDb.store (juce::Decibels::gainToDecibels (minGr, -40.0f));
}

int FixerProcessor::getNumPrograms() { return (int) asf::factoryPresets().size(); }

const juce::String FixerProcessor::getProgramName (int i)
{
    const auto& l = asf::factoryPresets();
    return juce::isPositiveAndBelow (i, (int) l.size()) ? juce::String (l[(size_t) i].name) : juce::String();
}

void FixerProcessor::setParamValue (const char* id, float value)
{
    if (auto* prm = apvts.getParameter (id))
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void FixerProcessor::setCurrentProgram (int i)
{
    const auto& l = asf::factoryPresets();
    if (! juce::isPositiveAndBelow (i, (int) l.size())) return;
    currentProgram = i;
    currentUser = -1;
    const auto& pr = l[(size_t) i];
    setParamValue ("mud", pr.mud); setParamValue ("harsh", pr.harsh); setParamValue ("sens", pr.sens);
    setParamValue ("fizz", pr.fizz); setParamValue ("fizzfreq", pr.fizzFreq); setParamValue ("deess", pr.deess);
    setParamValue ("vocal", pr.vocal); setParamValue ("air", pr.air); setParamValue ("bassmono", pr.bassMono);
    setParamValue ("warmth", pr.warmth); setParamValue ("lowcut", pr.lowCut);
}

void FixerProcessor::refreshUserPresets()
{
    const auto oldFile = (currentUser >= 0 && currentUser < (int) userPresets.size()) ? userPresets[(size_t) currentUser].file : juce::File();
    userPresets.clear();
    for (const auto& f : presetDir.findChildFiles (juce::File::findFiles, false, "*.xml"))
        userPresets.push_back ({ f.getFileNameWithoutExtension(), f });
    std::sort (userPresets.begin(), userPresets.end(),
               [] (const UserPreset& a, const UserPreset& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    currentUser = -1;
    for (int i = 0; i < (int) userPresets.size(); ++i)
        if (oldFile != juce::File() && userPresets[(size_t) i].file == oldFile) currentUser = i;
}

bool FixerProcessor::saveUserPreset (const juce::String& requestedName, juce::String& error)
{
    const auto name = cleanPresetName (requestedName);
    if (name.isEmpty())
    {
        error = "Scrivi un nome per il preset (lettere, numeri, spazi)";
        return false;
    }
    if (! presetDir.createDirectory())
    {
        error = "Impossibile creare la cartella dei preset: " + presetDir.getFullPathName();
        return false;
    }

    juce::XmlElement x (presetTag);
    x.setAttribute ("version", 1);
    x.setAttribute ("name", name);
    for (auto* id : presetParamIds)
        x.setAttribute (id, (double) p (id));

    const auto file = presetDir.getChildFile (name + ".xml");
    if (! x.writeTo (file))
    {
        error = "Impossibile scrivere " + file.getFileName();
        return false;
    }
    refreshUserPresets();
    for (int i = 0; i < (int) userPresets.size(); ++i)
        if (userPresets[(size_t) i].file == file) currentUser = i;
    return true;
}

bool FixerProcessor::loadUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size())) return false;
    std::unique_ptr<juce::XmlElement> x (juce::XmlDocument::parse (userPresets[(size_t) index].file));
    if (x == nullptr || ! x->hasTagName (presetTag)) return false;

    for (auto* id : presetParamIds)
        if (x->hasAttribute (id))
            setParamValue (id, (float) x->getDoubleAttribute (id));
    currentUser = index;
    return true;
}

bool FixerProcessor::deleteUserPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) userPresets.size())) return false;
    if (! userPresets[(size_t) index].file.deleteFile()) return false;
    currentUser = -1;
    refreshUserPresets();
    return true;
}

void FixerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void FixerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            currentProgram = t.getProperty ("program", 0);
            apvts.replaceState (t);
        }
}

juce::AudioProcessorEditor* FixerProcessor::createEditor() { return new FixerEditor (*this, juce::JUCEApplicationBase::isStandaloneApp()); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FixerProcessor(); }
