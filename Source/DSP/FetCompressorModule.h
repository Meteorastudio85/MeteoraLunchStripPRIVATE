#pragma once
#include "ModuleBase.h"
#include "fet1179/ChronoEngine.h"

// FET compressor: motore = meteoraFET1179 (Source/DSP/fet1179, Meteora Plugins), lo stesso DSP del plugin:
//   INPUT -> Coils2 d'ingresso -> Logical4 (FET, attack/release separati) ->
//   [curva Pultec fissa + Tube2, col suo blend] -> Coils2 d'uscita
//
// Mappatura del pannello della Lunch Strip sui comandi di meteoraFET1179:
//   INPUT  -20..+20 dB          -> INPUT (alza per comprimere di piu', come sul 1176)
//   ATTACK / RELEASE 0..10      -> ATTACK / RELEASE 0..1 (in senso orario = piu' veloce)
//   RATIO 4 / 8 / 12 / 20       -> tasti 4:1 / 8:1 / 12:1 / 20:1 (rapporti di Logical4)
//   DRY/WET 0..100 %            -> DRY/WET master
//   blend Pultec + Tube2        -> fisso al 30% (default di meteoraFET1179)
//   LED VOCAL / PARALLEL        -> preset (li carica l'editor, vedi LunchStripProcessor::setFetPreset);
//                                  con PARALLEL acceso il FET lavora in "all buttons" (tutti i tasti premuti)
//   HPF / LPF del sidechain     -> spenti (il pannello non li ha)
//   OUTPUT                      -> applicato DOPO l'auto gain (la manopola funziona anche con AG acceso)
// Il VU mostra la gain reduction calcolata come in meteoraFET1179 (energia prima/dopo il compressore).
class FetCompressorModule : public ModuleBase
{
public:
    explicit FetCompressorModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "fet")
    {
        input = param("input");   output = param("output");
        attack = param("attack"); release = param("release");
        ratio = param("ratio");   mix = param("mix");       parallel = param("parallel");
    }

private:
    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        engine.prepare(spec.sampleRate);
        prevOut = dbToGain(output->load());
        grSmooth = 0.0;
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);
        if (nc == 0) return;

        const int ratioSel = parallel->load() > 0.5f ? 4                                   // "all buttons"
                                                     : juce::jlimit(0, 3, (int) std::lround(ratio->load()));
        engine.setParams(input->load(), 0.0, 0.0, ratioSel,
                         attack->load() / 10.0, release->load() / 10.0,
                         0.30,                                          // blend Pultec + Tube2 (default FET1179)
                         0.0,                                           // OUTPUT: in processMakeup
                         juce::jlimit(0.0, 1.0, mix->load() / 100.0));
        engine.preE = engine.postE = 0.0;

        float* l = buffer.getWritePointer(0);
        float* r = nc > 1 ? buffer.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            float x[2] = { l[i], r != nullptr ? r[i] : l[i] };
            engine.process(x, 1.0);
            l[i] = x[0];
            if (r != nullptr) r[i] = x[1];
        }

        const double gr = engine.preE > 1.0e-9 ? 10.0 * std::log10(juce::jmax(engine.postE, 1.0e-20) / engine.preE) : 0.0;
        grSmooth = juce::jmin(0.0, gr);
        grMeterDb.store((float) grSmooth);
    }

    void processMakeup(juce::AudioBuffer<float>& buffer) override
    {
        applyRamp(buffer, prevOut, dbToGain(output->load()));
    }

    std::atomic<float> *input, *output, *attack, *release, *ratio, *mix, *parallel;
    fet1179::ChronoEngine engine;
    float prevOut = 1.0f;
    double grSmooth = 0.0;
};
