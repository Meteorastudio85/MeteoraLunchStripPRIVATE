// Test offline (senza JUCE) dei motori di De-Esser, EQ e Opto (Source/DSP/DynamicsCore.h) e del FET (fet1179).
// Controlla che ogni modulo faccia davvero quello che promette il pannello.
#include "../Source/DSP/DynamicsCore.h"
#include "../Source/DSP/dirtydesk/ChannelX32.h"
#include "../Source/ChainOrder.h"
#include "../Source/DSP/fet1179/ChronoEngine.h"
#include "../Source/DSP/marley/MarleyEngine.h"
#include "../Source/DSP/Saturators.h"
#include <cstdio>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what, double value)
{
    std::printf("  %-58s %9.2f  %s\n", what, value, ok ? "ok" : "<-- FAIL");
    if (! ok) ++failures;
}

static const double FS = 48000.0;
static const double TWO_PI = 6.283185307179586;

static double rmsDb(const std::vector<float>& x, size_t from)
{
    double s = 0.0;
    for (size_t i = from; i < x.size(); ++i) s += (double) x[i] * x[i];
    return 10.0 * std::log10(s / (double) (x.size() - from) + 1e-30);
}

static std::vector<float> sine(double f, double ampDb, double seconds)
{
    std::vector<float> v((size_t) (seconds * FS));
    const double a = lunch::fromDb(ampDb);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (float) (a * std::sin(TWO_PI * f * (double) i / FS));
    return v;
}

int main()
{
    //--------------------------------------------------------------------------
    std::printf("Preamp (motore Marley): VU rosso e lancetta\n");
    {
        // stessa soglia dell'editor: il vetro comincia ad arrossarsi a -6 dB, pieno a -1 dB
        auto run = [](double drive01, double rmsDb, double& vu)
        {
            marley::MarleyEngine e; e.prepare(FS, 512);
            marley::MarleyEngine::Params p; p.drive01 = (float) drive01;
            std::vector<float> L(512), R(512); float* io[2] = { L.data(), R.data() };
            double ph = 0.0; const double a = lunch::fromDb(rmsDb) * 1.41421356;
            for (int b = 0; b < 150; ++b)
            {
                for (int i = 0; i < 512; ++i) { L[(size_t) i] = R[(size_t) i] = (float) (a * std::sin(ph)); ph += TWO_PI * 300.0 / FS; }
                e.process(io, 2, 512, p);
            }
            vu = e.driveLevelDb.load() - marley::MarleyEngine::kVuReferenceDb;
            return (double) e.distortionDb.load();
        };
        double vuLow = 0, vuHigh = 0;
        const double dLow = run(0.2, -18, vuLow), dHigh = run(1.0, -18, vuHigh), dHot = run(0.7, -6, vuHigh);
        check(dLow < -6.0, "Drive 2, voce a -18 dBFS: niente rosso (dB distorsione)", dLow);
        check(dHigh > -3.0, "Drive 10, -18 dBFS: rosso quasi pieno (dB distorsione)", dHigh);
        check(dHot > -2.0, "Drive 7 con segnale a -6 dBFS: rosso pieno (dB distorsione)", dHot);
        check(vuHigh > vuLow + 6.0, "piu' Drive = la lancetta sale (VU)", vuHigh - vuLow);
    }

    //--------------------------------------------------------------------------
    std::printf("EQ\n");
    {
        lunch::EqCore eq; eq.prepare(FS);
        lunch::EqSettings s;
        eq.set(s);
        double worst = 0.0;
        for (double f = 20; f < 20000; f *= 1.1) worst = std::max(worst, std::fabs(eq.magnitudeDb(f)));
        check(worst < 0.01, "tutto a zero = trasparente (max scarto dB)", worst);

        s.midGain = 10; s.midFreq = 1000; s.midQ = 1.0; eq.set(s);
        check(std::fabs(eq.magnitudeDb(1000) - 10) < 0.05, "MID +10 dB a 1 kHz", eq.magnitudeDb(1000));
        s.midGain = 0;

        s.highGain = 8; s.highFreq = 8000; eq.set(s);
        check(std::fabs(eq.magnitudeDb(8000) - 4) < 0.3, "HIGH Baxandall +8: meta' (+4 dB) a 8 kHz", eq.magnitudeDb(8000));
        check(eq.magnitudeDb(19000) > 5.5, "HIGH Baxandall +8: sale verso gli acuti (19 kHz)", eq.magnitudeDb(19000));
        check(std::fabs(eq.magnitudeDb(200)) < 0.3, "HIGH shelf quasi non tocca i 200 Hz", eq.magnitudeDb(200));
        s.highShelf = false; eq.set(s);
        check(std::fabs(eq.magnitudeDb(8000) - 8) < 0.1, "HIGH bell +8 dB a 8 kHz", eq.magnitudeDb(8000));
        s.highGain = 0; s.highShelf = true;

        s.lowGain = -9; s.lowFreq = 100; eq.set(s);
        check(std::fabs(eq.magnitudeDb(100) + 4.5) < 0.3, "LOW Baxandall -9: meta' (-4.5 dB) a 100 Hz", eq.magnitudeDb(100));
        check(eq.magnitudeDb(20) < -7, "LOW Baxandall -9: scende verso i bassi (20 Hz)", eq.magnitudeDb(20));
        s.lowGain = 0;

        s.hpf = 150; eq.set(s);
        check(std::fabs(eq.magnitudeDb(150) + 3) < 0.5, "HPF 150 Hz: -3 dB a 150 Hz", eq.magnitudeDb(150));
        check(eq.magnitudeDb(40) < -15, "HPF 150 Hz (Capacitor, 3 poli): 40 Hz attenuati", eq.magnitudeDb(40));
        s.hpf = 20; s.lpf = 5000; eq.set(s);
        check(eq.magnitudeDb(15000) < -15, "LPF 5 kHz: 15 kHz attenuati", eq.magnitudeDb(15000));

        // processo reale: un seno a 1 kHz con MID +6
        lunch::EqCore e2; e2.prepare(FS);
        lunch::EqSettings s2; s2.midGain = 6; s2.midFreq = 1000; e2.set(s2);
        auto x = sine(1000, -20, 0.5); const double inDb = rmsDb(x, 0);
        for (auto& v : x) v = e2.process(0, v);
        check(std::fabs(rmsDb(x, 4800) - inDb - 6) < 0.1, "audio: seno 1 kHz con MID +6 dB", rmsDb(x, 4800) - inDb);
    }

    //--------------------------------------------------------------------------
    std::printf("EQ Pultec (motore Placebo EQ)\n");
    {
        lunch::PultecCore p; p.prepare(FS);
        lunch::PultecSettings s; p.set(s);
        double worst = 0.0;
        for (double f = 20; f < 20000; f *= 1.1) worst = std::max(worst, std::fabs(p.magnitudeDb(f)));
        check(worst < 0.01, "tutto a zero = trasparente (max scarto dB)", worst);
        s.hfBoost = 10; s.hfFreq = 3; p.set(s);
        check(std::fabs(p.magnitudeDb(8000) - 18) < 0.5, "HF BOOST 10 a 8 kHz = +18 dB (come Placebo)", p.magnitudeDb(8000));
        s.hfBw = 0; p.set(s); const double sharp = p.magnitudeDb(5000);
        s.hfBw = 10; p.set(s); const double broad = p.magnitudeDb(5000);
        check(broad > sharp + 3, "BANDWIDTH: BROAD piu' largo di SHARP (dB a 5 kHz)", broad - sharp);
        s = {}; s.lfFreq = 3; s.lfBoost = 5; s.lfAtten = 5; p.set(s);
        std::printf("    (100 Hz, boost 5 + atten 5: 20 Hz %.1f dB, 100 Hz %.1f dB, 300 Hz %.1f dB, 1 kHz %.1f dB)\n",
                    p.magnitudeDb(20), p.magnitudeDb(100), p.magnitudeDb(300), p.magnitudeDb(1000));
        s = {}; s.lfFreq = 3; s.lfBoost = 10; p.set(s);
        check(p.magnitudeDb(20) > 11.0 && p.magnitudeDb(20) < 14.0, "LF BOOST 10 a 100 Hz: +13,5 dB in basso (20 Hz)", p.magnitudeDb(20));
        s = {}; s.lfFreq = 3; s.lfAtten = 10; p.set(s);
        check(p.magnitudeDb(30) < -7, "LF ATTEN da solo taglia i bassi (dB a 30 Hz)", p.magnitudeDb(30));
        s = {}; s.hfAtten = 10; p.set(s);
        check(p.magnitudeDb(18000) < -12, "HF ATTEN 10: acuti tagliati (dB a 18 kHz)", p.magnitudeDb(18000));

        // DRIVE con autogain: il livello resta quello (entro 1 dB) e compaiono armoniche
        lunch::PultecCore d; d.prepare(FS); lunch::PultecSettings sd; sd.drive = 10; d.set(sd);
        auto x = sine(200, -12, 2.0); const double inDb = rmsDb(x, 0);
        std::vector<float> y(x.size());
        for (size_t i = 0; i < x.size(); ++i) { y[i] = d.process(0, x[i]); d.process(1, x[i]); }
        check(std::fabs(rmsDb(y, 48000) - inDb) < 1.0, "DRIVE 10: autogain tiene il livello (dB)", rmsDb(y, 48000) - inDb);
        double err = 0, sig = 0;
        for (size_t i = 48000; i < y.size(); ++i) { err += (y[i] - x[i]) * (y[i] - x[i]); sig += (double) x[i] * x[i]; }
        check(10 * std::log10(err / sig) > -30, "DRIVE 10: satura davvero (dB di differenza)", 10 * std::log10(err / sig));
    }

    //--------------------------------------------------------------------------
    std::printf("De-Esser\n");
    {
        auto run = [](double f, double ampDb, double red, bool listen, double& grOut)
        {
            lunch::DeEssCore d; d.prepare(FS); d.set(6500, red);
            auto x = sine(f, ampDb, 0.6); const double inDb = rmsDb(x, 0);
            grOut = 0.0;
            for (auto& v : x) { float l = v, r = v; d.tick(l, r, listen, true); v = l; grOut = std::min(grOut, d.gr); }
            return rmsDb(x, 9600) - inDb;
        };
        double gr = 0;
        double c = run(7000, -12, 6, false, gr);
        check(c < -6, "sibilante 7 kHz forte, Reduction 6: attenuata (dB)", c);
        check(gr < -6, "  ...e il meter mostra la riduzione (dB)", gr);
        c = run(300, -6, 6, false, gr);
        check(std::fabs(c) < 0.3, "voce 300 Hz forte: non toccata (dB)", c);
        c = run(7000, -12, 0, false, gr);
        check(std::fabs(c) < 0.05, "Reduction 0 = nessun effetto (dB)", c);
        c = run(300, -12, 6, true, gr);
        check(c < -15, "Listen: i 300 Hz quasi spariscono (dB)", c);
    }

    //--------------------------------------------------------------------------
    std::printf("FET (motore meteoraFET1179)\n");
    {
        // riduzione di guadagno come la misura meteoraFET1179 (energia prima/dopo il compressore)
        auto grFor = [](int ratio, double ampDb, double atk, double rel, double inDb = 0.0)
        {
            fet1179::ChronoEngine e; e.prepare(FS);
            e.setParams(inDb, 0, 0, ratio, atk, rel, 0.0, 0.0, 1.0);
            auto x = sine(1000, ampDb, 1.0);
            for (size_t i = 0; i < x.size(); ++i)
            {
                if (i == x.size() - 4800) e.preE = e.postE = 0.0;   // misura sugli ultimi 100 ms
                float s[2] = { x[i], x[i] }; e.process(s, 1.0);
            }
            return 10.0 * std::log10(e.postE / e.preE);
        };
        check(grFor(0, -40, 0.5, 0.5) > -0.5, "segnale basso: quasi nessuna riduzione (dB)", grFor(0, -40, 0.5, 0.5));
        const double g12 = grFor(0, -12, 0.5, 0.5);
        check(g12 < -3 && g12 > -9, "4:1 a -12 dBFS: comprime con misura (dB)", g12);
        check(grFor(3, -12, 0.5, 0.5) < g12, "20:1 comprime piu' di 4:1 (dB)", grFor(3, -12, 0.5, 0.5));
        check(grFor(0, -12, 0.5, 0.5, 10.0) < g12 - 2, "INPUT +10 dB = piu' compressione (dB)", grFor(0, -12, 0.5, 0.5, 10.0));
    }

    //--------------------------------------------------------------------------
    std::printf("Opto (LA-2A)\n");
    {
        auto grFor = [](double peak, bool limit, double ampDb)
        {
            lunch::OptoCore o; o.prepare(FS); o.set(peak, limit);
            auto x = sine(1000, ampDb, 1.0);
            for (auto v : x) o.tick(v, v);
            return o.currentGrDb();
        };
        check(grFor(0, false, -3) > -0.1, "PEAK 0: nessuna compressione (dB)", grFor(0, false, -3));
        check(grFor(50, false, -6) < -3, "PEAK 50, segnale -6 dBFS: comprime (dB)", grFor(50, false, -6));
        check(grFor(80, false, -12) < grFor(40, false, -12), "piu' PEAK = piu' riduzione (dB)", grFor(80, false, -12));
        check(grFor(50, true, -6) < grFor(50, false, -6), "LIMIT comprime piu' di COMPRESS (dB)", grFor(50, true, -6));

        // il meter torna a zero col silenzio (la lancetta deve stare ferma sullo 0)
        lunch::OptoCore o; o.prepare(FS); o.set(60, false);
        auto x = sine(1000, -6, 1.0); for (auto v : x) o.tick(v, v);
        const double during = o.currentGrDb();
        for (int i = 0; i < (int) (FS * 0.1); ++i) o.tick(0, 0);
        const double after100ms = o.currentGrDb();
        for (int i = 0; i < (int) (FS * 8); ++i) o.tick(0, 0);
        check(after100ms > during * 0.65, "release a due stadi: dopo 100 ms recupera circa meta' (rapporto)", after100ms / during);
        check(o.currentGrDb() > -0.05, "dopo 8 s di silenzio il meter e' a 0 (dB)", o.currentGrDb());
    }

    //--------------------------------------------------------------------------
    std::printf("LLS 4000 (banco LLS di Meteora DirtyDESK)\n");
    {
        auto run = [](double drive01, double ampDb, double& peakOut)
        {
            dirtydesk::ChannelX32 c; c.setSampleRate(FS); c.setConsoleType(dirtydesk::kSSL); c.setChannelID(1);
            c.setDrive(drive01); c.reset();
            auto x = sine(1000, ampDb, 1.0); auto y = x; const double inDb = rmsDb(x, 0);
            std::vector<float> r = x; float* io[2] = { y.data(), r.data() };
            peakOut = 0.0;
            for (size_t i = 0; i < y.size(); i += 512)
            {
                const int n = (int) std::min<size_t>(512, y.size() - i);
                float* b[2] = { io[0] + i, io[1] + i };
                c.processStereo(b, b, n);
                peakOut = std::max(peakOut, c.getAndResetDrivePeak());
            }
            return rmsDb(y, 9600) - inDb;
        };
        double pk0 = 0, pk1 = 0;
        const double d0 = run(0.0, -20, pk0), d1 = run(1.0, -20, pk1);
        check(std::fabs(d0) < 0.5 && std::fabs(d1) < 0.5, "GAIN (drive) compensato: il livello non cambia (dB a drive 10)", d1);
        check(pk1 > pk0 * 4, "piu' GAIN = banco piu' spinto (spia del VU rosso)", pk1 / std::max(pk0, 1e-9));
        double pkHot = 0; run(1.0, -3, pkHot);
        check(pkHot > 1.1, "segnale forte a GAIN 10: il VU diventa rosso (picco oltre il 50% del clip)", pkHot);
    }

    //--------------------------------------------------------------------------
    std::printf("Ordine dei moduli 2..5\n");
    {
        bool ok = chain::order(0) == chain::Order { 0, 1, 2, 3 };
        int idx = chain::moved(0, 2, -1);                    // EQ a sinistra del FET
        ok = ok && chain::order(idx) == chain::Order { 0, 2, 1, 3 };
        idx = chain::moved(idx, 0, -1);                      // De-Esser gia' primo: non si sposta
        ok = ok && chain::order(idx) == chain::Order { 0, 2, 1, 3 };
        check(ok, "scambi: EQ prima del FET, il primo non va oltre il bordo", (double) idx);
    }

    std::printf("\n%s\n", failures == 0 ? "TUTTI OK" : "ALCUNI TEST FALLITI");
    return failures == 0 ? 0 : 1;
}
