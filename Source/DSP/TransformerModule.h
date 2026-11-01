#pragma once
#include "ModuleBase.h"
#include "Biquad.h"
#include "Saturators.h"

// Trasformatore d'ingresso: scelta unica Clean / Neve, prima dei sei moduli.
// UN algoritmo, parametri interpolati da "character" (0 = Clean, 1 = Neve):
//   pre-enfasi bassi -> saturazione (oversampling 4x) -> de-enfasi -> roll-off alto
// Riferimenti Airwindows: Channel9, Desk4.
class TransformerModule : public ModuleBase
{
public:
    explicit TransformerModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "xfmr", false)
    {
        modeParam = param("neve");
    }

    int getLatency() const override { return juce::roundToInt(os.getLatencyInSamples()); }

private:
    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        os.initProcessing((size_t) spec.maximumBlockSize);
        os.reset();

        character.reset(spec.sampleRate, 0.04);   // 40 ms
        character.setCurrentAndTargetValue(modeParam->load() > 0.5f ? 1.0f : 0.0f);

        for (int i = 0; i < 2; ++i) { pre[i].reset(); de[i].reset(); hf[i].reset(); dc[i].prepare(spec.sampleRate); }
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);

        character.setTargetValue(modeParam->load() > 0.5f ? 1.0f : 0.0f);
        const float c = character.skip(n);   // valore a fine blocco

        const float satAmount  = juce::jmap(c, 0.05f, 1.0f);
        const float lowBoostDb = 5.0f * c;
        const float hfCutoff   = juce::jmap(c, 40000.0f, 20000.0f);

        // 1) pre-enfasi dei bassi (frequenza di campionamento originale)
        for (int ch = 0; ch < nc; ++ch)
        {
            pre[ch].setLowShelf(sampleRate, 80.0, +lowBoostDb);
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < n; ++i) d[i] = pre[ch].process(d[i]);
        }

        // 2) saturazione a 4x: le armoniche create sopra Nyquist vengono filtrate prima di tornare giu'
        juce::dsp::AudioBlock<float> block(buffer);
        auto up = os.processSamplesUp(block);
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            auto* d = up.getChannelPointer(ch);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = satAmount * lunch::transformerSaturate(d[i]) + (1.0f - satAmount) * d[i];
        }
        os.processSamplesDown(block);

        // 3) de-enfasi, roll-off alto, blocco continua
        for (int ch = 0; ch < nc; ++ch)
        {
            de[ch].setLowShelf(sampleRate, 80.0, -lowBoostDb);
            hf[ch].setLowpass (sampleRate, hfCutoff);
            auto* d = buffer.getWritePointer(ch);
            for (int i = 0; i < n; ++i)
                d[i] = dc[ch].process(hf[ch].process(de[ch].process(d[i])));
        }
    }

    std::atomic<float>* modeParam = nullptr;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> character;
    juce::dsp::Oversampling<float> os { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true };
    Biquad pre[2], de[2], hf[2];
    lunch::DcBlocker dc[2];
};
