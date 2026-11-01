#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <vector>

// Ritardo intero a campione, usato per compensare la latenza nel percorso bypassato.
struct SimpleDelay
{
    std::vector<float> buf;
    int pos = 0, len = 0;

    void prepare(int delaySamples)
    {
        len = juce::jmax(0, delaySamples);
        buf.assign((size_t) len, 0.0f);
        pos = 0;
    }
    float process(float x)
    {
        if (len == 0) return x;
        const float y = buf[(size_t) pos];
        buf[(size_t) pos] = x;
        if (++pos >= len) pos = 0;
        return y;
    }
};

// Classe base di ogni stadio della catena.
//   - bypass con crossfade (15 ms); il percorso "dry" e' SEMPRE ritardato della latenza del modulo,
//     quindi attivare/disattivare un modulo non sposta la fase del resto della catena
//   - a bypass completo non si processa nulla (zero CPU)
//   - auto gain lento (~400 ms), limitato a +/-12 dB, congelato in bypass e sul silenzio
//   - le manopole di uscita / make-up (processMakeup) vengono applicate DOPO l'auto gain:
//     l'auto gain compensa solo quello che fa il modulo, non annulla la manopola Output/Gain
// Con userControls = false (trasformatore d'ingresso) non ci sono bypass ne' auto gain esposti:
// l'auto gain interno e' sempre attivo, cosi' Clean e Neve escono allo stesso volume.
class ModuleBase
{
public:
    ModuleBase(juce::AudioProcessorValueTreeState& state, const juce::String& prefix, bool userControls = true)
        : apvts(state), idPrefix(prefix)
    {
        if (userControls)
        {
            bypassParam   = param("bypass");
            autoGainParam = param("autogain");
        }
    }

    virtual ~ModuleBase() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

        bypassSmooth.reset(sampleRate, 0.015);
        bypassSmooth.setCurrentAndTargetValue(isBypassRequested() ? 1.0f : 0.0f);

        dry.setSize((int) spec.numChannels, (int) spec.maximumBlockSize, false, false, true);

        inAvg = outAvg = 1.0e-4f;
        agGain = 1.0f;

        prepareModule(spec);

        dryDelay.assign((size_t) spec.numChannels, SimpleDelay{});
        for (auto& d : dryDelay) d.prepare(getLatency());
    }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        if (n == 0) return;

        const int nc = juce::jmin(buffer.getNumChannels(), dry.getNumChannels());
        jassert(n <= dry.getNumSamples());

        const bool wantBypass = isBypassRequested();
        const bool wantAuto   = autoGainParam != nullptr ? autoGainParam->load() > 0.5f : true;

        bypassSmooth.setTargetValue(wantBypass ? 1.0f : 0.0f);

        // percorso dry, ritardato come il modulo
        for (int ch = 0; ch < nc; ++ch)
        {
            const float* in = buffer.getReadPointer(ch);
            float* d = dry.getWritePointer(ch);
            auto& dl = dryDelay[(size_t) ch];
            for (int i = 0; i < n; ++i) d[i] = dl.process(in[i]);
        }

        // bypass completo: esce il dry ritardato, il modulo non gira, auto gain congelato
        if (wantBypass && ! bypassSmooth.isSmoothing())
        {
            for (int ch = 0; ch < nc; ++ch)
                buffer.copyFrom(ch, 0, dry, ch, 0, n);
            grMeterDb.store(0.0f);
            onFullyBypassed();
            updateMeter(buffer);
            return;
        }

        const bool crossfading = bypassSmooth.isSmoothing();
        const float inMs = meanSquare(buffer);

        processModule(buffer);
        applyAutoGain(buffer, inMs, wantAuto);
        processMakeup(buffer);

        if (crossfading)
        {
            for (int i = 0; i < n; ++i)
            {
                const float b = bypassSmooth.getNextValue();   // 0 = attivo, 1 = bypass
                for (int ch = 0; ch < nc; ++ch)
                {
                    auto* w = buffer.getWritePointer(ch);
                    w[i] = dry.getSample(ch, i) * b + w[i] * (1.0f - b);
                }
            }
        }
        updateMeter(buffer);
    }

    // Latenza introdotta dal modulo, in campioni interi.
    virtual int getLatency() const { return 0; }

    // Per i meter dell'editor (letti dal thread grafico):
    float getOutputDb() const { return outputDb.load(); }                // livello d'uscita, ballistica ~300 ms
    float getGainReductionDb() const { return grMeterDb.load(); }       // <= 0
    virtual float getDistortionDb() const { return -100.0f; }           // solo il preamp: distorsione della valvola (dB)
    virtual float getVuLevel() const { return getOutputDb() + 18.0f; }  // unita' VU (0 = 0 VU); default: 0 VU = -18 dBFS
    virtual bool getSignalLed() const { return false; }                 // LLS 4000: LED SIGNAL
    virtual bool getPeakLed() const   { return false; }                 // LLS 4000: LED PEAK

protected:
    virtual void prepareModule(const juce::dsp::ProcessSpec&) {}
    virtual void processModule(juce::AudioBuffer<float>& buffer) = 0;
    virtual void processMakeup(juce::AudioBuffer<float>&) {}   // dopo l'auto gain
    virtual void onFullyBypassed() {}                           // il modulo e' escluso (per azzerare i meter)

    // guadagno con rampa lineare su tutti i canali (per le manopole di uscita)
    static void applyRamp(juce::AudioBuffer<float>& b, float& prev, float target)
    {
        if (std::abs(prev - target) < 1.0e-6f && std::abs(target - 1.0f) < 1.0e-6f) { prev = target; return; }
        b.applyGainRamp(0, b.getNumSamples(), prev, target);
        prev = target;
    }

    std::atomic<float> grMeterDb { 0.0f };   // riduzione di guadagno per i meter (dB, <= 0)

    std::atomic<float>* param(const juce::String& suffix)
    {
        auto* p = apvts.getRawParameterValue(idPrefix + "_" + suffix);
        jassert(p != nullptr);   // id non registrato in Parameters.cpp
        return p;
    }

    static float dbToGain(float db) { return std::pow(10.0f, db * 0.05f); }

    juce::AudioProcessorValueTreeState& apvts;
    juce::String idPrefix;
    double sampleRate = 44100.0;

private:
    void updateMeter(const juce::AudioBuffer<float>& b)
    {
        const float coef = 1.0f - std::exp(-(float) b.getNumSamples() / (float) (sampleRate * 0.3));
        meterMs += coef * (meanSquare(b) - meterMs);
        outputDb.store(10.0f * std::log10(meterMs + 1.0e-12f));
    }

    bool isBypassRequested() const { return bypassParam != nullptr && bypassParam->load() > 0.5f; }

    static float meanSquare(const juce::AudioBuffer<float>& b)
    {
        const int n = b.getNumSamples(), nc = b.getNumChannels();
        if (n == 0 || nc == 0) return 0.f;
        double sum = 0.0;
        for (int c = 0; c < nc; ++c)
        {
            const float* p = b.getReadPointer(c);
            for (int i = 0; i < n; ++i) sum += (double) p[i] * p[i];
        }
        return float(sum / (double) (n * nc));
    }

    void applyAutoGain(juce::AudioBuffer<float>& buffer, float inMs, bool enabled)
    {
        const int n = buffer.getNumSamples();
        const float coef = 1.0f - std::exp(-(float) n / (float) (sampleRate * 0.4));

        float target = 1.0f;
        if (enabled)
        {
            if (inMs > 1.0e-6f)   // aggiorna le medie solo con segnale (> circa -60 dBFS)
            {
                inAvg  += coef * (inMs - inAvg);
                outAvg += coef * (meanSquare(buffer) - outAvg);
            }
            if (outAvg > 1.0e-9f)
                target = juce::jlimit(0.25f, 4.0f, std::sqrt(inAvg / outAvg));   // +/-12 dB
        }

        const float newGain = agGain + (target - agGain) * coef;
        if (std::abs(newGain - 1.0f) > 1.0e-4f || std::abs(agGain - 1.0f) > 1.0e-4f)
            buffer.applyGainRamp(0, n, agGain, newGain);
        agGain = newGain;
    }

    std::atomic<float>* bypassParam   = nullptr;
    std::atomic<float>* autoGainParam = nullptr;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> bypassSmooth;
    juce::AudioBuffer<float> dry;
    std::vector<SimpleDelay> dryDelay;

    float inAvg = 1.0e-4f, outAvg = 1.0e-4f, agGain = 1.0f;
    float meterMs = 0.0f;
    std::atomic<float> outputDb { -120.0f };
};
