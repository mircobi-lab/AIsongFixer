#include "SpectralCleaner.h"
#include <cmath>
#include <limits>

namespace asf
{
namespace
{
    inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }

    // 0 sotto a, 1 sopra b, passaggio morbido in scala logaritmica
    inline float ramp (float f, float a, float b) noexcept
    {
        if (f <= a) return 0.0f;
        if (f >= b) return 1.0f;
        const float x = std::log2 (f / a) / std::log2 (b / a);
        return x * x * (3.0f - 2.0f * x);
    }

    inline float bump (float f, float lo1, float lo2, float hi1, float hi2) noexcept
    {
        return ramp (f, lo1, lo2) * (1.0f - ramp (f, hi1, hi2));
    }

    // campana gaussiana in ottave (sigma in ottave)
    inline float bell (float f, float centre, float sigmaOct) noexcept
    {
        if (f <= 0) return 0.0f;
        const float x = std::log2 (f / centre) / sigmaOct;
        return std::exp (-0.5f * x * x);
    }
}

void SpectralCleaner::prepare (double sampleRate)
{
    sr = sampleRate;
    fftOrder = sampleRate <= 50000.0 ? 11 : (sampleRate <= 100000.0 ? 12 : 13);
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;
    numBins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)   // sqrt-Hann periodica: analisi e sintesi, il prodotto è una Hann
        window[(size_t) i] = std::sqrt (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize));

    // somma delle finestre (analisi x sintesi) sovrapposte: normalizza la ricostruzione a 1
    double sum = 0;
    for (int i = 0; i < fftSize; i += hop)
        sum += (double) window[(size_t) i] * window[(size_t) i];
    olaScale = (float) (1.0 / sum);

    inM.assign ((size_t) fftSize, 0); inS.assign ((size_t) fftSize, 0);
    outM.assign ((size_t) fftSize, 0); outS.assign ((size_t) fftSize, 0);
    bufM.assign ((size_t) fftSize * 2, 0); bufS.assign ((size_t) fftSize * 2, 0);

    fastP.assign ((size_t) numBins, 0); slowP.assign ((size_t) numBins, 0);
    noiseFloor.assign ((size_t) numBins, 0); logP.assign ((size_t) numBins, -200);
    prefix.assign ((size_t) numBins + 1, 0.0);
    gHarshDb.assign ((size_t) numBins, 0); gFizz.assign ((size_t) numBins, 1); grDisplay.assign ((size_t) numBins, 0);

    // inviluppo spettrale locale: media del livello in dB su circa un'ottava (±0,5 ott), minimo ±3 bin
    envLo.resize ((size_t) numBins); envHi.resize ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
    {
        const int lo = (int) std::floor ((float) k / std::sqrt (2.0f));
        const int hi = (int) std::ceil ((float) k * std::sqrt (2.0f));
        envLo[(size_t) k] = juce::jlimit (0, numBins - 1, std::min (lo, k - 3));
        envHi[(size_t) k] = juce::jlimit (0, numBins - 1, std::max (hi, k + 3));
    }

    // bande per l'interfaccia
    bandOfBin.assign ((size_t) numBins, -1);
    for (int k = 1; k < numBins; ++k)
    {
        const float f = binFreq (k);
        if (f < 20.0f || f > 20000.0f) continue;
        const int b = (int) (std::log2 (f / 20.0f) / std::log2 (1000.0f) * (float) numBands);
        bandOfBin[(size_t) k] = juce::jlimit (0, numBands - 1, b);
    }

    const double hopSeconds = (double) hop / sampleRate;
    minBlockFrames = std::max (4, (int) std::round (0.75 / hopSeconds));
    curMin.assign ((size_t) numBins, std::numeric_limits<float>::max());
    prevMin.assign ((size_t) numBins, std::numeric_limits<float>::max());

    reset();
}

void SpectralCleaner::reset()
{
    std::fill (inM.begin(), inM.end(), 0.0f); std::fill (inS.begin(), inS.end(), 0.0f);
    std::fill (outM.begin(), outM.end(), 0.0f); std::fill (outS.begin(), outS.end(), 0.0f);
    std::fill (fastP.begin(), fastP.end(), 0.0f); std::fill (slowP.begin(), slowP.end(), 0.0f);
    std::fill (noiseFloor.begin(), noiseFloor.end(), 0.0f);
    std::fill (curMin.begin(), curMin.end(), std::numeric_limits<float>::max());
    std::fill (prevMin.begin(), prevMin.end(), std::numeric_limits<float>::max());
    minFrames = 0;
    std::fill (gHarshDb.begin(), gHarshDb.end(), 0.0f); std::fill (gFizz.begin(), gFizz.end(), 1.0f);
    writePos = 0; hopCount = 0; deessDb = 0; deessRatio = 0; mudDb = 0;
    for (auto& b : bandIn) b.store (-120.0f);
    for (auto& b : bandOut) b.store (-120.0f);
    for (auto& b : bandGr) b.store (0.0f);
    deessGrDb.store (0);
}

void SpectralCleaner::process (float* left, float* right, int numSamples, const CleanSettings& s)
{
    cur = s;
    const int mask = fftSize - 1;
    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        inM[(size_t) writePos] = 0.5f * (l + r);
        inS[(size_t) writePos] = 0.5f * (l - r);

        const float m = outM[(size_t) writePos], sd = outS[(size_t) writePos];
        outM[(size_t) writePos] = 0; outS[(size_t) writePos] = 0;
        left[i] = m + sd;
        right[i] = m - sd;

        writePos = (writePos + 1) & mask;
        if (++hopCount == hop)
        {
            hopCount = 0;
            processFrame();
        }
    }
}

void SpectralCleaner::processFrame()
{
    const int N = fftSize, mask = N - 1;
    for (int j = 0; j < N; ++j)
    {
        const int idx = (writePos + j) & mask;          // writePos punta al campione più vecchio
        bufM[(size_t) j] = inM[(size_t) idx] * window[(size_t) j];
        bufS[(size_t) j] = inS[(size_t) idx] * window[(size_t) j];
    }
    std::fill (bufM.begin() + N, bufM.end(), 0.0f);
    std::fill (bufS.begin() + N, bufS.end(), 0.0f);
    fft->performRealOnlyForwardTransform (bufM.data(), true);
    fft->performRealOnlyForwardTransform (bufS.data(), true);

    const CleanSettings s = cur;

    // ---- livelli per bin e rumore di fondo
    double eBand = 0, eTotal = 0, eMud = 0, eRef = 0;
    for (int k = 0; k < numBins; ++k)
    {
        const float mr = bufM[(size_t) (2 * k)], mi = bufM[(size_t) (2 * k + 1)];
        const float sr_ = bufS[(size_t) (2 * k)], si = bufS[(size_t) (2 * k + 1)];
        const float pm = mr * mr + mi * mi;
        const float p = pm + sr_ * sr_ + si * si + 1e-20f;

        fastP[(size_t) k] = 0.5f * fastP[(size_t) k] + 0.5f * p;
        slowP[(size_t) k] = 0.85f * slowP[(size_t) k] + 0.15f * p;
        logP[(size_t) k] = 10.0f * std::log10 (fastP[(size_t) k] + 1e-20f);
        prefix[(size_t) k + 1] = prefix[(size_t) k] + (double) fastP[(size_t) k];

        // rumore di fondo: minimo di slowP su una finestra mobile di ~1,5 s (due blocchi), con correzione di bias
        auto& cm = curMin[(size_t) k];
        cm = std::min (cm, slowP[(size_t) k]);
        noiseFloor[(size_t) k] = 1.5f * std::min (cm, prevMin[(size_t) k]);

        const float f = binFreq (k);
        if (f >= 80.0f && f <= 16000.0f) eTotal += pm;
        if (f >= 4500.0f && f <= 10000.0f) eBand += pm;
        if (f >= 150.0f && f < 500.0f) eMud += p;
        if (f >= 500.0f && f < 2000.0f) eRef += p;
    }

    // ---- MUD: bilancio medio-bassi (150-500 Hz) contro medi (500-2000 Hz)
    {
        float target = 0;
        if (s.mud > 0.001f && eRef > 1e-12)
        {
            const float ratioDb = 10.0f * (float) std::log10 (eMud / eRef + 1e-12);
            const float thrDb = juce::jmap (s.mud, 10.0f, 4.0f);
            target = juce::jlimit (0.0f, 4.0f, (ratioDb - thrDb) * 0.5f);
        }
        mudDb = target > mudDb ? 0.7f * mudDb + 0.3f * target : 0.92f * mudDb + 0.08f * target;
    }

    if (++minFrames >= minBlockFrames)
    {
        minFrames = 0;
        prevMin.swap (curMin);
        std::fill (curMin.begin(), curMin.end(), std::numeric_limits<float>::max());
    }

    // ---- de-esser: quota di energia della banda sibilante sul totale del mid
    {
        const float ratio = eTotal > 1e-12 ? (float) (eBand / eTotal) : 0.0f;
        deessRatio = ratio > deessRatio ? 0.5f * deessRatio + 0.5f * ratio : 0.8f * deessRatio + 0.2f * ratio;
        float target = 0;
        if (s.deess > 0.001f)
        {
            const float rMax = juce::jmap (s.deess, 0.35f, 0.07f);
            if (deessRatio > rMax)
                target = std::min (12.0f, 10.0f * std::log10 (deessRatio / rMax) * (1.0f + 2.0f * s.deess));
        }
        deessDb = target > deessDb ? 0.4f * deessDb + 0.6f * target : 0.8f * deessDb + 0.2f * target;
        deessGrDb.store (-deessDb);
    }

    const float thr = 10.0f - 8.0f * s.sens;            // dB sopra l'inviluppo locale
    const float fizzFloor = dbToGain (-15.0f * s.fizz);
    const float alpha = 1.0f + 3.0f * s.fizz;

    std::array<float, numBands> accIn {}, accOut {};
    std::array<int, numBands> cnt {};

    for (int k = 0; k < numBins; ++k)
    {
        const float f = std::max (binFreq (k), 1.0f);

        // HARSH / MUD dinamici: picchi sopra l'inviluppo locale
        const int lo = envLo[(size_t) k], hi = envHi[(size_t) k];
        // inviluppo = potenza media su circa un'ottava: sporgono solo le risonanze isolate, non le serie armoniche
        const double envP = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (double) (hi - lo + 1);
        const float excess = logP[(size_t) k] - 10.0f * (float) std::log10 (envP + 1e-20) - thr;
        const float w = bump (f, 700, 1500, 9000, 14000) * s.harsh;
        const float targetDb = (excess > 0 && w > 0) ? -std::min (excess * w * 0.9f, 15.0f) : 0.0f;
        auto& gh = gHarshDb[(size_t) k];
        gh = targetDb < gh ? 0.35f * gh + 0.65f * targetDb : 0.8f * gh + 0.2f * targetDb;

        // FIZZ: sottrazione spettrale sopra FIZZ FREQ
        const float wf = s.fizz > 0.001f ? ramp (f, s.fizzFreq * 0.7f, s.fizzFreq) : 0.0f;
        float gfTarget = 1.0f;
        if (wf > 0)
        {
            const float snr = slowP[(size_t) k] / std::max (noiseFloor[(size_t) k], 1e-20f);
            const float g = std::max (fizzFloor, 1.0f - alpha / std::max (snr, 1e-6f));
            gfTarget = 1.0f - wf * (1.0f - g);
        }
        auto& gf = gFizz[(size_t) k];
        gf = gfTarget < gf ? 0.5f * gf + 0.5f * gfTarget : 0.85f * gf + 0.15f * gfTarget;

        // statici
        const float mudStatic = -(1.5f * s.mud + mudDb) * bell (f, 300.0f, 0.8f);
        const float vocalDb = 3.0f * s.vocal * bell (f, 2600.0f, 0.9f);
        const float airDb = 3.5f * s.air * ramp (f, 7000.0f, 12000.0f);
        const float deessHere = -deessDb * bump (f, 3500, 5000, 9000, 12000);

        const float gCommon = dbToGain (gh + mudStatic + airDb + deessHere) * gf;
        const float gM = gCommon * dbToGain (vocalDb);
        const float sideTame = 1.0f - 0.6f * s.fizz * ramp (f, s.fizzFreq * 0.5f, s.fizzFreq * 0.8f);
        const float mono = s.bassMono > 1.0f ? ramp (f, s.bassMono * 0.7f, s.bassMono * 1.4f) : 1.0f;
        const float gS = gCommon * sideTame * mono;

        const float mr = bufM[(size_t) (2 * k)], mi = bufM[(size_t) (2 * k + 1)];
        const float sr_ = bufS[(size_t) (2 * k)], si = bufS[(size_t) (2 * k + 1)];
        const float pIn = mr * mr + mi * mi + sr_ * sr_ + si * si;
        const float pOut = (mr * mr + mi * mi) * gM * gM + (sr_ * sr_ + si * si) * gS * gS;

        bufM[(size_t) (2 * k)] = mr * gM; bufM[(size_t) (2 * k + 1)] = mi * gM;
        bufS[(size_t) (2 * k)] = sr_ * gS; bufS[(size_t) (2 * k + 1)] = si * gS;

        const int b = bandOfBin[(size_t) k];
        if (b >= 0) { accIn[(size_t) b] += pIn; accOut[(size_t) b] += pOut; ++cnt[(size_t) b]; }
    }

    // metà negativa dello spettro: coniugata (vale per tutti i motori FFT di JUCE)
    for (int k = 1; k < N / 2; ++k)
    {
        bufM[(size_t) (2 * (N - k))] = bufM[(size_t) (2 * k)];  bufM[(size_t) (2 * (N - k) + 1)] = -bufM[(size_t) (2 * k + 1)];
        bufS[(size_t) (2 * (N - k))] = bufS[(size_t) (2 * k)];  bufS[(size_t) (2 * (N - k) + 1)] = -bufS[(size_t) (2 * k + 1)];
    }

    fft->performRealOnlyInverseTransform (bufM.data());
    fft->performRealOnlyInverseTransform (bufS.data());

    for (int j = 0; j < N; ++j)
    {
        const int idx = (writePos + j) & mask;
        const float w = window[(size_t) j] * olaScale;
        outM[(size_t) idx] += bufM[(size_t) j] * w;
        outS[(size_t) idx] += bufS[(size_t) j] * w;
    }

    // ---- interfaccia: bande vuote (in basso con FFT corta) prendono il valore della vicina
    const float norm = 4.0f / ((float) N * (float) N);
    float lastIn = -120, lastOut = -120;
    for (int b = 0; b < numBands; ++b)
    {
        if (cnt[(size_t) b] > 0)
        {
            lastIn = 10.0f * std::log10 (accIn[(size_t) b] / (float) cnt[(size_t) b] * norm + 1e-12f);
            lastOut = 10.0f * std::log10 (accOut[(size_t) b] / (float) cnt[(size_t) b] * norm + 1e-12f);
        }
        bandIn[(size_t) b].store (lastIn);
        bandOut[(size_t) b].store (lastOut);
        bandGr[(size_t) b].store (juce::jlimit (-24.0f, 6.0f, lastOut - lastIn));
    }
    // le prime bande (sotto il primo bin) prendono il valore della prima banda piena
    int first = 0;
    while (first < numBands && cnt[(size_t) first] == 0) ++first;
    for (int b = 0; b < first && first < numBands; ++b)
    {
        bandIn[(size_t) b].store (bandIn[(size_t) first].load());
        bandOut[(size_t) b].store (bandOut[(size_t) first].load());
        bandGr[(size_t) b].store (bandGr[(size_t) first].load());
    }
}
}
