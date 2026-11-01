#pragma once
#include "ModuleBase.h"
#include "Biquad.h"
#include "marley/MarleyEngine.h"
#include "dirtydesk/ChannelX32.h"

// Valve preamp: motore = Marley (Source/DSP/marley, Meteora Plugins), lo stesso DSP del plugin:
//   [4x fase lineare: Tube2 col Drive di Marley] -> DC blocker 2 Hz -> autogain di Marley (loudness K-pesata)
//
// Catena del modulo:
//   HPF -> Pad / Phase / GAIN -> Marley (DRIVE, Mix 100%) -> [auto gain della Lunch Strip] -> OUTPUT
//
// Mappatura del pannello:
//   GAIN   -20..+20 dB  -> livello che entra in Marley (piu' Gain = piu' la valvola lavora)
//   DRIVE  0..10        -> Drive di Marley 0..100 %; l'autogain di Marley tiene la loudness: Drive cambia il carattere
//   OUTPUT              -> dopo l'auto gain (la manopola funziona anche con AG acceso)
// Meter (come Marley):
//   VU     = livello RMS che entra nella valvola (dopo il Drive), 0 VU = -8 dBFS: piu' spingi, piu' sale
//   rosso  = distortionDb di Marley (energia aggiunta/tolta dalla valvola rispetto al segnale pulito)
//
// LLS 4000 PREAMP (pre_type = 1): motore = stadio del banco di Meteora DirtyDESK, profilo "LLS"
// (Source/DSP/dirtydesk/ChannelX32.h, copiato senza modifiche al calcolo):
//   HPF (OFF/40/80/150/300) -> PAD -20 dB / fase -> MIC (+10 dB dentro il banco, -10 dB dopo) -> banco LLS (GAIN = DRIVE)
//   -> [auto gain della Lunch Strip] -> OUTPUT
//   GAIN (LEVEL) 0..10 = DRIVE del banco 0..100 %: come in DirtyDESK il livello e' compensato (autogain),
//                        quindi GAIN cambia quanto il banco satura, non il volume
//   OUTPUT 0..10       = -15..+15 dB (5 = 0 dB)
//   VU   = livello d'uscita (0 VU = -18 dBFS, come DirtyDESK)
//   rosso del VU = come DirtyDESK: si arrossa quando lo stadio del banco e' spinto oltre ~50% del clip
//   LED SIGNAL = c'e' segnale; LED PEAK = banco spinto o uscita vicina allo 0 dBFS
// Il cambio Valve <-> LLS fa una dissolvenza di 30 ms; il ramo LLS e' ritardato quanto Marley,
// cosi' la latenza del modulo non cambia.
class PreampModule : public ModuleBase
{
public:
    explicit PreampModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "pre")
    {
        gain = param("gain");  drive = param("drive");  output = param("output");
        hpf  = param("hpf");   phase = param("phase");  pad    = param("pad");
        type = param("type");
        auto raw = [&s](const char* id) { return s.getRawParameterValue(id); };
        lGain = raw("lls_gain"); lOut = raw("lls_output"); lHpf = raw("lls_hpf");
        lPad = raw("lls_pad");   lPhase = raw("lls_phase"); lMic = raw("lls_mic");
    }

    bool isLls() const { return type->load() > 0.5f; }
    bool getSignalLed() const override { return signalLed.load(); }
    bool getPeakLed() const override   { return peakLed.load(); }

    int getLatency() const override { return engine.getLatencySamples(); }

    // VU in unita' VU (0 = 0 VU): livello all'ingresso della valvola, riferimento di Marley
    float getVuLevel() const override
    {
        if (isLls()) return llsVuDb.load();
        return engine.driveLevelDb.load() - marley::MarleyEngine::kVuReferenceDb;
    }

    // distorsione della valvola in dB rispetto al segnale pulito (-100 = niente / silenzio)
    // con l'LLS: la spia di saturazione di DirtyDESK (0..1) riportata sulla stessa scala (-6 dB = inizia, -1 dB = rosso pieno)
    float getDistortionDb() const override
    {
        if (isLls()) { const float od = llsOverdrive.load(); return od > 0.0f ? -6.0f + 5.0f * od : -100.0f; }
        return engine.distortionDb.load();
    }

private:
    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        engine.prepare(spec.sampleRate, (int) spec.maximumBlockSize);
        for (auto& h : hp) h.reset();
        prevG1 = currentG1();
        prevOut = dbToGain(output->load());

        lls.setSampleRate(spec.sampleRate);
        lls.setConsoleType(dirtydesk::kSSL);
        lls.setChannelID(1);
        lls.setDrive(llsDrive01());
        lls.reset();
        for (auto& h : lhp) h.reset();
        prevLlsIn = llsInGain(); prevLlsPost = llsPostGain(); prevLlsOut = llsOutGain();
        llsBuf.setSize(2, (int) spec.maximumBlockSize, false, false, true);
        for (auto& d : llsDelay) d.prepare(engine.getLatencySamples());
        odEnv = 0.0; vuEnv = 0.0;
        typeMix = isLls() ? 1.0f : 0.0f;
        typeStep = (float) (1.0 / (0.03 * spec.sampleRate));
    }

    // --- LLS 4000 ---
    static float hpfHz(int i) { static const float f[5] = { 0.0f, 40.0f, 80.0f, 150.0f, 300.0f }; return f[juce::jlimit(0, 4, i)]; }
    double llsDrive01() const { return juce::jlimit(0.0, 1.0, (double) lGain->load() / 10.0); }
    float llsInGain() const
    {
        const float db = (lPad->load() > 0.5f ? -20.0f : 0.0f) + (lMic->load() > 0.5f ? 10.0f : 0.0f);
        return dbToGain(db) * (lPhase->load() > 0.5f ? -1.0f : 1.0f);
    }
    float llsPostGain() const { return lMic->load() > 0.5f ? dbToGain(-10.0f) : 1.0f; }   // MIC: torna al livello di prima
    float llsOutGain() const  { return dbToGain(-15.0f + 3.0f * juce::jlimit(0.0f, 10.0f, lOut->load())); }

    void processLls(float* const* io, int nc, int n)
    {
        // HPF, pad, fase, MIC (con rampa)
        const int hsel = (int) std::lround(lHpf->load());
        const bool hpfOn = hsel > 0;
        const float gIn = llsInGain(), inc = (gIn - prevLlsIn) / (float) n;
        float inPeak = 0.0f;
        for (int ch = 0; ch < nc; ++ch)
        {
            if (hpfOn) lhp[ch].setHighpass(sampleRate, hpfHz(hsel));
            float g = prevLlsIn;
            for (int i = 0; i < n; ++i)
            {
                inPeak = juce::jmax(inPeak, std::abs(io[ch][i]));
                const float v = hpfOn ? lhp[ch].process(io[ch][i]) : io[ch][i];
                io[ch][i] = v * g;
                g += inc;
            }
        }
        prevLlsIn = gIn;

        // banco LLS di DirtyDESK (stereo: in mono il canale destro e' una copia)
        lls.setDrive(llsDrive01());
        float* r = nc > 1 ? io[1] : llsBuf.getWritePointer(1);
        if (nc == 1) llsBuf.copyFrom(1, 0, io[0], n);
        float* st[2] = { io[0], r };
        lls.processStereo(st, st, n);

        // dopo il banco: MIC torna al livello di prima; ritardo pari a Marley (latenza costante)
        const float gPost = llsPostGain(), incP = (gPost - prevLlsPost) / (float) n;
        const double vuK = 1.0 - std::exp(-1.0 / (0.065 * sampleRate));
        float outPeak = 0.0f;
        for (int ch = 0; ch < nc; ++ch)
        {
            float g = prevLlsPost;
            for (int i = 0; i < n; ++i)
            {
                const float v = llsDelay[ch].process(io[ch][i] * g);
                io[ch][i] = v;
                g += incP;
                outPeak = juce::jmax(outPeak, std::abs(v));
                if (ch == 0) vuEnv += vuK * (std::abs((double) v) - vuEnv);
            }
        }
        prevLlsPost = gPost;

        // spie come DirtyDESK: rosso quando il banco supera ~50% del clip, scende in ~0,4 s
        const double secs = n / sampleRate;
        const double peak = lls.getAndResetDrivePeak();
        const double target = juce::jlimit(0.0, 1.0, (peak - 0.50) / 0.60);
        odEnv = juce::jmax(target, odEnv * std::exp(-secs / 0.4));
        llsOverdrive.store((float) odEnv);
        llsVuDb.store((float) (20.0 * std::log10(juce::jmax(vuEnv * 1.1107, 1.0e-9)) + 18.0));
        signalLed.store(inPeak > 0.003f);                       // circa -50 dBFS
        peakLed.store(odEnv > 0.5 || outPeak > 0.89f);           // banco spinto o uscita a -1 dBFS
    }

    float currentG1() const
    {
        const float db = gain->load() + (pad->load() > 0.5f ? -20.0f : 0.0f);
        return dbToGain(db) * (phase->load() > 0.5f ? -1.0f : 1.0f);
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);
        if (nc == 0) return;
        const float target = isLls() ? 1.0f : 0.0f;

        if (typeMix == target)
        {
            if (target > 0.5f) processLls(buffer.getArrayOfWritePointers(), nc, n);
            else               processValve(buffer, nc, n);
            return;
        }
        // dissolvenza Valve <-> LLS (girano entrambi per 30 ms)
        for (int ch = 0; ch < nc; ++ch) llsBuf.copyFrom(ch, 0, buffer, ch, 0, n);
        processValve(buffer, nc, n);
        float* lio[2] = { llsBuf.getWritePointer(0), llsBuf.getWritePointer(1) };
        processLls(lio, nc, n);
        for (int i = 0; i < n; ++i)
        {
            typeMix = target > typeMix ? juce::jmin(target, typeMix + typeStep) : juce::jmax(target, typeMix - typeStep);
            for (int ch = 0; ch < nc; ++ch)
            {
                float* a = buffer.getWritePointer(ch);
                a[i] += typeMix * (llsBuf.getSample(ch, i) - a[i]);
            }
        }
    }

    void processValve(juce::AudioBuffer<float>& buffer, int nc, int n)
    {
        // 1) HPF + pad/phase/gain (con rampa)
        const float g1 = currentG1();
        const bool  hpfOn = hpf->load() > 20.5f;
        const float inc1 = (g1 - prevG1) / (float) n;
        for (int ch = 0; ch < nc; ++ch)
        {
            if (hpfOn) hp[ch].setHighpass(sampleRate, hpf->load());
            auto* x = buffer.getWritePointer(ch);
            float cg = prevG1;
            for (int i = 0; i < n; ++i)
            {
                const float v = hpfOn ? hp[ch].process(x[i]) : x[i];
                x[i] = v * cg;
                cg += inc1;
            }
        }
        prevG1 = g1;

        // 2) Marley: Drive, Mix 100%, Output 0 dB (l'Output del pannello e' in processMakeup)
        marley::MarleyEngine::Params p;
        p.drive01  = juce::jlimit(0.0f, 1.0f, drive->load() / 10.0f);
        p.mix01    = 1.0f;
        p.outputDb = 0.0f;
        float* io[2] = { buffer.getWritePointer(0), nc > 1 ? buffer.getWritePointer(1) : nullptr };
        engine.process(io, nc, n, p);
    }

    void onFullyBypassed() override
    {
        engine.distortionDb.store(-100.0f);
        engine.driveLevelDb.store(-100.0f);
        llsOverdrive.store(0.0f); llsVuDb.store(-100.0f);
        signalLed.store(false); peakLed.store(false);
    }

    // OUTPUT dopo l'auto gain: con AG acceso la manopola funziona lo stesso (Valve o LLS, con la dissolvenza)
    void processMakeup(juce::AudioBuffer<float>& buffer) override
    {
        const float g = dbToGain(output->load()) * (1.0f - typeMix) + llsOutGain() * typeMix;
        applyRamp(buffer, prevOut, g);
    }

    std::atomic<float> *gain, *drive, *output, *hpf, *phase, *pad, *type;
    std::atomic<float> *lGain, *lOut, *lHpf, *lPad, *lPhase, *lMic;
    marley::MarleyEngine engine;
    Biquad hp[2], lhp[2];
    float prevG1 = 1.0f, prevOut = 1.0f;

    dirtydesk::ChannelX32 lls;
    juce::AudioBuffer<float> llsBuf;
    SimpleDelay llsDelay[2];
    float prevLlsIn = 1.0f, prevLlsPost = 1.0f, prevLlsOut = 1.0f, typeMix = 0.0f, typeStep = 0.001f;
    double odEnv = 0.0, vuEnv = 0.0;
    std::atomic<float> llsOverdrive { 0.0f }, llsVuDb { -100.0f };
    std::atomic<bool> signalLed { false }, peakLed { false };
};
