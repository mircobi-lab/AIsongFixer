// Test offline di AI Song Fixer: ricostruzione, latenza, limiter, stabilità, FAIR, preset.
// Uscita 0 se tutto passa.
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <random>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  OK   " : "  FAIL ") << what << std::endl;
    if (! ok) ++failures;
}

static void setParam (FixerProcessor& p, const char* id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// segnale di prova: accordo + rumore filtrato + risonanza aspra a 3,1 kHz + sibilanti a raffiche
static juce::AudioBuffer<float> makeSignal (double sr, double seconds, unsigned seed = 1)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    float lp = 0, b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        float s = 0;
        for (double f : { 110.0, 220.0, 277.18, 329.63, 440.0 })
            s += 0.08f * (float) std::sin (2 * juce::MathConstants<double>::pi * f * t);
        s += 0.06f * (float) std::sin (2 * juce::MathConstants<double>::pi * 3100.0 * t);   // risonanza
        lp = 0.9f * lp + 0.1f * nd (rng);
        {   // rumore rosa (Paul Kellet): energia uguale per ottava, come un mix pieno
            const float w = nd (rng) * 0.05f;
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f; b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f;
            s += 0.5f * (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f); b6 = w * 0.115926f;
        }
        const float hiss = 0.01f * nd (rng);
        const float burst = (std::fmod (t, 0.5) < 0.08) ? 0.05f * nd (rng) : 0.0f;          // "s"
        b.setSample (0, i, s + 0.05f * lp + hiss + burst);
        b.setSample (1, i, s + 0.05f * lp - hiss + burst * 0.8f);
    }
    return b;
}

static void run (FixerProcessor& p, juce::AudioBuffer<float>& buf, int block)
{
    juce::MidiBuffer midi;
    for (int start = 0; start < buf.getNumSamples(); start += block)
    {
        const int n = std::min (block, buf.getNumSamples() - start);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, start, n);
        p.processBlock (view, midi);
    }
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

#include "PluginEditor.h"

// --snapshot file.png : salva un'immagine dell'interfaccia dopo aver fatto suonare il segnale di prova
static int snapshot (const juce::File& file)
{
    FixerProcessor p; p.setCurrentProgram (0); p.prepareToPlay (48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto buf = makeSignal (48000, 4.0, 7);
    juce::MidiBuffer midi;
    for (int start = 0; start < buf.getNumSamples(); start += 512)
    {
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, start, 512);
        p.processBlock (view, midi);
        if (start % (512 * 4) == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
    juce::FileOutputStream out (file); out.setPosition (0); out.truncate();
    juce::PNGImageFormat().writeImageToStream (img, out);
    std::cout << "snapshot: " << file.getFullPathName() << std::endl;
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
        return snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "== " << sr << " Hz" << std::endl;

        // 1) FLAT, limiter spento: l'uscita deve essere l'ingresso ritardato della latenza dichiarata
        {
            FixerProcessor p;
            p.setCurrentProgram (6);            // FLAT
            setParam (p, "limit", 0.0f); setParam (p, "lowcut", 10.0f);
            p.prepareToPlay (sr, 333);
            const int lat = p.getLatencySamples();
            auto in = makeSignal (sr, 2.0);
            auto out = in;
            run (p, out, 333);
            asf::Biquad hp; hp.setHighPass (sr, 10.0);         // stesso passa-alto del plugin
            for (int i = 0; i < in.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) in.setSample (c, i, hp.run (in.getSample (c, i), c));
            double err = 0, ref = 0;
            for (int c = 0; c < 2; ++c)
                for (int i = lat + (int) sr / 2; i < in.getNumSamples(); ++i)
                {
                    const double d = out.getSample (c, i) - in.getSample (c, i - lat);
                    err += d * d; ref += (double) in.getSample (c, i - lat) * in.getSample (c, i - lat);
                }
            const double db = 10.0 * std::log10 (err / ref + 1e-30);
            check (db < -50.0, "FLAT ricostruisce l'ingresso (errore " + juce::String (db, 1) + " dB, latenza " + juce::String (lat) + ")");
        }

        // 2) preset SUNO FULL MIX: stabile, toglie la risonanza a 3,1 kHz e un po' di fizz
        {
            FixerProcessor p;
            p.setCurrentProgram (0);
            setParam (p, "limit", 0.0f);
            p.prepareToPlay (sr, 512);
            auto in = makeSignal (sr, 3.0, 2);
            auto out = in;
            run (p, out, 512);
            check (allFinite (out), "SUNO FULL MIX: nessun NaN/inf");

            auto toneLevel = [sr] (const juce::AudioBuffer<float>& b, int from, double f)
            {
                double re = 0, im = 0; const int n = b.getNumSamples() - from;
                for (int i = from; i < b.getNumSamples(); ++i)
                {
                    const double w = 2 * juce::MathConstants<double>::pi * f * (i - from) / sr;
                    re += b.getSample (0, i) * std::cos (w); im += b.getSample (0, i) * std::sin (w);
                }
                return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / n + 1e-12);
            };
            const int lat = p.getLatencySamples();
            const double before = toneLevel (in, (int) sr, 3100.0), after = toneLevel (out, (int) sr + lat, 3100.0);
            check (after < before - 1.0, "risonanza 3,1 kHz ridotta di " + juce::String (before - after, 1) + " dB");
            const double bassBefore = toneLevel (in, (int) sr, 110.0), bassAfter = toneLevel (out, (int) sr + lat, 110.0);
            check (std::abs (bassAfter - bassBefore) < 1.5, "110 Hz quasi invariato (" + juce::String (bassAfter - bassBefore, 2) + " dB)");
            asf::LoudnessMeter a, b; a.prepare (sr); b.prepare (sr);
            const int len = (int) sr * 2;
            a.process (in.getReadPointer (0, (int) sr - lat), in.getReadPointer (1, (int) sr - lat), len);
            b.process (out.getReadPointer (0, (int) sr), out.getReadPointer (1, (int) sr), len);
            auto hiEnergy = [sr] (const juce::AudioBuffer<float>& buf, int from, int len)
            {
                juce::dsp::FFT f (12); std::vector<float> d (8192); double e = 0; int frames = 0;
                for (int pos = from; pos + 4096 <= from + len; pos += 4096, ++frames)
                {
                    std::fill (d.begin(), d.end(), 0.0f);
                    for (int i = 0; i < 4096; ++i) d[(size_t) i] = buf.getSample (0, pos + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / 4096));
                    f.performFrequencyOnlyForwardTransform (d.data());
                    for (int k = (int) (12000.0 * 4096 / sr); k < 2048; ++k) e += (double) d[(size_t) k] * d[(size_t) k];
                }
                return 10.0 * std::log10 (e / frames + 1e-20);
            };
            const double hiIn = hiEnergy (in, (int) sr - lat, len), hiOut = hiEnergy (out, (int) sr, len);
            check (hiOut < hiIn - 2.0, "fizz: energia sopra 12 kHz ridotta di " + juce::String (hiIn - hiOut, 1) + " dB");
            check (std::abs (b.shortTerm() - a.shortTerm()) < 4.0f, "loudness: " + juce::String (a.shortTerm(), 1) + " -> " + juce::String (b.shortTerm(), 1) + " LUFS");
        }

        // 3) limiter: +9 dB in ingresso, il picco resta sotto il ceiling
        {
            FixerProcessor p;
            p.setCurrentProgram (0);
            setParam (p, "input", 9.0f); setParam (p, "ceiling", -1.0f); setParam (p, "limit", 1.0f);
            p.prepareToPlay (sr, 256);
            auto buf = makeSignal (sr, 2.0, 3);
            run (p, buf, 256);
            const float ceil = juce::Decibels::decibelsToGain (-1.0f);
            float peak = 0;
            for (int c = 0; c < 2; ++c) peak = std::max (peak, buf.getMagnitude (c, (int) sr / 2, buf.getNumSamples() - (int) sr / 2));
            check (peak <= ceil * 1.0005f, "limiter: picco " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS (ceiling -1)");
        }

        // 4) A/B con FAIR: l'originale esce alla loudness del pulito
        {
            FixerProcessor p;
            p.setCurrentProgram (2);              // STRONG: differenza di livello ben udibile
            setParam (p, "output", -4.0f); setParam (p, "bypass", 1.0f); setParam (p, "fair", 1.0f);
            p.prepareToPlay (sr, 512);
            auto buf = makeSignal (sr, 6.0, 4);
            run (p, buf, 512);
            const float wet = p.lufsOut.load(), dry = p.lufsIn.load(), fair = p.fairGainDb.load();
            asf::LoudnessMeter m; m.prepare (sr);
            const int from = buf.getNumSamples() - (int) (sr * 3);
            m.process (buf.getReadPointer (0, from), buf.getReadPointer (1, from), (int) (sr * 3));
            check (std::abs (m.shortTerm() - wet) < 0.7f, "FAIR: originale a " + juce::String (m.shortTerm(), 1) + " LUFS, pulito "
                   + juce::String (wet, 1) + " (senza FAIR " + juce::String (dry, 1) + ", guadagno " + juce::String (fair, 1) + " dB)");
        }

        // 5) DELTA + mono + blocchi irregolari
        {
            FixerProcessor p;
            p.setCurrentProgram (4);
            setParam (p, "delta", 1.0f);
            p.prepareToPlay (sr, 1000);
            auto buf = makeSignal (sr, 1.5, 5);
            juce::MidiBuffer midi;
            int pos = 0, k = 0; const int sizes[] = { 1, 17, 1000, 64, 333 };
            while (pos < buf.getNumSamples())
            {
                const int n = std::min (sizes[k++ % 5], buf.getNumSamples() - pos);
                juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, pos, n);
                p.processBlock (view, midi); pos += n;
            }
            check (allFinite (buf), "DELTA e blocchi irregolari: nessun NaN/inf");
        }
    }

    // CPU: 60 s di audio a 48 kHz, blocchi da 512
    {
        FixerProcessor p; p.setCurrentProgram (0); p.prepareToPlay (48000, 512);
        auto buf = makeSignal (48000, 60.0, 9);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, buf, 512);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  CPU: 60 s elaborati in " << ms / 1000.0 << " s (" << ms / 600.0 << " % di un core)" << std::endl;
    }

    // 6) stato: salva e ricarica
    {
        FixerProcessor a, b;
        a.setCurrentProgram (3); setParam (a, "ceiling", -0.5f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.apvts.getRawParameterValue ("ceiling")->load() + 0.5f) < 0.01f && b.getCurrentProgram() == 3, "stato salvato e ricaricato");
    }

    std::cout << (failures == 0 ? "TUTTI I TEST PASSATI" : juce::String (failures) + " TEST FALLITI") << std::endl;
    return failures == 0 ? 0 : 1;
}
