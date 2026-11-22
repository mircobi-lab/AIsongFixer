#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/*  Limitatore di picco con lookahead (1,5 ms).
    Guadagno richiesto per campione -> minimo sulla finestra di lookahead -> rilascio -> media mobile
    sulla stessa lunghezza; l'audio è ritardato di (L - 1) campioni, così il guadagno è già sceso
    quando arriva il picco. Lavora sul picco dei campioni (non true-peak).                         */
namespace asf
{
class PeakLimiter
{
public:
    void prepare (double sampleRate)
    {
        L = std::max (2, (int) std::round (sampleRate * 0.0015));
        targets.assign ((size_t) L, 1.0f);
        box.assign ((size_t) L, 1.0f);
        delayL.assign ((size_t) L, 0.0f);
        delayR.assign ((size_t) L, 0.0f);
        releaseCoef = (float) std::exp (-1.0 / (0.12 * sampleRate));   // rilascio ~120 ms
        reset();
    }

    void reset()
    {
        std::fill (targets.begin(), targets.end(), 1.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        pos = 0; released = 1.0f; boxSum = (double) L; minGain = 1.0f;
    }

    int getLatency() const noexcept { return L - 1; }

    // ceilingGain lineare; active = false lascia passare l'audio (solo ritardato)
    void process (float* l, float* r, int n, float ceilingGain, bool active)
    {
        float blockMin = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float peak = std::max (std::abs (l[i]), std::abs (r[i]));
            const float target = (active && peak > ceilingGain) ? ceilingGain / peak : 1.0f;
            targets[(size_t) pos] = target;

            float m = 1.0f;
            for (float t : targets) m = std::min (m, t);

            released = m < released ? m : m + (released - m) * releaseCoef;

            boxSum += (double) released - (double) box[(size_t) pos];
            box[(size_t) pos] = released;
            const float g = std::min (1.0f, (float) (boxSum / (double) L));

            // ritardo di L-1 campioni: leggo il valore scritto L-1 campioni fa
            const int readPos = (pos + 1) % L;
            delayL[(size_t) pos] = l[i];
            delayR[(size_t) pos] = r[i];
            const float dl = delayL[(size_t) readPos], dr = delayR[(size_t) readPos];

            l[i] = active ? dl * g : dl;
            r[i] = active ? dr * g : dr;
            if (active) blockMin = std::min (blockMin, g);
            pos = (pos + 1) % L;
        }
        minGain = blockMin;
    }

    float getLastMinGain() const noexcept { return minGain; }

private:
    int L = 64, pos = 0;
    std::vector<float> targets, box, delayL, delayR;
    double boxSum = 64;
    float released = 1.0f, releaseCoef = 0.9999f, minGain = 1.0f;
};
}
