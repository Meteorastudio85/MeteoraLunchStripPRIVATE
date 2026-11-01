#pragma once
// Copiato da meteoraFET1179 (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace fet1179.
// "Coils2" di Airwindows (Chris Johnson), licenza MIT - porting fedele, senza dither.
// A = Saturate, B = Cheapness, C = Dry/Wet (0-1).
#include <cmath>
#include <algorithm>

namespace fet1179
{

class AirwindowsCoils2
{
public:
    void prepare (double sampleRate) { sr = sampleRate; for (auto& v : bqA) v = 0; for (auto& v : bqB) v = 0; hysL = hysR = 0; }
    void setParams (double A, double B, double C)
    {
        const double overallscale = sr / 44100.0;
        distScaling = std::max (0.0001, std::pow (1.0 - A, 2));
        bqA[0] = 600.0 / sr;
        bqA[1] = 0.01 + (std::pow (B, 2) * 0.5);
        iirAmount = bqA[1] / overallscale;
        double K = std::tan (pi * bqA[0]);
        double norm = 1.0 / (1.0 + K / bqA[1] + K * K);
        bqA[2] = K / bqA[1] * norm; bqA[4] = -bqA[2];
        bqA[5] = 2.0 * (K * K - 1.0) * norm; bqA[6] = (1.0 - K / bqA[1] + K * K) * norm;
        bqB[0] = (21890.0 - (bqA[1] * 890.0)) / sr;
        bqB[1] = 0.89;
        K = std::tan (pi * std::min (bqB[0], 0.4999));
        norm = 1.0 / (1.0 + K / bqB[1] + K * K);
        bqB[2] = K * K * norm; bqB[3] = 2.0 * bqB[2]; bqB[4] = bqB[2];
        bqB[5] = 2.0 * (K * K - 1.0) * norm; bqB[6] = (1.0 - K / bqB[1] + K * K) * norm;
        wet = C;
    }
    void process (double& L, double& R)
    {
        const double dryL = L, dryR = R;
        if (bqA[0] < 0.49999)
        {
            double t = (L * bqA[2]) + bqA[7]; bqA[7] = -(t * bqA[5]) + bqA[8]; bqA[8] = (L * bqA[4]) - (t * bqA[6]); L = t;
            t = (R * bqA[2]) + bqA[9]; bqA[9] = -(t * bqA[5]) + bqA[10]; bqA[10] = (R * bqA[4]) - (t * bqA[6]); R = t;
        }
        double dL = (dryL - L) / distScaling, dR = (dryR - R) / distScaling;
        if (bqB[0] < 0.49999)
        {
            double t = (dL * bqB[2]) + bqB[7]; bqB[7] = (dL * bqB[3]) - (t * bqB[5]) + bqB[8]; bqB[8] = (dL * bqB[4]) - (t * bqB[6]); dL = t;
            t = (dR * bqB[2]) + bqB[9]; bqB[9] = (dR * bqB[3]) - (t * bqB[5]) + bqB[10]; bqB[10] = (dR * bqB[4]) - (t * bqB[6]); dR = t;
        }
        hysL = (hysL * (1.0 - iirAmount)) + (dL * iirAmount);
        if (std::fabs (hysL) < 1.18e-37) hysL = 0.0; else dL -= hysL;
        dL = std::clamp (dL, -1.571, 1.571); hysL = std::clamp (hysL, -1.571, 1.571);
        hysR = (hysR * (1.0 - iirAmount)) + (dR * iirAmount);
        if (std::fabs (hysR) < 1.18e-37) hysR = 0.0; else dR -= hysR;
        dR = std::clamp (dR, -1.571, 1.571); hysR = std::clamp (hysR, -1.571, 1.571);
        L += (std::sin (dL) - std::sin (hysL)) * distScaling;
        R += (std::sin (dR) - std::sin (hysR)) * distScaling;
        if (wet != 1.0) { L = (L * wet) + (dryL * (1.0 - wet)); R = (R * wet) + (dryR * (1.0 - wet)); }
    }
private:
    static constexpr double pi = 3.14159265358979323846;
    double sr = 44100.0, bqA[15] {}, bqB[15] {}, hysL = 0, hysR = 0, distScaling = 1.0, iirAmount = 0.01, wet = 1.0;
};
} // namespace fet1179
