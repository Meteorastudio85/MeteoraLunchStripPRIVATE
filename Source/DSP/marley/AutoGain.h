/*
    Marley - Meteora Plugins
    Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
    Vedi il file LICENSE nella cartella principale del progetto.
*/

#pragma once
// Copiato da Marley (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace marley.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Tube2Stage.h"

namespace marley
{

/*
    Autogain di Marley, in due parti:

    1) TABELLA (feed-forward, niente anelli di retroazione):
       all'avvio si misura lo stadio Tube2 per 51 posizioni del Drive e 13
       livelli d'ingresso (-60..0 dBFS RMS) con rumore rosa, e si salva il
       guadagno che riporta l'uscita alla stessa LOUDNESS dell'ingresso
       (misura K-pesata come gli LUFS: la saturazione aggiunge armoniche e il
       suono sembrerebbe piu' forte anche a parita' di RMS). In funzione si
       legge la tabella con il Drive attuale e il livello del dry.
       Il rilevatore di livello e' un RMS medio (~300 ms) e si
       "congela" nelle pause (< -55 dBFS): niente risalite o cali di volume
       quando riparte una nota dopo il silenzio.

    2) RIFINITURA sul materiale vero: la tabella e' tarata su rumore rosa,
       la musica ha spettri diversi. Un confronto lento (~1 s) tra dry e wet
       pesati K corregge il resto, al massimo +-3 dB. Anche questo si congela nelle pause.
       Il K-weighting serve SOLO per misurare: il segnale non passa da li'.
*/

//==============================================================================
// Biquad (RBJ) usato solo per le misure
struct MeasureBiquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void setHighShelf (double sr, double f, double gainDb, double q)
    {
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * 3.14159265358979323846 * f / sr;
        const double c = std::cos (w), al = std::sin (w) / (2 * q), s = 2 * std::sqrt (A) * al;
        const double a0 = (A + 1) - (A - 1) * c + s;
        b0 = A * ((A + 1) + (A - 1) * c + s) / a0;
        b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - s) / a0;
        a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - s) / a0;
    }
    void setHighPass (double sr, double f, double q)
    {
        const double w = 2 * 3.14159265358979323846 * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        const double a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0;
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};

struct KWeighting
{
    MeasureBiquad shelf, hp;
    void prepare (double sr) { shelf.setHighShelf (sr, 1681.97, 4.0, 0.7071); hp.setHighPass (sr, 38.13, 0.5003); reset(); }
    void reset() { shelf.reset(); hp.reset(); }
    inline double process (double x) { return hp.process (shelf.process (x)); }
};

//==============================================================================
struct AutoGainTable
{
    static constexpr int kDriveSteps = 51;
    static constexpr int kLevelSteps = 13;
    static constexpr double kLevelMinDb = -60.0;
    static constexpr double kLevelStepDb = 5.0;

    void build (double osRate)
    {
        if (osRate == builtRate)
            return; // stessa frequenza di prima: la tabella e' gia' pronta
        builtRate = osRate;

        constexpr int N = 12288, skip = 2048;

        // rumore rosa deterministico (filtro di Paul Kellet) + passa-basso a 16 kHz
        std::vector<double> sig ((size_t) N);
        uint32_t seed = 0x2545F491u;
        double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, lp = 0, sumSq = 0;
        const double a = std::exp (-2.0 * 3.14159265358979323846 * 16000.0 / osRate);
        for (int i = 0; i < N; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const double w = (double (seed >> 8) / 8388608.0) - 1.0;
            b0 = 0.99886 * b0 + w * 0.0555179;  b1 = 0.99332 * b1 + w * 0.0750759;
            b2 = 0.96900 * b2 + w * 0.1538520;  b3 = 0.86650 * b3 + w * 0.3104856;
            b4 = 0.55000 * b4 + w * 0.5329522;  b5 = -0.7616 * b5 - w * 0.0168980;
            const double pink = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
            b6 = w * 0.115926;
            lp = a * lp + (1.0 - a) * pink;
            sig[(size_t) i] = lp;
            if (i >= skip) sumSq += lp * lp;
        }
        const double sigRms = std::sqrt (sumSq / (N - skip));

        // loudness del segnale di prova (K-pesato, alla frequenza di oversampling)
        double inK = 0.0;
        {
            KWeighting k; k.prepare (osRate);
            for (int i = 0; i < N; ++i)
            {
                const double y = k.process (sig[(size_t) i]);
                if (i >= skip) inK += y * y;
            }
            inK = std::sqrt (inK / (N - skip)) / sigRms; // K-rms per unita' di rms
        }

        for (int d = 0; d < kDriveSteps; ++d)
        {
            const auto prm = DriveVoicing::params (d / double (kDriveSteps - 1));
            for (int l = 0; l < kLevelSteps; ++l)
            {
                const double rms = std::pow (10.0, (kLevelMinDb + kLevelStepDb * l) / 20.0);
                const double g = rms / sigRms;

                Tube2Stage st;
                st.prepare (osRate);
                KWeighting k;
                k.prepare (osRate);
                double outK = 0.0;
                for (int i = 0; i < N; ++i)
                {
                    const double y = k.process (st.process (sig[(size_t) i] * g, prm));
                    if (i >= skip) outK += y * y;
                }
                outK = std::sqrt (outK / (N - skip));
                table[(size_t) d][(size_t) l] = (inK * rms) / std::max (outK, 1.0e-12);
            }
        }
    }

    double get (double drive01, double levelDb) const
    {
        const double dp = std::clamp (drive01, 0.0, 1.0) * (kDriveSteps - 1);
        const double lp = std::clamp ((levelDb - kLevelMinDb) / kLevelStepDb, 0.0, double (kLevelSteps - 1));
        const int d0 = std::min ((int) dp, kDriveSteps - 2), l0 = std::min ((int) lp, kLevelSteps - 2);
        const double fd = dp - d0, fl = lp - l0;
        const auto& A = table[(size_t) d0];
        const auto& B = table[(size_t) d0 + 1];
        // interpolazione in dB (piu' regolare del lineare)
        auto db = [] (double v) { return std::log (v); };
        const double v = (1 - fd) * ((1 - fl) * db (A[(size_t) l0]) + fl * db (A[(size_t) l0 + 1]))
                       +      fd  * ((1 - fl) * db (B[(size_t) l0]) + fl * db (B[(size_t) l0 + 1]));
        return std::exp (v);
    }

private:
    std::array<std::array<double, kLevelSteps>, kDriveSteps> table {};
    double builtRate = 0.0;
};

//==============================================================================
class AutoGain
{
public:
    static constexpr double kGateDb   = -55.0;
    static constexpr double kTrimMaxDb = 3.0;

    void prepare (double sampleRate, double osRate, int maxChannels)
    {
        sr = sampleRate;
        table.build (osRate);
        kDry.assign ((size_t) maxChannels, {});
        kWet.assign ((size_t) maxChannels, {});
        for (auto& k : kDry) k.prepare (sampleRate);
        for (auto& k : kWet) k.prepare (sampleRate);
        atk  = std::exp (-1.0 / (0.30 * sampleRate));
        rel  = atk; // simmetrico: misura il livello medio, senza pendere verso i picchi
        kAvg = std::exp (-1.0 / (0.5 * sampleRate));
        gSm  = std::exp (-1.0 / (0.03 * sampleRate));
        reset();
    }

    void reset()
    {
        levelMs = std::pow (10.0, -18.0 / 10.0); // si parte dal livello tipico (-18 dBFS)
        dryK = wetK = 0.0;
        trim = 1.0;
        gain = -1.0;
        for (auto& k : kDry) k.reset();
        for (auto& k : kWet) k.reset();
    }

    // potenza media del dry (tra i canali) per il rilevatore di livello
    inline void detect (double dryPower)
    {
        if (dryPower < gatePow && dryPower < levelMs)
            return; // pausa: il livello resta congelato
        const double c = dryPower > levelMs ? atk : rel;
        levelMs = c * levelMs + (1.0 - c) * dryPower;
    }

    // guadagno statico: la tabella si legge una volta per sotto-blocco (n campioni),
    // poi il guadagno scorre in modo lineare campione per campione (nessun gradino).
    // Restituisce il guadagno di fine sotto-blocco; quello d'inizio e' getStaticGain().
    inline double updateStaticGain (double drive01, int n)
    {
        const double target = table.get (drive01, 10.0 * std::log10 (levelMs + 1.0e-20));
        if (gain < 0.0) gain = target;
        const double c = std::pow (gSm, (double) n);
        gain = c * gain + (1.0 - c) * target;
        return gain;
    }
    double getStaticGain (double drive01)
    {
        if (gain < 0.0) gain = table.get (drive01, 10.0 * std::log10 (levelMs + 1.0e-20));
        return gain;
    }

    // misura K-pesata (dry e wet dopo la tabella, prima della rifinitura)
    inline void measureLoudness (int ch, double dry, double wet)
    {
        const double d = kDry[(size_t) ch].process (dry);
        const double w = kWet[(size_t) ch].process (wet);
        accDry += d * d;
        accWet += w * w;
        ++accN;
    }

    // una volta per blocco: aggiorna la rifinitura (restituisce quella di fine blocco)
    double updateTrim (int n)
    {
        if (accN > 0)
        {
            const double c = std::pow (kAvg, (double) n);
            dryK = c * dryK + (1.0 - c) * (accDry / accN);
            wetK = c * wetK + (1.0 - c) * (accWet / accN);
        }
        const bool active = levelMs > gatePow && (accN > 0 && accDry / accN > gatePow * 0.1);
        accDry = accWet = 0.0;
        accN = 0;

        if (active && wetK > 1.0e-12)
        {
            const double lim = std::pow (10.0, kTrimMaxDb / 20.0);
            const double target = std::clamp (std::sqrt (dryK / wetK), 1.0 / lim, lim);
            const double a = 1.0 - std::exp (-(double) n / (0.7 * sr));
            trim += (target - trim) * a;
        }
        return trim;
    }

    double getTrim() const { return trim; }
    double getLevelDb() const { return 10.0 * std::log10 (levelMs + 1.0e-20); }

private:
    const double gatePow = std::pow (10.0, kGateDb / 10.0);

    AutoGainTable table;
    std::vector<KWeighting> kDry, kWet;
    double sr = 44100.0;
    double atk = 0, rel = 0, kAvg = 0, gSm = 0;
    double levelMs = 0.0, dryK = 0.0, wetK = 0.0, trim = 1.0, gain = -1.0;
    double accDry = 0.0, accWet = 0.0;
    int accN = 0;
};
} // namespace marley
