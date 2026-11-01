#pragma once
// Copiato da meteoraFET1179 (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace fet1179.
// Biquad RBJ semplice: coefficienti e stato fissi, nessuna allocazione.
#include <cmath>

namespace fet1179
{

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    void set (double nb0, double nb1, double nb2, double a0, double na1, double na2)
    { b0 = nb0 / a0; b1 = nb1 / a0; b2 = nb2 / a0; a1 = na1 / a0; a2 = na2 / a0; }
    void peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highShelf (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w), sA = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }
    void highPass (double sr, double f, double q)
    {
        const double w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowPass (double sr, double f, double q)
    {
        const double w = 2.0 * pi * f / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    double proc (double x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    static constexpr double pi = 3.14159265358979323846;
};

// Crossover Linkwitz-Riley 24 dB/ott: basso + alto risommati danno di nuovo il segnale intero
// (stessa ampiezza, solo una rotazione di fase), quindi dividere e riunire non filtra il suono.
struct LR4
{
    Biquad lp1, lp2, hp1, hp2;
    void set (double sr, double f)
    {
        lp1.lowPass (sr, f, 0.7071); lp2.lowPass (sr, f, 0.7071);
        hp1.highPass (sr, f, 0.7071); hp2.highPass (sr, f, 0.7071);
    }
    void reset() { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    void split (double x, int ch, double& lo, double& hi)
    {
        lo = lp2.proc (lp1.proc (x, ch), ch);
        hi = hp2.proc (hp1.proc (x, ch), ch);
    }
};
} // namespace fet1179
