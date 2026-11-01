#include "Parameters.h"

using namespace juce;

namespace
{
using Params = std::vector<std::unique_ptr<RangedAudioParameter>>;

void addCommon(Params& p, const String& prefix, const String& name, bool autoGainDefault = true)
{
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ prefix + "_bypass", 1 },   name + " Bypass",    false));
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ prefix + "_autogain", 1 }, name + " Auto Gain", autoGainDefault));
}

void addFloat(Params& p, const String& id, const String& name,
              float lo, float hi, float def, float step = 0.1f, float skew = 1.0f, const String& unit = {})
{
    p.push_back(std::make_unique<AudioParameterFloat>(
        ParameterID{ id, 1 }, name, NormalisableRange<float>(lo, hi, step, skew), def,
        AudioParameterFloatAttributes().withLabel(unit)));
}

void addBool(Params& p, const String& id, const String& name, bool def = false)
{
    p.push_back(std::make_unique<AudioParameterBool>(ParameterID{ id, 1 }, name, def));
}

void addChoice(Params& p, const String& id, const String& name, const StringArray& items, int def = 0)
{
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ id, 1 }, name, items, def));
}
} // namespace

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Params p;

    // 0 - Trasformatore d'ingresso: scelta UNICA all'inizio della catena (Clean / Neve).
    //     Non e' uno dei sei moduli: niente bypass ne' auto gain esposti.
    addBool  (p, "xfmr_neve", "Input Transformer Neve", true);   // false = Clean

    // 1 - Valve preamp
    addCommon(p, "pre", "Preamp");
    addFloat (p, "pre_gain",   "Preamp Gain",   -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "pre_drive",  "Preamp Drive",    0.f, 10.f, 2.f, 0.1f);
    addFloat (p, "pre_output", "Preamp Output", -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "pre_hpf",    "Preamp HPF",     20.f, 300.f, 20.f, 1.f, 0.4f, "Hz");  // 20 = off
    addBool  (p, "pre_phase",  "Preamp Phase");
    addBool  (p, "pre_pad",    "Preamp Pad -20dB");
    // 1b - LLS 4000 PREAMP (stadio del banco LLS di Meteora DirtyDESK): si sceglie con la linguetta del modulo 1
    addChoice(p, "pre_type",   "Preamp Type", { "Valve", "LLS 4000" }, 0);
    addFloat (p, "lls_gain",   "LLS Gain (Drive)", 0.f, 10.f, 3.f, 0.1f);
    addFloat (p, "lls_output", "LLS Output",       0.f, 10.f, 5.f, 0.1f);        // 5 = 0 dB, 3 dB a scatto
    addChoice(p, "lls_hpf",    "LLS HPF", { "Off", "40 Hz", "80 Hz", "150 Hz", "300 Hz" }, 0);
    addBool  (p, "lls_pad",    "LLS Pad -20dB");
    addBool  (p, "lls_phase",  "LLS Phase");
    addBool  (p, "lls_mic",    "LLS Mic (on) / Line (off)");

    // ordine dei moduli 2..5 (De-Esser, FET, EQ, Opto): una delle 24 permutazioni, si cambia dalle frecce sotto i moduli
    p.push_back(std::make_unique<AudioParameterInt>(ParameterID{ "chain_order", 1 }, "Module Order", 0, 23, 0,
                                                    AudioParameterIntAttributes().withAutomatable(false)));

    // 2 - De-esser
    addCommon(p, "de", "De-Esser", false);   // auto gain spento: altrimenti restituirebbe parte della riduzione
    addFloat (p, "de_freq",      "De-Esser Frequency", 2000.f, 12000.f, 6500.f, 1.f, 0.5f, "Hz");
    addFloat (p, "de_reduction", "De-Esser Reduction",    0.f,    10.f,    4.f, 0.1f);
    addBool  (p, "de_listen",    "De-Esser Listen");

    // 3 - FET compressor (stile 1176, vedi DynamicsCore.h)
    addCommon(p, "fet", "FET Comp");
    addFloat (p, "fet_input",   "FET Input",  -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");   // quanto "spingi" dentro la soglia fissa
    addFloat (p, "fet_output",  "FET Output", -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "fet_attack",  "FET Attack",   0.f, 10.f, 5.f, 0.1f);                // 10 = piu' veloce
    addFloat (p, "fet_release", "FET Release",  0.f, 10.f, 5.f, 0.1f);                // 10 = piu' veloce
    addChoice(p, "fet_ratio",   "FET Ratio",   { "4:1", "8:1", "12:1", "20:1" }, 0);
    addFloat (p, "fet_mix",     "FET Dry/Wet",  0.f, 100.f, 100.f, 1.f, 1.f, "%");
    addBool  (p, "fet_vocal",   "FET Preset Vocal");                                     // LED: preset vocale caricato
    addBool  (p, "fet_parallel","FET Preset Parallel");                                  // LED: preset compressione forte ("all buttons")

    // 4 - Equalizer
    addCommon(p, "eq", "EQ", false);         // auto gain spento: altrimenti annullerebbe boost e tagli
    addFloat (p, "eq_high_gain", "EQ High Gain", -15.f, 15.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "eq_high_freq", "EQ High Freq", 3000.f, 16000.f, 8000.f, 1.f, 0.5f, "Hz");
    addBool  (p, "eq_high_shelf","EQ High Shelf", true);
    addFloat (p, "eq_mid_gain",  "EQ Mid Gain",  -15.f, 15.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "eq_mid_freq",  "EQ Mid Freq",  200.f, 8000.f, 1600.f, 1.f, 0.5f, "Hz");
    addFloat (p, "eq_mid_q",     "EQ Mid Q",     0.3f, 5.f, 1.f, 0.01f);
    addFloat (p, "eq_low_gain",  "EQ Low Gain",  -15.f, 15.f, 0.f, 0.1f, 1.f, "dB");
    addFloat (p, "eq_low_freq",  "EQ Low Freq",  20.f, 600.f, 110.f, 1.f, 0.5f, "Hz");
    addBool  (p, "eq_low_shelf", "EQ Low Shelf", true);
    addFloat (p, "eq_hpf",       "EQ HPF",       20.f, 500.f, 20.f, 1.f, 0.4f, "Hz");
    addFloat (p, "eq_lpf",       "EQ LPF",       2000.f, 20000.f, 20000.f, 1.f, 0.4f, "Hz");
    // tipo di EQ: il pannello arancione (Meteora, curve Winter Dreams) oppure il Pultec (pannello blu "BASED PLACEBO")
    addChoice(p, "eq_type",      "EQ Type",      { "Meteora", "Pultec" }, 0);
    addChoice(p, "pul_lf_freq",  "Pultec Low Freq",  { "20 Hz", "30 Hz", "60 Hz", "100 Hz" }, 3);   // default di Placebo: 100 Hz
    addFloat (p, "pul_lf_boost", "Pultec LF Boost",  0.f, 10.f, 0.f, 0.1f);
    addFloat (p, "pul_lf_atten", "Pultec LF Atten",  0.f, 10.f, 0.f, 0.1f);
    addChoice(p, "pul_hf_freq",  "Pultec High Freq", { "3 kHz", "4 kHz", "5 kHz", "8 kHz", "10 kHz", "12 kHz", "16 kHz" }, 4);   // default di Placebo: 10 kHz
    addFloat (p, "pul_hf_boost", "Pultec HF Boost",  0.f, 10.f, 0.f, 0.1f);
    addFloat (p, "pul_hf_bw",    "Pultec Bandwidth", 0.f, 10.f, 5.f, 0.1f);
    addFloat (p, "pul_hf_atten", "Pultec HF Atten",  0.f, 10.f, 0.f, 0.1f);
    addFloat (p, "pul_drive",    "Pultec Drive",     0.f, 10.f, 0.f, 0.1f);

    // 5 - Opto compressor
    addCommon(p, "opto", "Opto");
    addFloat (p, "opto_peak", "Opto Peak Reduction", 0.f, 100.f, 30.f, 0.1f);
    addFloat (p, "opto_gain", "Opto Gain", -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");
    addBool  (p, "opto_limit","Opto Limit Mode");

    // 6 - Master out
    addCommon(p, "master", "Master", false);   // il Master non ha auto gain: il pulsante e' stato tolto
    addFloat (p, "master_output", "Master Output", -20.f, 20.f, 0.f, 0.1f, 1.f, "dB");
    addBool  (p, "master_limiter","Master Limiter", true);
    addBool  (p, "master_link",   "Master Stereo Link", true);

    AudioProcessorValueTreeState::ParameterLayout layout;
    for (auto& param : p)
        layout.add(std::move(param));
    return layout;
}
