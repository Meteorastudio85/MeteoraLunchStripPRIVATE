/*
    Placebo EQ - motore DSP (indipendente da JUCE, tutto in double)
    Meteora Plugins

    Modello di un equalizzatore passivo stile Pultec EQP-1A, voicing calibrato
    sulle misure del Klark Teknik EQP-KT dell'utente (100 Hz boost/atten 3,
    BW 5, 10 kHz boost 3, atten sel 10 kHz atten 3) e sulla forma dello
    shelf misurata sul Pultec UAD (100 Hz boost 3 da solo).

    Catena:
      Low Boost (shelf 1° ordine, molto largo)
      Low Atten (shelf 1° ordine, leggermente piu' "alto" del boost -> trucco Pultec)
      High Boost (campana risonante, Q dal Bandwidth)
      High Atten (shelf 1° ordine verso il basso)
      Stadio valvolare (Drive, armoniche pari asimmetriche)
      Trasformatore d'uscita (passa-alto ~15 Hz, passa-basso ~52 kHz)
      Output

    I filtri sono "matched" (metodo di M. Vicanek): poli per invarianza
    all'impulso e zeri calcolati sul modulo del prototipo analogico.
    Cosi' le curve alte (10-16 kHz) non si schiacciano verso Nyquist a 44.1/48 kHz
    come succede con la trasformata bilineare.
*/
#pragma once
// Copiato da Placebo EQ (Meteora Plugins) senza modifiche al DSP.

#include <cmath>
#include <algorithm>

namespace placebo
{

constexpr double kPi = 3.14159265358979323846;

//==============================================================================
// Mappature manopole -> valori fisici (voicing EQP-KT)
//==============================================================================
struct Voicing
{
    // Massimi dichiarati EQP-1A
    static constexpr double lowBoostMaxDb  = 13.5;
    static constexpr double lowAttenMaxDb  = 17.5;
    static constexpr double highBoostMaxDb = 18.0;
    static constexpr double highAttenMaxDb = 16.0;

    // Esponenti di taratura (fit sulle misure a manopola "3")
    static constexpr double lowBoostTaper  = 1.61;
    static constexpr double lowAttenTaper  = 1.577;
    static constexpr double highBoostTaper = 0.836;
    static constexpr double highAttenTaper = 0.837;

    static double taper (double knob0to10, double maxDb, double expo)
    {
        const double k = std::clamp (knob0to10 / 10.0, 0.0, 1.0);
        return k <= 0.0 ? 0.0 : maxDb * std::pow (k, expo);
    }

    static double lowBoostDb  (double k) { return taper (k, lowBoostMaxDb,  lowBoostTaper); }
    static double lowAttenDb  (double k) { return taper (k, lowAttenMaxDb,  lowAttenTaper); }
    static double highBoostDb (double k) { return taper (k, highBoostMaxDb, highBoostTaper); }
    static double highAttenDb (double k) { return taper (k, highAttenMaxDb, highAttenTaper); }

    // Punto medio (geometrico) dello shelf basso in funzione di frequenza e quantita'.
    // Con poco boost lo shelf e' larghissimo (misura UAD: 100 Hz/3 -> centro ~2.3 kHz),
    // con molto boost si stringe verso il basso.
    static double lowBoostMidHz (double freqHz, double boostDb)
    {
        return freqHz * 23.0 * std::exp (-0.151 * (boostDb - 1.93));
    }

    // L'attenuazione resta piu' "alta" del boost: e' questo che crea il famoso
    // avvallamento sopra la gobba quando si usano boost e atten insieme.
    static double lowAttenMidHz (double freqHz, double attenDb)
    {
        return freqHz * 37.0 * std::exp (-0.10 * (attenDb - 2.62));
    }

    // Shelf di attenuazione alto: punto medio legato all'Atten Sel (5/10/20 kHz).
    // Fit EQP-KT a manopola 3 (-5.8 dB, sel 10k -> ~700 Hz); con tagli piu' forti
    // il punto medio sale un po', cosi' gli estremi non diventano un "tilt" totale.
    static double highAttenMidHz (double selHz, double attenDb)
    {
        return selHz * 0.0697 * std::exp (0.06 * (attenDb - 5.84));
    }

    // Bandwidth 0 (sharp) .. 10 (broad) -> Q della campana. BW 5 = 0.30 (fit EQP-KT)
    static double bandwidthToQ (double bw)
    {
        bw = std::clamp (bw, 0.0, 10.0);
        if (bw <= 5.0)
            return 1.8 * std::pow (0.30 / 1.8, bw / 5.0);
        return 0.30 * std::pow (0.17 / 0.30, (bw - 5.0) / 5.0);
    }
};

//==============================================================================
// Biquad (Direct Form II transposto, double)
//==============================================================================
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    void setIdentity() { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    inline double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    // Risposta in modulo (per test / display)
    double magnitude (double w) const
    {
        const double c1 = std::cos (w), s1 = std::sin (w);
        const double c2 = std::cos (2 * w), s2 = std::sin (2 * w);
        const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1 + b2 * s2);
        const double dr = 1 + a1 * c1 + a2 * c2,  di = -(a1 * s1 + a2 * s2);
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

//==============================================================================
// Progettazione filtri "matched"
//==============================================================================
namespace design
{
    // Primo ordine analogico generico:  H(s) = (g0 + gInf * s/wp) / (1 + s/wp)
    // poleHz = frequenza del polo, g0 = guadagno a DC, gInf = guadagno ad alta frequenza
    inline void firstOrder (Biquad& bq, double sampleRate, double poleHz, double g0, double gInf)
    {
        const double wp = 2.0 * kPi * poleHz;
        const double p  = std::exp (-wp / sampleRate);          // polo per invarianza all'impulso
        const double a1 = -p;

        // Modulo analogico a Nyquist
        const double wn = kPi * sampleRate / wp;                 // omega_nyq / wp
        const double hnRe = g0 + gInf * wn * wn;                 // (g0 + j gInf wn)(1 - j wn) / (1 + wn^2)
        const double hnIm = (gInf - g0) * wn;
        const double hn = std::sqrt (hnRe * hnRe + hnIm * hnIm) / (1.0 + wn * wn);

        const double sum  = g0 * (1.0 + a1);                     // b0 + b1  (match a DC)
        const double diff = hn * (1.0 - a1);                     // b0 - b1  (match a Nyquist)

        bq.b0 = 0.5 * (sum + diff);
        bq.b1 = 0.5 * (sum - diff);
        bq.b2 = 0.0;
        bq.a1 = a1;
        bq.a2 = 0.0;
    }

    // Low shelf 1° ordine: guadagno g (lineare) sotto, 1 sopra; fmHz = punto medio geometrico
    inline void lowShelf1 (Biquad& bq, double sr, double fmHz, double gainDb)
    {
        if (std::abs (gainDb) < 1.0e-4) { bq.setIdentity(); return; }
        const double g = std::pow (10.0, gainDb / 20.0);
        firstOrder (bq, sr, fmHz / std::sqrt (g), g, 1.0);
    }

    // High shelf 1° ordine: 1 sotto, g sopra
    inline void highShelf1 (Biquad& bq, double sr, double fmHz, double gainDb)
    {
        if (std::abs (gainDb) < 1.0e-4) { bq.setIdentity(); return; }
        const double g = std::pow (10.0, gainDb / 20.0);
        firstOrder (bq, sr, fmHz * std::sqrt (g), 1.0, g);
    }

    // Campana (peaking) con prototipo: (s^2 + s*A/Q*w0 + w0^2) / (s^2 + s/(A*Q)*w0 + w0^2)
    inline void peak (Biquad& bq, double sr, double f0, double q, double gainDb)
    {
        if (std::abs (gainDb) < 1.0e-4) { bq.setIdentity(); return; }

        f0 = std::min (f0, 0.49 * sr);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * f0 / sr;                    // rad/campione
        const double zeta = 1.0 / (2.0 * A * q);

        // Poli per invarianza all'impulso
        double a1;
        if (zeta <= 1.0)
            a1 = -2.0 * std::exp (-zeta * w0) * std::cos (std::sqrt (1.0 - zeta * zeta) * w0);
        else
            a1 = -2.0 * std::exp (-zeta * w0) * std::cosh (std::sqrt (zeta * zeta - 1.0) * w0);
        const double a2 = std::exp (-2.0 * zeta * w0);

        // Modulo^2 del prototipo analogico a frequenza normalizzata x = w/w0
        auto analogMag2 = [A, q] (double x)
        {
            const double re = 1.0 - x * x;
            const double ni = x * A / q, di = x / (A * q);
            return (re * re + ni * ni) / (re * re + di * di);
        };

        auto phis = [] (double w, double& p0, double& p1, double& p2)
        {
            const double s = std::sin (0.5 * w);
            p1 = s * s;
            p0 = 1.0 - p1;
            p2 = 4.0 * p0 * p1;
        };

        const double A0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
        const double A1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
        const double A2 = -4.0 * a2;

        const double B0 = A0;                                         // |H(0)| = 1
        const double B1 = A1 * analogMag2 (kPi / w0);                 // Nyquist

        double p0, p1, p2;
        phis (w0, p0, p1, p2);
        const double R  = analogMag2 (1.0);                           // = A^4 al centro
        double B2 = (R * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;

        const double sB0 = std::sqrt (B0), sB1 = std::sqrt (B1);
        const double W = 0.5 * (sB0 + sB1);
        double disc = W * W + B2;
        if (disc < 0.0) disc = 0.0;

        bq.b0 = 0.5 * (W + std::sqrt (disc));
        bq.b1 = 0.5 * (sB0 - sB1);
        bq.b2 = (bq.b0 != 0.0) ? -B2 / (4.0 * bq.b0) : 0.0;
        bq.a1 = a1;
        bq.a2 = a2;
    }
} // namespace design

//==============================================================================
// Parametri (valori "da pannello")
//==============================================================================
struct Params
{
    double lowBoost  = 0.0;     // 0..10
    double lowAtten  = 0.0;     // 0..10
    double lowFreqHz = 100.0;   // 20/30/60/100
    double highBoost = 0.0;     // 0..10
    double bandwidth = 5.0;     // 0..10 (sharp..broad)
    double highFreqHz = 10000.0;// 3k..16k
    double highAtten = 0.0;     // 0..10
    double attenSelHz = 10000.0;// 5k/10k/20k
    double eqIn = 1.0;          // 0..1 (sfumato, IN switch)
};

//==============================================================================
// Canale EQ (lineare)
//==============================================================================
class EqChannel
{
public:
    void reset()
    {
        lowBoost.reset(); lowAtten.reset(); highBoost.reset(); highAtten.reset();
        xfHp.reset(); xfLp.reset();
    }

    inline double process (double x)
    {
        x = lowBoost.process (x);
        x = lowAtten.process (x);
        x = highBoost.process (x);
        x = highAtten.process (x);
        return x;
    }

    inline double transformer (double x)
    {
        return xfLp.process (xfHp.process (x));
    }

    Biquad lowBoost, lowAtten, highBoost, highAtten;
    Biquad xfHp, xfLp;
};

// Calcola i coefficienti dell'EQ (uguali per tutti i canali)
inline void computeEq (const Params& p, double sr, EqChannel& c)
{
    const double in = std::clamp (p.eqIn, 0.0, 1.0);

    const double lbDb = Voicing::lowBoostDb (p.lowBoost) * in;
    const double laDb = Voicing::lowAttenDb (p.lowAtten) * in;
    const double hbDb = Voicing::highBoostDb (p.highBoost) * in;
    const double haDb = Voicing::highAttenDb (p.highAtten) * in;

    // il punto medio dipende dalla quantita' "di pannello" (non sfumata dall'IN)
    design::lowShelf1 (c.lowBoost, sr, Voicing::lowBoostMidHz (p.lowFreqHz, Voicing::lowBoostDb (p.lowBoost)), lbDb);
    design::lowShelf1 (c.lowAtten, sr, Voicing::lowAttenMidHz (p.lowFreqHz, Voicing::lowAttenDb (p.lowAtten)), -laDb);
    design::peak      (c.highBoost, sr, p.highFreqHz, Voicing::bandwidthToQ (p.bandwidth), hbDb);
    design::highShelf1(c.highAtten, sr, Voicing::highAttenMidHz (p.attenSelHz, Voicing::highAttenDb (p.highAtten)), -haDb);
}

// Trasformatore d'uscita: leggero taglio sub (~15 Hz) e lieve caduta in cima
// (misura EQP-KT a riposo: circa -0.6 dB a 20 kHz)
inline void computeTransformer (double sr, EqChannel& c)
{
    design::firstOrder (c.xfHp, sr, 15.0, 0.0, 1.0);
    design::firstOrder (c.xfLp, sr, 52000.0, 1.0, 0.0);
}

//==============================================================================
// Stadio valvolare (Drive): curva tanh polarizzata -> armoniche pari,
// guadagno per piccoli segnali = 1 (il Drive non alza il volume, aggiunge colore).
//==============================================================================
struct TubeStage
{
    static inline double process (double x, double drive01)
    {
        if (drive01 <= 1.0e-5)
            return x;

        constexpr double headroom = 0.25;          // 0 dBFS entra nella valvola a 0.25
        constexpr double bias = 0.22;              // asimmetria (griglia)
        const double pre = 1.0 + 7.0 * drive01 * drive01;
        const double tb = std::tanh (bias);
        const double slope = 1.0 - tb * tb;        // derivata in zero

        const double u = x * headroom * pre;
        const double shaped = (std::tanh (u + bias) - tb) / (slope * headroom * pre);

        const double mix = std::min (1.0, drive01 * 5.0);   // ingresso morbido della saturazione
        return x + (shaped - x) * mix;
    }
};

} // namespace placebo
