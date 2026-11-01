#pragma once
// Copiato da meteoraFET1179 (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace fet1179.
// ---------------------------------------------------------------------------
// "Logical4" di Airwindows (Chris Johnson), licenza MIT. Vedi THIRD_PARTY_NOTICES.txt.
// Tre ButterComp in cascata con "Power Sag", come l'originale (stesso ordine,
// stesse formule, collegamento stereo, interpolazione tra gli stadi col ratio).
// Modifica per il 1176: il controllo unico "Speed" e' diviso in due velocita':
// ATTACK (quando la riduzione aumenta) e RELEASE (quando torna indietro).
// I filtri HPF/LPF del plugin agiscono solo sul segnale che comanda la compressione.
// Lo stadio C usa divisore e resto coerenti come A e B (nell'originale il
// divisore di C non segue la velocita' e il guadagno dello stadio cambia con essa).
// Niente dither; nessuna allocazione durante l'elaborazione.
// ---------------------------------------------------------------------------
#include <cmath>
#include <algorithm>
#include "Biquad.h"

namespace fet1179
{

class AirwindowsLogical4
{
public:
    void prepare (double sampleRate)
    {
        os = sampleRate / 44100.0;
        for (int s = 0; s < 3; ++s)
            for (int c = 0; c < 2; ++c)
            {
                auto& st = S[s][c];
                st.cApos = st.cAneg = st.cBpos = st.cBneg = 1.0;
                st.tpos = st.tneg = 1.0;
                st.avg = st.nvg = 0.0;
                st.sag = 0.0;
                for (auto& v : st.buf) v = 0.0;
            }
        gcount = 0; fpFlip = true;
        offset = std::clamp ((int) (2.42 * os), 1, 498);
    }

    // A: threshold (0-1, 0.5 = 0 dB), B: ratio (0-1, come l'originale), D: make-up (0-1, 0.5 = 0 dB)
    // atkRem / relRem: velocita' per campione dello stadio A (gli stadi B e C seguono in proporzione)
    void setParams (double A, double B, double D, double atkRem, double relRem)
    {
        inputgain = std::pow (10.0, (-((A * 40.0) - 20.0)) / 20.0);
        compoutgain = inputgain;
        double ratio = std::sqrt (((B * B) * 15.0) + 1.0) - 1.0;
        ratio = std::clamp (ratio, 0.0, 2.99999);
        ratioselector = (int) std::floor (ratio);
        ratioFrac = ratio - ratioselector;
        outputgain = std::pow (10.0, ((D * 40.0) - 20.0) / 20.0);
        const double k[3] = { 0.000782, 0.000819, 0.000857 };
        for (int s = 0; s < 3; ++s)
        {
            remAtk[s] = std::clamp (atkRem * k[s] / k[0], 1.0e-7, 0.5);
            remRel[s] = std::clamp (relRem * k[s] / k[0], 1.0e-7, 0.5);
        }
    }

    // filtri del rilevatore (sidechain): copia solo i coefficienti, lo stato di ogni stadio resta suo
    void setSidechain (bool hpOn, const Biquad& hp, bool lpOn, const Biquad& lp)
    {
        scHpOn = hpOn; scLpOn = lpOn;
        for (int s = 0; s < 3; ++s)
        {
            copyCoeffs (scHp[s], hp);
            copyCoeffs (scLp[s], lp);
        }
    }

    // velocita' "Speed" dell'originale (1-100 ms) convertita in resto per campione dello stadio A
    double speedToRem (double ms) const { return 0.000782 * (10.0 / std::sqrt (ms)) / os; }

    void process (double& L, double& R)
    {
        const double dryL = L, dryR = R;
        gcount--; if (gcount < 0 || gcount > 499) gcount = 499;
        double out[3][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 } };
        double x[2] = { L, R };
        for (int s = 0; s < 3; ++s)
        {
            if (s > ratioselector) break;
            powerSag (x[0], S[s][0]);
            powerSag (x[1], S[s][1]);
            for (int c = 0; c < 2; ++c)
            {
                if (s == 0 && inputgain != 1.0) x[c] *= inputgain;
                double det = x[c];                                   // senza filtri: identico all'originale
                if (scHpOn) det = scHp[s].proc (det, c);
                if (scLpOn) det = scLp[s].proc (det, c);
                compress (x[c], det, S[s][c], S[s][1 - c], s);
                out[s][c] = x[c] / compoutgain;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            const double dry = c == 0 ? dryL : dryR;
            double y;
            switch (ratioselector)
            {
                case 0:  y = (dry * (1.0 - ratioFrac)) + (out[0][c] * ratioFrac); break;
                case 1:  y = (out[0][c] * (1.0 - ratioFrac)) + (out[1][c] * ratioFrac); break;
                default: y = (out[1][c] * (1.0 - ratioFrac)) + (out[2][c] * ratioFrac); break;
            }
            if (outputgain != 1.0) y *= outputgain;
            (c == 0 ? L : R) = y;
        }
        fpFlip = ! fpFlip;
    }

private:
    struct Stage
    {
        double cApos, cAneg, cBpos, cBneg, tpos, tneg, avg, nvg, sag;
        double buf[1000];
    };

    void powerSag (double& x, Stage& st)
    {
        const double intensity = 0.0445556, powerSagK = 0.003300223685324102874217;
        st.buf[gcount + 499] = st.buf[gcount] = std::fabs (x) * (intensity - ((st.cApos + st.cBpos + st.cAneg + st.cBneg) * powerSagK));
        st.sag += (st.buf[gcount] / offset);
        st.sag -= (st.buf[gcount + offset] / offset);
        st.sag -= 0.000001;
        double clamp = 1;
        if (st.sag < 0) st.sag = 0;
        if (st.sag > 1) { clamp -= (st.sag - 1); st.sag = 1; }
        if (clamp < 0.5) clamp = 0.5;
        const double thickness = ((1.0 - st.sag) * 2.0) - 1.0;
        const double o = std::fabs (thickness);
        double br = std::fabs (x);
        if (br > 1.57079633) br = 1.57079633;
        if (thickness > 0) br = std::sin (br); else br = 1 - std::cos (br);
        if (x > 0) x = (x * (1 - o)) + (br * o); else x = (x * (1 - o)) - (br * o);
        if (clamp != 1.0) x *= clamp;
    }

    static void copyCoeffs (Biquad& d, const Biquad& s) { d.b0 = s.b0; d.b1 = s.b1; d.b2 = s.b2; d.a1 = s.a1; d.a2 = s.a2; }

    // det: segnale che decide la compressione (filtrato se i filtri sono attivi); x: audio a cui si applica
    void compress (double& x, double det, Stage& st, Stage& other, int s)
    {
        const double fpOld = 0.618033988749894848204586, fpNew = 1.0 - fpOld;
        double inputpos = (det * fpOld) + (st.avg * fpNew) + 1.0;
        st.avg = det;
        if (inputpos < 0.001) inputpos = 0.001;
        double outputpos = inputpos / 2.0; if (outputpos > 1.0) outputpos = 1.0;
        inputpos *= inputpos;
        double r = (inputpos > st.tpos ? remAtk[s] : remRel[s]);
        double dyn = std::min (1.0, r * (inputpos + 1.0));
        st.tpos = st.tpos * (1.0 - dyn) + inputpos * dyn;
        const double calcpos = std::pow ((1.0 / st.tpos), 2);

        double inputneg = (-det * fpOld) + (st.nvg * fpNew) + 1.0;
        st.nvg = -det;
        if (inputneg < 0.001) inputneg = 0.001;
        double outputneg = inputneg / 2.0; if (outputneg > 1.0) outputneg = 1.0;
        inputneg *= inputneg;
        r = (inputneg > st.tneg ? remAtk[s] : remRel[s]);
        dyn = std::min (1.0, r * (inputneg + 1.0));
        st.tneg = st.tneg * (1.0 - dyn) + inputneg * dyn;
        const double calcneg = std::pow ((1.0 / st.tneg), 2);

        auto chase = [&] (double& ctl, double calc, double& link)
        {
            const double rr = calc < ctl ? remAtk[s] : remRel[s];   // piu' riduzione = attack, ritorno = release
            ctl = ctl * (1.0 - rr) + calc * rr;
            if (link > ctl) link = (link + ctl) * 0.5;               // stereo collegato come nell'originale
        };
        if (det > 0) { if (fpFlip) chase (st.cApos, calcpos, other.cApos); else chase (st.cBpos, calcpos, other.cBpos); }
        else       { if (fpFlip) chase (st.cAneg, calcneg, other.cAneg); else chase (st.cBneg, calcneg, other.cBneg); }

        const double total = fpFlip ? (st.cApos * outputpos) + (st.cAneg * outputneg)
                                    : (st.cBpos * outputpos) + (st.cBneg * outputneg);
        if (total != 1.0) x *= total;
        x = std::clamp (x, -36.0, 36.0);
    }

    double os = 1.0, inputgain = 1.0, compoutgain = 1.0, outputgain = 1.0, ratioFrac = 0.0;
    int ratioselector = 0, gcount = 0, offset = 2;
    bool fpFlip = true;
    double remAtk[3] { 0.001, 0.001, 0.001 }, remRel[3] { 0.001, 0.001, 0.001 };
    Stage S[3][2] {};
    Biquad scHp[3], scLp[3];
    bool scHpOn = false, scLpOn = false;
};
} // namespace fet1179
