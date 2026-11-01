#pragma once
// Saturazione del trasformatore e DC blocker, senza dipendenze da JUCE (usati anche dal test di aliasing).
#include <cmath>
#include <algorithm>

namespace lunch
{
//==============================================================================
// Saturazione del trasformatore: tanh con piccolo offset (armoniche pari + dispari).
// Pendenza 1 nell'origine. Da usare fra pre-enfasi e de-enfasi.
inline float transformerSaturate(float x)
{
    constexpr float k = 1.5f, bias = 0.08f;
    const float tb    = std::tanh(bias);
    const float unity = k * (1.0f - tb * tb);
    return (std::tanh(k * x + bias) - tb) / unity;
}

//==============================================================================
// Blocca la continua prodotta dalle non linearita' asimmetriche (~5 Hz).
struct DcBlocker
{
    void prepare(double fs) { R = 1.0 - 2.0 * 3.14159265358979323846 * 5.0 / fs; x1 = y1 = 0.0; }
    float process(float x)
    {
        const double y = (double) x - x1 + R * y1;
        x1 = x; y1 = y;
        return (float) y;
    }
    double R = 0.999, x1 = 0.0, y1 = 0.0;
};
} // namespace lunch
