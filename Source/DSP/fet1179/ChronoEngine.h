#pragma once
// Copiato da meteoraFET1179 (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace fet1179.
// ---------------------------------------------------------------------------
// Catena del Chrono-Limiter (Meteora Plugins):
// INPUT -> Coils2 (ingresso, leggero) -> Logical4 (FET) ->
// [Pultec fisso + Tube2, con il suo blend] -> Coils2 (uscita) -> OUTPUT -> DRY/WET master
// HPF/LPF: delimitano la banda su cui lavora il compressore. Sotto l'HPF e sopra l'LPF il suono
// passa intero senza compressione; la banda in mezzo viene compressa; poi tutto viene risommato.
// Nessuna allocazione durante l'elaborazione: tutti i filtri e gli stati sono membri fissi.
// ---------------------------------------------------------------------------
#include "AirwindowsCoils2.h"
#include "AirwindowsLogical4.h"
#include "AirwindowsTube2.h"
#include "Biquad.h"
#include <cmath>
#include <algorithm>

namespace fet1179
{


class ChronoEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        coilsIn.prepare (sr); coilsOut.prepare (sr); comp.prepare (sr); tube.prepare (sr);
        eqPeak.reset(); eqShelf.reset(); eqDip.reset();
        xH.reset(); xL.reset(); dH.reset(); dL.reset();
        // curva Pultec fissa: +2.8 dB a 5 kHz (Q 0.8) + shelf alto +2.5 dB da 5 kHz con la piccola
        // "gobba" degli shelf analogici (Q 1.0), -0.4 dB a 1.2 kHz. Bassi invariati.
        eqPeak.peak (sr, 5000.0, 0.8, 2.8);
        eqShelf.highShelf (sr, 5000.0, 1.0, 2.5);
        eqDip.peak (sr, 1200.0, 1.0, -0.4);
        coilsIn.setParams (0.15, 0.0, 1.0);          // trasformatore d'ingresso: molto leggero, Cheapness 0
        tube.setParams (0.52, 0.5);                  // Tube2: pad e "Tube" a meta'
        preE = postE = 0.0;
    }

    // ratio: 0 = 4:1, 1 = 8:1, 2 = 12:1, 3 = 20:1, 4 = tutti i tasti premuti
    void setParams (double inDb, double hpV, double lpV, int ratio, double atkV, double relV,
                    double color01, double outDb, double mix01)
    {
        inG = std::pow (10.0, inDb / 20.0);
        outG = std::pow (10.0, outDb / 20.0);
        color = std::clamp (color01, 0.0, 1.0);
        mix = std::clamp (mix01, 0.0, 1.0);
        // HPF e LPF: il compressore lavora solo tra le due frequenze, il resto passa senza compressione
        if (! hpOn && hpV >= 0.02) { xH.reset(); dH.reset(); }
        if (! lpOn && lpV >= 0.02) { xL.reset(); dL.reset(); }
        hpOn = hpV >= 0.02; lpOn = lpV >= 0.02;
        if (hpOn) { const double f = std::min (20.0 * std::pow (1000.0, (hpV - 0.02) / 0.98), sr * 0.45); xH.set (sr, f); dH.set (sr, f); }
        if (lpOn) { const double f = std::min (20000.0 * std::pow (0.001, (lpV - 0.02) / 0.98), sr * 0.45); xL.set (sr, f); dL.set (sr, f); }
        ratio = std::clamp (ratio, 0, 4);
        static const double Bs[5] = { 0.4472136, 0.6831301, 0.8563488, 1.0, 1.0 };
        allButtons = ratio == 4;
        comp.setParams (0.6, Bs[ratio], 0.5, atkRem (atkV), relRem (relV));   // soglia interna un po' piu' alta: INPUT a 0 dB comprime con misura
        coilsOut.setParams (allButtons ? 0.45 : 0.35, allButtons ? 0.40 : 0.32, 1.0);
    }

    // velocita' interne: ATTACK e RELEASE in senso orario = piu' veloci, come sul 1176
    static double atkRem (double v) { return 0.002 * std::pow (40.0, std::clamp (v, 0.0, 1.0)); }
    static double relRem (double v) { return 0.00005 * std::pow (20.0, std::clamp (v, 0.0, 1.0)); }
    static double atkMs (double v)  { return 0.11 / atkRem (v); }     // tempi misurati con un gradino a 1 kHz
    static double relMs (double v)  { return 0.064 / relRem (v); }

    void process (float* x, double run)
    {
        const double dryL = x[0], dryR = x[1];
        double L = dryL * inG, R = dryR * inG;

        coilsIn.process (L, R);

        // divisione in bande: solo quella centrale va nel compressore
        double loL = 0, loR = 0, hiL = 0, hiR = 0, mL = L, mR = R;
        if (hpOn) { xH.split (L, 0, loL, mL); xH.split (R, 1, loR, mR); }
        if (lpOn) { double a, b; xL.split (mL, 0, a, hiL); mL = a; xL.split (mR, 1, b, hiR); mR = b; }
        preE += mL * mL + mR * mR;
        if (allButtons) { mL *= 2.0; mR *= 2.0; }     // tutti i tasti: si spinge di piu' nel FET
        comp.process (mL, mR);
        if (allButtons) { mL *= 0.5; mR *= 0.5; }
        postE += mL * mL + mR * mR;
        L = loL + mL + hiL;
        R = loR + mR + hiR;

        if (color > 0.0)
        {
            double eL = eqDip.proc (eqShelf.proc (eqPeak.proc (L, 0), 0), 0);
            double eR = eqDip.proc (eqShelf.proc (eqPeak.proc (R, 1), 1), 1);
            tube.process (eL, eR);
            eL *= tubeComp; eR *= tubeComp;
            L = L * (1.0 - color) + eL * color;
            R = R * (1.0 - color) + eR * color;
        }
        coilsOut.process (L, R);
        L *= outG; R *= outG;

        // il dry del mix master riceve la stessa rotazione di fase dei crossover: nessun buco quando si miscela
        double cL = dryL, cR = dryR;
        if (hpOn) { double a, b; dH.split (cL, 0, a, b); cL = a + b; dH.split (cR, 1, a, b); cR = a + b; }
        if (lpOn) { double a, b; dL.split (cL, 0, a, b); cL = a + b; dL.split (cR, 1, a, b); cR = a + b; }
        const double oL = cL * (1.0 - mix) + L * mix, oR = cR * (1.0 - mix) + R * mix;
        x[0] = (float) (dryL * (1.0 - run) + oL * run);
        x[1] = (float) (dryR * (1.0 - run) + oR * run);
    }

    // energia prima e dopo il compressore (per il VU in riduzione di guadagno)
    double preE = 0.0, postE = 0.0;

private:
    double sr = 44100.0, inG = 1.0, outG = 1.0, color = 0.0, mix = 1.0;
    static constexpr double tubeComp = 0.433 / 0.52;   // Tube2 riportato a guadagno unitario
    bool allButtons = false;
    Biquad eqPeak, eqShelf, eqDip;
    LR4 xH, xL, dH, dL;
    bool hpOn = false, lpOn = false;
    AirwindowsCoils2 coilsIn, coilsOut;
    AirwindowsLogical4 comp;
    AirwindowsTube2 tube;
};
} // namespace fet1179
