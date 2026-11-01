#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Editor grafico: l'immagine del mockup e' lo sfondo; sopra ci sono controlli trasparenti
// (manopole che ruotano la texture reale, pulsanti, LED) e le lancette dei VU disegnate dal codice.
// Tutte le coordinate sono in pixel dell'immagine (vedi GuiLayout.h) e vengono scalate alla finestra.
class LunchStripEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LunchStripEditor(LunchStripProcessor&);
    ~LunchStripEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

private:
    struct Impl;
    void timerCallback() override;

    LunchStripProcessor& proc;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LunchStripEditor)
};
