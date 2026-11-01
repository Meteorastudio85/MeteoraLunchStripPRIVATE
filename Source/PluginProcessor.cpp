#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DSP/TransformerModule.h"
#include "DSP/PreampModule.h"
#include "DSP/DeEsserModule.h"
#include "DSP/FetCompressorModule.h"
#include "DSP/EqModule.h"
#include "DSP/OptoModule.h"
#include "DSP/MasterModule.h"
#include "ChainOrder.h"

LunchStripProcessor::LunchStripProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createParameterLayout())
{
    modules.push_back(std::make_unique<TransformerModule>(apvts));
    modules.push_back(std::make_unique<PreampModule>(apvts));
    modules.push_back(std::make_unique<DeEsserModule>(apvts));
    modules.push_back(std::make_unique<FetCompressorModule>(apvts));
    modules.push_back(std::make_unique<EqModule>(apvts));
    modules.push_back(std::make_unique<OptoModule>(apvts));
    modules.push_back(std::make_unique<MasterModule>(apvts));
    chainOrder = apvts.getRawParameterValue("chain_order");
}

void LunchStripProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec{ sampleRate,
                                 (juce::uint32) samplesPerBlock,
                                 (juce::uint32) getTotalNumOutputChannels() };
    int latency = 0;
    for (auto& m : modules)
    {
        m->prepare(spec);
        latency += m->getLatency();
    }
    setLatencySamples(latency);
    appliedOrder = juce::jlimit(0, 23, (int) std::lround(chainOrder->load()));
    fadeIn = false;
}

bool LunchStripProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void LunchStripProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    lastProcessMs.store(juce::Time::getMillisecondCounter());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    // trasformatore e preamp, poi i moduli 2..5 nell'ordine scelto, poi il master
    const int wanted = juce::jlimit(0, 23, (int) std::lround(chainOrder->load()));
    const bool changing = wanted != appliedOrder;
    const auto ord = chain::order(appliedOrder);

    modules[(size_t) Xfmr]->process(buffer);
    modules[(size_t) Pre]->process(buffer);
    for (int m : ord) modules[(size_t) (De + m)]->process(buffer);
    modules[(size_t) Master]->process(buffer);

    const int n = buffer.getNumSamples();
    if (changing)                      // sfuma a zero con l'ordine vecchio, dal prossimo blocco quello nuovo
    {
        buffer.applyGainRamp(0, n, 1.0f, 0.0f);
        appliedOrder = wanted;
        fadeIn = true;
    }
    else if (fadeIn)
    {
        buffer.applyGainRamp(0, n, 0.0f, 1.0f);
        fadeIn = false;
    }
}

juce::AudioProcessorEditor* LunchStripProcessor::createEditor()
{
    return new LunchStripEditor(*this);
}

void LunchStripProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void LunchStripProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LunchStripProcessor();
}

//==============================================================================
// Preset del FET
//   VOCAL:    voce in primo piano, compressione costante ma naturale (circa 3-5 dB su una voce a -18 dBFS)
//             4:1, attack medio (~9 ms), release medio-veloce (~210 ms), Dry/Wet 100%
//   PARALLEL: compressione molto accentuata da miscelare al dry: "all buttons" del 1176 (vedi FetCompressorModule),
//             Input +12 dB, attack veloce, release veloce, Dry/Wet 50%
const char* const LunchStripProcessor::kFetPresetIds[6] = { "fet_input", "fet_output", "fet_attack", "fet_release", "fet_ratio", "fet_mix" };

float LunchStripProcessor::getParam(const juce::String& id) const
{
    if (auto* v = apvts.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

void LunchStripProcessor::setParam(const juce::String& id, float value)
{
    if (auto* p = apvts.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(value));
        p->endChangeGesture();
    }
}

void LunchStripProcessor::setFetPreset(FetPreset which, bool on)
{
    const juce::String mine  = which == FetPreset::Vocal ? "fet_vocal" : "fet_parallel";
    const juce::String other = which == FetPreset::Vocal ? "fet_parallel" : "fet_vocal";

    if (on)
    {
        // salva le manopole solo se non c'era gia' un preset acceso (cosi' si torna sempre alle tue)
        if (! fetSnapshotValid)
        {
            for (int i = 0; i < 6; ++i) fetSnapshot[i] = getParam(kFetPresetIds[i]);
            fetSnapshotValid = true;
        }
        setParam(other, 0.0f);
        setParam(mine, 1.0f);

        if (which == FetPreset::Vocal)
        {
            setParam("fet_input",   4.0f);
            setParam("fet_output",  0.0f);
            setParam("fet_attack",  5.0f);
            setParam("fet_release", 6.0f);
            setParam("fet_ratio",   0.0f);    // 4:1
            setParam("fet_mix",   100.0f);
        }
        else
        {
            setParam("fet_input",  12.0f);
            setParam("fet_output",  0.0f);
            setParam("fet_attack",  9.0f);
            setParam("fet_release", 8.0f);
            setParam("fet_mix",    50.0f);   // il ratio e' "all buttons" finche' il LED e' acceso
        }
    }
    else
    {
        setParam(mine, 0.0f);
        if (fetSnapshotValid)
            for (int i = 0; i < 6; ++i) setParam(kFetPresetIds[i], fetSnapshot[i]);
        fetSnapshotValid = false;
    }
}

void LunchStripProcessor::leaveFetParallel()
{
    if (getParam("fet_parallel") > 0.5f)
    {
        setParam("fet_parallel", 0.0f);
        fetSnapshotValid = false;
    }
}
