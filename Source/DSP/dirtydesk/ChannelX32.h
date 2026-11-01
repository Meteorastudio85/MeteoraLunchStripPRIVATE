#pragma once
// Copiato da Meteora DirtyDESK (Meteora Plugins): stadio del banco (qui si usa il profilo LLS).
// Unico cambiamento: reso header-only (funzioni inline) e racchiuso nel namespace dirtydesk.
// =============================================================================
//  Meteora Plugins - Meteora DirtyDESK
//  Simulazione del canale del banco (DESK STYLE) + 32 canali con tolleranze proprie
// =============================================================================
#include "MeteoraDSPUtils.h"

namespace dirtydesk
{

enum ConsoleType
{
    kNeve = 0,   // a schermo: "Niv"
    kAPI,        // a schermo: "Epai"
    kSSL,        // a schermo: "LLS"
    kMackie,     // a schermo: "Mecki"
    kTeac,       // a schermo: "Tek"
    kNumConsoleTypes
};

class ChannelX32
{
public:
    ChannelX32();

    void reset();
    void setSampleRate(double sampleRate);
    void setConsoleType(int type);       // vedi ConsoleType
    void setChannelID(int id);           // 1..32
    void setDrive(double drive01);       // 0..1 (pomello DRIVE), con smoothing interno

    int  getConsoleType() const { return consoleType; }
    static const char* getConsoleName(int type);   // "Niv", "Epai", "LLS", "Mecki", "Tek"

    void processStereo(float** inputs, float** outputs, int sampleFrames);

    // Quanto lo stadio del banco e' spinto (picco relativo al clip, 1.0 = clip pieno).
    // Legge e azzera: chiamarlo una volta per blocco dal thread audio.
    double getAndResetDrivePeak() { const double p = drivePeak; drivePeak = 0.0; return p; }

private:
    void updateCoefficients();

    struct Side
    {
        meteora::Biquad    lowShelf, peak, highShelf;  // impronta tonale del banco
        meteora::OnePoleLP fluxLP;                     // trasformatori (Niv / Epai / Tek)
        meteora::OnePoleHP dcBlock;
        meteora::Allpass1  phase;
        meteora::OnePoleLP rollOff;
        bool   useRollOff = false;
        double tolDrive = 1.0;                         // tolleranze del canale
        double bias = 0.0, biasOut = 0.0, biasSlope = 1.0;
        void reset() { lowShelf.reset(); peak.reset(); highShelf.reset(); fluxLP.reset();
                       dcBlock.reset(); phase.reset(); rollOff.reset(); }
    };

    Side   side[2];
    double sampleRate  = 44100.0;
    int    consoleType = kSSL;
    int    channelID   = 1;

    meteora::Smoother drive;

    // derivati dal profilo
    double clipLevel = 2.0, invClip = 0.5, maxDriveDb = 18.0, knee = 3.0;
    double fluxAmount = 0.0, fluxDrive = 0.2;
    double drivePeak = 0.0;
};

using namespace meteora;


namespace profiles
{
    // Profili dei banchi. Sono un'interpretazione del carattere noto di ogni console
    // (non una misura di laboratorio): valori volutamente sottili, come sui banchi veri.
    struct ConsoleProfile
    {
        const char* name;
        double lowShelfHz, lowShelfDb;
        double peakHz, peakQ, peakDb;
        double highShelfHz, highShelfDb;
        double lowCutHz, rollOffHz, phaseHz;
        double flux;        // quanto lavorano i trasformatori sulle basse
        double asym;        // asimmetria -> 2a armonica
        double knee;        // morbidezza del clip (2 morbido ... 5 duro)
        double headroomDb;  // margine sopra 0 dBFS prima che il banco saturi
        double maxDriveDb;  // escursione del pomello DRIVE
    };

    inline const ConsoleProfile kProfiles[kNumConsoleTypes] =
    {
        //  name     LS Hz  LS dB   Pk Hz  Q    Pk dB   HS Hz   HS dB   LoCut RollOff  Phase  flux  asym  knee hdrm drv
        { "Niv",     90.0,  0.7,  2500.0, 0.6,  0.3,  16000.0, -0.3,  12.0, 26000.0, 30.0, 0.60, 0.35, 2.5, 8.0, 20.0 }, // Neve: trasformatori, basse piene, morbido
        { "Epai",    60.0,  0.4,  1200.0, 0.7,  0.5,  12000.0,  0.2,  15.0, 32000.0, 22.0, 0.35, 0.15, 3.0, 7.0, 18.0 }, // API: medie avanti, punch
        { "LLS",     50.0,  0.0,  3500.0, 0.5,  0.3,  10000.0,  0.4,   8.0, 40000.0, 10.0, 0.10, 0.05, 4.0, 8.0, 18.0 }, // SSL: pulito, brillante, clip netto
        { "Mecki",   80.0,  0.3,   400.0, 0.8, -0.3,  12000.0,  0.2,  18.0, 30000.0, 15.0, 0.00, 0.08, 5.0, 4.0, 18.0 }, // Mackie: integrati, clip duro
        { "Tek",    120.0,  0.5,   250.0, 0.7,  0.4,   9000.0, -0.7,  25.0, 18000.0, 25.0, 0.25, 0.25, 2.5, 6.0, 20.0 }, // Teac: semi-pro, alte morbide
    };
}

inline const char* ChannelX32::getConsoleName(int type)
{
    return (type >= 0 && type < kNumConsoleTypes) ? profiles::kProfiles[type].name : "";
}

inline ChannelX32::ChannelX32()
{
    drive.snap(0.30);
    updateCoefficients();
    reset();
}

inline void ChannelX32::reset()
{
    for (auto& s : side) s.reset();
    drive.snap(drive.target);
}

inline void ChannelX32::setSampleRate(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    drive.setTime(30.0, sampleRate);
    updateCoefficients();
}

inline void ChannelX32::setConsoleType(int type)
{
    if (type >= 0 && type < kNumConsoleTypes && type != consoleType)
    {
        consoleType = type;
        updateCoefficients();
    }
}

inline void ChannelX32::setChannelID(int id)
{
    if (id >= 1 && id <= 32 && id != channelID)
    {
        channelID = id;
        updateCoefficients();   // nuova "impronta" analogica del canale
    }
}

inline void ChannelX32::setDrive(double drive01)
{
    drive.setTarget(clamp(drive01, 0.0, 1.0));
}

inline void ChannelX32::updateCoefficients()
{
    const profiles::ConsoleProfile& p = profiles::kProfiles[consoleType];

    clipLevel  = dbToGain(p.headroomDb);
    invClip    = 1.0 / clipLevel;
    maxDriveDb = p.maxDriveDb;
    knee       = p.knee;
    fluxAmount = p.flux;
    fluxDrive  = 0.05 + p.flux * 0.8;

    for (int ch = 0; ch < 2; ++ch)
    {
        Side& s = side[ch];
        const int salt = ch * 10;

        // Tolleranze per canale (L e R diverse = immagine stereo piu' "aperta")
        s.tolDrive         = tolerance(channelID, 1 + salt, 0.03);   // +/-1.5% guadagno
        const double tolF  = tolerance(channelID, 2 + salt, 0.10);   // +/-5% frequenze
        const double tolA  = tolerance(channelID, 3 + salt, 0.20);   // +/-10% asimmetria
        const double tolEq = tolerance(channelID, 4 + salt, 0.20);   // +/-10% EQ

        s.lowShelf .setLowShelf (p.lowShelfHz  * tolF, p.lowShelfDb  * tolEq, sampleRate);
        s.peak     .setPeak     (p.peakHz      * tolF, p.peakQ, p.peakDb * tolEq, sampleRate);
        s.highShelf.setHighShelf(p.highShelfHz * tolF, p.highShelfDb * tolEq, sampleRate);

        s.fluxLP .setCutoff(120.0 * tolF, sampleRate);
        s.dcBlock.setCutoff(p.lowCutHz * tolF, sampleRate);
        s.phase  .setCutoff(p.phaseHz  * tolF, sampleRate);

        // Il roll-off si applica solo se cade davvero sotto Nyquist,
        // cosi' non scurisce il suono a 44.1/48 kHz.
        const double ro = p.rollOffHz * tolF;
        s.useRollOff = ro < sampleRate * 0.42;
        s.rollOff.setCutoff(ro, sampleRate);

        // Polarizzazione dello stadio di guadagno (asimmetria) e compensazione
        s.bias      = p.asym * 0.08 * tolA;
        s.biasOut   = kneeSat(s.bias, knee);
        s.biasSlope = (kneeSat(s.bias + 1.0e-4, knee) - kneeSat(s.bias - 1.0e-4, knee)) / 2.0e-4;
    }
}



inline void ChannelX32::processStereo(float** inputs, float** outputs, int sampleFrames)
{
    for (int i = 0; i < sampleFrames; ++i)
    {
        // DRIVE: spinge lo stadio del banco e compensa il livello,
        // quindi aumenta il carattere senza alzare il volume.
        const double driveGain = dbToGain(drive.next() * maxDriveDb);

        for (int ch = 0; ch < 2; ++ch)
        {
            Side& s = side[ch];
            double x = inputs[ch][i];

            // 1. Impronta tonale del banco
            x = s.lowShelf.process(x);
            x = s.peak.process(x);
            x = s.highShelf.process(x);

            // 2. Trasformatori: saturano solo le basse e solo quando spingi
            if (fluxAmount > 0.0)
            {
                const double low = s.fluxLP.process(x);
                x += softSat(low * fluxDrive) / fluxDrive - low;
            }

            // 3. Stadio di guadagno (limitato: non puo' esplodere)
            const double g = driveGain * s.tolDrive;
            const double u = x * g * invClip;
            const double au = std::fabs(u);
            if (au > drivePeak) drivePeak = au;
            const double y = (kneeSat(u + s.bias, knee) - s.biasOut) / s.biasSlope;
            x = y * clipLevel / g;

            // 4. DC blocker / taglio basso, fase, roll-off delle alte
            x = s.dcBlock.process(x);
            x = s.phase.process(x);
            if (s.useRollOff) x = s.rollOff.process(x);

            outputs[ch][i] = (float) x;
        }
    }
}

} // namespace dirtydesk
