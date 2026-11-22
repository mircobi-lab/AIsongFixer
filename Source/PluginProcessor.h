#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "Dsp/SpectralCleaner.h"
#include "Dsp/Loudness.h"
#include "Dsp/Limiter.h"
#include "FilePlayer.h"

namespace asf
{
struct Preset
{
    const char* name;
    float mud, harsh, sens, fizz, fizzFreq, deess, vocal, air, bassMono, warmth, lowCut;
};

// Preset di fabbrica: toccano solo i controlli di pulizia e tono, non INPUT/OUTPUT/CEILING
const std::vector<Preset>& factoryPresets();

// ritardo fisso stereo (allinea il segnale originale all'uscita elaborata)
class StereoDelay
{
public:
    void prepare (int delaySamples)
    {
        d = std::max (0, delaySamples);
        bufL.assign ((size_t) d + 1, 0.0f); bufR.assign ((size_t) d + 1, 0.0f); pos = 0;
    }
    void reset() { std::fill (bufL.begin(), bufL.end(), 0.0f); std::fill (bufR.begin(), bufR.end(), 0.0f); pos = 0; }
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const int size = d + 1;
        for (int i = 0; i < n; ++i)
        {
            bufL[(size_t) pos] = inL[i]; bufR[(size_t) pos] = inR[i];
            const int rp = (pos + 1) % size;          // scritto d campioni fa
            outL[i] = bufL[(size_t) rp]; outR[i] = bufR[(size_t) rp];
            pos = (pos + 1) % size;
        }
    }
private:
    int d = 0, pos = 0;
    std::vector<float> bufL, bufR;
};

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[2] {}, z2[2] {};
    void setHighPass (double sr, double f, double q = 0.7071)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f / sr;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) ((1.0 + cw) / 2.0 / a0); b1 = (float) (-(1.0 + cw) / a0); b2 = b0;
        a1 = (float) (-2.0 * cw / a0); a2 = (float) ((1.0 - alpha) / a0);
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    inline float run (float x, int ch)
    {
        const float y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
};
}

class FixerProcessor : public juce::AudioProcessor
{
public:
    FixerProcessor();
    ~FixerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MBStudio91 AI Song Fixer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;

    // banco Personal: preset salvati dall'utente come file XML in una cartella dell'account
    struct UserPreset { juce::String name; juce::File file; };
    const std::vector<UserPreset>& getUserPresets() const noexcept { return userPresets; }
    int getCurrentUserPreset() const noexcept { return currentUser; }   // -1 se è selezionato un preset di fabbrica
    bool saveUserPreset (const juce::String& name, juce::String& error);  // sostituisce un preset con lo stesso nome
    bool loadUserPreset (int index);
    bool deleteUserPreset (int index);
    void setUserPresetDir (const juce::File& dir) { presetDir = dir; refreshUserPresets(); }
    juce::File getUserPresetDir() const { return presetDir; }
    void setParamValue (const char* id, float value);
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    asf::SpectralCleaner cleaner;
    asf::FilePlayer filePlayer;                // solo Standalone: il brano caricato sostituisce l'ingresso

    // misure per l'interfaccia
    std::atomic<float> inPeak { 0 }, outPeak { 0 }, lufsIn { -70 }, lufsOut { -70 }, limiterGrDb { 0 }, fairGainDb { 0 };
    std::atomic<int> latency { 0 };
    std::atomic<double> preparedRate { 44100.0 };

private:
    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    static constexpr int chunk = 512;
    double sr = 44100;
    asf::Biquad hp;
    float hpFreq = -1;
    asf::StereoDelay preDelay, rawDelay;
    asf::PeakLimiter limiter;
    asf::LoudnessMeter dryMeter, wetMeter;
    juce::SmoothedValue<float> inGain, outGain, mix, bypassMix, fairLin;
    juce::AudioBuffer<float> raw, pre, preD, rawD, work;
    int currentProgram = 0;
    int currentUser = -1;
    std::vector<UserPreset> userPresets;
    juce::File presetDir;
    void refreshUserPresets();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FixerProcessor)
};
