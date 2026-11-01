#pragma once
// Motori di De-Esser, Opto ed EQ, senza dipendenze da JUCE (il FET e' in fet1179/):
// li usano i moduli del plugin e il test offline Tests/dsp_test.cpp.
#include "Biquad.h"
#include "placebo/PlaceboDSP.h"
#include <algorithm>
#include <cmath>

namespace lunch
{
inline double toDb(double g)     { return 20.0 * std::log10(std::max(g, 1.0e-12)); }
inline double fromDb(double db)  { return std::pow(10.0, db * 0.05); }
inline double coefFor(double seconds, double fs) { return std::exp(-1.0 / (std::max(seconds, 1.0e-7) * fs)); }

// Curva statica con ginocchio morbido (dB): restituisce la riduzione (<= 0).
inline double softKneeGr(double xDb, double thrDb, double ratio, double kneeDb)
{
    const double over = xDb - thrDb;
    const double slope = 1.0 - 1.0 / ratio;
    if (kneeDb > 0.0 && 2.0 * std::fabs(over) <= kneeDb)
    {
        const double t = over + kneeDb * 0.5;
        return -slope * t * t / (2.0 * kneeDb);
    }
    return over > 0.0 ? -slope * over : 0.0;
}

//==============================================================================
// De-esser split-band:
//   sidechain = passa banda sulla frequenza scelta (rileva le sibilanti)
//   la riduzione si applica SOLO alla parte alta: uno shelf alto dinamico la cui attenuazione segue il
//   detector (a riduzione zero lo shelf e' esattamente trasparente, niente sfasamenti fra le bande)
//   REDUCTION 0..10: 0 = spento; salendo scende la soglia e cresce il ratio (fino a -24 dB di profondita')
//   LISTEN: esce il sidechain (quello che il de-esser "sente")
struct DeEssCore
{
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        for (int c = 0; c < 2; ++c) { shelf[c].reset(); bp[c].reset(); }
        env = 0.0; gr = 0.0; appliedGr = 0.0; counter = 0;
        envA = coefFor(0.0003, fs);  envR = coefFor(0.030, fs);
        grA  = coefFor(0.0008, fs);  grR  = coefFor(0.060, fs);
        lastFreq = -1.0;
    }

    void set(double freq, double reduction0to10)
    {
        const double amt = std::clamp(reduction0to10, 0.0, 10.0) / 10.0;
        thr   = -14.0 - 26.0 * amt;     // da -14 a -40 dBFS
        ratio = 1.0 + 7.0 * amt;        // da 1:1 (spento) a 8:1
        depth = -24.0 * std::sqrt(amt); // massima riduzione
        if (std::fabs(freq - lastFreq) > 0.5)
        {
            for (int c = 0; c < 2; ++c) bp[c].setBandpass(fs, freq, 1.2);
            lastFreq = freq;
            updateShelf();
        }
    }

    void tick(float& l, float& r, bool listen, bool stereo)
    {
        const float sl = bp[0].process(l);
        const float sr = stereo ? bp[1].process(r) : sl;
        const double lev = std::max(std::fabs((double) sl), std::fabs((double) sr));
        env = lev > env ? lev + envA * (env - lev) : lev + envR * (env - lev);

        const double target = std::max(depth, softKneeGr(toDb(env), thr, ratio, 6.0));
        gr = target < gr ? target + grA * (gr - target) : target + grR * (gr - target);

        if (++counter >= 8) { counter = 0; if (std::fabs(gr - appliedGr) > 0.01) { appliedGr = gr; updateShelf(); } }

        const float ol = shelf[0].process(l);
        const float orr = stereo ? shelf[1].process(r) : ol;
        if (listen) { l = sl; r = sr; return; }
        l = ol;
        if (stereo) r = orr;
    }

    double fs = 44100.0, thr = -30.0, ratio = 1.0, depth = 0.0, lastFreq = -1.0;
    double env = 0.0, gr = 0.0, appliedGr = 0.0, envA = 0.0, envR = 0.0, grA = 0.0, grR = 0.0;
    int counter = 0;
    Biquad shelf[2], bp[2];

private:
    void updateShelf()
    {
        for (int c = 0; c < 2; ++c) shelf[c].setHighShelf(fs, std::max(1000.0, lastFreq * 0.8), appliedGr, 0.7071);
    }
};

//==============================================================================
// Opto stile LA-2A (cella T4):
//   - detector a energia (RMS veloce), attacco ~10 ms
//   - release a due stadi: circa meta' della riduzione torna in ~60 ms, il resto in 0,5..5 s;
//     piu' a lungo si comprime, piu' la parte lenta diventa lenta (memoria della cella)
//   - PEAK REDUCTION 0..100 = guadagno del sidechain (0 = nessuna compressione)
//   - COMPRESS ~3:1 ginocchio largo, LIMIT ~12:1 ginocchio stretto
struct OptoCore
{
    void prepare(double sampleRate)
    {
        fs = sampleRate;
        ms = 0.0; fast = slow = 0.0; mem = 0.0;
        msCoef  = coefFor(0.004, fs);
        atk     = coefFor(0.010, fs);
        relFast = coefFor(0.060, fs);
        memUp   = coefFor(3.0, fs);
        memDown = coefFor(8.0, fs);
    }

    void set(double peak0to100, bool limit)
    {
        const double p = std::clamp(peak0to100, 0.0, 100.0);
        thr   = 8.0 - 0.5 * p;          // 0 -> +8 dBFS (spento), 50 -> -17, 100 -> -42 dBFS
        ratio = limit ? 12.0 : 3.0;
        knee  = limit ? 4.0 : 14.0;
    }

    double tick(double l, double r)
    {
        const double x2 = 0.5 * (l * l + r * r);
        ms = x2 + msCoef * (ms - x2);
        const double levDb = 10.0 * std::log10(ms + 1.0e-24) + 3.0;   // +3: un seno a fondo scala = 0 dBFS
        const double target = softKneeGr(levDb, thr, ratio, knee);

        if (target < -1.0) mem = 1.0 - memUp * (1.0 - mem);
        else               mem *= memDown;
        const double relSlow = coefFor(0.5 + 4.5 * mem, fs);

        fast = target + (target < fast ? atk : relFast) * (fast - target);
        slow = target + (target < slow ? atk : relSlow) * (slow - target);
        return fromDb(0.5 * (fast + slow));
    }

    double currentGrDb() const { return 0.5 * (fast + slow); }

    double fs = 44100.0, thr = -10.0, ratio = 3.0, knee = 14.0;
    double ms = 0.0, fast = 0.0, slow = 0.0, mem = 0.0;
    double msCoef = 0.0, atk = 0.0, relFast = 0.0, memUp = 0.0, memDown = 0.0;
};

//==============================================================================
// Filtri di Meteora Winter Dreams (Source/dsp/Filters.h): sezione del 1o ordine (TDF-II) con gli shelf
// Baxandall a pendenza dolcissima e il passa alto "Capacitor". Copiati senza modifiche al calcolo.
struct OnePole
{
    double b0 = 1.0, b1 = 0.0, a1 = 0.0, s = 0.0;

    void reset() { s = 0.0; }

    float process(float xf)
    {
        const double x = xf;
        const double y = b0 * x + s;
        s = b1 * x - a1 * y;
        return (float) y;
    }

    static double prewarp(double fc, double fs) { return std::tan(kPi * std::min(fc, fs * 0.49) / fs); }

    void setHighpass(double fc, double fs)
    {
        const double K = prewarp(fc, fs);
        const double n = 1.0 / (1.0 + K);
        b0 = n; b1 = -n; a1 = (K - 1.0) * n;
    }

    // Baxandall 1o ordine, transizione centrata su fc, boost e cut speculari
    void setLowShelf(double fc, double fs, double gainDb)
    {
        const double K     = prewarp(fc, fs);
        const double rootA = std::pow(10.0, std::abs(gainDb) / 40.0);
        const double num   = gainDb >= 0.0 ? rootA : 1.0 / rootA;
        const double den   = 1.0 / num;
        const double norm  = 1.0 / (1.0 + den * K);
        b0 = (1.0 + num * K) * norm;
        b1 = (num * K - 1.0) * norm;
        a1 = (den * K - 1.0) * norm;
    }

    void setHighShelf(double fc, double fs, double gainDb)
    {
        const double K     = prewarp(fc, fs);
        const double rootA = std::pow(10.0, std::abs(gainDb) / 40.0);
        const double num   = gainDb >= 0.0 ? rootA : 1.0 / rootA;
        const double den   = 1.0 / num;
        const double norm  = 1.0 / (den + K);
        b0 = (num + K) * norm;
        b1 = (K - num) * norm;
        a1 = (K - den) * norm;
    }

    double magnitudeDb(double fs, double f) const
    {
        const double w = 2.0 * kPi * f / fs;
        const double nr = b0 + b1 * std::cos(w), ni = -b1 * std::sin(w);
        const double dr = 1.0 + a1 * std::cos(w), di = -a1 * std::sin(w);
        return 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
    }
};

// Campana "induttiva" di Winter Dreams: la campana si stringe quando il guadagno sale (Q = 0,6 + 0,07 * |dB|).
// qScale = Q del pannello (1 = esattamente la curva di Winter Dreams).
inline double inductorQ(double gainDb, double qScale) { return qScale * (0.6 + 0.07 * std::abs(gainDb)); }

//==============================================================================
// EQ della Lunch Strip con le curve di Meteora Winter Dreams:
//   HIGH  shelf Baxandall 1o ordine (oppure campana induttiva)
//   MID   campana induttiva (Q del pannello = moltiplicatore, 1 = Winter Dreams)
//   LOW   shelf Baxandall 1o ordine (oppure campana induttiva)
//   HPF   "Capacitor" 18 dB/ottava (tre poli del 1o ordine, -3 dB corretti sulla frequenza scelta)
//   LPF   12 dB/ottava (Winter Dreams non lo ha)
//   HPF al minimo (20 Hz) e LPF al massimo (20 kHz) sono SPENTI: vicino agli estremi il filtro
//   sfuma verso il segnale diretto, quindi non ci sono scatti.
struct EqSettings
{
    double highGain = 0, highFreq = 8000; bool highShelf = true;
    double midGain = 0, midFreq = 1500, midQ = 1.0;
    double lowGain = 0, lowFreq = 100; bool lowShelf = true;
    double hpf = 20, lpf = 20000;
};

struct EqCore
{
    static constexpr double hpf3PoleCorrection = 1.9615;   // da Winter Dreams

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        for (int c = 0; c < 2; ++c)
        {
            hiS[c].reset(); loS[c].reset(); hiB[c].reset(); loB[c].reset(); mi[c].reset(); lp[c].reset();
            for (auto& h : hp[c]) h.reset();
        }
    }

    // da chiamare ogni pochi campioni (il modulo lo fa ogni 16, come Winter Dreams) con i valori gia' smussati
    void set(const EqSettings& st)
    {
        // cambio shelf <-> campana o filtro che si riaccende: il filtro che entra parte da stato pulito
        if (st.highShelf != highShelf) for (int c = 0; c < 2; ++c) { hiS[c].reset(); hiB[c].reset(); }
        if (st.lowShelf  != lowShelf)  for (int c = 0; c < 2; ++c) { loS[c].reset(); loB[c].reset(); }
        if (hpMix <= 0.0 && st.hpf > 20.0)   for (int c = 0; c < 2; ++c) for (auto& h : hp[c]) h.reset();
        if (lpMix <= 0.0 && st.lpf < 20000.0) for (int c = 0; c < 2; ++c) lp[c].reset();
        highShelf = st.highShelf; lowShelf = st.lowShelf;
        for (int c = 0; c < 2; ++c)
        {
            if (highShelf) hiS[c].setHighShelf(st.highFreq, fs, st.highGain);
            else           hiB[c].setPeak(fs, st.highFreq, st.highGain, inductorQ(st.highGain, 1.0));
            mi[c].setPeak(fs, st.midFreq, st.midGain, inductorQ(st.midGain, st.midQ));
            if (lowShelf)  loS[c].setLowShelf(st.lowFreq, fs, st.lowGain);
            else           loB[c].setPeak(fs, st.lowFreq, st.lowGain, inductorQ(st.lowGain, 1.0));
            for (auto& h : hp[c]) h.setHighpass(std::max(20.0, st.hpf) / hpf3PoleCorrection, fs);
            lp[c].setLowpass(fs, std::min(st.lpf, 0.45 * fs), 0.7071);
        }
        hpMix = std::clamp((st.hpf - 20.0) / 10.0, 0.0, 1.0);           // 20 Hz = spento, 30 Hz = tutto
        lpMix = std::clamp((20000.0 - st.lpf) / 2000.0, 0.0, 1.0);      // 20 kHz = spento, 18 kHz = tutto
    }

    float process(int ch, float x)
    {
        if (hpMix > 0.0)
        {
            const float h = hp[ch][2].process(hp[ch][1].process(hp[ch][0].process(x)));
            x += (float) hpMix * (h - x);
        }
        x = highShelf ? hiS[ch].process(x) : hiB[ch].process(x);
        x = mi[ch].process(x);
        x = lowShelf ? loS[ch].process(x) : loB[ch].process(x);
        if (lpMix > 0.0)
        {
            const float l = lp[ch].process(x);
            x += (float) lpMix * (l - x);
        }
        return x;
    }

    // risposta complessiva in dB (per i test), con i filtri del canale 0 e i filtri HPF/LPF tutti dentro o tutti fuori
    double magnitudeDb(double f) const
    {
        double db = (highShelf ? hiS[0].magnitudeDb(fs, f) : hiB[0].magnitudeDb(fs, f))
                  + mi[0].magnitudeDb(fs, f)
                  + (lowShelf ? loS[0].magnitudeDb(fs, f) : loB[0].magnitudeDb(fs, f));
        if (hpMix >= 1.0) for (auto& h : hp[0]) db += h.magnitudeDb(fs, f);
        if (lpMix >= 1.0) db += lp[0].magnitudeDb(fs, f);
        return db;
    }

    double fs = 44100.0, hpMix = 0.0, lpMix = 0.0;
    bool highShelf = true, lowShelf = true;
    OnePole hiS[2], loS[2], hp[2][3];
    Biquad hiB[2], loB[2], mi[2], lp[2];
};
//==============================================================================
// EQ PULTEC (pannello blu "BASED PLACEBO"): motore = Placebo EQ (Source/DSP/placebo/PlaceboDSP.h, Meteora Plugins),
// lo stesso DSP del plugin, voicing tarato sul Klark Teknik EQP-KT, filtri "matched" (Vicanek):
//   Low Boost / Low Atten (shelf 1o ordine, l'atten piu' "alto" del boost -> trucco Pultec)
//   High Boost (campana, Q dal Bandwidth) / High Atten (shelf 1o ordine, Atten Sel fisso a 10 kHz, il default di Placebo)
//   Drive: stadio valvolare di Placebo (armoniche pari, guadagno a segnale piccolo = 1: non alza il volume)
//   Trasformatore d'uscita di Placebo (passa alto ~15 Hz, passa basso ~52 kHz)
// Differenze rispetto al plugin Placebo: niente 200 Hz su LOW FREQ e niente Atten Sel / Output (non sono sul pannello);
// l'IN/bypass lo fa la levetta POWER (bypass del modulo EQ, con dissolvenza).
struct PultecSettings
{
    int lfFreq = 3; double lfBoost = 0, lfAtten = 0;      // 20 / 30 / 60 / 100 Hz
    int hfFreq = 4; double hfBoost = 0, hfBw = 5, hfAtten = 0;   // 3 / 4 / 5 / 8 / 10 / 12 / 16 kHz
    double drive = 0;
};

struct PultecCore
{
    static double lfHz(int i) { static const double f[4] = { 20, 30, 60, 100 }; return f[std::clamp(i, 0, 3)]; }
    static double hfHz(int i) { static const double f[7] = { 3000, 4000, 5000, 8000, 10000, 12000, 16000 }; return f[std::clamp(i, 0, 6)]; }
    static constexpr double kAttenSelHz = 10000.0;

    void prepare(double sampleRate)
    {
        fs = sampleRate;
        for (auto& c : ch) { c.reset(); placebo::computeTransformer(fs, c); }
        last = {}; last.lowBoost = -1.0;   // forza il primo calcolo
        set(PultecSettings());
    }

    void set(const PultecSettings& s)
    {
        placebo::Params p;
        p.lowBoost = std::clamp(s.lfBoost, 0.0, 10.0);  p.lowAtten = std::clamp(s.lfAtten, 0.0, 10.0);
        p.lowFreqHz = lfHz(s.lfFreq);
        p.highBoost = std::clamp(s.hfBoost, 0.0, 10.0); p.bandwidth = std::clamp(s.hfBw, 0.0, 10.0);
        p.highFreqHz = hfHz(s.hfFreq);
        p.highAtten = std::clamp(s.hfAtten, 0.0, 10.0); p.attenSelHz = kAttenSelHz;
        p.eqIn = 1.0;
        drive01 = std::clamp(s.drive, 0.0, 10.0) / 10.0;

        const bool changed = p.lowBoost != last.lowBoost || p.lowAtten != last.lowAtten || p.lowFreqHz != last.lowFreqHz
                          || p.highBoost != last.highBoost || p.bandwidth != last.bandwidth || p.highFreqHz != last.highFreqHz
                          || p.highAtten != last.highAtten;
        if (! changed) return;
        placebo::computeEq(p, fs, proto);
        for (auto& c : ch)
        {
            copy(c.lowBoost, proto.lowBoost);   copy(c.lowAtten, proto.lowAtten);
            copy(c.highBoost, proto.highBoost); copy(c.highAtten, proto.highAtten);
        }
        last = p;
    }

    float process(int c, float x)
    {
        auto& e = ch[c];
        double y = e.process((double) x);
        y = placebo::TubeStage::process(y, drive01);
        return (float) e.transformer(y);
    }

    // risposta della sola rete EQ in dB (per i test)
    double magnitudeDb(double f) const
    {
        const double w = 2.0 * kPi * f / fs;
        return 20.0 * std::log10(proto.lowBoost.magnitude(w) * proto.lowAtten.magnitude(w)
                                 * proto.highBoost.magnitude(w) * proto.highAtten.magnitude(w));
    }

private:
    static void copy(placebo::Biquad& d, const placebo::Biquad& s) { d.b0 = s.b0; d.b1 = s.b1; d.b2 = s.b2; d.a1 = s.a1; d.a2 = s.a2; }

    double fs = 44100.0, drive01 = 0.0;
    placebo::Params last;
    placebo::EqChannel proto, ch[2];
};
} // namespace lunch
