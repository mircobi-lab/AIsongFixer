#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

/*  Motore spettrale di AI Song Fixer.
    STFT con finestra sqrt-Hann, sovrapposizione 75%, lavoro in mid/side.
    Per ogni frame calcola un guadagno per bin che somma:
      MUD     taglio largo intorno a 300 Hz: fisso leggero + dinamico quando 150-500 Hz pesano troppo sui medi
      HARSH   soppressione delle risonanze aspre (circa 1-12 kHz): picchi sopra la potenza media della loro ottava
      FIZZ    sottrazione spettrale del velo metallico/fruscio sopra FIZZ FREQ + riduzione del side in alto
      DE-ESS  riduzione dinamica della banda 4,5-10 kHz quando le sibilanti dominano
      VOCAL   presenza sul mid (2,5 kHz)
      AIR     mensola larga sopra 10 kHz, applicata dopo la pulizia del fizz
      BASS MONO  side azzerato sotto la frequenza scelta
    La latenza è pari alla lunghezza della FFT.                                                   */
namespace asf
{
struct CleanSettings
{
    float mud = 0, harsh = 0, sens = 0.5f, fizz = 0, fizzFreq = 12000, deess = 0, vocal = 0, air = 0, bassMono = 0;
};

class SpectralCleaner
{
public:
    static constexpr int numBands = 96;              // bande logaritmiche per lo spettro dell'interfaccia (20 Hz - 20 kHz)

    void prepare (double sampleRate);
    void reset();
    int getLatency() const noexcept { return fftSize; }

    // elabora in place; left/right possono essere lo stesso puntatore per il mono
    void process (float* left, float* right, int numSamples, const CleanSettings&);

    // dati per l'interfaccia (dB), aggiornati a ogni frame
    std::array<std::atomic<float>, numBands> bandIn, bandOut, bandGr;
    std::atomic<float> deessGrDb { 0 };

private:
    void processFrame();
    float binFreq (int k) const noexcept { return (float) k * (float) sr / (float) fftSize; }

    double sr = 44100;
    int fftOrder = 11, fftSize = 2048, hop = 512, numBins = 1025;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;
    float olaScale = 1;

    std::vector<float> inM, inS, outM, outS;          // buffer circolari lunghi fftSize
    int writePos = 0, hopCount = 0;
    std::vector<float> bufM, bufS;                    // 2 * fftSize per la FFT reale

    // stato per bin
    std::vector<float> fastP, slowP, noiseFloor, logP;
    std::vector<double> prefix;
    std::vector<float> gHarshDb, gFizz, grDisplay;
    std::vector<int> envLo, envHi, bandOfBin;
    float deessDb = 0, deessRatio = 0, mudDb = 0;
    std::vector<float> curMin, prevMin;                // minimum statistics per il rumore di fondo
    int minFrames = 0, minBlockFrames = 32;

    CleanSettings cur;
};
}
