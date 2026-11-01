#pragma once
#include "ModuleBase.h"
#include "DynamicsCore.h"

// De-esser split-band (motore: lunch::DeEssCore in DynamicsCore.h).
//   FREQUENZA = dove cercare le sibilanti (2..12 kHz); la riduzione agisce da poco sotto quella frequenza in su
//   REDUCTION 0..10 = quanto intervenire (0 = spento)
//   LISTEN = ascolti il sidechain, cioe' la banda che il de-esser sta controllando
// Il meter a LED mostra la riduzione reale.
class DeEsserModule : public ModuleBase
{
public:
    explicit DeEsserModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "de")
    {
        freq = param("freq");  reduction = param("reduction");  listen = param("listen");
    }

private:
    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        core.prepare(spec.sampleRate);
        smoothFreq = freq->load();
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);
        if (nc == 0) return;

        // la frequenza si muove dolcemente (niente scatti se giri la manopola mentre suona)
        const float target = freq->load();
        smoothFreq += (target - smoothFreq) * juce::jmin(1.0f, (float) n / (float) (sampleRate * 0.03));
        core.set(smoothFreq, reduction->load());
        const bool lis = listen->load() > 0.5f;

        float* l = buffer.getWritePointer(0);
        float* r = nc > 1 ? buffer.getWritePointer(1) : nullptr;
        double minGr = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float a = l[i], b = r != nullptr ? r[i] : a;
            core.tick(a, b, lis, r != nullptr);
            l[i] = a;
            if (r != nullptr) r[i] = b;
            minGr = std::min(minGr, core.gr);
        }
        grMeterDb.store((float) minGr);
    }

    std::atomic<float> *freq, *reduction, *listen;
    lunch::DeEssCore core;
    float smoothFreq = 6500.0f;
};
