#pragma once
// Copiato da meteoraFET1179 (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace fet1179.
// "Tube2" di Airwindows (Chris Johnson), licenza MIT - porting fedele, senza dither.
// A = Input (pad, 0-1), B = Tube (0-1).
#include <cmath>

namespace fet1179
{

class AirwindowsTube2
{
public:
    void prepare (double sampleRate) { overallscale = sampleRate / 44100.0; pA = pB = pC = pD = pE = pF = 0.0; }
    void setParams (double A, double B)
    {
        inputPad = A;
        const double iterations = 1.0 - B;
        powerfactor = (int) ((9.0 * iterations) + 1);
        asymPad = (double) powerfactor;
        gainscaling = 1.0 / (double) (powerfactor + 1);
        outputscaling = 1.0 + (1.0 / (double) powerfactor);
    }
    void process (double& L, double& R)
    {
        if (inputPad < 1.0) { L *= inputPad; R *= inputPad; }
        if (overallscale > 1.9) { double s = L; L += pA; pA = s; L *= 0.5; s = R; R += pB; pB = s; R *= 0.5; }
        L = std::fmax (-1.0, std::fmin (1.0, L)); R = std::fmax (-1.0, std::fmin (1.0, R));
        L = asym (L); R = asym (R);
        L = tube (L); R = tube (R);
        if (overallscale > 1.9) { double s = L; L += pC; pC = s; L *= 0.5; s = R; R += pD; pD = s; R *= 0.5; }
        L = fuzz (L, pE); R = fuzz (R, pF);
    }
private:
    double asym (double s) const
    {
        s /= asymPad;
        double sharpen = -s;
        if (sharpen > 0.0) sharpen = 1.0 + std::sqrt (sharpen); else sharpen = 1.0 - std::sqrt (-sharpen);
        s -= s * std::fabs (s) * sharpen * 0.25;
        return s * asymPad;
    }
    double tube (double s) const
    {
        double factor = s;
        for (int x = 0; x < powerfactor; x++) factor *= s;
        if ((powerfactor % 2 == 1) && (s != 0.0)) factor = (factor / s) * std::fabs (s);
        factor *= gainscaling;
        s -= factor;
        return s * outputscaling;
    }
    double fuzz (double s, double& prev)
    {
        double slew = prev - s;
        if (overallscale > 1.9) { double st = s; s += prev; prev = st; s *= 0.5; } else prev = s;
        if (slew > 0.0) slew = 1.0 + (std::sqrt (slew) * 0.5); else slew = 1.0 - (std::sqrt (-slew) * 0.5);
        s -= s * std::fabs (s) * slew * gainscaling;
        if (s > 0.52) s = 0.52;
        if (s < -0.52) s = -0.52;
        return s * 1.923076923076923;
    }
    double overallscale = 1.0, inputPad = 1.0, asymPad = 5.0, gainscaling = 1.0 / 6.0, outputscaling = 1.2;
    int powerfactor = 5;
    double pA = 0, pB = 0, pC = 0, pD = 0, pE = 0, pF = 0;
};
} // namespace fet1179
