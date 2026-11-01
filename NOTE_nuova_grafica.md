# Nuova grafica (moduli ridisegnati) - note di integrazione

Sostituiti: `Assets/background.jpg` (6 moduli nuovi nella cornice originale), tutte le texture `knob_*.png`,
`pultec_*` (pannello Placebo con Drive al posto di EQ IN/OUT, manopole, levetta POWER) e le miniature `eq_thumb_*`.

Codice: `GuiLayout.h` (nuova tabella manopole con corsa propria), `PluginEditor.cpp` (VU con scala stampata,
valvola, LED de-esser, pulsanti, EQ Neve a scatti, Placebo), `Parameters.cpp` (default Mid 1600 Hz e Low 110 Hz,
cosi' cadono su uno scatto stampato).

NON compilato in questo ambiente (JUCE non disponibile): provare la build e correggere eventuali errori di sintassi.
