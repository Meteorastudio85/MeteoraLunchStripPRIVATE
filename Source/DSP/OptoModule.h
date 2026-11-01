#pragma once
#include "ModuleBase.h"
#include "DynamicsCore.h"

// Opto stile LA-2A (motore: lunch::OptoCore in DynamicsCore.h).
//   PEAK REDUCTION 0..100 = quanto comprimi (0 = niente)
//   GAIN = make-up, applicato DOPO l'auto gain (quindi funziona anche con AG acceso)
//   COMPRESS / LIMIT
// Il VU di questo modulo mostra la GAIN REDUCTION: a riposo la lancetta sta ferma sullo 0
// e scende verso sinistra solo quando il compressore lavora.
class OptoModule : public ModuleBase
{
public:
    explicit OptoModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "opto")
    {
        peak = param("peak");  gain = param("gain");  limit = param("limit");
    }

private:
    void prepareModule(const juce::dsp::ProcessSpec& spec) override
    {
        core.prepare(spec.sampleRate);
        smoothPeak = peak->load();
        prevGain = dbToGain(gain->load());
    }

    void processModule(juce::AudioBuffer<float>& buffer) override
    {
        const int n  = buffer.getNumSamples();
        const int nc = juce::jmin(buffer.getNumChannels(), 2);
        if (nc == 0) return;

        smoothPeak += (peak->load() - smoothPeak) * juce::jmin(1.0f, (float) n / (float) (sampleRate * 0.03));
        core.set(smoothPeak, limit->load() > 0.5f);

        float* l = buffer.getWritePointer(0);
        float* r = nc > 1 ? buffer.getWritePointer(1) : nullptr;
        double minGr = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double inL = l[i], inR = r != nullptr ? (double) r[i] : inL;
            const float g = (float) core.tick(inL, inR);
            l[i] *= g;
            if (r != nullptr) r[i] *= g;
            minGr = std::min(minGr, core.currentGrDb());
        }
        grMeterDb.store((float) minGr);
    }

    void processMakeup(juce::AudioBuffer<float>& buffer) override
    {
        applyRamp(buffer, prevGain, dbToGain(gain->load()));
    }

    std::atomic<float> *peak, *gain, *limit;
    lunch::OptoCore core;
    float smoothPeak = 30.0f, prevGain = 1.0f;
};
