#pragma once
// Copiato da Meteora DirtyDESK (Meteora Plugins) senza modifiche.
// =============================================================================
//  Meteora Plugins - Meteora DirtyDESK
//  Utility DSP condivise (filtri, saturazioni, tolleranze, smoothing)
// =============================================================================
#include <cmath>
#include <cstdint>

namespace meteora {

constexpr double kPi = 3.14159265358979323846;

inline double clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
inline double gainToDb(double g)  { return g > 1.0e-9 ? 20.0 * std::log10(g) : -180.0; }

// Evita i denormali quando il segnale si spegne (code dei filtri)
inline double flushDenormal(double x) { return (std::fabs(x) < 1.0e-24) ? 0.0 : x; }

// Frequenza sempre valida rispetto al sample rate
inline double clampHz(double hz, double sr) { return clamp(hz, 0.5, sr * 0.45); }

// Hash deterministico 0..1 (stessa formula delle stringhe originali):
// lo stesso canale produce sempre le stesse "tolleranze dei componenti".
inline double hashSeed(int id, int salt)
{
    const double raw = std::sin(id * 12.9898 + salt * 78.233) * 43758.5453123;
    return raw - std::floor(raw);
}
inline double tolerance(int id, int salt, double spread) { return 1.0 + (hashSeed(id, salt) - 0.5) * spread; }

// Saturazione morbida limitata (Pade di tanh): monotona, |y| <= 1
inline double softSat(double x)
{
    if (x >  3.0) return  1.0;
    if (x < -3.0) return -1.0;
    const double x2 = x * x;
    return x * (27.0 + x2) / (27.0 + 9.0 * x2);
}

// Clip con "ginocchio" regolabile: knee 2 = molto morbido, 5+ = duro (circuiti integrati)
inline double kneeSat(double x, double knee)
{
    const double ax = std::fabs(x);
    if (ax < 1.0e-12) return x;
    return x / std::pow(1.0 + std::pow(ax, knee), 1.0 / knee);
}

// ---------------------------------------------------------------------------
struct OnePoleLP
{
    double a = 1.0, z = 0.0;
    void setCutoff(double hz, double sr) { a = 1.0 - std::exp(-2.0 * kPi * clampHz(hz, sr) / sr); }
    void reset() { z = 0.0; }
    double process(double x) { z += a * (x - z); z = flushDenormal(z); return z; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff(double hz, double sr) { lp.setCutoff(hz, sr); }
    void reset() { lp.reset(); }
    double process(double x) { return x - lp.process(x); }
};

// All-pass del 1° ordine: ruota la fase SOTTO la frequenza indicata
struct Allpass1
{
    double c = 0.0, x1 = 0.0, y1 = 0.0;
    void setCutoff(double hz, double sr)
    {
        const double t = std::tan(kPi * clampHz(hz, sr) / sr);
        c = (t - 1.0) / (t + 1.0);
    }
    void reset() { x1 = y1 = 0.0; }
    double process(double x)
    {
        const double y = c * x + x1 - c * y1;
        x1 = x; y1 = flushDenormal(y);
        return y;
    }
};

// Biquad RBJ (Transposed Direct Form II)
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }
    void setIdentity() { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }

    void setPeak(double f, double q, double db, double sr)
    {
        if (std::fabs(db) < 1.0e-4) { setIdentity(); return; }
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * kPi * clampHz(f, sr) / sr;
        const double cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
        normalise(1.0 + alpha * A, -2.0 * cs, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cs, 1.0 - alpha / A);
    }
    void setLowShelf(double f, double db, double sr)
    {
        if (std::fabs(db) < 1.0e-4) { setIdentity(); return; }
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * kPi * clampHz(f, sr) / sr;
        const double cs = std::cos(w), alpha = std::sin(w) / 2.0 * std::sqrt(2.0), sA = 2.0 * std::sqrt(A) * alpha;
        normalise(A * ((A + 1) - (A - 1) * cs + sA), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sA),
                  (A + 1) + (A - 1) * cs + sA, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sA);
    }
    void setHighShelf(double f, double db, double sr)
    {
        if (std::fabs(db) < 1.0e-4) { setIdentity(); return; }
        const double A = std::pow(10.0, db / 40.0), w = 2.0 * kPi * clampHz(f, sr) / sr;
        const double cs = std::cos(w), alpha = std::sin(w) / 2.0 * std::sqrt(2.0), sA = 2.0 * std::sqrt(A) * alpha;
        normalise(A * ((A + 1) + (A - 1) * cs + sA), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sA),
                  (A + 1) - (A - 1) * cs + sA, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sA);
    }
    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = flushDenormal(b1 * x - a1 * y + z2);
        z2 = flushDenormal(b2 * x - a2 * y);
        return y;
    }
private:
    void normalise(double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
};

// Smoothing dei parametri (niente "zipper noise" quando muovi le manopole)
struct Smoother
{
    double cur = 0.0, target = 0.0, k = 1.0;
    void setTime(double ms, double sr) { k = 1.0 - std::exp(-1.0 / (ms * 0.001 * sr)); }
    void setTarget(double t) { target = t; }
    void snap(double t) { cur = target = t; }
    double next() { cur += k * (target - cur); if (std::fabs(target - cur) < 1.0e-9) cur = target; return cur; }
};

} // namespace meteora
