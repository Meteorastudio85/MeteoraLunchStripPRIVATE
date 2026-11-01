/*
    Marley - Meteora Plugins
    Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
    Vedi il file LICENSE nella cartella principale del progetto.

    Questo file contiene codice derivato da Airwindows Tube2:
    Copyright (c) 2016 airwindows (Chris Johnson), licenza MIT.
    Il testo completo della licenza MIT e' in THIRD_PARTY_NOTICES.md.
*/

#pragma once
// Copiato da Marley (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace marley.
#include <algorithm>
#include <cmath>

namespace marley
{

/*
    Tube2Stage - lo stadio valvolare di Marley.

    E' l'algoritmo di Airwindows Tube2 (Chris Johnson, licenza MIT,
    www.airwindows.com), riscritto per un canale con due differenze:

    1) il parametro "Tube" e' continuo: in Tube2 la potenza del waveshaper
       (powerfactor) va a scatti interi da 10 a 1; qui tra due potenze intere
       vicine si passa in modo graduale: |x|^(n+1) * ((1-f) + f*|x|), con
       p = n + f. Con f = 0 e' esattamente Tube2; il calcolo usa solo
       moltiplicazioni (niente pow(), molto piu' leggero per la CPU).
    2) le medie a due campioni che Tube2 usa ad alta frequenza di
       campionamento sono tolte: introducono mezzo campione di ritardo
       (quindi una fase diversa dal dry) e qui non servono, perche' lo
       stadio gira gia' in oversampling 4x a fase lineare.

    Lo "slew" dell'isteresi e' scalato sulla frequenza di lavoro, in modo che
    il carattere sia quello di Tube2 a 44,1 kHz qualunque sia il sample rate.
*/
struct Tube2Stage
{
    // parametri derivati dal Drive (uguali per tutto il blocco di campioni)
    struct Params
    {
        double inGain = 1.0;   // guadagno in ingresso (pad di Tube2 + drive)
        double power  = 5.0;   // powerfactor continuo: 10 = lineare, 1 = valvola tutta aperta
        double gainScaling = 1.0 / 6.0;
        double outScaling  = 1.2;
        double smallSignal = 1.0; // guadagno per segnali piccoli (per misurare la distorsione)
        int    powInt  = 5;       // parte intera della potenza
        double powFrac = 0.0;     // parte decimale
        double powFloor = 0.0;    // sotto questo livello il termine e' < -140 dB: si salta il calcolo
    };

    static constexpr double kHardClip = 0.52;
    static constexpr double kHardGain = 1.0 / kHardClip; // 1.923076923...

    static Params makeParams (double inGainLin, double power)
    {
        Params p;
        p.inGain = inGainLin;
        p.power  = std::clamp (power, 1.0, 10.0);
        p.gainScaling = 1.0 / (p.power + 1.0);
        p.outScaling  = 1.0 + 1.0 / p.power;
        p.smallSignal = p.inGain * p.outScaling * kHardGain;
        p.powInt   = std::min (9, (int) p.power);
        p.powFrac  = p.power - p.powInt;
        p.powFloor = std::pow (1.0e-7, 1.0 / p.power);
        return p;
    }

    void prepare (double processingRate)
    {
        slewScale = processingRate / 44100.0;
        reset();
    }

    void reset() { prevE = 0.0; }

    inline double process (double x, const Params& p)
    {
        x *= p.inGain;
        x = std::clamp (x, -1.0, 1.0);

        // ---- 1) "flatten bottom, point top": waveshaper asimmetrico (armoniche pari)
        x /= p.power;
        double sharpen = -x;
        sharpen = (sharpen > 0.0) ? 1.0 + std::sqrt (sharpen) : 1.0 - std::sqrt (-sharpen);
        x -= x * std::fabs (x) * sharpen * 0.25;
        x *= p.power;

        // ---- 2) Tube originale: la potenza allarga la zona lineare
        const double ax = std::fabs (x);
        if (ax > p.powFloor) // sotto la soglia il termine e' inudibile: si salta
        {
            double xp = ax;                       // |x|^(n+1) con sole moltiplicazioni
            for (int k = 0; k < p.powInt; ++k) xp *= ax;
            xp *= (1.0 - p.powFrac) + p.powFrac * ax;
            x -= std::copysign (xp, x) * p.gainScaling;
        }
        x *= p.outScaling;

        // ---- 3) isteresi e "spiky fuzz"
        double slew = std::clamp ((prevE - x) * slewScale, -2.0, 2.0);
        prevE = x;
        slew = (slew > 0.0) ? 1.0 + std::sqrt (slew) * 0.5 : 1.0 - std::sqrt (-slew) * 0.5;
        x -= x * std::fabs (x) * slew * p.gainScaling;
        x = std::clamp (x, -kHardClip, kHardClip);
        return x * kHardGain;
    }

private:
    double slewScale = 1.0;
    double prevE = 0.0;
};

//==============================================================================
// Come il Drive (0..1) muove lo stadio Tube2.
// Qui si regola il carattere: pad iniziale, gain massimo e apertura della valvola.
//==============================================================================
struct DriveVoicing
{
    static constexpr double kPadDb      = -6.0;  // come Tube2 con Input = 0.5
    static constexpr double kMaxDriveDb = 24.0;  // gain aggiunto a Drive 100
    static constexpr double kPowerMin   = 1.0;   // Drive 100: Tube tutto aperto
    static constexpr double kPowerMax   = 7.0;   // Drive 0: quasi lineare

    static double inGainDb (double d01) { return kPadDb + kMaxDriveDb * d01; }

    static double power (double d01)
    {
        // la valvola si "apre" piu' in fretta all'inizio della corsa del pomello
        const double t = 1.0 - std::pow (1.0 - std::clamp (d01, 0.0, 1.0), 1.6);
        return kPowerMax + (kPowerMin - kPowerMax) * t;
    }

    static Tube2Stage::Params params (double d01)
    {
        return Tube2Stage::makeParams (std::pow (10.0, inGainDb (d01) / 20.0), power (d01));
    }
};
} // namespace marley
