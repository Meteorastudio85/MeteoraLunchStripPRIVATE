#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Ogni modulo ha un prefisso: xfmr, pre, de, fet, eq, opto, master
// e due parametri comuni: <prefisso>_bypass e <prefisso>_autogain.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
