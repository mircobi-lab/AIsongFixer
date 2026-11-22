#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

/*  Loudness short-term (3 s) secondo ITU-R BS.1770: filtro K (mensola + passa-alto), blocchi da 100 ms.
    Coefficienti ricavati per qualsiasi frequenza di campionamento con la trasformata bilineare
    (stesse costanti analogiche usate da libebur128 e pyloudnorm).                                  */
namespace asf
{
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        const double pi = 3.14159265358979323846;
        {   // stadio 1: mensola alta +4 dB
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {   // stadio 2: passa-alto 38 Hz
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        blockLen = std::max (1, (int) (sampleRate * 0.1));
        blocks.assign (30, 0.0);
        reset();
    }

    void reset()
    {
        for (auto& st : state) st = {};
        std::fill (blocks.begin(), blocks.end(), 0.0);
        acc = 0; count = 0; blockIndex = 0; filled = 0;
    }

    void process (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const double yl = run (1, run (0, l[i], s1, 0), s2, 0);
            const double yr = run (1, run (0, r[i], s1, 1), s2, 1);
            acc += yl * yl + yr * yr;
            if (++count == blockLen)
            {
                blocks[(size_t) blockIndex] = acc / blockLen;
                blockIndex = (blockIndex + 1) % (int) blocks.size();
                filled = std::min (filled + 1, (int) blocks.size());
                acc = 0; count = 0;
            }
        }
    }

    // LUFS short-term (−70 se silenzio o dati insufficienti)
    float shortTerm() const
    {
        if (filled == 0) return -70.0f;
        double sum = 0;
        for (int i = 0; i < filled; ++i) sum += blocks[(size_t) i];
        const double ms = sum / filled;
        return ms > 1e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -70.0f;
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    double run (int stage, double x, const Biquad& c, int ch)
    {
        auto& st = state[(size_t) (stage * 2 + ch)];
        const double y = c.b0 * x + c.b1 * st.x1 + c.b2 * st.x2 - c.a1 * st.y1 - c.a2 * st.y2;
        st.x2 = st.x1; st.x1 = x; st.y2 = st.y1; st.y1 = y;
        return y;
    }

    Biquad s1 {}, s2 {};
    std::array<State, 4> state {};
    std::vector<double> blocks;
    double acc = 0;
    int count = 0, blockLen = 4410, blockIndex = 0, filled = 0;
};
}
