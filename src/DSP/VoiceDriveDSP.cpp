#include "VoiceDriveDSP.h"
#include <cmath>
#include <memory>

namespace voicedrive
{

//==============================================================================
// SimplexNoise - 1-D fBm gradient noise, phase-locked to the oscillator period
// (docs/deep-dive-oscillator.md sec.5: MT-seed-10 shuffled 256-table, 3 octaves,
//  lacunarity 2.0, gain 0.5, kernel g*t*(1-t^2)^4, xSum0.5^k = 1.75)
SimplexNoise::SimplexNoise()
{
    // exact permutation captured from the original plugin (engine+7224, int32[256],
    // verified a true permutation of 0..255; build/splx_perm.inc holds the source)
    static const std::uint8_t origPerm[256] = {
    200, 227, 35, 114, 93, 251, 77, 240, 137, 225, 34, 25, 21, 97, 126, 11, 23, 108, 3, 29, 172, 224, 105, 134, 14, 2, 146, 132, 243, 242, 31, 141, 247, 167, 95, 125, 40, 195, 157, 162, 222, 203, 139, 46, 244, 22, 169, 133, 160, 73, 234, 4, 85, 233, 166, 43, 205, 88, 26, 81, 213, 204, 147, 75, 74, 115, 171, 254, 168, 41, 238, 215, 158, 153, 84, 197, 60, 47, 12, 214, 151, 64, 6, 94, 182, 55, 142, 118, 59, 127, 119, 207, 181, 122, 28, 82, 24, 123, 98, 136, 15, 106, 138, 83, 102, 38, 124, 121, 185, 101, 30, 17, 241, 219, 175, 62, 163, 18, 112, 229, 202, 156, 90, 61, 209, 186, 159, 145,
    37, 58, 140, 130, 198, 27, 177, 170, 180, 189, 100, 66, 231, 19, 57, 104, 135, 155, 110, 206, 111, 53, 246, 113, 70, 154, 80, 194, 237, 220, 92, 76, 201, 235, 32, 67, 144, 143, 103, 165, 86, 50, 117, 44, 221, 236, 208, 226, 245, 184, 10, 250, 218, 48, 183, 173, 129, 109, 20, 9, 248, 174, 39, 8, 216, 150, 193, 239, 13, 191, 79, 33, 5, 164, 152, 252, 228, 63, 36, 116, 249, 149, 1, 68, 188, 211, 0, 199, 232, 187, 217, 99, 49, 91, 51, 89, 87, 54, 176, 230, 42, 178, 69, 190, 128, 223, 131, 65, 196, 16, 192, 120, 212, 255, 52, 210, 72, 107, 78, 148, 179, 253, 56, 96, 45, 71, 161, 7,    };
    for (int i = 0; i < 256; ++i) table[(size_t) i] = origPerm[(size_t) i];
}

double SimplexNoise::fBm (double x) const
{
    // EXACT transcription of sub_18052DBB0 (all float arithmetic, octave kernel
    // g(b)=((b&7)+1)*sign(b&8), kern t*(1-t^2)^4, scale 0.395f, 3 octaves,
    // persistence 0.5, freq doubling; caller multiplies by sum(0.5^k)=1.75)
    auto gradVal = [&] (int i)
    {
        int b = (int) table[(size_t) ((unsigned) i & 0xFFu)];
        float g0 = (float) ((b & 7) + 1);
        return (b & 8) != 0 ? -g0 : g0;
    };
    auto kernVal = [&] (float t, int i)
    {
        float u = 1.0f - t * t;
        return gradVal (i) * t * (u * u * u * u);   // t*(1-t^2)^4
    };

    float f = 1.0f, a = 1.0f, total = 0.0f, sum = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        float xf = f * (float) x;
        int i0 = (int) xf;
        if ((float) i0 > xf) --i0;                   // floor
        float t0 = xf - (float) i0, t1 = t0 - 1.0f;
        sum += (kernVal (t0, i0) + kernVal (t1, i0 + 1)) * 0.39500001f * a;
        total += a;
        f *= 2.0f;
        a *= 0.5f;
    }
    return (double) (sum / total);
}
double SimplexNoise::getWave (double tRadians) const
{
    double x = tRadians / (2.0 * juce::MathConstants<double>::pi);
    return fBm (x) * 1.75;
}

//==============================================================================
// VibrationBank - f_osc = OSCSpeed * F0 (linear, F0-locked), t = phase1 + 0.25 rad
void VibrationBank::step (double fs, double f0, float oscSpeed)
{
    phase1 += ((double) oscSpeed * 2.0 * juce::MathConstants<double>::pi * f0) / fs;
    phase2 += 3.0 / fs;   // dead secondary ramp (kept for fidelity)
    constexpr auto twoPi = 2.0 * juce::MathConstants<double>::pi;
    if (phase1 >= 8.988465674311579e307)
        while (phase1 >= twoPi) phase1 -= twoPi;
}

double VibrationBank::getSample (int waveform, float noiseRatio)
{
    constexpr auto twoPi = 2.0 * juce::MathConstants<double>::pi;
    constexpr auto pi    = juce::MathConstants<double>::pi;

    auto sinWave = [&] (double t) { return std::sin (t); };
    auto squareWave = [&] (double t)
    {
        double p = std::fmod (std::fmod (t, twoPi) + twoPi, twoPi);
        return p >= pi ? -1.0 : 1.0;
    };
    auto triangleWave = [&] (double t)
    {
        double p = std::fmod (std::fmod (t, twoPi) + twoPi, twoPi);
        if (p < pi / 2.0)   return p / (pi / 2.0);
        if (p < 3 * pi / 2) return 1.0 - (p - pi / 2.0) * (2.0 / pi);
        return (p - 3.0 * pi / 2.0) * (2.0 / pi) - 1.0;
    };
    auto fullWave = [&] (double t) { return 2.0 * std::fabs (std::sin ((t + pi / 2.0) * 0.5)) - 1.0; };

    double t = phase1 + 0.25;
    double wave = 0.0;
    switch (waveform)
    {
        case 2:  wave = squareWave (t);   break;
        case 3:  wave = triangleWave (t); break;
        case 4:  wave = fullWave (t);     break;
        case 5:  wave = noise.next();     break;    // fresh MT19937 value per call
        case 6:  wave = (sinWave (t) + 0.2 * triangleWave (t)
                         + 0.05 * noise.next() + 0.2 * fullWave (t)) / 1.45;
                  break;
        default: wave = sinWave (t);      break;    // 1 and >=7 -> Sin
    }
    static const double w5Gain = [] { const char* e = getenv ("VD_W5_GAIN"); return e ? atof (e) : 1.0; }();
    if (waveform == 5) wave *= w5Gain;
    double nr = (double) noiseRatio;
    return simplex.getWave (t) * nr + wave * (1.0 - nr);
}

//==============================================================================
// PeakingEQ - exact RBJ peaking design of sub_180528A90 (transposed DF-II runtime)
void PeakingEQ::reset()
{
    b0 = 1.0f; b1 = b2 = a1 = a2 = 0.0f;
    s1 = s2 = 0.0f;
}

void PeakingEQ::design (double fsRef, double f, double q, double gainLin)
{
    // EXACT transcription of sub_180528A90 (verified against decompile):
    //   A  = max(sqrtf(X), 0)                     X = gain (linear)
    //   w0 = 2*pi*max(2, f) / fsRef
    //   al = sin(w0) * sqrt(A) / Q                (Q = 1/sqrt(2), from qword_1807F3D88)
    //   a0 = 1 / (A + 1 - (A-1)cw + al)           <-- minus sign (earlier transcription had +, unstable)
    //   b0 = A*((A-1)cw + A + 1 + al)*a0
    //   b1 = -2A*((A+1)cw + A - 1)*a0
    //   b2 = A*((A-1)cw + A + 1 - al)*a0
    //   a1 = 2*(A - 1 - (A+1)cw)*a0
    //   a2 = (A + 1 - (A-1)cw - al)*a0
    constexpr auto pi = juce::MathConstants<double>::pi;
    double A = (double) juce::jmax (0.0f, std::sqrt ((float) gainLin));
    double w0 = 2.0 * pi * juce::jmax (2.0, f) / fsRef;
    w0 = juce::jlimit (0.0005, 0.9999 * pi, w0);   // numeric guard; original reaches pi at f == fsRef/2
    double cw = std::cos (w0), sw = std::sin (w0);
    double al = sw * std::sqrt (A) / q;
    double a0r = 1.0 / (double) (float) (A + 1.0 - (A - 1.0) * cw + al);

    b0 = (float) (A * ((A - 1.0) * cw + A + 1.0 + al) * a0r);
    b1 = (float) (-2.0 * A * ((A + 1.0) * cw + A - 1.0) * a0r);
    b2 = (float) (A * ((A - 1.0) * cw + A + 1.0 - al) * a0r);
    a1 = (float) (2.0 * (A - 1.0 - (A + 1.0) * cw) * a0r);
    a2 = (float) ((A + 1.0 - (A - 1.0) * cw - al) * a0r);
}

void PeakingEQ::process (float* d, int n)
{
    for (int i = 0; i < n; ++i)
    {
        float x = d[i];
        float y = b0 * x + s1;
        d[i] = y;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
    }
}

//==============================================================================
// SpectralSeparator - ENV = |FFT(lifter x IFFT(mag))|, binomial lifter
void SpectralSeparator::prepare (int order)
{
    fftOrder = order;
    fft = std::make_unique<juce::dsp::FFT> (order);
    work.assign ((size_t) (2 << order), 0.0f);
    lifter.clear();
    lifterWidth = -1;
}

void SpectralSeparator::buildLifter (int width)
{
    int n = 1 << fftOrder;
    int bins = n / 2 + 1;
    if (width == lifterWidth && ! lifter.empty())
        return;
    lifterWidth = width;
    // exact ref (sub_1805215E0): ONE-SIDED over bins (mirror handles symmetry):
    //   L[0]=1; L[i] = L[i-1] * (a - i + 1)/(i + a), i = 1..bins-1, a = lag/2
    // callers pass a directly (lag = 2a; ref nominal lag 5000 -> a 2500)
    int a = juce::jmax (1, width);
    lifter.assign ((size_t) bins, 1.0);
    double v = 1.0;
    for (int i = 1; i < bins; ++i)
    {
        v *= ((double) a - (double) i + 1.0) / ((double) i + (double) a);
        lifter[(size_t) i] = v;
    }
}

void SpectralSeparator::separate (const double* magIn, int numBins, int width, double* env)
{
    jassert (fft != nullptr);
    int n = 1 << fftOrder;

    // exact ref form (sub_18051F4E0 + 1805213F0): dB-quefrency -> lifter -> mirror -> fwd
    // NOTE: measured WORSE on the shift path (ref pass-A is the linear form) - keep off
    static const bool exact = [] { const char* e = getenv ("VD_SEP_EXACT"); return e && atoi (e) != 0; }();
    if (exact)
    {
        static std::vector<double> quef, reF;
        buildLifter (width);
        quef.assign ((size_t) n, 0.0);
        cepstrumDb (magIn, numBins, quef.data());
        for (int i = 0; i < n; ++i) quef[(size_t) i] *= lifter[(size_t) i];
        // mirror the first half over the second (data[n-k] = data[k])
        for (int k = 1; k < numBins - 1; ++k) quef[(size_t) (n - k)] = quef[(size_t) k];
        reF.assign ((size_t) (2 * n), 0.0);
        forwardRe (quef.data(), numBins, reF.data());
        for (int k = 0; k < numBins; ++k)
        {
            double v = magIn[k] * std::pow (10.0, reF[(size_t) k] / 40.0);
            if (v <= 2.220446049250313e-16) v = 0.0;
            env[k] = juce::jmax (0.0, v);
        }
        return;
    }

    // IFFT of the magnitude spectrum (zero phase) -> even-symmetric quefrency signal
    std::fill (work.begin(), work.end(), 0.0f);
    for (int k = 0; k < numBins && k <= n / 2; ++k)
    {
        work[(size_t) (2 * k)]     = (float) magIn[k];
        work[(size_t) (2 * k + 1)] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (work.data());

    // one-sided binomial lifter over quef[0..bins), then mirror (ref sub_180520780)
    buildLifter (width);
    for (int i = 0; i < numBins; ++i) work[(size_t) i] *= (float) lifter[(size_t) i];
    for (int k = 1; k < numBins - 1; ++k) work[(size_t) (n - k)] = work[(size_t) k];

    // forward transform -> envelope magnitude
    std::fill (work.begin() + n, work.end(), 0.0f);
    fft->performRealOnlyForwardTransform (work.data());
    for (int k = 0; k < numBins; ++k)
    {
        // ref (sub_180520780 linear mode): negatives CLAMP to 0 (not abs)
        double re = work[(size_t) (2 * k)];
        env[k] = juce::jmax (0.0, (double) re);
    }
}

//==============================================================================
void SpectralSeparator::cepstrumDb (const double* x, int numBins, double* quef)
{
    jassert (fft != nullptr);
    int n = 1 << fftOrder;
    std::fill (work.begin(), work.end(), 0.0f);
    for (int k = 0; k < numBins && k <= n / 2; ++k)
    {
        double db = 20.0 * std::log10 (std::max (1e-12, x[k] * x[k]));
        work[(size_t) (2 * k)] = (float) db;
    }
    fft->performRealOnlyInverseTransform (work.data());
    // JUCE's real inverse scales by 1/n already? verify against separate(): there the
    // envelope comes from |FFT(IFFT(mag))| and self-cancels; for the cepstrum keep raw.
    for (int i = 0; i < n; ++i) quef[i] = (double) work[(size_t) (2 * i)];
}

void SpectralSeparator::forwardRe (const double* sig, int numBins, double* reF)
{
    jassert (fft != nullptr);
    int n = 1 << fftOrder;
    std::fill (work.begin(), work.end(), 0.0f);
    for (int i = 0; i < n; ++i) work[(size_t) (2 * i)] = (float) sig[i];
    fft->performRealOnlyForwardTransform (work.data());
    for (int k = 0; k < numBins; ++k) reF[k] = (double) work[(size_t) (2 * k)];
}

//==============================================================================
// F0Estimator - homomorphic separation + SHS on 6-cent grid (docs/deep-dive-f0.md)
void F0Estimator::prepare (int frameSize)
{
    frame = frameSize;
    hamming.resize ((size_t) frame);
    // symmetric Hamming (verified bit-level against the original's windowed-frame buffer
    // at engine+400: 0.54-0.46*cos(2*pi*i/(N-1)), max err 3e-18)
    for (int i = 0; i < frame; ++i)
        hamming[(size_t) i] = 0.54 - 0.46 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (double) (frame - 1));

    fftWork.assign ((size_t) (2 * frame), 0.0f);
    mag.resize ((size_t) frame / 2 + 1);
    envWide.resize ((size_t) frame / 2 + 1);
    flat.resize ((size_t) frame / 2 + 1);
    flat2.resize ((size_t) frame / 2 + 1);
    buildGrid();
    sepWide.prepare (10);   // frame = 1024
    sepNarrow.prepare (10); // frame = 1024
}

void F0Estimator::buildGrid()
{
    // 10-cent bins from origin (38.89 Hz = 440*2^-3.5) up to Nyquist: 1101 bins.
    // (Measured from the original's persistent grid vector at engine+648: 1101 doubles,
    //  argmax bin k <-> f0 = originHz * 2^(10k/1200), verified over many frames.)
    numGridBins = 1101;
    gridVals.assign ((size_t) numGridBins, 0.0);
    shs.assign ((size_t) numGridBins, 0.0);
}

F0Estimator::Result F0Estimator::estimate (const double* fr, int frameSize, int type,
                                           double fMin, double fMax)
{
    Result r;
    if (frameSize != frame) prepare (frameSize);
    int n = frame, bins = n / 2 + 1;
    constexpr double fs = 44100.0;

    // search-range rule from the original (sub_1805268C0):
    //   fLow = max(param min, ceil(fs / (frame/2)));  fHi = min(param max, ...)
    double fLow = juce::jmax (fMin, std::ceil (fs / (double) (n / 2)));
    double fHi = juce::jmin (fMax, fs * 0.5);

    // Hamming window + FFT -> |X|
    std::fill (fftWork.begin(), fftWork.end(), 0.0f);
    for (int i = 0; i < n; ++i)
        fftWork[(size_t) i] = (float) (fr[i] * hamming[(size_t) i]);
    fft.performRealOnlyForwardTransform (fftWork.data());
    for (int k = 0; k < bins; ++k)
    {
        double re = fftWork[(size_t) (2 * k)], im = fftWork[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
    }

    // type 1: cepstrum variant - F0 = fs / argmax quefrency in the search range
    if (type == 1)
    {
        for (int k = 0; k < bins; ++k)
        {
            double v = std::log (mag[(size_t) k] * mag[(size_t) k] + 1e-30);
            fftWork[(size_t) (2 * k)] = (float) v;
            fftWork[(size_t) (2 * k + 1)] = 0.0f;
        }
        std::fill (fftWork.begin() + 2 * bins, fftWork.end(), 0.0f);
        fft.performRealOnlyInverseTransform (fftWork.data());
        int lo = (int) (fs / fHi), hi = (int) (fs / fLow);
        int bestIdx = 0; double best = -1e300;
        for (int q = juce::jmax (2, lo); q <= hi && q < n / 2; ++q)
        {
            double v = std::fabs ((double) fftWork[(size_t) q]);
            if (v > best) { best = v; bestIdx = q; }
        }
        if (bestIdx > 0) r.f0 = fs / (double) bestIdx;
        r.voiced = r.f0 > fLow && r.f0 < fHi && best > 0.05;
        r.stable = hasPrev && std::fabs (r.f0 - prevF0) / juce::jmax (prevF0, 1.0) < 0.1;
        prevF02 = prevF0; prevF0 = r.f0; hasPrev2 = hasPrev; hasPrev = true;
        return r;
    }

    // adaptive 3-pass separation (sub_18051F4E0, docs/deep-dive-f0.md §4.3)
    // A: separate(mag, NARROW 2500) -> envA ; B: dB-cepstrum of flatA with soft expander
    // (4% slope above 0.1) -> spectral re-weighting mag2 = mag*10^(ReF/40)
    // C: separate(mag2, WIDE 25000) -> envB ; D: separate(mag2, 2500) -> envC
    //    envC = envC*max(eps,envC)/max(eps,envB) ; FLAT = mag/envC
    static const bool useAdaptive3 = [] { const char* e = getenv ("VD_SEP"); return ! e || ! strcmp (e, "adaptive3"); }();
    if (useAdaptive3)
    {
        envA.assign ((size_t) bins, 0.0);
        sepWide.separate (mag.data(), bins, 2500, envA.data());
        for (int k = 0; k < bins; ++k) flat[(size_t) k] = mag[(size_t) k] / juce::jmax (envA[(size_t) k], 1e-12);
        quef.assign ((size_t) n, 0.0);
        sepWide.cepstrumDb (flat.data(), bins, quef.data());
        static const double cepScale = [] { const char* e = getenv ("VD_CEP_SCALE"); return e ? atof (e) : 1.0; }();
        for (int i = 0; i < n; ++i) quef[(size_t) i] *= cepScale;
        for (int i = 0; i < n; ++i)
            if (quef[(size_t) i] > 0.1) quef[(size_t) i] = (quef[(size_t) i] - 0.1) * 0.04 + 0.1;
        reF.assign ((size_t) bins, 0.0);
        sepWide.forwardRe (quef.data(), bins, reF.data());
        mag2.assign ((size_t) bins, 0.0);
        for (int k = 0; k < bins; ++k)
            mag2[(size_t) k] = mag[(size_t) k] * std::pow (10.0, reF[(size_t) k] * 0.5 / 20.0);
        envB.assign ((size_t) bins, 0.0);
        sepWide.separate (mag2.data(), bins, 25000, envB.data());
        envC.assign ((size_t) bins, 0.0);
        sepWide.separate (mag2.data(), bins, 2500, envC.data());
        for (int k = 0; k < bins; ++k)
        {
            envC[(size_t) k] = envC[(size_t) k] * juce::jmax (envC[(size_t) k], 1e-12)
                               / juce::jmax (envB[(size_t) k], 1e-12);
            flat[(size_t) k] = mag[(size_t) k] / juce::jmax (envC[(size_t) k], 1e-12);
        }
    }
    else
    {
        // legacy two-pass approximation (kept for A/B)
        int aEnv = juce::jlimit (8, n / 4, (int) std::round (fs / juce::jmax (50.0, fHi)));
        sepWide.separate (mag.data(), bins, aEnv, envWide.data());
        for (int k = 0; k < bins; ++k)
            flat[(size_t) k] = mag[(size_t) k] / juce::jmax (envWide[(size_t) k], 1e-12);
    }

    // log-domain re-separation with lifter 1000: strip FLAT's own envelope
    logFlat.assign ((size_t) bins, 0.0);
    logEnv.assign ((size_t) bins, 0.0);
    double mx = 1e-30;
    for (int k = 0; k < bins; ++k) mx = juce::jmax (mx, flat[(size_t) k]);
    for (int k = 0; k < bins; ++k) logFlat[(size_t) k] = std::log (flat[(size_t) k] + mx * 1e-9);
    sepNarrow.separate (logFlat.data(), bins, 1000, logEnv.data());
    for (int k = 0; k < bins; ++k)
        flat2[(size_t) k] = std::exp (logFlat[(size_t) k] - logEnv[(size_t) k]);
    // normalize flat2 to unit median (the original's flattened spectrum sits at ~1.0:
    // observed ref grid medians 1.0-1.2 across frames), enabling the S[k] >= 1.0 gate
    {
        std::vector<double> srt (flat2.begin(), flat2.begin() + bins);
        std::nth_element (srt.begin(), srt.begin() + bins / 2, srt.end());
        double med = srt[(size_t) (bins / 2)];
        if (med > 0.0)
            for (int k = 0; k < bins; ++k) flat2[(size_t) k] /= med;
    }

    // resample flat2 onto the 10-cent grid. Two selectable interpolators (A/B ablation):
    // VD_GRID_INTERP=linear (default) | spline (natural cubic over log values)
    // VD_GRID_RATE: FFT-bin mapping rate (default 44100; engine+24 stores 50000/44100)
    // (values below 1.0 are gated OFF in the SHS below - the original's S[k] >= 1.0 rule)
    constexpr double originHz = F0Estimator::originHz;
    static const double gridRate = [] { const char* e = getenv ("VD_GRID_RATE"); return e ? atof (e) : 44100.0; }();
    static const bool useSpline = [] { const char* e = getenv ("VD_GRID_INTERP"); return e && ! strcmp (e, "spline"); }();
    for (int g = 0; g < numGridBins; ++g)
    {
        double cents = 10.0 * g;
        double hz = originHz * std::pow (2.0, cents / 1200.0);
        double pos = hz * n / gridRate;
        if (pos >= bins - 1 || pos < 0) { gridVals[(size_t) g] = 0.0; continue; }
        int i0 = (int) pos; double t = pos - i0;
        double v0 = std::log (flat2[(size_t) i0] + 1e-30);
        double v1 = std::log (flat2[(size_t) (i0 + 1)] + 1e-30);
        gridVals[(size_t) g] = std::exp (v0 + t * (v1 - v0));
    }
    if (useSpline)
    {
        // natural cubic spline through log(flat2) sampled at integer FFT bins, evaluated
        // at the grid positions (approximation of the original's spline mapper sub_18039B540)
        static std::vector<double> sy2;
        int m = bins;
        sy2.assign ((size_t) m, 0.0);
        std::vector<double> y ((size_t) m);
        for (int k = 0; k < m; ++k) y[(size_t) k] = std::log (flat2[(size_t) k] + 1e-30);
        std::vector<double> u ((size_t) m, 0.0);
        for (int k = 1; k < m - 1; ++k)
        {
            double sig = (y[(size_t) k] - y[(size_t) (k - 1)]) / 6.0;
            double p = 0.5 * (sy2[(size_t) (k - 1)] + 2.0 * sy2[(size_t) k] + sig);
            sy2[(size_t) (k + 1)] = 0.5 * (sy2[(size_t) k] + sig);
            u[(size_t) k] = (y[(size_t) (k + 1)] - y[(size_t) k]) - (y[(size_t) k] - y[(size_t) (k - 1)]);
        }
        for (int k = m - 2; k >= 1; --k) u[(size_t) k] = (u[(size_t) k] - 0.5 * u[(size_t) (k + 1)]) / (1.0 + 0.5 * sy2[(size_t) (k + 1)]);
        for (int g = 0; g < numGridBins; ++g)
        {
            double hz = originHz * std::pow (2.0, (10.0 * g) / 1200.0);
            double pos = hz * n / gridRate;
            if (pos >= bins - 1 || pos < 0) continue;
            int i0 = (int) pos; double t = pos - i0;
            double a = 1.0 - t;
            double v = a * std::log (flat2[(size_t) i0] + 1e-30) + t * std::log (flat2[(size_t) (i0 + 1)] + 1e-30)
                       + ((a * a * a - a) * sy2[(size_t) i0] + (t * t * t - t) * sy2[(size_t) (i0 + 1)]) / 6.0;
            gridVals[(size_t) g] = std::exp (v);
        }
    }

    // SHS: SHS[k] = Sum_{h=1..15} 0.86^(h-1) * S[bin + 1200*log2(h)/10c]
    // VD_SHS_ROUND=round (default) | floor (harmonic offset rounding ablation)
    static const bool useFloor = [] { const char* e = getenv ("VD_SHS_ROUND"); return e && ! strcmp (e, "floor"); }();
    auto hOff = [] (int h) -> int
    {
        double x = 1200.0 * std::log2 ((double) h) / 10.0;
        return useFloor ? (int) std::floor (x) : (int) std::lround (x);
    };
    for (int g = 0; g < numGridBins; ++g)
    {
        double s = 0.0, w = 1.0;
        if (gridVals[(size_t) g] < 1.0) { shs[(size_t) g] = 0.0; continue; }   // S[k] >= 1.0 gate
        for (int h = 1; h <= 15; ++h)
        {
            int idx = g + hOff (h);
            if (idx >= numGridBins) break;
            if (idx >= 0) s += w * gridVals[(size_t) idx];
            w *= 0.86;
        }
        shs[(size_t) g] = s;
    }
    dbgGrid = gridVals;
    dbgShs = shs;

    // contiguous-run peak picking over the search range -> top-2 candidates
    // (grid bin g holds 10*g cents above originHz, so bin(f) = 120*log2(f/originHz))
    auto binOf = [&originHz] (double hz) { return 120.0 * std::log2 (hz / originHz); };
    int gLo = juce::jmax (0, (int) std::ceil (binOf (juce::jmax (fLow, originHz))));
    int gHi = juce::jmin (numGridBins - 1, (int) std::floor (binOf (juce::jmin (fHi, 20000.0))));
    if (gHi <= gLo + 2) { r.f0 = 0.0; r.voiced = false; return r; }

    struct Cand { double f0, raw, score, prom; };
    Cand c1 { 0, 0, -1, 1 }, c2 { 0, 0, -1, 1 };
    double meanShs = 0.0;
    for (int i = gLo; i <= gHi; ++i) meanShs += shs[(size_t) i];
    meanShs /= (gHi - gLo + 1);

    int g = gLo;
    while (g <= gHi)
    {
        if (shs[(size_t) g] > meanShs)
        {
            int s0 = g;
            double peak = -1e300; int pIdx = g;
            while (g <= gHi && shs[(size_t) g] > meanShs)
            {
                if (shs[(size_t) g] > peak) { peak = shs[(size_t) g]; pIdx = g; }
                ++g;
            }
            double valley = juce::jmax (juce::jmin (shs[(size_t) s0], shs[(size_t) juce::jmax (g - 1, s0)]),
                                        meanShs * 0.25);
            double prom = peak / valley;
            double f0 = originHz * std::pow (2.0, (10.0 * pIdx) / 1200.0);
            // sub-bin refinement: parabolic interpolation over the SHS peak (the original's
            // winners land BETWEEN grid points, e.g. 162.92 Hz <-> bin 248.1 -> they refine)
            // (the original does NOT refine the peak: empirically 55.3% vs 45.3% exact
            //  frame winners without parabolic interpolation on the 10-cent grid)
            static const bool parab = [] { const char* e = getenv ("VD_SHS_PARAB"); return e && ! strcmp (e, "1"); }();
            if (parab && pIdx > 0 && pIdx < numGridBins - 1)
            {
                double sm = shs[(size_t) (pIdx - 1)], s0 = shs[(size_t) pIdx], sp = shs[(size_t) (pIdx + 1)];
                double den = sm - 2.0 * s0 + sp;
                if (den < 0.0)
                {
                    double delta = 0.5 * (sm - sp) / den;
                    if (juce::jlimit (-1.0, 1.0, delta) == delta)
                        f0 = originHz * std::pow (2.0, (10.0 * ((double) pIdx + delta)) / 1200.0);
                }
            }
            static const int shsShift = [] { const char* e = getenv ("VD_SHS_SHIFT"); return e ? atoi (e) : 0; }();
            if (shsShift != 0)
                f0 = originHz * std::pow (2.0, (10.0 * (double) (pIdx + shsShift)) / 1200.0);
            Cand c { f0, peak, peak * prom, prom };
            if (c.score > c1.score) { c2 = c1; c1 = c; }
            else if (c.score > c2.score) c2 = c;
        }
        else ++g;
    }

    if (c1.score <= 0.0) { r.f0 = 0.0; r.voiced = false; r.stable = false; return r; }

    // octave correction: sub-candidate at ~1/2 or 1/4 f_top with raw >= 0.95x top's -> score x2
    if (c2.f0 > 0.0 && c1.f0 > 0.0)
    {
        double ratio = c2.f0 / c1.f0;
        if ((std::fabs (ratio - 0.5) <= 0.05 || std::fabs (ratio - 0.25) <= 0.05)
            && c2.raw >= 0.95 * c1.raw)
            c2.score *= 2.0;
    }

    // continuity smoothing vs the previous two F0s
    Cand winner = c1;
    if (hasPrev)
    {
        auto cont = [] (Cand& c, double prev)
        {
            if (prev > 0.0)
                c.score *= juce::jmin (c.f0, prev) / juce::jmax (c.f0, prev);
        };
        cont (c1, prevF0); cont (c2, prevF0);
        if (hasPrev2) { cont (c1, prevF02); cont (c2, prevF02); }
        winner = (c1.score >= c2.score) ? c1 : c2;
    }

    r.f0 = juce::jmin (juce::jmax (winner.f0, 0.0), fMax);   // hard clamp to the search range
    r.shs = winner.raw;
    r.voiced = winner.raw > meanShs * 1.8 && winner.prom > 1.6;
    r.stable = hasPrev && std::fabs (r.f0 - prevF0) / juce::jmax (prevF0, 1.0) < 0.1;

    voicedHist.push_back (r.voiced); if (voicedHist.size() > 4) voicedHist.pop_front();
    stableHist.push_back (r.stable); if (stableHist.size() > 4) stableHist.pop_front();

    prevF02 = prevF0; prevF0 = r.f0;
    hasPrev2 = hasPrev; hasPrev = true;
    return r;
}

//==============================================================================
// PsolaStage
void PsolaStage::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    numChans = numChannels;
    history.assign ((size_t) numChans, std::vector<float> ((size_t) historyLen, 0.0f));
    wetWindow.assign ((size_t) numChans, std::vector<float> ((size_t) historyLen, 0.0f));
    reset();
}

void PsolaStage::reset()
{
    for (auto& h : history) std::fill (h.begin(), h.end(), 0.0f);
    for (auto& w : wetWindow) std::fill (w.begin(), w.end(), 0.0f);
    marks.clear();
    lfoAtMark.clear();
    lastProcessedMark = -1;
    emittedUpTo = 0;
    histWrite = 0;
    eqA.reset();
    eqB.reset();
}

void PsolaStage::pushSample (const float* audio, int numChannels, int state, double lfo)
{
    for (int c = 0; c < juce::jmin (numChannels, numChans); ++c)
        history[(size_t) c][(size_t) (histWrite % historyLen)] = audio[c];
    if (state != 0)
    {
        marks.push_back (histWrite);
        lfoAtMark.push_back (lfo);
        // probe: dump per-mark LFO for comparison with the ref state dump
        static FILE* lf = getenv ("VD_LFO_DUMP") ? fopen (getenv ("VD_LFO_DUMP"), "wb") : nullptr;
        if (lf) { float v = (float) lfo; fwrite (&v, 4, 1, lf); fflush (lf); }
    }
    ++histWrite;

    // drop stale marks beyond the history horizon
    while (! marks.empty() && histWrite - marks.front() > historyLen / 2)
    {
        marks.erase (marks.begin());
        lfoAtMark.erase (lfoAtMark.begin());
        --lastProcessedMark;
    }
}

void PsolaStage::process (const Params& p, double f0, juce::AudioBuffer<float>& out)
{
    const int n = out.getNumSamples();
    const int chs = juce::jmin (out.getNumChannels(), numChans);

    // process every mark whose grain has fully arrived
    static const bool dbg = getenv ("VD_DEBUG") != nullptr;
    int dbgProcessed = 0;
    while (lastProcessedMark + 3 < (int) marks.size())
    {
        int idx = lastProcessedMark + 1;
        int64_t m1 = marks[(size_t) idx];
        int64_t m0 = idx > 0 ? marks[(size_t) (idx - 1)] : 0;   // original: first mark's m0 = 0 (sub_180390AD0)
        // EXACT grain geometry (sub_180390AD0): span [m[k-1], m[k+1]] INCLUSIVE, the
        // window peaks at the CENTER mark m1; for the last mark extrapolate
        // m2 = min(2*m1 - m0, ringCap-1)
        int64_t m2 = (idx + 1 < (int) marks.size()) ? marks[(size_t) (idx + 1)]
                                                     : juce::jmin ((int64_t) (2 * m1 - m0), (int64_t) (historyLen - 1));
        int grainLen = (int) (m2 - m0 + 1);
        if (grainLen < 4 || grainLen > historyLen) { ++lastProcessedMark; continue; }
        if (m2 >= histWrite) break;

        double lfo = lfoAtMark[(size_t) idx];
        // EXACT jitter formula (disasm 0x18050A642-0x18050A6E1):
        //   period = marks[k] - marks[k-1]
        //   jAmt   = (float)(Jitter*EffectLevel) ->double, * track1.0[marks[k]] * lfo[marks[k]]
        //            * (double)period
        //   shift  = round(jAmt)
        //   target = marks[k-1] + (EfxMark - shift) - marks[k]
        // The Efx lane (phase1) advances with the SAME increments as the PMD lane (phase0),
        // so its marks coincide with PMD marks -> target = m0 - shift.
        double period = (double) (m1 - m0);
        double jAmt = (double) ((float) ((float) p.jitter * (float) p.effectLevel))
                      * 1.0 * lfo * period;
        int64_t shift = (int64_t) std::llround (jAmt);
        int64_t place = m0 - shift;

        // shimmer gain at this mark: original disasm (0x18050A720-0x18050A75A):
        //   v226 = ShimmerParam * LFO[k] * track[k]   (track lane = 1.0)
        //   gain = 10^(+/-v226 * xmm8/xmm9)           (xmm8/xmm9 = 10/20)
        // my p.shimmer may already absorb effectLevel depending on mapping; VD_SHIM_NOEL
        // selects the pure param*LFO product
        static const double shimScale = [] { const char* e = getenv ("VD_SHIM_SCALE"); return e ? atof (e) : 1.0; }();
        static const bool shimNoEl = [] { const char* e = getenv ("VD_SHIM_NOEL"); return e && ! strcmp (e, "1"); }();
        double v226 = shimNoEl ? ((double) p.shimmer * lfo)
                               : ((double) p.shimmer * (double) p.effectLevel * lfo);
        v226 *= shimScale;
        double g1 = std::pow (10.0, v226 * 10.0 / 20.0);
        double g2 = std::pow (10.0, -v226 * 10.0 / 20.0);
        constexpr double eqQ = 0.7071;

        if (dbg)
            fprintf (stderr, "  mark idx=%d m0=%lld m1=%lld m2=%lld grainLen=%d lfo=%.3e shift=%lld v226=%.3e g1=%.3e\n",
                     idx, (long long) m0, (long long) m1, (long long) m2, grainLen, lfo, (long long) shift, v226, g1);

        grain.assign ((size_t) grainLen, 0.0f);
        if (grainSpec && grainSpec->grainSpectralOn())
            grainSpec->beginGrain();   // per-mark MT/noise reset (shared stream across channels)
        for (int c = 0; c < chs; ++c)
        {
            auto& hist = history[(size_t) c];
            for (int i = 0; i < grainLen; ++i)
            {
                // Hann over the inclusive span, t in [0,1] across grainLen-1
                double t = (double) i / (double) (grainLen - 1);
                double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * t);
                int64_t p = m0 + i;
                size_t h = (size_t) ((p % (int64_t) historyLen + (int64_t) historyLen) % (int64_t) historyLen);   // safe modulo
                grain[(size_t) i] = (float) (hist[h] * w);
            }
            // verified bit-close: reciprocal pair at Shimmer Freq1 (A) / Freq2 (B), fs = 44100
            static const double shimFreqA = [] { const char* e = getenv ("VD_SHIM_FREQ"); return e ? atof (e) : -1.0; }();
            static const double shimFreqB = [] { const char* e = getenv ("VD_SHIM_FREQ2"); return e ? atof (e) : -1.0; }();
            double fa = shimFreqA > 0.0 ? shimFreqA : juce::jmax (2.0, (double) p.shimmerFreq1);
            double fb = shimFreqB > 0.0 ? shimFreqB : juce::jmax (2.0, (double) p.shimmerFreq2);
            eqA.reset(); eqA.design (fs, fa, eqQ, g1);
            eqA.process (grain.data(), grainLen);
            eqB.reset(); eqB.design (fs, fb, eqQ, g2);
            eqB.process (grain.data(), grainLen);

            // ref stage 2: per-grain FFT spectral chain (warp + boost) replaces the grain;
            // the level/F0 come from per-mark rings -> modulate with the LFO at this mark
            if (grainSpec && grainSpec->grainSpectralOn())
                grainSpec->processGrain (grain.data(), grainLen, p, f0, lfo);

            if (dbg && c == 0)
            {
                float gpeak = 0.0f;
                for (int i = 0; i < grainLen; ++i) gpeak = juce::jmax (gpeak, std::abs (grain[(size_t) i]));
                fprintf (stderr, "    ch%d afterEQ peak=%.3e  coeffsA b0=%.4f b1=%.4f b2=%.4f a1=%.4f a2=%.4f s1=%.3e s2=%.3e\n",
                         c, gpeak, eqA.b0, eqA.b1, eqA.b2, eqA.a1, eqA.a2, eqA.s1, eqA.s2);
            }

            auto& wet = wetWindow[(size_t) c];
            for (int i = 0; i < grainLen; ++i)
            {
                int64_t dst = place + i;
                if (dst < 0) continue;
                size_t h = (size_t) (dst % (int64_t) historyLen);   // dst >= 0 here
                wet[h] += grain[(size_t) i];
            }
        }
        ++lastProcessedMark;
        ++dbgProcessed;
    }

    if (dbg)
    {
        double wetPeak = 0.0;
        for (auto& w : wetWindow)
            for (float v : w) wetPeak = juce::jmax (wetPeak, std::fabs ((double) v));
        double histPeak = 0.0;
        for (auto& h : history)
            for (float v : h) histPeak = juce::jmax (histPeak, std::fabs ((double) v));
        fprintf (stderr, "blk n=%d marks=%zd processed=%d lastIdx=%d wetPeak=%.3e histPeak=%.3e f0=%.1f\n",
                 n, marks.size(), dbgProcessed, (int) lastProcessedMark, wetPeak, histPeak, f0);
    }

    // emit the delayed wet region for this block: [histWrite-n-outputDelay, histWrite-outputDelay)
    if (grainSpec) grainSpec->endGrainBlock();
    int64_t emitStart = juce::jmax ((int64_t) 0, histWrite - n - outputDelay - extraDelay);
    for (int c = 0; c < chs; ++c)
    {
        auto* dst = out.getWritePointer (c);
        auto& wet = wetWindow[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            int64_t src = emitStart + (int64_t) i;
            if (src < emittedUpTo) { dst[i] = 0.0f; continue; }
            int64_t rel = src % historyLen;
            dst[i] = wet[(size_t) rel];
            wet[(size_t) rel] = 0.0f;
        }
    }
    emittedUpTo = juce::jmax (emittedUpTo, emitStart + n);
}

//==============================================================================
// SpectralStage - STFT formant-warp (mode 4) + harmonic boost + AddGain
void SpectralStage::prepare (double sampleRate, int numChannels)
{
    fs = sampleRate;
    numChans = numChannels;
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (double) i / (double) fftSize);

    inFi.assign ((size_t) numChans, std::vector<float> ((size_t) fftSize, 0.0f));
    outFi.assign ((size_t) numChans, std::vector<float> ((size_t) fftSize, 0.0f));
    fftWork.assign ((size_t) numChans, std::vector<float> ((size_t) (2 * fftSize), 0.0f));

    // measure the forward+inverse round-trip scale of this FFT engine (JUCE pairs may be unnormalized)
    std::vector<float> cal ((size_t) (2 * fftSize), 0.0f);
    cal[(size_t) (fftSize / 2)] = 1.0f;
    fft.performRealOnlyForwardTransform (cal.data());
    fft.performRealOnlyInverseTransform (cal.data());
    roundTripInv = (std::fabs ((double) cal[(size_t) (fftSize / 2)]) > 1e-12)
                       ? (float) (1.0 / (double) cal[(size_t) (fftSize / 2)])
                       : 1.0f / (float) fftSize;
    mag.resize ((size_t) (fftSize / 2 + 1));
    env.resize ((size_t) (fftSize / 2 + 1));
    newEnv.assign ((size_t) (fftSize / 2 + 1), 0.0);
    phaseV.resize ((size_t) (fftSize / 2 + 1));
    sep.prepare (fftOrder);
    reset();
}

void SpectralStage::reset()
{
    for (auto& f : inFi) std::fill (f.begin(), f.end(), 0.0f);
    for (auto& f : outFi) std::fill (f.begin(), f.end(), 0.0f);
    boostNoise.seed (5489u);
    totalIn = 0;
}

void SpectralStage::beginGrain()
{
    boostDrawCount = 0;
    frameNoise.clear();
    grainChannel = 0;
}

static const char* vdEnvDumpPath = getenv ("VD_GRAIN_ENV_DUMP");
static const bool vdEnvDumpOn = vdEnvDumpPath != nullptr;

void SpectralStage::endGrainBlock()
{
    if (vdEnvDumpOn && ! lastEnv.empty())
    {
        static FILE* ef = vdEnvDumpPath ? fopen (vdEnvDumpPath, "wb") : nullptr;
        if (ef)
        {
            int32_t cnt = (int32_t) lastEnv.size();
            fwrite (&cnt, 4, 1, ef);
            fwrite (lastEnv.data(), 8, lastEnv.size(), ef);
            fflush (ef);
        }
    }
    static const char* flatDumpPath = getenv ("VD_GRAIN_FLAT_DUMP");
    if (flatDumpPath && ! lastFlat.empty())
    {
        static FILE* ff = fopen (flatDumpPath, "wb");
        if (ff)
        {
            int32_t cnt = (int32_t) lastFlat.size();
            fwrite (&cnt, 4, 1, ff);
            fwrite (lastFlat.data(), 8, lastFlat.size(), ff);
            fflush (ff);
        }
    }
}

void SpectralStage::separateAdaptive3 (const double* mag, int bins, double* env, double* flat)
{
    const int n = grainN;
    static std::vector<double> envA, quef, reF, mag2, envB, envC, flatA;
    envA.assign ((size_t) bins, 0.0);
    flatA.assign ((size_t) bins, 0.0);
    static const int ad3A = [] { const char* e = getenv ("VD_AD3_A"); return e ? atoi (e) : 1000; }();
    sepG.separate (mag, bins, ad3A, envA.data());
    for (int k = 0; k < bins; ++k) flatA[(size_t) k] = mag[(size_t) k] / juce::jmax (envA[(size_t) k], 1e-12);
    quef.assign ((size_t) n, 0.0);
    sepG.cepstrumDb (flatA.data(), bins, quef.data());
    for (int i = 0; i < n; ++i)
        if (quef[(size_t) i] > 0.1) quef[(size_t) i] = (quef[(size_t) i] - 0.1) * 0.04 + 0.1;
    reF.assign ((size_t) bins, 0.0);
    sepG.forwardRe (quef.data(), bins, reF.data());
    mag2.assign ((size_t) bins, 0.0);
    for (int k = 0; k < bins; ++k)
        mag2[(size_t) k] = mag[(size_t) k] * std::pow (10.0, reF[(size_t) k] * 0.5 / 20.0);
    static const int ad3Wide = [] { const char* e = getenv ("VD_AD3_WIDE"); return e ? atoi (e) : 1000; }();
    envB.assign ((size_t) bins, 0.0);
    sepG.separate (mag2.data(), bins, ad3Wide, envB.data());
    static const int ad3C = [] { const char* e = getenv ("VD_AD3_C"); return e ? atoi (e) : 1000; }();
    envC.assign ((size_t) bins, 0.0);
    sepG.separate (mag2.data(), bins, ad3C, envC.data());
    static const bool passCenvA = [] { const char* e = getenv ("VD_AD3_CENV"); return ! e || atoi (e) != 0; }();
    static const double ad3Eps = [] { const char* e = getenv ("VD_AD3_EPS"); return e ? atof (e) : 1e-12; }();
    for (int k = 0; k < bins; ++k)
    {
        // ref pass C (sub_18051F4E0 296-350): out *= max(eps,X)/max(eps,Y) - all clamped
        double e = passCenvA
            ? std::max (ad3Eps, envA[(size_t) k]) * std::max (ad3Eps, envC[(size_t) k]) / std::max (ad3Eps, envB[(size_t) k])
            : std::max (ad3Eps, envC[(size_t) k]) * std::max (ad3Eps, envC[(size_t) k]) / std::max (ad3Eps, envB[(size_t) k]);
        env[(size_t) k] = e;
        flat[(size_t) k] = mag[(size_t) k] / juce::jmax (e, 1e-12);
    }
}

void SpectralStage::processGrain (float* data, int len, const Params& p, double f0, double levelMod)
{
    if (len < 4 || len > grainN)
        return;
    const int bins = grainBins;

    // grain round-trip calibration (once)
    if (gRoundTripInv == 1.0f)
    {
        gWork.assign ((size_t) (2 * grainN), 0.0f);
        gWork[(size_t) (grainN / 2)] = 1.0f;
        fftG.performRealOnlyForwardTransform (gWork.data());
        fftG.performRealOnlyInverseTransform (gWork.data());
        gRoundTripInv = std::fabs ((double) gWork[(size_t) (grainN / 2)]) > 1e-12
                            ? (float) (1.0 / (double) gWork[(size_t) (grainN / 2)]) : 1.0f / (float) grainN;
    }

    gBuf.assign ((size_t) (2 * grainN), 0.0f);
    for (int i = 0; i < len; ++i) gBuf[(size_t) i] = data[i];
    fftG.performRealOnlyForwardTransform (gBuf.data());

    gMag.assign ((size_t) bins, 0.0);
    gPhase.assign ((size_t) bins, 0.0);
    double magSum = 0.0;
    for (int k = 0; k < bins; ++k)
    {
        double re = (double) gBuf[(size_t) (2 * k)], im = (double) gBuf[(size_t) (2 * k + 1)];
        gMag[(size_t) k] = std::sqrt (re * re + im * im);
        gPhase[(size_t) k] = std::atan2 (im, re);
        magSum += gMag[(size_t) k];
    }

    // warp anchors: the ref maps Hz->bins with a rate that may be the engine grid rate
    // (50000, engine+24 = 50000/44100) rather than fs - VD_WARP_RATE selects (default fs)
    static const double warpBinRate = [] { const char* e = getenv ("VD_WARP_RATE"); return e ? atof (e) : 44100.0; }();
    const double binHz = warpBinRate / (double) grainN;
    const double shiftAmt  = (double) p.shiftLevel * (double) p.effectLevel * levelMod;
    const double shiftRate = (double) p.shiftRate;
    const double boostLevel = (double) p.bstLevel * (double) p.effectLevel * levelMod;
    const double addGainLin = std::pow (10.0, (double) p.addGain * (double) p.effectLevel * levelMod / 20.0);
    const bool shiftActive = std::fabs (shiftRate * shiftAmt) > 1e-6;
    const bool boostActive = std::fabs (boostLevel) > 1e-9 && magSum > 0.0;

    // ---- separation once (ref: sub_1805219E0 dual output = adaptive3 family) ----
    std::vector<double> envT ((size_t) bins, 1.0), flatT ((size_t) bins, 0.0);
    if (shiftActive || boostActive)
    {
        if (! sepReady) { sepG.prepare (grainOrder); sepReady = true; }
        static const bool useAd3 = [] { const char* e = getenv ("VD_GRAIN_SEP"); return ! e || strcmp (e, "simple") != 0; }();
        if (useAd3)
        {
            separateAdaptive3 (gMag.data(), bins, envT.data(), flatT.data());
        }
        else
        {
            static const int grainFlatEnv = [] { const char* e = getenv ("VD_BOOST_FLATENV"); return e ? atoi (e) : 2500; }();
            int aEnv = grainFlatEnv > 0 ? grainFlatEnv : juce::jlimit (8, grainN / 4, (int) std::round (fs / 1000.0));
            gEnv.assign ((size_t) bins, 0.0);
            sepG.separate (gMag.data(), bins, aEnv, gEnv.data());
            for (int k = 0; k < bins; ++k)
            {
                envT[(size_t) k] = std::max (1e-12, gEnv[(size_t) k]);
                flatT[(size_t) k] = (double) gMag[(size_t) k] / envT[(size_t) k];
            }
        }
    }

    // ---- formant warp (mode 4, sub_18051ED70 EXACT) on the ENV in ln domain ----
    // anchors (Hz): aLow = min + (max-min)/3, aHigh = min + 2(max-min)/3, d = rate·amt·(aLow-min)/2
    // output breakpoints k3 = round((aLow+d)/binHz), k4 = round((aHigh+d)/binHz); source k1, k2.
    // seg A: out [lo,k3) <- src lo..k1; seg B: [k3,k4) <- k1..k2; seg C: [k4,hi] <- k2..hi
    if (shiftActive)
    {
        int lo = juce::jlimit (1, bins - 3, (int) ((double) p.shiftRgeMin / binHz));
        int hi = juce::jlimit (lo + 6, bins - 2, (int) ((double) p.shiftRgeMax / binHz));
        double rMin = (double) p.shiftRgeMin, rMax = (double) p.shiftRgeMax;
        double aLow = rMin + (rMax - rMin) / 3.0, aHigh = rMin + 2.0 * (rMax - rMin) / 3.0;
        static const double dScale = [] { const char* e = getenv ("VD_SHIFT_DSCALE"); return e ? atof (e) : 1.0; }();
        double d = shiftRate * shiftAmt * dScale * 0.5 * (aLow - rMin);
        auto toBin = [&] (double hz) { int k = (int) std::lround (hz / binHz); return juce::jlimit (0, bins - 1, k); };
        int k1 = toBin (aLow), k2 = toBin (aHigh), k3 = toBin (aLow + d), k4 = toBin (aHigh + d);
        static const double mixOverride = [] { const char* e = getenv ("VD_SHIFT_MIX"); return e ? atof (e) : -1.0; }();
        double mix = mixOverride >= 0.0 ? mixOverride : (double) p.shiftMixLevel;

        std::vector<double> lnv ((size_t) bins), warped ((size_t) bins);
        for (int k = 0; k < bins; ++k)
            lnv[(size_t) k] = std::log (std::max (1e-300, envT[(size_t) k]));
        auto readLn = [&] (double pos)
        {
            double c = juce::jlimit (0.0, (double) (bins - 1), pos);
            int i0 = (int) c;
            double t = c - (double) i0;
            int i1 = juce::jmin (bins - 1, i0 + 1);
            return lnv[(size_t) i0] * (1.0 - t) + lnv[(size_t) i1] * t;
        };
        for (int out = lo; out <= hi; ++out)
        {
            double src;
            if (out < k3)      src = (double) lo + (double) (k1 - lo) * (double) (out - lo) / (double) juce::jmax (1, k3 - lo);
            else if (out < k4) src = (double) k1 + (double) (k2 - k1) * (double) (out - k3) / (double) juce::jmax (1, k4 - k3);
            else               src = (double) k2 + (double) (hi - k2) * (double) (out - k4) / (double) juce::jmax (1, hi - k4);
            warped[(size_t) out] = readLn (src);
        }
        for (int out = lo; out <= hi; ++out)
            envT[(size_t) out] = std::exp ((1.0 - mix) * lnv[(size_t) out] + mix * warped[(size_t) out]);
        // probe: keep the post-warp env for the block-end flush (compare with ref envHist)
        if (grainChannel == 0 && vdEnvDumpOn)
            lastEnv = envT;
    }

    // ---- NI-harmonics boost (case-3) on the FLAT cepstrum; ratio applied to flat ----
    double boostRatioScale = 1.0;
    if (boostActive)
    {
        const int nq = grainN;
        boostFlat = flatT;
        boostCep.assign ((size_t) nq, 0.0);
        {
            boostCepBuf.assign ((size_t) (2 * grainN), 0.0f);
            for (int k = 0; k < bins; ++k)
            {
                float db = (float) (20.0 * std::log10 (std::max (1e-300, boostFlat[(size_t) k] * boostFlat[(size_t) k])));
                boostCepBuf[(size_t) (2 * k)] = db;
                if (k > 0 && k < grainN / 2)
                    boostCepBuf[(size_t) (2 * (grainN - k))] = db;
            }
            fftG.performRealOnlyInverseTransform (boostCepBuf.data());
            for (int k = 0; k < nq; ++k) boostCep[(size_t) k] = boostCepBuf[(size_t) (2 * k)];
        }
        std::vector<double> refCep = boostCep;
        static const double bstLscale = [] { const char* e = getenv ("VD_BST_LSCALE"); return e ? atof (e) : 1.0; }();
        double levelDb = boostLevel * 30.0 * bstLscale;
        double wAbs = std::fabs (levelDb) / 15.0;
        int L = (f0 > 20.0) ? (int) std::lround (fs / f0) : 0;
        if (L > 0)
        {
            int halfw = juce::jmax (1, (int) std::lround (25.0 * fs / 44100.0) / 2);
            for (int h = 1; h <= 10; ++h)
            {
                int lag = h * L;
                if (lag >= bins) break;
                if (lag > 0 && lag + 1 < bins
                    && boostCep[(size_t) lag] < 0.0 && boostCep[(size_t) lag - 1] < 0.0
                    && boostCep[(size_t) lag + 1] < 0.0)
                    continue;
                int lo = juce::jmax (0, lag - halfw), hi = juce::jmin (bins - 1, lag + halfw);
                if (hi < lo) continue;
                double nAmp = juce::jmax (0.1, 0.25 - (double) (h - 1) * 0.05);
                static const int mxMode = [] { const char* e = getenv ("VD_BST_MX"); return e ? atoi (e) : 1; }();
                double mx = 0.0;
                if (mxMode == 1)
                { for (int k = 0; k < nq; ++k) mx = juce::jmax (mx, std::fabs (refCep[(size_t) k])); }
                else if (mxMode == 2)
                { for (int k = juce::jmax (0, lag - halfw * 3); k <= juce::jmin (bins - 1, lag + halfw * 3); ++k) mx = juce::jmax (mx, std::fabs (refCep[(size_t) k])); }
                else
                { for (int k = lo; k <= hi; ++k) mx = juce::jmax (mx, std::fabs (refCep[(size_t) k])); }
                if (mx <= 0.0) mx = 1e-12;
                for (int k = lo; k <= hi; ++k)
                {
                    size_t dIdx = (size_t) (boostDrawCount++);
                    if (grainChannel == 0)
                    {
                        static const int ncMode = [] { const char* e = getenv ("VD_BST_NC"); return e ? atoi (e) : 0; }();
                        double noise = std::generate_canonical<double, 53> (boostNoise);
                        if (ncMode == 0) noise = noise * 2.0 - 1.0;
                        if (frameNoise.size() <= dIdx) frameNoise.resize (dIdx + 1, 0.0);
                        frameNoise[dIdx] = noise;
                    }
                    double noise = (dIdx < frameNoise.size()) ? frameNoise[dIdx] : 0.0;
                    double nk = noise * nAmp;
                    double x = refCep[(size_t) k];
                    static const int reflMode = [] { const char* e = getenv ("VD_BST_REFL"); return e ? atoi (e) : 0; }();
                    if (reflMode == 1 && wAbs > 1.0)
                        boostCep[(size_t) k] = (2.0 - wAbs) * nk - x * (wAbs - 1.0);   // no /mx
                    else
                        boostCep[(size_t) k] = (wAbs > 1.0)
                            ? ((2.0 - wAbs) * nk - (1.0 / mx) * x * (wAbs - 1.0))
                            : (wAbs * nk + (1.0 - wAbs) * x);
                }
            }
        }
        auto reconstructDb = [&] (const std::vector<double>& cep, std::vector<double>& outDb)
        {
            // exact ref (sub_180521270): mirror-extend the cepstrum [X0..X_{n/2}] +
            // reversed [X_{n/2-1}..X_1] into a 1024-real buffer, forward FFT, bins real
            boostCepBuf.assign ((size_t) (2 * grainN), 0.0f);
            for (int k = 0; k < bins; ++k)
                boostCepBuf[(size_t) k] = (float) cep[(size_t) k];
            for (int k = 1; k < bins - 1; ++k)
                boostCepBuf[(size_t) (2 * grainN - k)] = (float) cep[(size_t) k];
            fftG.performRealOnlyForwardTransform (boostCepBuf.data());
            outDb.assign ((size_t) bins, 0.0);
            for (int k = 0; k < bins; ++k)
                outDb[(size_t) k] = (double) boostCepBuf[(size_t) (2 * k)];
        };

        // MT-stream parity: the ref's recursive level=-1 pass (sub_180522C00 tail, level
        // 0xBFF0000000000000) walks the SAME lag windows on the SAME original cepstrum
        // and DRAWS the shared stream per element (its output coefficient (2-w)=0 makes
        // it output-transparent, but the draws still advance). Consume them here.
        if (L > 0)
        {
            int halfw2 = juce::jmax (1, (int) std::lround (25.0 * fs / 44100.0) / 2);
            for (int h = 1; h <= 10; ++h)
            {
                int lag = h * L;
                if (lag >= bins) break;
                if (lag > 0 && lag + 1 < bins
                    && refCep[(size_t) lag] < 0.0 && refCep[(size_t) (lag - 1)] < 0.0
                    && refCep[(size_t) (lag + 1)] < 0.0)
                    continue;
                int lo2 = juce::jmax (0, lag - halfw2), hi2 = juce::jmin (bins - 1, lag + halfw2);
                if (hi2 < lo2) continue;
                for (int k = lo2; k <= hi2; ++k)
                {
                    size_t dIdx = (size_t) (boostDrawCount++);
                    if (grainChannel == 0)
                    {
                        (void) std::generate_canonical<double, 53> (boostNoise);
                        if (frameNoise.size() <= dIdx) frameNoise.resize (dIdx + 1, 0.0);
                    }
                }
            }
        }

        reconstructDb (boostCep, boostDbNew);
        reconstructDb (refCep, boostDbRef);
        static const double bstExp = [] { const char* e = getenv ("VD_BST_EXP"); return e ? atof (e) : 40.0; }();
        for (int k = 0; k < bins; ++k)
        {
            double ratio = std::pow (10.0, (boostDbNew[(size_t) k] - boostDbRef[(size_t) k]) / bstExp);
            if (ratio <= 2.220446049250313e-16) ratio = 0.0;
            flatT[(size_t) k] *= juce::jmax (0.0, ratio);
        }

        // ---- stage II+III (ref tail 1622-1684): the recursive level=-1 reflection pass
        // X'[k] = -X[k]/mx on the same windows of the ORIGINAL cepstrum (deterministic,
        // coefficient (2-w)=0), then out = magRefl * 10^((dB env(magMod) - dB env(magRefl))/20)
        static const bool boost3 = [] { const char* e = getenv ("VD_BOOST3"); return e && atoi (e) == 1; }();
        if (boost3 && L > 0)
        {
            // stage II: reflection cepstrum from the ORIGINAL flat
            std::vector<double> reflCep = refCep;
            int halfw3 = juce::jmax (1, (int) std::lround (25.0 * fs / 44100.0) / 2);
            for (int h = 1; h <= 10; ++h)
            {
                int lag = h * L;
                if (lag >= bins) break;
                if (lag > 0 && lag + 1 < bins
                    && refCep[(size_t) lag] < 0.0 && refCep[(size_t) (lag - 1)] < 0.0
                    && refCep[(size_t) (lag + 1)] < 0.0)
                    continue;
                int lo3 = juce::jmax (0, lag - halfw3), hi3 = juce::jmin (bins - 1, lag + halfw3);
                if (hi3 < lo3) continue;
                double mx3 = 0.0;
                for (int k = lo3; k <= hi3; ++k) mx3 = juce::jmax (mx3, std::fabs (refCep[(size_t) k]));
                if (mx3 <= 0.0) mx3 = 1e-12;
                for (int k = lo3; k <= hi3; ++k)
                    reflCep[(size_t) k] = -refCep[(size_t) k] / mx3;
            }
            // dB-domain reconstruction ratio of the reflected cepstrum vs original
            std::vector<double> dbRefl, dbOrig;
            reconstructDb (reflCep, dbRefl);
            reconstructDb (refCep, dbOrig);
            std::vector<double> magRefl ((size_t) bins), magMod2 ((size_t) bins);
            for (int k = 0; k < bins; ++k)
            {
                double rr = std::pow (10.0, (dbRefl[(size_t) k] - dbOrig[(size_t) k]) / 40.0);
                magRefl[(size_t) k] = (double) gMag[(size_t) k] * juce::jmax (0.0, rr);
                magMod2[(size_t) k] = envT[(size_t) k] * flatT[(size_t) k];
            }
            // stage III: env-ratio combine (lifter 2000 in ref; saturated ~ any >=1000)
            std::vector<double> envR ((size_t) bins), envM2 ((size_t) bins), fl3 ((size_t) bins);
            sepG.separate (magRefl.data(), bins, 2000, envR.data());
            sepG.separate (magMod2.data(), bins, 2000, envM2.data());
            for (int k = 0; k < bins; ++k)
            {
                double dbR = 20.0 * std::log10 (std::max (1e-300, envM2[(size_t) k]));   // env of magMod
                double dbRf = 20.0 * std::log10 (std::max (1e-300, envR[(size_t) k]));   // env of magRefl
                double ratio3 = std::pow (10.0, (dbR - dbRf) / 20.0);
                if (ratio3 <= 2.220446049250313e-16) ratio3 = 0.0;
                flatT[(size_t) k] = magRefl[(size_t) k] * juce::jmax (0.0, ratio3) / std::max (1e-12, envT[(size_t) k]);
            }
        }

        // ---- stage 2 (ref tail, sub_180522C00 1690-1751): dual re-separation in dB
        // domain. magMod = env*flat(boosted); separate both mags with lifter 5000-ish,
        // then flat *= 10^((dB(envMod) - dB(envOrig)) * gain / 20)
        static const bool boost2 = [] { const char* e = getenv ("VD_BOOST2"); return ! e || atoi (e) != 0; }();
        if (boost2)
        {
            std::vector<double> magOrig ((size_t) bins), magMod ((size_t) bins);
            for (int k = 0; k < bins; ++k)
            {
                magOrig[(size_t) k] = gMag[(size_t) k];
                magMod[(size_t) k]  = envT[(size_t) k] * flatT[(size_t) k];
            }
            static const int b2Lifter = [] { const char* e = getenv ("VD_BOOST2_LIFTER"); return e ? atoi (e) : 511; }();
            std::vector<double> envO ((size_t) bins), envM ((size_t) bins), fl ((size_t) bins);
            sepG.separate (magOrig.data(), bins, b2Lifter, envO.data());
            sepG.separate (magMod.data(), bins, b2Lifter, envM.data());
            double v256 = juce::jlimit (0.0, 1.0, wAbs);   // positive-level gain ~ w
            for (int k = 0; k < bins; ++k)
            {
                double dbO = 20.0 * std::log10 (std::max (1e-300, envO[(size_t) k]));
                double dbM = 20.0 * std::log10 (std::max (1e-300, envM[(size_t) k]));
                double delta = (dbO - dbM) * v256;   // ref: dB(env orig) - dB(env modified)
                double v = flatT[(size_t) k] * std::pow (10.0, delta / 20.0);
                flatT[(size_t) k] = (v <= 2.220446049250313e-16) ? 0.0 : v;
            }
        }
    }
    (void) boostRatioScale;

    // ---- recombine: mag = env (warped) * flat (boosted), original phases ----
    if (shiftActive || boostActive)
    {
        if (vdEnvDumpOn && grainChannel == 0)
            lastFlat = flatT;
        for (int k = 0; k < bins; ++k)
            gMag[(size_t) k] = (float) juce::jmax (0.0, envT[(size_t) k] * flatT[(size_t) k]);
    }



    // recombine with original phases, inverse FFT, write back
    gBuf.assign ((size_t) (2 * grainN), 0.0f);
    for (int k = 0; k < bins; ++k)
    {
        gBuf[(size_t) (2 * k)]     = (float) (gMag[(size_t) k] * std::cos (gPhase[(size_t) k]));
        gBuf[(size_t) (2 * k + 1)] = (float) (gMag[(size_t) k] * std::sin (gPhase[(size_t) k]));
    }
    fftG.performRealOnlyInverseTransform (gBuf.data());
    for (int i = 0; i < len; ++i)
        data[i] = (float) ((double) gBuf[(size_t) i] * (double) gRoundTripInv * addGainLin);
    ++grainChannel;
    if (grainChannel >= numChans) grainChannel = 0;   // wrap after the last channel of this grain
}

void SpectralStage::process (juce::AudioBuffer<float>& buffer, const Params& p, double f0)
{
    const int n = buffer.getNumSamples();
    const int chs = juce::jmin (buffer.getNumChannels(), numChans);
    const int bins = fftSize / 2 + 1;
    const double binHz = fs / (double) fftSize;

    const double shiftAmt  = (double) p.shiftLevel * (double) p.effectLevel;
    const double shiftRate = (double) p.shiftRate;                    // bipolar
    const double boostLevel = (double) p.bstLevel * (double) p.effectLevel;
    const double addGainLin = std::pow (10.0, (double) p.addGain * (double) p.effectLevel / 20.0);

    for (int s = 0; s < n; ++s)
    {
        // push this sample into every channel fifo & pull OLA output
        for (int c = 0; c < chs; ++c)
        {
            auto& in = inFi[(size_t) c];
            auto& ovl = outFi[(size_t) c];
            std::memmove (in.data(), in.data() + 1, sizeof (float) * (size_t) (fftSize - 1));
            in[(size_t) (fftSize - 1)] = buffer.getSample (c, s);
            float wetOut = ovl[0];
            std::memmove (ovl.data(), ovl.data() + 1, sizeof (float) * (size_t) (fftSize - 1));
            ovl[(size_t) (fftSize - 1)] = 0.0f;
            buffer.setSample (c, s, wetOut * (float) addGainLin);
        }

        totalIn++;
        if (totalIn % hopSize != 0) continue;

        boostDrawCount = 0;   // per-frame MT draw index (shared across channels)
        frameNoise.clear();
        flatHopCount++;       // per-hop flat-track record index (regardless of boost skip)
        // a frame is complete for every channel - process each
        for (int c = 0; c < chs; ++c)
        {
            auto& in = inFi[(size_t) c];
            auto& fw = fftWork[(size_t) c];
            std::fill (fw.begin(), fw.end(), 0.0f);
            for (int i = 0; i < fftSize; ++i)
                fw[(size_t) i] = (float) (in[(size_t) i] * window[(size_t) i]);
            fft.performRealOnlyForwardTransform (fw.data());

            for (int k = 0; k < bins; ++k)
            {
                double re = fw[(size_t) (2 * k)], im = fw[(size_t) (2 * k + 1)];
                mag[(size_t) k] = std::sqrt (re * re + im * im);
                phaseV[(size_t) k] = std::atan2 (im, re);
            }

            int aEnv = juce::jlimit (8, fftSize / 4, (int) std::round (fs / 1000.0));
            sep.separate (mag.data(), bins, aEnv, env.data());

            // ---- formant warp, mode 4 (piecewise thirds) with Mix crossfade ----
            if (std::fabs (shiftRate * shiftAmt) > 1e-6)
            {
                int lo = juce::jlimit (1, bins - 3, (int) ((double) p.shiftRgeMin / binHz));
                int hi = juce::jlimit (lo + 6, bins - 2, (int) ((double) p.shiftRgeMax / binHz));
                double span = (double) (hi - lo);
                double a1b = lo + span / 3.0, b1b = lo + 2.0 * span / 3.0;
                double d = shiftRate * shiftAmt * span / 6.0;
                double mix = (double) p.shiftMixLevel;

                // inverse warp: for each output bin find contributing input position
                std::vector<double> warped ((size_t) bins);
                auto inPos = [&] (double u)   // uin [0,1] -> warped source coordinate (anchors 0->lo, 1/3->a1b+d, 2/3->b1b+d, 1->hi)
                {
                    if (u < 1.0 / 3.0)
                        return lo + 3.0 * u * ((a1b + d) - lo);
                    if (u < 2.0 / 3.0)
                        return (a1b + d) + 3.0 * (u - 1.0 / 3.0) * ((b1b + d) - (a1b + d));
                    return (b1b + d) + 3.0 * (u - 2.0 / 3.0) * (hi - (b1b + d));
                };
                for (int k = lo; k <= hi; ++k)
                {
                    double u = ((double) k - lo) / span;
                    double srcPos = juce::jlimit ((double) lo, (double) hi, inPos (u));
                    int i0 = (int) srcPos;
                    double t = srcPos - (double) i0;
                    double v = mag[(size_t) i0] * (1.0 - t) + mag[(size_t) (i0 + 1)] * t;
                    warped[(size_t) k] = mag[(size_t) k] * (1.0 - mix) + v * mix;
                }
                for (int k = lo; k <= hi; ++k) mag[(size_t) k] = warped[(size_t) k];
            }

            // ---- harmonic boost ("NI-Harmonics") - EXACT case-3 skeleton (sub_180522C00):
            // X = dB-cepstrum of FLAT (= mag/env, the harmonic residual companion a6);
            // L = round(fs/F0); for h = 1..10 lag = h*L; window [lag-12, lag+12];
            // noise[k] = MT_draw * max(0.1, 0.25-(h-1)*0.05); w = |level_dB|/15;
            //   w<=1: X[k] = w*noise[k] + (1-w)*X[k]
            //   w>1 : X[k] = (2-w)*noise[k] - (1/maxAbsX)*X[k]*(w-1)   (inverted mix)
            // reconstruct flat' = ReFFT(binomialLifter(2500) * X) (sub_180521270),
            // then mag = env * flat'  (tail: pow(10, (dB(env)+dB(flat'))/20)).
            // the original guards on the SUM of the magnitudes: sum <= 0 -> the whole
            // boost returns early (no MT draws on silent frames)
            double magSum = 0.0;
            for (int k = 0; k < bins; ++k) magSum += (double) mag[(size_t) k];
            if (std::fabs (boostLevel) > 1e-9 && magSum > 0.0)
            {
                const int nq = fftSize;
                // FLAT = mag / env (env = separated envelope; VD_BOOST_FLATENV selects the
                // liffer used for THIS envelope, independent of the aEnv one above)
                static const int flatEnvA = [] { const char* e = getenv ("VD_BOOST_FLATENV"); return e ? atoi (e) : 0; }();
                // VD_FLATFILE: replay the ORIGINAL's per-frame flat track (extracted from
                // its state dump) as the boost input - isolates the flat-source error
                static std::vector<int> flatCnts;
                static std::vector<double> flatData;
                static const bool loadFlat = [] {
                    const char* fn = getenv ("VD_FLATFILE");
                    if (! fn) return false;
                    FILE* f = fopen (fn, "rb");
                    if (! f) return false;
                    flatCnts.clear(); flatData.clear();
                    int cnt;
                    while (fread (&cnt, 4, 1, f) == 1)
                    {
                        flatCnts.push_back (cnt);
                        size_t was = flatData.size();
                        flatData.resize (was + (size_t) std::max (cnt, 0));
                        if (cnt > 0 && fread (flatData.data() + was, 8, (size_t) cnt, f) != (size_t) cnt) break;
                    }
                    fclose (f);
                    return flatCnts.size() > 0;
                } ();
                (void) loadFlat;
                bool usedTrack = false;
                size_t trackBase = 0;
                if (! flatCnts.empty() && flatHopCount > 0 && flatHopCount <= (int) flatCnts.size())
                {
                    int rec = flatHopCount - 1;                     // 0-based record index
                    for (int r = 0; r < rec; ++r)
                        trackBase += (size_t) std::max (flatCnts[(size_t) r], 0);
                    int cnt = flatCnts[(size_t) rec];
                    if (cnt > 0 && trackBase + (size_t) cnt <= flatData.size())
                    {
                        boostFlat.assign ((size_t) juce::jmin (cnt, bins), 0.0);
                        for (int k = 0; k < juce::jmin (cnt, bins); ++k)
                            boostFlat[(size_t) k] = flatData[trackBase + (size_t) k];
                        usedTrack = true;
                    }
                }
                if (! usedTrack && flatEnvA > 0)
                {
                    static const bool dbg2 = getenv ("VD_FLATDBG") != nullptr;
                    if (dbg2 && flatHopCount < 12)
                        fprintf (stderr, "FLATDBG-used f=%d used=%d\n", flatHopCount, (int) usedTrack);
                    boostEnv.assign ((size_t) bins, 0.0);
                    sep.separate (mag.data(), bins, flatEnvA, boostEnv.data());
                    boostFlat.assign ((size_t) bins, 0.0);
                    for (int k = 0; k < bins; ++k)
                    {
                        double e = std::max (1e-12, boostEnv[(size_t) k]);
                        boostFlat[(size_t) k] = (double) mag[(size_t) k] / e;
                    }
                }
                else
                {
                    boostFlat.assign ((size_t) bins, 0.0);
                    for (int k = 0; k < bins; ++k)
                    {
                        double e = std::max (1e-12, (double) env[(size_t) k]);
                        boostFlat[(size_t) k] = (double) mag[(size_t) k] / e;
                    }
                }
                boostCep.assign ((size_t) nq, 0.0);
                {
                    boostCepBuf.assign ((size_t) (2 * fftSize), 0.0f);
                    for (int k = 0; k < bins; ++k)
                    {
                        float db = (float) (20.0 * std::log10 (std::max (1e-300, boostFlat[(size_t) k] * boostFlat[(size_t) k])));
                        boostCepBuf[(size_t) (2 * k)] = db;
                        if (k > 0 && k < fftSize / 2)
                            boostCepBuf[(size_t) (2 * (fftSize - k))] = db;
                    }
                    fft.performRealOnlyInverseTransform (boostCepBuf.data());
                    for (int k = 0; k < nq; ++k) boostCep[(size_t) k] = boostCepBuf[(size_t) (2 * k)];
                }
                // IPP inverse FFTs are typically unnormalized; JUCE scales by 1/N.
                // VD_BOOST_CEPMUL scales X to probe the original's cepstrum magnitude.
                static const double cepMul = [] { const char* e = getenv ("VD_BOOST_CEPMUL"); return e ? atof (e) : 1.0; }();
                if (cepMul != 1.0)
                    for (auto& v : boostCep) v *= cepMul;
                std::vector<double> refCep = boostCep;
                double levelDb = boostLevel * 30.0;
                double wAbs = std::fabs (levelDb) / 15.0;
                int L = (f0 > 20.0) ? (int) std::lround (fs / f0) : 0;
                static const int lShift = [] { const char* e = getenv ("VD_BOOST_LSH"); return e ? atoi (e) : 0; }();
                L += lShift;
                if (L > 0)
                {
                    int halfw = juce::jmax (1, (int) std::lround (25.0 * fs / 44100.0) / 2);
                    for (int h = 1; h <= 10; ++h)
                    {
                        int lag = h * L;
                        if (lag >= bins) break;
                        if (lag > 0 && lag + 1 < bins
                            && boostCep[(size_t) lag] < 0.0 && boostCep[(size_t) lag - 1] < 0.0
                            && boostCep[(size_t) lag + 1] < 0.0)
                            continue;
                        int lo = juce::jmax (0, lag - halfw), hi = juce::jmin (bins - 1, lag + halfw);
                        if (hi < lo) continue;
                        double nAmp = juce::jmax (0.1, 0.25 - (double) (h - 1) * 0.05);
                        double mx = 0.0;
                        for (int k = lo; k <= hi; ++k) mx = juce::jmax (mx, std::fabs (refCep[(size_t) k]));
                        if (mx <= 0.0) mx = 1e-12;
                        for (int k = lo; k <= hi; ++k)
                        {
                            static const int noiseKind = [] { const char* e = getenv ("VD_BOOST_NOISE"); return e ? atoi (e) : 0; }();
                            // the original draws the MT stream ONCE per frame and shares it
                            // across channels (verified: ref L == R bit-exact); cache the
                            // frame's draws when c == 0 and replay for c > 0
                            size_t dIdx = (size_t) (boostDrawCount++);
                            if (c == 0)
                            {
                                double noise;
                                if (noiseKind == 1) noise = std::generate_canonical<double, 53> (boostNoise);
                                else if (noiseKind == 2)
                                    noise = (double) ((boostNoise() >> 11) * (1.0 / 9007199254740992.0)) * 2.0 - 1.0;
                                else noise = std::generate_canonical<double, 53> (boostNoise) * 2.0 - 1.0;
                                if (frameNoise.size() <= dIdx) frameNoise.resize (dIdx + 1, 0.0);
                                frameNoise[dIdx] = noise;
                            }
                            double noise = (dIdx < frameNoise.size()) ? frameNoise[dIdx] : 0.0;
                            double nk = noise * nAmp;
                            double x = refCep[(size_t) k];
                            boostCep[(size_t) k] = (wAbs > 1.0)
                                ? ((2.0 - wAbs) * nk - (1.0 / mx) * x * (wAbs - 1.0))
                                : (wAbs * nk + (1.0 - wAbs) * x);
                        }
                    }
                }
                // reconstruction: liftered dual-reconstruction ratio (empirical optimum,
                // A=100 taper, /20 scale - the exact /40 unliftered variant measured worse:
                // our flat track differs from the caller's temporally-smoothed env2Hist)
                boostLifter.assign ((size_t) nq, 0.0);
                {
                    static const int A = [] { const char* e = getenv ("VD_BOOST_LIFTER"); return e ? atoi (e) : 1 << 24; }();
                    double wgt = 1.0;
                    boostLifter[0] = wgt;
                    for (int i = 1; i < nq / 2 && i <= 2 * A; ++i)
                    {
                        wgt *= (double) (2 * A - i + 1) / (double) (2 * A + i);
                        boostLifter[(size_t) i] = wgt;
                        boostLifter[(size_t) (nq - i)] = wgt;
                    }
                }
                auto reconstructDb = [&] (const std::vector<double>& cep, std::vector<double>& outDb)
                {
                    boostCepBuf.assign ((size_t) (2 * fftSize), 0.0f);
                    for (int k = 0; k < nq; ++k)
                        boostCepBuf[(size_t) (2 * k)] = (float) (cep[(size_t) k] * boostLifter[(size_t) k]);
                    fft.performRealOnlyForwardTransform (boostCepBuf.data());
                    outDb.assign ((size_t) bins, 0.0);
                    for (int k = 0; k < bins; ++k)
                        outDb[(size_t) k] = (double) boostCepBuf[(size_t) (2 * k)];
                };
                reconstructDb (boostCep, boostDbNew);
                reconstructDb (refCep, boostDbRef);
                {
                    static const int dbScale = [] { const char* e = getenv ("VD_BOOST_SCALE"); return e ? atoi (e) : 40; }();
                    for (int k = 0; k < bins; ++k)
                    {
                        double ratio = std::pow (10.0, (boostDbNew[(size_t) k] - boostDbRef[(size_t) k]) / (double) dbScale);
                        if (ratio <= 2.220446049250313e-16) ratio = 0.0;
                        mag[(size_t) k] *= (float) juce::jmax (0.0, ratio);
                    }
                }
            }

            // recombine (original phases) + IFFT + weighted OLA
            std::fill (fw.begin(), fw.end(), 0.0f);
            for (int k = 0; k < bins; ++k)
            {
                fw[(size_t) (2 * k)]     = (float) (mag[(size_t) k] * std::cos (phaseV[(size_t) k]));
                fw[(size_t) (2 * k + 1)] = (float) (mag[(size_t) k] * std::sin (phaseV[(size_t) k]));
            }
            fft.performRealOnlyInverseTransform (fw.data());
            auto& ovl = outFi[(size_t) c];
            for (int i = 0; i < fftSize; ++i)
                ovl[(size_t) i] += (float) ((double) fw[(size_t) i] * window[(size_t) i] * (2.0 / 3.0)) * roundTripInv;
        }
    }
}

//==============================================================================
// VoiceDriveDSP
void VoiceDriveDSP::prepare (double sr, juce::uint32 maxBlock, juce::uint32 numChannels)
{
    juce::ignoreUnused (maxBlock);
    sampleRate = sr;
    channels = numChannels;
    f0Fifo.assign ((size_t) f0Frame, 0.0);
    f0Fill = 0;
    estimator.prepare (f0Frame);
    psola.prepare (sr, (int) numChannels);
    spectral.prepare (sr, (int) numChannels);
    {
        static const bool grainArch = [] { const char* e = getenv ("VD_GRAIN"); return ! e || atoi (e) != 0; }();
        spectral.setGrainOn (grainArch);
        psola.setGrainSpectral (grainArch ? &spectral : nullptr);
        psola.setExtraDelay (grainArch ? 2048 : 0);
    }
    reset();
}

void VoiceDriveDSP::reset()
{
    std::fill (f0Fifo.begin(), f0Fifo.end(), 0.0);
    f0Fill = 0;
    currentF0 = 0.0;
    voiced = stable = false;
    wasSilent = true;
    clockA = 0.0;
    markJitter.mt.seed (5489u);
    vibration.resetPhase();
    vibration.resetNoise();
    psola.reset();
    spectral.reset();
}

int VoiceDriveDSP::getLatencySamples() const
{
    return psola.getOutputDelay() + SpectralStage::fftSize;
}

void VoiceDriveDSP::process (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int chs = buffer.getNumChannels();
    monoScratch.assign ((size_t) n, 0.0);

    // silence detection (original: RMS < 0.001 -> reset EVERY silent block:
    // phases clamped >= 0.5, LFO phase reset, engines incl. the clock MT reseeded)
    double sumSq = 0.0;
    for (int c = 0; c < chs; ++c)
    {
        auto* d = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i) sumSq += (double) d[i] * d[i];
    }
    bool silent = std::sqrt (sumSq / juce::jmax (1.0, (double) n * chs)) < 0.001;
    if (silent)
    {
        vibration.resetNoise();
        static const bool noJitReset = [] { const char* e = getenv ("VD_NO_JIT_RESET"); return e && ! strcmp (e, "1"); }();
        if (! noJitReset) markJitter.reset();
        clockA = juce::jmax (clockA, 0.5);
        vibration.resetPhase();
    }
    wasSilent = silent;

    // mono mix for the F0 front-end
    for (int i = 0; i < n; ++i)
    {
        double m = 0.0;
        for (int c = 0; c < chs; ++c) m += buffer.getSample (c, i);
        monoScratch[(size_t) i] = m / (double) chs;
    }

    // F0 front-end (at 44100 internally, Catmull-Rom resampled when the host rate differs)
    if (std::fabs (sampleRate - 44100.0) < 0.5)
    {
        for (int i = 0; i < n; ++i)
        {
            f0Fifo[(size_t) f0Fill++] = monoScratch[(size_t) i];
            if (f0Fill == f0Frame) { runEstimator (p); }
        }
    }
    else
    {
        double ratio = 44100.0 / sampleRate;
        int outLen = (int) std::lround ((double) n * ratio);
        resampleCatmullRom (monoScratch.data(), n, ratio, resampleBuf, outLen);
        for (int i = 0; i < outLen; ++i)
        {
            f0Fifo[(size_t) f0Fill++] = resampleBuf[(size_t) i];
            if (f0Fill == f0Frame) { runEstimator (p); }
        }
    }

    // per-sample: pitch-mark clocks (+ Phase-Shift random walk) & vibration LFO
    lastLfo.assign ((size_t) n, 0.0);
    lastState.assign ((size_t) n, 0);
    lastNewMarks.clear();
    for (int i = 0; i < n; ++i)
    {
        clockA += currentF0 / sampleRate
                  + markJitter.next() * 1000.0 * (double) p.phaseShift / sampleRate;   // [-1,1) centered
        int state = 0;
        while (clockA >= 1.0)
        {
            clockA -= 1.0;
            state = voiced ? (stable ? 1 : 3) : 2;
        }
        if (clockA < 0.0) clockA = 0.0;   // heavy jitter guard

        vibration.step (sampleRate, currentF0, p.oscSpeed);   // step, then sample (A/B verified)
        static const double nrOverride = [] { const char* e = getenv ("VD_NR"); return e ? atof (e) : -1.0; }();
        double nrU = nrOverride >= 0.0 ? nrOverride : (double) p.oscNoiseRatio;
        double lfo = vibration.getSample (p.oscWaveform, (float) nrU);
        lastLfo[(size_t) i] = lfo;
        lastState[(size_t) i] = state;

        float samples[8] = { 0 };
        for (int c = 0; c < chs && c < 8; ++c) samples[c] = buffer.getSample (c, i);
        psola.pushSample (samples, chs, state, lfo);
        if (state != 0)
            lastNewMarks.push_back ((int32_t) (psola.getHistWrite() - 1));
    }

    // stage 1: PSOLA relocation + shimmer EQ pair (wet replaces dry, as in the original)
    static const int stageSel = []()
    {
        const char* e = getenv ("VD_STAGE");
        return e ? atoi (e) : 3;   // 1 = stage1 only, 2 = stage2 only, 3 = both
    }();
    if (stageSel & 1)
        psola.process (p, currentF0 > 0.0 ? currentF0 : 150.0, buffer);
    else if (stageSel == 2)
    {
        // stage-2-only mode: pass the dry input through untouched
    }

    // stage 2: spectral formant warp + NI-harmonics boost + AddGain -
    // in grain mode this runs per grain inside stage 1 (ref architecture); the fixed
    // STFT here is the legacy approximation (VD_GRAIN=0 restores it)
    if ((stageSel & 2) && ! spectral.grainSpectralOn())
        spectral.process (buffer, p, currentF0);

    // safety: replace any non-finite sample with silence
    for (int c = 0; c < chs; ++c)
    {
        auto* d = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (d[i])) d[i] = 0.0f;
    }

    // probe: per-block LFO lane dump (last n samples, f64) for ref comparison
    static FILE* laneF = getenv ("VD_LANE_DUMP") ? fopen (getenv ("VD_LANE_DUMP"), "wb") : nullptr;
    if (laneF)
    {
        for (int i = 0; i < n; ++i)
        {
            double v = (double) lastLfo[(size_t) i];
            fwrite (&v, 8, 1, laneF);
        }
        fflush (laneF);
    }
}

VoiceDriveDSP::StateSnapshot VoiceDriveDSP::getStateSnapshot (juce::uint32 n) const
{
    StateSnapshot s;
    s.numSamples = n;
    s.lfo = lastLfo;
    s.state = lastState;
    s.marks = lastNewMarks;
    s.f0 = currentF0;
    return s;
}

void VoiceDriveDSP::runEstimator (const Params& p)
{
    auto r = estimator.estimate (f0Fifo.data(), f0Frame, p.f0EstType,
                                 (double) p.f0EstRgeMin, (double) p.f0EstRgeMax);
    // The original's pitch clock reads the raw last entry of the F0-history vector
    // (processor+0x630 <- *(engine+152 vector end - 1)) with NO voicing gate:
    // unvoiced frames still drive the clock (observed 850-916 Hz on the quiet intro).
    if (std::isfinite (r.f0) && r.f0 > 0.0 && ! f0OverrideActive)
        currentF0 = r.f0;
    voiced = r.voiced;
    stable = r.stable;
    std::memmove (f0Fifo.data(), f0Fifo.data() + f0Hop, sizeof (double) * (size_t) (f0Frame - f0Hop));
    f0Fill = f0Frame - f0Hop;
}

} // namespace voicedrive

