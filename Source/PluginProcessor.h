#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "DSP/ModuleBase.h"

class LunchStripProcessor : public juce::AudioProcessor
{
public:
    LunchStripProcessor();
    ~LunchStripProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "THE LUNCH STRIP"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    enum Stage { Xfmr = 0, Pre, De, Fet, Eq, Opto, Master };
    const ModuleBase& getModule(Stage st) const { return *modules[(size_t) st]; }

    // true se il DAW sta mandando audio (processBlock chiamato negli ultimi 400 ms): accende la valvola
    bool isProcessingAudio() const
    {
        return juce::Time::getMillisecondCounter() - lastProcessMs.load() < 400u;
    }

    // Preset del FET (LED VOCAL e PARALLEL sul pannello), da chiamare dal thread dell'interfaccia.
    //   acceso: salva le manopole del FET e carica il preset (l'altro LED si spegne)
    //   spento: rimette le manopole com'erano prima del preset
    enum class FetPreset { Vocal, Parallel };
    void setFetPreset(FetPreset which, bool on);
    // un tasto RATIO premuto a mano esce dal preset Parallel ("all buttons") senza ripristinare
    void leaveFetParallel();

    juce::AudioProcessorValueTreeState apvts;   // deve stare PRIMA di modules

private:
    // ordine del segnale: trasformatore -> preamp -> de-esser -> FET -> EQ -> opto -> master
    std::vector<std::unique_ptr<ModuleBase>> modules;
    std::atomic<juce::uint32> lastProcessMs { 0 };

    // ordine dei moduli 2..5 (vedi ChainOrder.h): quando cambia, un blocco sfuma a zero col vecchio ordine
    // e il successivo riparte da zero col nuovo, cosi' lo scambio non fa click
    std::atomic<float>* chainOrder = nullptr;
    int appliedOrder = 0;
    bool fadeIn = false;

    void setParam(const juce::String& id, float value);
    float getParam(const juce::String& id) const;
    static const char* const kFetPresetIds[6];
    float fetSnapshot[6] {};
    bool  fetSnapshotValid = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LunchStripProcessor)
};
