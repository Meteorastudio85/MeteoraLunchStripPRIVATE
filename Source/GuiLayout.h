#pragma once
// Coordinate in pixel dell'immagine di sfondo (2624 x 1404). Generato dalle misure sull'immagine.
namespace gui
{
constexpr float kImgW = 2624.0f, kImgH = 1404.0f;

struct KnobSpec
{
    const char* tex;        // nome della texture (Assets/knob_<tex>.png)
    const char* paramId;
    float cx, cy;           // centro
    float size;             // diametro della texture (px immagine)
    float baseDeg;          // angolo dell'indicatore stampato nella texture (0 = su)
    bool  hasCap;           // Master: la ghiera ruota, il logo al centro resta fermo
    const char* tip;
    int   eqPopup;          // 1 = tasto destro apre il popup EQ (shelf/bell)
    float startDeg, endDeg; // corsa della manopola in gradi (0 = su, senso orario)
};

constexpr KnobSpec kKnobs[] = {
    { "pre_gain", "pre_gain", 211, 766, 152, -35.0f, false, "Gain", 0, -135.0f, 135.0f },
    { "pre_drive", "pre_drive", 445, 764, 142, 21.0f, false, "Drive (Marley)", 0, -135.0f, 135.0f },
    { "pre_output", "pre_output", 326, 949, 135, 42.0f, false, "Output", 0, -135.0f, 135.0f },
    { "pre_hpf", "pre_hpf", 148, 1140, 92, -141.0f, false, "HPF", 0, -135.0f, 135.0f },
    { "de_freq", "de_freq", 762, 697, 160, 0.0f, false, "Frequency", 0, -100.0f, 100.0f },
    { "de_reduction", "de_reduction", 762, 938, 160, 0.0f, false, "Reduction", 0, -140.0f, 140.0f },
    { "fet_input", "fet_input", 1050, 563, 118, 39.0f, false, "Input", 0, -135.0f, 135.0f },
    { "fet_output", "fet_output", 1246, 560, 118, -49.0f, false, "Output", 0, -135.0f, 135.0f },
    { "fet_attack", "fet_attack", 1048, 781, 118, 111.0f, false, "Attack", 0, -135.0f, 135.0f },
    { "fet_release", "fet_release", 1252, 780, 118, -138.0f, false, "Release", 0, -135.0f, 135.0f },
    { "fet_mix", "fet_mix", 1021, 1162, 72, 137.0f, false, "Dry/Wet (0 = dry only, 100 = compressed only)", 0, -135.0f, 135.0f },
    { "eq_high", "eq_high_gain", 1665, 361, 122, 0.0f, false, "High (right-click: Bell or High shelf)", 1, -135.0f, 135.0f },
    { "eq_mid", "eq_mid_gain", 1665, 599, 122, 0.0f, false, "Mid", 0, -135.0f, 135.0f },
    { "eq_low", "eq_low_gain", 1665, 839, 122, 0.0f, false, "Low (right-click: shelf / bell)", 1, -135.0f, 135.0f },
    { "opto_peak", "opto_peak", 1978, 585, 186, 0.0f, false, "Peak Reduction", 0, -150.0f, 150.0f },
    { "opto_gain", "opto_gain", 1980, 882, 186, 0.0f, false, "Gain", 0, -150.0f, 150.0f },
    { "master_out", "master_output", 2372, 785, 200, 0.0f, true, "Output", 0, -148.0f, 148.0f },
};
} // namespace gui
