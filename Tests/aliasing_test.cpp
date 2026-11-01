// Test offline di aliasing (senza JUCE).
// Usa la STESSA saturazione del trasformatore del plugin (Source/DSP/Saturators.h)
// (il preamp usa Marley, con il suo oversampling FIR a fase lineare) e un oversampler 4x
// FIR (Kaiser, circa -90 dB di stopband) come stand-in di juce::dsp::Oversampling.
// Metodo: seno coerente con la FFT; le armoniche "vere" (m*f0 < Nyquist) vengono escluse,
// tutto il resto e' aliasing. Si riporta il picco piu' alto, in dB rispetto alla fondamentale.
#include "../Source/DSP/Saturators.h"
#include <complex>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>


using cd = std::complex<double>;
static const double PI = 3.14159265358979323846;

static void fft(std::vector<cd>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * PI / (double) len;
        const cd wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            cd w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const cd u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
    return sum;
}

// FIR passa-basso Kaiser; cutoff in cicli/campione (rispetto al rate sovracampionato)
static std::vector<double> designLowpass(int taps, double fc, double beta)
{
    std::vector<double> h((size_t) taps);
    const int M = taps - 1;
    for (int n = 0; n < taps; ++n)
    {
        const double t = n - M / 2.0;
        const double sinc = t == 0.0 ? 2.0 * fc : std::sin(2.0 * PI * fc * t) / (PI * t);
        const double r = 2.0 * n / M - 1.0;
        h[(size_t) n] = sinc * besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
    }
    return h;
}

static std::vector<float> processOS(const std::vector<float>& in, const std::function<float(float)> f, int L)
{
    const auto h = designLowpass(256 * L + 1, 0.5 / L, 9.5);   // taglio a Nyquist del rate base
    const int T = (int) h.size();
    const size_t N = in.size();
    std::vector<float> up(N * (size_t) L);
    for (size_t m = 0; m < up.size(); ++m)
    {
        double acc = 0.0;
        for (int k = (int) (m % (size_t) L); k < T; k += L)
        {
            const long idx = (long) m - k;
            if (idx < 0) break;
            acc += h[(size_t) k] * in[(size_t) (idx / L)];
        }
        up[m] = f((float) (acc * (double) L));    // guadagno L per lo zero-stuffing
    }
    std::vector<float> out(N);
    for (size_t n = 0; n < N; ++n)
    {
        double acc = 0.0;
        for (int k = 0; k < T; ++k)
        {
            const long idx = (long) ((size_t) L * n) - k;
            if (idx < 0) break;
            acc += h[(size_t) k] * up[(size_t) idx];
        }
        out[n] = (float) acc;
    }
    return out;
}

struct Result { double maxAliasDb, totalAliasDb; };

static Result measure(const std::vector<float>& y, int k0)
{
    const size_t N = y.size();
    std::vector<cd> a(N);
    for (size_t i = 0; i < N; ++i)
    {
        const double x = 2.0 * PI * (double) i / (double) (N - 1);   // Blackman-Harris 4 termini
        const double w = 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
        a[i] = (double) y[i] * w;
    }
    fft(a);

    std::vector<char> legit(N / 2 + 1, 0);
    for (long m = 0; m * k0 <= (long) N / 2; ++m)
        for (long b = m * k0 - 8; b <= m * k0 + 8; ++b)
            if (b >= 0 && b <= (long) N / 2) legit[(size_t) b] = 1;

    double fund = 0.0;
    for (long b = k0 - 3; b <= k0 + 3; ++b) fund = std::max(fund, std::abs(a[(size_t) b]));

    double maxAlias = 0.0, sumAlias = 0.0;
    for (size_t b = 0; b <= N / 2; ++b)
        if (! legit[b]) { const double v = std::abs(a[b]); maxAlias = std::max(maxAlias, v); sumAlias += v * v; }

    return { 20.0 * std::log10(maxAlias / fund + 1e-12), 10.0 * std::log10(sumAlias / (fund * fund) + 1e-24) };
}


int main()
{
    const size_t N = 65536, skip = 4096;
    const double fs = 44100.0;
    const double requiredDb = -70.0;      // soglia: aliasing massimo col fattore usato dal plugin
    int failures = 0;

    using Fn = std::function<float(float)>;
    // Ogni curva e' una "fabbrica" (una curva con stato dipende dal rate effettivo),
    // quindi se ne crea una istanza nuova per ogni fattore di oversampling.
    struct Shaper { const char* name; std::function<Fn(int)> make; int pluginFactor; std::vector<float> amps; };
    const std::vector<Shaper> shapers = {
        { "Neve transform.", [](int) -> Fn { return [](float x) { return lunch::transformerSaturate(x); }; }, 4, { 0.1f, 0.25f, 0.5f } },
    };
    const int   k0s[]  = { 5003, 9001 };      // ~3.4 kHz e ~6.1 kHz a 44.1 kHz

    std::printf("%-16s %5s %7s | %8s | %8s | %8s | usato\n", "curva", "amp", "f0(Hz)", "1x (dB)", "4x (dB)", "8x (dB)");
    for (auto& sh : shapers)
        for (float amp : sh.amps)
            for (int k0 : k0s)
            {
                std::vector<float> in(N + skip);
                for (size_t i = 0; i < in.size(); ++i)
                    in[i] = amp * (float) std::sin(2.0 * PI * (double) k0 * (double) i / (double) N);

                // 1x: processo tutta la sequenza (la curva ha stato) e prendo la coda
                auto f1 = sh.make(1);
                std::vector<float> y1(N), y4(N), y8(N);
                std::vector<float> y1full(in.size());
                for (size_t i = 0; i < in.size(); ++i) y1full[i] = f1(in[i]);
                const auto y4full = processOS(in, sh.make(4), 4), y8full = processOS(in, sh.make(8), 8);
                for (size_t i = 0; i < N; ++i) { y1[i] = y1full[skip + i]; y4[i] = y4full[skip + i]; y8[i] = y8full[skip + i]; }

                const auto r1 = measure(y1, k0), r4 = measure(y4, k0), r8 = measure(y8, k0);
                const double used = sh.pluginFactor == 8 ? r8.maxAliasDb : r4.maxAliasDb;
                const bool ok = used < requiredDb;
                if (! ok) ++failures;
                std::printf("%-16s %5.2f %7.0f | %8.1f | %8.1f | %8.1f | %dx %s\n", sh.name, amp,
                            k0 * fs / (double) N, r1.maxAliasDb, r4.maxAliasDb, r8.maxAliasDb, sh.pluginFactor, ok ? "" : " <-- FAIL");
            }

    std::printf("\nSoglia: < %.0f dB rispetto alla fondamentale. %s\n", requiredDb,
                failures == 0 ? "TUTTI OK" : "ALCUNI CASI FALLITI");
    return failures == 0 ? 0 : 1;
}
