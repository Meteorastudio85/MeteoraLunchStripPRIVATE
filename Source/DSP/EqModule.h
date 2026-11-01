#pragma once
#include "ModuleBase.h"
#include "DynamicsCore.h"

// EQ (motore: lunch::EqCore in DynamicsCore.h, con le curve di Meteora Winter Dreams):
//   HIGH / LOW  = shelf Baxandall del 1o ordine (pendenza dolcissima) oppure campana induttiva:
//                 si sceglie col tasto destro sulla manopola
//   MID         = campana induttiva (si stringe quando alzi il guadagno); Q dal tasto destro (1 = Winter Dreams)
//   HPF         = "Capacitor" 18 dB/ottava come Winter Dreams; LPF 12 dB/ottava
//   HPF tutto a sinistra (20 Hz) e LPF tutto a destra (20 kHz) = spenti
// I valori vengono smussati e i coefficienti ricalcolati ogni 16 campioni: nessuno scatto girando le manopole.
//
// Con "eq_type" = Pultec lavora invece lunch::PultecCore (pannello blu "BASED PLACEBO").
// Il cambio di tipo fa una dissolvenza di 30 ms fra i due EQ (durante la dissolvenza girano entrambi).
class EqModule : public ModuleBase
{
public:
    explicit EqModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "eq")
    {
        hiG = param("high_gain"); hiF = param("high_freq"); hiS = param("high_shelf");
        miG = param("mid_gain");  miF = param("mid_freq");  miQ = param("mid_q");
        loG = param("low_gain");  loF = param("low_freq");  loS = param("low_shelf");
        hpf = param("hpf");       lpf = param("lpf");
        type = param("type");
        auto raw = [&s](const char* id) { return s.getRawParameterValue(id); };
        pLfF = raw("pul_lf_freq"); pLfB = raw("pul_lf_boost"); pLfA = raw("pul_lf_atten");
        pHfF = raw("pul_hf_freq"); pHfB = raw("pul_hf_boost"); pBw  = raw("pul_hf_bw");
        pHfA = raw("pul_hf_atten"); pDrv = raw("pul_drive");
    }

private:
    static constexpr int kStep = 16;   // come Winter Dreams

    lunch::EqSettings target() const
    {
        lunch::EqSettings s;
        s.highGain = hiG->load(); s.highFreq = hiF->load(); s.highShelf = hiS->load() > 0.5f;
        s.midGain  = miG->load(); s.midFreq  = miF->load(); s.midQ = miQ->load();
        s.lowGain  = loG->load(); s.lowFreq  = loF->load(); s.lowShelf = loS->load() > 0.5f;
        s.hpf = hpf->load(); s.lpf = lpf->load();
        return s;
    }

    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        core.prepare(spec.sampleRate);
        cur = target();
        core.set(cur);
        smoothCoef = 1.0 - std::exp(-(double) kStep / (spec.sampleRate * 0.02));   // ~20 ms
        pultec.prepare(spec.sampleRate);
        pcur = pultecTarget();
        pultec.set(pcur);
        typeMix = type->load() > 0.5f ? 1.0f : 0.0f;
        typeStep = (float) (1.0 / (0.03 * spec.sampleRate));
        tmp.setSize(2, (int) spec.maximumBlockSize, false, false, true);
    }

    lunch::PultecSettings pultecTarget() const
    {
        lunch::PultecSettings p;
        p.lfFreq = (int) std::lround(pLfF->load()); p.lfBoost = pLfB->load(); p.lfAtten = pLfA->load();
        p.hfFreq = (int) std::lround(pHfF->load()); p.hfBoost = pHfB->load(); p.hfBw = pBw->load();
        p.hfAtten = pHfA->load(); p.drive = pDrv->load();
        return p;
    }

    void processPultec(float* const* io, int nc, int n)
    {
        const auto t = pultecTarget();
        for (int start = 0; start < n; start += kStep)
        {
            auto lin = [this](double& c, double tg) { c += smoothCoef * (tg - c); };
            lin(pcur.lfBoost, t.lfBoost); lin(pcur.lfAtten, t.lfAtten); lin(pcur.hfBoost, t.hfBoost);
            lin(pcur.hfBw, t.hfBw);       lin(pcur.hfAtten, t.hfAtten); lin(pcur.drive, t.drive);
            pcur.lfFreq = t.lfFreq; pcur.hfFreq = t.hfFreq;   // a scatti, come sull'hardware
            pultec.set(pcur);
            const int len = juce::jmin(kStep, n - start);
            for (int i = start; i < start + len; ++i)
                for (int ch = 0; ch < nc; ++ch)
                    io[ch][i] = pultec.process(ch, io[ch][i]);
        }
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);
        if (nc == 0) return;
        const float targetMix = type->load() > 0.5f ? 1.0f : 0.0f;

        if (typeMix == targetMix)
        {
            if (targetMix > 0.5f) processPultec(buffer.getArrayOfWritePointers(), nc, n);
            else                  processMeteora(buffer.getArrayOfWritePointers(), nc, n);
            return;
        }

        // dissolvenza fra i due EQ
        for (int ch = 0; ch < nc; ++ch) tmp.copyFrom(ch, 0, buffer, ch, 0, n);
        processMeteora(buffer.getArrayOfWritePointers(), nc, n);
        processPultec(tmp.getArrayOfWritePointers(), nc, n);
        for (int i = 0; i < n; ++i)
        {
            typeMix = targetMix > typeMix ? juce::jmin(targetMix, typeMix + typeStep) : juce::jmax(targetMix, typeMix - typeStep);
            for (int ch = 0; ch < nc; ++ch)
            {
                float* a = buffer.getWritePointer(ch);
                a[i] += typeMix * (tmp.getSample(ch, i) - a[i]);
            }
        }
    }

    void processMeteora(float* const* io, int nc, int n)
    {
        const auto t = target();

        for (int start = 0; start < n; start += kStep)
        {
            // guadagni in lineare (dB), frequenze in logaritmico
            auto lin = [this](double& c, double tg) { c += smoothCoef * (tg - c); };
            auto lg  = [this](double& c, double tg) { c *= std::pow(tg / c, smoothCoef); };
            lin(cur.highGain, t.highGain); lg(cur.highFreq, t.highFreq);
            lin(cur.midGain,  t.midGain);  lg(cur.midFreq,  t.midFreq);  lg(cur.midQ, t.midQ);
            lin(cur.lowGain,  t.lowGain);  lg(cur.lowFreq,  t.lowFreq);
            lg(cur.hpf, t.hpf);            lg(cur.lpf, t.lpf);
            cur.highShelf = t.highShelf;   cur.lowShelf = t.lowShelf;
            core.set(cur);

            const int len = juce::jmin(kStep, n - start);
            for (int ch = 0; ch < nc; ++ch)
            {
                float* x = io[ch] + start;
                for (int i = 0; i < len; ++i) x[i] = core.process(ch, x[i]);
            }
        }
    }

    std::atomic<float> *hiG, *hiF, *hiS, *miG, *miF, *miQ, *loG, *loF, *loS, *hpf, *lpf, *type;
    std::atomic<float> *pLfF, *pLfB, *pLfA, *pHfF, *pHfB, *pBw, *pHfA, *pDrv;
    lunch::EqCore core;
    lunch::EqSettings cur;
    lunch::PultecCore pultec;
    lunch::PultecSettings pcur;
    juce::AudioBuffer<float> tmp;
    float typeMix = 0.0f, typeStep = 0.001f;
    double smoothCoef = 0.1;
};
