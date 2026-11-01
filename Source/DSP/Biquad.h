#pragma once
#include <cmath>
#include <algorithm>

// Biquad TDF2 senza allocazioni (doppia precisione): i coefficienti si possono cambiare
// in sicurezza da thread audio (formule RBJ Audio EQ Cookbook).
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    // coefficienti e stato in doppia precisione: con filtri molto bassi (20 Hz a 96 kHz) i float non bastano
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    float process(float xf)
    {
        const double x = xf;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void setLowShelf(double fs, double f0, double gainDb, double q = 0.7071)
    {
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double tsa   = 2.0 * std::sqrt(A) * alpha;

        set(A * ((A + 1) - (A - 1) * cs + tsa),
            2.0 * A * ((A - 1) - (A + 1) * cs),
            A * ((A + 1) - (A - 1) * cs - tsa),
            (A + 1) + (A - 1) * cs + tsa,
            -2.0 * ((A - 1) + (A + 1) * cs),
            (A + 1) + (A - 1) * cs - tsa);
    }

    void setHighShelf(double fs, double f0, double gainDb, double q = 0.7071)
    {
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double tsa   = 2.0 * std::sqrt(A) * alpha;

        set(A * ((A + 1) + (A - 1) * cs + tsa),
            -2.0 * A * ((A - 1) + (A + 1) * cs),
            A * ((A + 1) + (A - 1) * cs - tsa),
            (A + 1) - (A - 1) * cs + tsa,
            2.0 * ((A - 1) - (A + 1) * cs),
            (A + 1) - (A - 1) * cs - tsa);
    }

    // Campana (peaking). Con gainDb = 0 il filtro e' esattamente trasparente.
    void setPeak(double fs, double f0, double gainDb, double q)
    {
        const double A     = std::pow(10.0, gainDb / 40.0);
        const double w0    = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs    = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        set(1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A);
    }

    // Passa banda con picco a 0 dB (usato come sidechain del de-esser).
    void setBandpass(double fs, double f0, double q)
    {
        const double w0 = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set(alpha, 0.0, -alpha, 1 + alpha, -2 * cs, 1 - alpha);
    }

    // Risposta in ampiezza (dB) alla frequenza f: serve ai test.
    double magnitudeDb(double fs, double f) const
    {
        const double w = 2.0 * kPi * f / fs;
        const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1 + a2 * s2);
        return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }

    void setLowpass(double fs, double f0, double q = 0.7071)
    {
        const double w0 = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
    }

    void setHighpass(double fs, double f0, double q = 0.7071)
    {
        const double w0 = 2.0 * kPi * clampFreq(fs, f0) / fs;
        const double cs = std::cos(w0), alpha = std::sin(w0) / (2.0 * q);
        set((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha);
    }

private:
    static double clampFreq(double fs, double f) { return std::clamp(f, 5.0, 0.45 * fs); }

    void set(double B0, double B1, double B2, double A0, double A1, double A2)
    {
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0;
        a1 = A1 / A0; a2 = A2 / A0;
    }
};
