/*
    Marley - Meteora Plugins
    Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
    Vedi il file LICENSE nella cartella principale del progetto.
*/

#pragma once
// Copiato da Marley (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace marley.
#include <array>
#include <cmath>
#include <algorithm>
#include <vector>

namespace marley
{

/*
    Oversampling 4x a FASE LINEARE (due stadi 2x con filtri FIR mezza banda
    simmetrici, finestra di Kaiser).

    Perche' non quello di JUCE con filtri IIR: gli IIR ruotano la fase in modo
    diverso a ogni frequenza, e mescolando wet e dry (Mix) questo dava un
    effetto "phaser" sugli acuti. Qui ogni frequenza e' ritardata dello stesso
    numero INTERO di campioni, quindi il dry ritardato combacia esattamente.

    Latenza totale = (kTaps1 - 1)/2 + (kTaps2 - 1)/4 campioni (= 71) in 4x,
    63 campioni in 2x (usato da 88,2 kHz in su).
*/
inline constexpr double kMarleyPi = 3.14159265358979323846;

class HalfbandFir
{
public:
    void design (int numTaps, double attenuationDb)
    {
        taps = numTaps;
        h.assign ((size_t) taps, 0.0);
        const double beta = attenuationDb > 50.0 ? 0.1102 * (attenuationDb - 8.7)
                                                 : 0.5842 * std::pow (attenuationDb - 21.0, 0.4) + 0.07886 * (attenuationDb - 21.0);
        const double c = 0.5 * (taps - 1);
        double sum = 0.0;
        for (int n = 0; n < taps; ++n)
        {
            const double m = n - c;
            const double sinc = (m == 0.0) ? 0.5 : std::sin (kMarleyPi * 0.5 * m) / (kMarleyPi * m);
            const double r = m / c;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            h[(size_t) n] = sinc * w;
            sum += h[(size_t) n];
        }
        for (auto& v : h) v /= sum; // guadagno 1 in continua

        // fasi polifase per l'interpolazione (guadagno 2: compensa gli zeri inseriti)
        ph0.clear(); ph1.clear();
        for (int n = 0; n < taps; ++n)
            ((n & 1) ? ph1 : ph0).push_back (2.0 * h[(size_t) n]);
    }

    int numTaps() const { return taps; }
    const std::vector<double>& coeffs() const { return h; }
    const std::vector<double>& phase0() const { return ph0; }
    const std::vector<double>& phase1() const { return ph1; }

    // Forma "furba" per il calcolo: in un filtro mezza banda quasi meta' dei
    // coefficienti e' esattamente zero, e quelli rimasti sono simmetrici.
    // Si tengono solo le coppie non nulle: c * (x[a] + x[b]). Stesso risultato,
    // circa un quarto delle moltiplicazioni.
    struct Pair { int a, b; double c; };

    // coppie per il filtro intero (decimazione), indici sullo storico ad alta frequenza
    std::vector<Pair> foldedFull() const { return fold (h, 1.0, -1); }
    // coppie per una fase polifase (interpolazione), indici sullo storico a bassa frequenza
    std::vector<Pair> foldedPhase (int phase) const { return fold (h, 2.0, phase); }

private:
    std::vector<Pair> fold (const std::vector<double>& c, double gain, int phase) const
    {
        std::vector<Pair> out;
        const double eps = 1.0e-14 * std::fabs (c[(size_t) (taps / 2)]);
        for (int n = 0; n < taps; ++n)
        {
            const int m = taps - 1 - n; // tap speculare (stesso coefficiente)
            if (m < n) break;
            if (std::fabs (c[(size_t) n]) <= eps) continue; // zero esatto del mezza banda
            if (phase >= 0 && (n & 1) != phase) continue;
            const int a = (phase >= 0 ? n >> 1 : n);
            const int b = (phase >= 0 ? m >> 1 : m);
            out.push_back ({ a, b, gain * c[(size_t) n] * (m == n ? 0.5 : 1.0) });
        }
        return out;
    }

    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < 1.0e-12 * sum) break;
        }
        return sum;
    }

    int taps = 0;
    std::vector<double> h, ph0, ph1;
};

// Storico circolare "doppio": l'ultimo campione e' sempre data()[0], il precedente data()[1]...
class History
{
public:
    void setSize (int n) { size = std::max (1, n); buf.assign ((size_t) (2 * size), 0.0); pos = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0); pos = 0; }
    inline void push (double x)
    {
        pos = (pos == 0 ? size - 1 : pos - 1);
        buf[(size_t) pos] = x;
        buf[(size_t) (pos + size)] = x;
    }
    inline double at (int k) const { return buf[(size_t) (pos + k)]; }

    // somma delle coppie simmetriche c * (x[-a] + x[-b])
    inline double dot (const std::vector<HalfbandFir::Pair>& pairs) const
    {
        const double* d = buf.data() + pos;
        double acc0 = 0.0, acc1 = 0.0;
        const size_t n = pairs.size();
        size_t k = 0;
        for (; k + 1 < n; k += 2)
        {
            acc0 += pairs[k].c     * (d[pairs[k].a]     + d[pairs[k].b]);
            acc1 += pairs[k + 1].c * (d[pairs[k + 1].a] + d[pairs[k + 1].b]);
        }
        if (k < n) acc0 += pairs[k].c * (d[pairs[k].a] + d[pairs[k].b]);
        return acc0 + acc1;
    }
private:
    std::vector<double> buf;
    int size = 1, pos = 0;
};

// Un canale: 1 campione in ingresso -> 4 (o 2) campioni in oversampling -> 1 in uscita.
// A 88,2 kHz e oltre basta il 2x (stessa qualita' del 4x a 44,1/48 kHz, meta' CPU).
class LinearPhaseOversampler4x
{
public:
    static constexpr int kMaxFactor = 4;
    static constexpr int kTaps1  = 127;  // 1x <-> 2x (filtro stretto, piatto fino a 20 kHz)
    static constexpr int kTaps2  = 33;   // 2x <-> 4x (filtro largo)
    static constexpr int kLatency4x = (kTaps1 - 1) / 2 + (kTaps2 - 1) / 4; // 71
    static constexpr int kLatency2x = (kTaps1 - 1) / 2;                    // 63

    void setFactor (int f) { factor = (f == 2 ? 2 : 4); reset(); }
    int getFactor() const { return factor; }
    int getLatency() const { return factor == 4 ? kLatency4x : kLatency2x; }

    LinearPhaseOversampler4x()
    {
        HalfbandFir f1, f2;
        f1.design (kTaps1, 100.0);
        f2.design (kTaps2, 100.0);
        up1[0] = f1.foldedPhase (0); up1[1] = f1.foldedPhase (1);
        up2[0] = f2.foldedPhase (0); up2[1] = f2.foldedPhase (1);
        dn1 = f1.foldedFull();
        dn2 = f2.foldedFull();
        upHist1.setSize ((kTaps1 + 1) / 2);
        upHist2.setSize ((kTaps2 + 1) / 2);
        dnHist1.setSize (kTaps1);
        dnHist2.setSize (kTaps2);
    }

    void reset() { upHist1.clear(); upHist2.clear(); dnHist2.clear(); dnHist1.clear(); }

    // 1 -> 4 (o 1 -> 2)
    inline void up (double x, double* out4)
    {
        upHist1.push (x);
        const double a0 = upHist1.dot (up1[0]);
        const double a1 = upHist1.dot (up1[1]);
        if (factor == 2) { out4[0] = a0; out4[1] = a1; return; }
        upHist2.push (a0);
        out4[0] = upHist2.dot (up2[0]);
        out4[1] = upHist2.dot (up2[1]);
        upHist2.push (a1);
        out4[2] = upHist2.dot (up2[0]);
        out4[3] = upHist2.dot (up2[1]);
    }

    // 4 -> 1 o 2 -> 1 (si tiene il campione di fase pari: ritardo intero)
    inline double down (const double* in4)
    {
        if (factor == 2)
        {
            dnHist1.push (in4[0]);
            const double y = dnHist1.dot (dn1);
            dnHist1.push (in4[1]);
            return y;
        }
        dnHist2.push (in4[0]);
        const double b0 = dnHist2.dot (dn2);
        dnHist2.push (in4[1]);
        dnHist2.push (in4[2]);
        const double b1 = dnHist2.dot (dn2);
        dnHist2.push (in4[3]);

        dnHist1.push (b0);
        const double y = dnHist1.dot (dn1);
        dnHist1.push (b1);
        return y;
    }

private:
    std::vector<HalfbandFir::Pair> up1[2], up2[2], dn1, dn2;
    History upHist1, upHist2, dnHist2, dnHist1;
    int factor = 4;
};
} // namespace marley
