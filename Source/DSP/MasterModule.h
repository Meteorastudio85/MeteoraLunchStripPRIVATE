#pragma once
#include "ModuleBase.h"

// Master out: Output + limiter soft (placeholder; da sostituire con logica tipo Airwindows ClipOnly2).
// Output e limiter stanno in processMakeup (dopo l'auto gain), cosi' la manopola Output non viene annullata
// dall'auto gain e il limiter resta l'ultimo stadio della catena.
class MasterModule : public ModuleBase
{
public:
    explicit MasterModule(juce::AudioProcessorValueTreeState& s) : ModuleBase(s, "master")
    {
        output  = param("output");
        limiter = param("limiter");
        link    = param("link");
    }

private:
    void prepareModule(const juce::dsp::ProcessSpec&) override { prevG = dbToGain(output->load()); }

    void processModule(juce::AudioBuffer<float>&) override {}

    void processMakeup(juce::AudioBuffer<float>& buffer) override
    {
        const int n = buffer.getNumSamples();
        const float g = dbToGain(output->load());
        const bool lim = limiter->load() > 0.5f;
        const bool linked = link->load() > 0.5f && buffer.getNumChannels() > 1;

        // Stereo link: il limiter guarda il canale piu' forte e applica la stessa riduzione a entrambi,
        // cosi' l'immagine stereo non si sposta quando uno dei due canali satura.
        if (linked && lim)
        {
            auto* l = buffer.getWritePointer(0);
            auto* r = buffer.getWritePointer(1);
            float cg = prevG;
            const float inc = (g - prevG) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const float a = l[i] * cg, b = r[i] * cg;
                const float peak = std::max(std::abs(a), std::abs(b));
                const float red = peak > 1.0e-9f ? softLimit(peak) / peak : 1.0f;
                l[i] = a * red;
                r[i] = b * red;
                cg += inc;
            }
            for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                buffer.applyGainRamp(ch, 0, n, prevG, g);
            prevG = g;
            return;
        }

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* x = buffer.getWritePointer(ch);
            float cg = prevG;
            const float inc = (g - prevG) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                float s = x[i] * cg;
                x[i] = lim ? softLimit(s) : s;
                cg += inc;
            }
        }
        prevG = g;
    }

    // lineare fino a 0.8, poi curva tanh che tende a 1.0 (derivata continua nel ginocchio)
    static float softLimit(float x)
    {
        constexpr float th = 0.8f, r = 1.0f - th;
        const float a = std::abs(x);
        if (a <= th) return x;
        return std::copysign(th + r * std::tanh((a - th) / r), x);
    }

    std::atomic<float> *output, *limiter, *link;
    float prevG = 1.0f;
};
