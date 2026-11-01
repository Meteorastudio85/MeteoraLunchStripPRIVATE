/*
    Marley - Meteora Plugins
    Copyright (c) 2026 Meteora Plugins. Tutti i diritti riservati.
    Vedi il file LICENSE nella cartella principale del progetto.
*/

#pragma once
// Copiato da Marley (Meteora Plugins) senza modifiche al DSP; solo racchiuso nel namespace marley.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

#include "AutoGain.h"
#include "LinearPhaseOversampler.h"
#include "Tube2Stage.h"

namespace marley
{

/*
    MarleyEngine - tutta la catena audio, senza dipendere da JUCE
    (cosi' si prova anche da riga di comando: vedi Tests/).

    Catena per canale:
      dry ----------------------------------------------> ritardo esatto di 71 campioni --+
      in -> [4x fase lineare: Tube2 (Drive)] -> DC blocker 2 Hz -> autogain -> wet ---------+-> Mix -> Output -> dither

    Nessun filtro che ruota la fase: oversampling FIR simmetrico e ritardo del
    dry intero ed esatto. Wet e dry restano in fase in tutta la banda audio
    (l'unica eccezione e' il DC blocker a 2 Hz: < 4 gradi a 30 Hz), quindi il
    Mix non crea effetti "phaser" o filtri a pettine. Con Mix 0 l'uscita e'
    l'ingresso identico, solo ritardato.
*/
class MarleyEngine
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr int kMaxOversample = LinearPhaseOversampler4x::kMaxFactor;
    static constexpr int kSubBlock    = 16; // ogni quanti campioni si ricalcolano i parametri della valvola

    struct Params
    {
        float drive01  = 0.5f;   // 0..1
        float mix01    = 1.0f;   // 0..1
        float outputDb = 0.0f;   // dB
    };

    // letti dalla GUI
    std::atomic<float> meterDb { -100.0f };  // RMS in uscita, dBFS (balistica VU)
    std::atomic<float> driveLevelDb { -100.0f }; // RMS del segnale che entra nella valvola (dopo il Drive), dBFS: e' quello che legge il VU

    // 0 VU = -8 dBFS RMS all'ingresso della valvola: la lancetta arriva verso lo zero
    // proprio quando la valvola comincia a saturare davvero (e il vetro ad arrossarsi).
    static constexpr float kVuReferenceDb = -8.0f;
    std::atomic<float> distortionDb { -100.0f }; // quanto la valvola sta distorcendo (dB rispetto al segnale pulito)

    //==========================================================================
    void prepare (double sampleRate, int /*maxBlockSize*/)
    {
        sr = sampleRate;
        osFactor = (sampleRate >= 88200.0 ? 2 : 4); // ad alta frequenza basta il 2x
        const double osRate = sampleRate * osFactor;

        for (int ch = 0; ch < kMaxChannels; ++ch)
        {
            oversampler[(size_t) ch].setFactor (osFactor);
            tube[(size_t) ch].prepare (osRate);
        }
        latency = oversampler[0].getLatency();
        for (auto& d : dryDelay)
            d.assign ((size_t) latency, 0.0);
        delayPos = 0;

        dcR = 1.0 - (2.0 * 3.14159265358979323846 * 2.0 / sampleRate); // 2 Hz: toglie solo la continua dell'asimmetria
        for (auto& d : dcWet) d = {};

        autoGain.prepare (sampleRate, osRate, kMaxChannels);

        smoothCoef = std::exp (-1.0 / (0.03 * sampleRate));
        snap = true;

        meterMs = 0.0;
        distErrMs = 0.0;
        distRefMs = 1.0e-20;
        meterDb.store (-100.0f);
        driveMs = 0.0;
        driveLevelDb.store (-100.0f);
        distortionDb.store (-100.0f);
    }

    double getTrimDb() const { return 20.0 * std::log10 (autoGain.getTrim()); }

    int getLatencySamples() const { return latency; }

    //==========================================================================
    void process (float* const* io, int numChannels, int numSamples, const Params& p)
    {
        const int numCh = std::min (numChannels, kMaxChannels);
        if (numCh <= 0 || numSamples <= 0)
            return;

        const double outTarget = std::pow (10.0, (double) p.outputDb / 20.0);
        if (snap)
        {
            drive = p.drive01; mix = p.mix01; outGain = outTarget;
            snap = false;
        }

        double outPow = 0.0, errPow = 0.0, refPow = 0.0, tubePow = 0.0;

        for (int start = 0; start < numSamples; start += kSubBlock)
        {
            const int n = std::min (kSubBlock, numSamples - start);

            // parametri lisciati (~30 ms), aggiornati a ogni sotto-blocco
            const double c = std::pow (smoothCoef, (double) n);
            drive   = c * drive   + (1.0 - c) * (double) p.drive01;
            mix     = c * mix     + (1.0 - c) * (double) p.mix01;
            outGain = c * outGain + (1.0 - c) * outTarget;

            Tube2Stage::Params tp[kMaxChannels];
            const double inGain = std::pow (10.0, DriveVoicing::inGainDb (drive) / 20.0);
            const double basePower = DriveVoicing::power (drive);
            const double tubeGain2 = inGain * inGain;
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                // tolleranze L/R: le due "valvole" hanno una curva appena diversa
                // (solo le armoniche cambiano: niente fase, niente differenza di livello)
                const double tol = (ch == 0 ? 1.0 + kTolerance : 1.0 - kTolerance);
                tp[ch] = Tube2Stage::makeParams (inGain, basePower * tol);
            }

            const double trim = autoGain.getTrim();
            const double ag0  = autoGain.getStaticGain (drive);
            const double ag1  = autoGain.updateStaticGain (drive, n);
            const double agStep = (ag1 - ag0) / (double) n;
            double ag = ag0;

            for (int i = start; i < start + n; ++i)
            {
                double dryPow = 0.0;
                double dryIn[kMaxChannels] {};
                for (int ch = 0; ch < numCh; ++ch)
                {
                    dryIn[ch] = (double) io[ch][i];
                    dryPow += dryIn[ch] * dryIn[ch];
                }
                autoGain.detect (dryPow / numCh);
                ag += agStep;
                double drySum = 0.0, wetSum = 0.0;
                tubePow += dryPow * tubeGain2;

                for (int ch = 0; ch < numCh; ++ch)
                {
                    const auto c_ = (size_t) ch;

                    // ---- wet: 4x, Tube2, ritorno a 1x
                    double os[kMaxOversample];
                    oversampler[c_].up (dryIn[ch], os);
                    for (int k = 0; k < osFactor; ++k)
                    {
                        const double ref = os[k] * tp[ch].smallSignal; // come uscirebbe se fosse lineare
                        const double y   = tube[c_].process (os[k], tp[ch]);
                        const double e   = y - ref;
                        errPow += e * e;
                        refPow += ref * ref;
                        os[k] = y;
                    }
                    double wet = dcBlock (dcWet[c_], oversampler[c_].down (os));

                    // ---- dry: ritardato esattamente quanto il wet, stesso DC blocker
                    auto& dl = dryDelay[c_];
                    const double delayed = dl[(size_t) delayPos];
                    dl[(size_t) delayPos] = dryIn[ch];
                    const double dry = delayed;

                    wet *= ag;
                    drySum += dry;
                    wetSum += wet;
                    wet *= trim;

                    const double o = (dry * (1.0 - mix) + wet * mix) * outGain;
                    const float out = dither (o, fpd[c_]);
                    io[ch][i] = out;
                    outPow += (double) out * (double) out;
                }
                autoGain.measureLoudness (0, drySum, wetSum); // loudness sulla somma dei canali: meta' calcoli
                delayPos = (delayPos + 1 == latency) ? 0 : delayPos + 1;
            }

            autoGain.updateTrim (n);
        }

        // VU: RMS con ~300 ms
        {
            const double ms = outPow / (double) (numSamples * numCh);
            const double c = std::exp (-(double) numSamples / (sr * 0.3));
            meterMs = c * meterMs + (1.0 - c) * ms;
            meterDb.store ((float) (10.0 * std::log10 (meterMs + 1.0e-12)));

            // livello che "spinge" la valvola: sale col Drive, come su un vero apparecchio
            driveMs = c * driveMs + (1.0 - c) * (tubePow / (double) (numSamples * numCh));
            driveLevelDb.store ((float) (10.0 * std::log10 (driveMs + 1.0e-12)));
        }

        // distorsione: energia di (uscita - versione lineare) / versione lineare, ~150 ms
        {
            const double cnt = (double) (numSamples * numCh * osFactor);
            const double c = std::exp (-(double) numSamples / (sr * 0.15));
            distErrMs = c * distErrMs + (1.0 - c) * (errPow / cnt);
            distRefMs = c * distRefMs + (1.0 - c) * (refPow / cnt);
            const bool silent = autoGain.getLevelDb() < -60.0 || distRefMs < 1.0e-10;
            distortionDb.store (silent ? -100.0f
                                       : (float) (10.0 * std::log10 ((distErrMs + 1.0e-20) / (distRefMs + 1.0e-20))));
        }
    }

private:
    static constexpr double kTolerance = 0.04; // +-4 % sulla curva della valvola

    struct DcState { double x1 = 0.0, y1 = 0.0; };
    inline double dcBlock (DcState& s, double x) const
    {
        const double y = x - s.x1 + dcR * s.y1;
        s.x1 = x;
        s.y1 = y;
        return y;
    }

    // dither a 32 bit float (tecnica Airwindows, licenza MIT)
    static inline float dither (double sample, uint32_t& fpd)
    {
        int expon = 0;
        std::frexp ((float) sample, &expon);
        fpd ^= fpd << 13;
        fpd ^= fpd >> 17;
        fpd ^= fpd << 5;
        sample += ((double (fpd) - double (0x7fffffff)) * 5.5e-36 * std::ldexp (1.0, expon + 62));
        return (float) sample;
    }

    double sr = 44100.0;
    int osFactor = 4;
    int latency = LinearPhaseOversampler4x::kLatency4x;
    std::array<LinearPhaseOversampler4x, kMaxChannels> oversampler;
    std::array<Tube2Stage, kMaxChannels> tube;
    std::array<std::vector<double>, kMaxChannels> dryDelay;
    int delayPos = 0;

    double dcR = 0.9995;
    std::array<DcState, kMaxChannels> dcWet;

    AutoGain autoGain;
    std::array<uint32_t, kMaxChannels> fpd { 0x1234567u, 0x7654321u };

    double smoothCoef = 0.0;
    bool snap = true;
    double drive = 0.5, mix = 1.0, outGain = 1.0;

    double meterMs = 0.0, driveMs = 0.0, distErrMs = 0.0, distRefMs = 1.0e-20;
};
} // namespace marley
