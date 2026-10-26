#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <deque>
#include <random>
#include <vector>

namespace voicedrive
{

//==============================================================================
struct Params
{
    float effectLevel   = 1.0f;
    int   oscWaveform   = 2;        // 1=Sin 2=Square 3=Triangle 4=FullWave 5=Noise 6=composite
    float oscSpeed      = 0.5f;
    float oscNoiseRatio = 0.3f;
    float jitter        = 0.025f;
    float shimmer       = 0.4f;
    float shimmerFreq1  = 2000.0f;
    float shimmerFreq2  = 7000.0f;
    int   f0EstType     = 3;
    float f0EstRgeMin   = 40.0f;
    float f0EstRgeMax   = 1000.0f;
    float bstLevel      = 0.4f;
    int   bstAlgo       = 3;
    float bstRgeTopMin  = 4000.0f;
    float bstRgeTopMax  = 22050.0f;
    float bstRgeBtmMin  = 0.0f;
    float bstRgeBtmMax  = 22050.0f;
    float shiftLevel    = 1.0f;
    float shiftRgeMin   = 0.0f;      // ref preset default = 0 Hz
    float shiftRgeMax   = 10000.0f;
    int   shiftStyle    = 4;
    float shiftRate     = 1.0f;
    float shiftMixLevel = 1.0f;
    float addGain       = 0.0f;
    float lagOrder      = 0.11337868f;
    float phaseShift    = 1.0f;
};

//==============================================================================
// MT19937 canonical draw (same pattern as the original: seed 5489, reseed 10 on silence)
class RandomWave
{
public:
    void reset() { mt.seed (10u); }
    double next01() { return std::generate_canonical<double, 53> (mt); }   // mark-clock jitter (one-sided)
    double next() { return next01() * 2.0 - 1.0; }                          // waveform-5 style [-1,1)
    std::mt19937 mt { 5489u };
};

//==============================================================================
// Simplex: 1-D fBm gradient noise phase-locked to the oscillator (3 octaves, kernel t(1-t^2)^4)
class SimplexNoise
{
public:
    SimplexNoise();
    double getWave (double tRadians) const;

private:
    std::array<juce::uint8, 256> table {};
    double fBm (double x) const;
};

//==============================================================================
// VIBRATION oscillator bank — exact waveforms & F0-locked rate (docs/deep-dive-oscillator.md)
class VibrationBank
{
public:
    void step (double fs, double f0, float oscSpeed);           // phase1 += 2π·speed·F0/fs
    double getSample (int waveform, float noiseRatio);          // t = phase1 + 0.25
    void  resetNoise() { noise.reset(); }
    void  resetPhase() { phase1 = 0.0; }

private:
    double phase1 = 0.0, phase2 = 0.0;
    RandomWave noise;
    SimplexNoise simplex;
};

//==============================================================================
// RBJ peaking biquad, transposed DF-II (sub_180528A90 / sub_1805287C0)
struct PeakingEQ
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float s1 = 0, s2 = 0;

    void reset();
    // NOTE: "fs" here is the *normalisation reference* (Shimmer Freq1/2 in the original),
    // "f" the centre (current F0) — making the EQ pitch-tracking & rate independent.
    void design (double fsRef, double f, double q, double gainDb10);
    void process (float* data, int n);
};

//==============================================================================
// Homomorphic spectral separator: envelope = Re FFT(lifter × IFFT(mag))
// binomial lifter w[i] = w[i-1]·(A-i+1)/(A+i)
class SpectralSeparator
{
public:
    void prepare (int order);                                   // FFT order
    void separate (const double* mag, int numBins, int lifterWidth, double* env);
    void buildLifter (int width);   // binomial lifter at current width (cached)
    // dB cepstrum of a spectrum (quef = IFFT of 20*log10(max(eps, x^2))), full n reals
    void cepstrumDb (const double* x, int numBins, double* quef);
    // forward FFT of an n-sample real signal -> Re F[k] for k < numBins
    void forwardRe (const double* sig, int numBins, double* reF);

private:
    int fftOrder = 12;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> work;                                    // 2N float for juce FFT
    std::vector<double> lifter;
    int lifterWidth = -1;
};

//==============================================================================
// F0 estimator: homomorphic separation + SHS on 6-cent grid (docs/deep-dive-f0.md)
class F0Estimator
{
public:
    struct Result { double f0 = 0.0; bool voiced = false; bool stable = false; double shs = 0.0; };

    void prepare (int frameSize);
    // frame is at 44100 Hz
    Result estimate (const double* frame, int frameSize, int type, double fMin, double fMax);

    // debug/analysis: last frame's 6-cent grid spectrum and SHS array (spectrum oracle)
    std::vector<double> dbgGrid, dbgShs;

private:
    void buildGrid();

    int frame = 4096;
    int numGridBins = 0;
    std::vector<double> hamming;
    std::vector<double> logFlat, logEnv;
    std::vector<float> fftWork;                                 // interleaved for juce::dsp::FFT
    std::vector<double> mag, envWide, flat, flat2, gridVals;
    std::vector<double> envA, mag2, envB, envC, quef, reF;       // adaptive3 work buffers
    SpectralSeparator sepWide, sepNarrow;
    std::vector<double> shs;
    double prevF0 = 0.0, prevF02 = 0.0;
    bool hasPrev = false, hasPrev2 = false;
    std::deque<bool> voicedHist, stableHist;
    // grid origin: bin g <-> cents 1500+6g, cents(f)=1200*log2(f/440)+5700 (exact from sub_1805268C0)
    static constexpr double originHz = 440.0 * 0.08838834764831845;   // 440*2^-3.5 = 38.890873 Hz
    juce::dsp::FFT fft { 10 };                                        // frame = 1024
};

//==============================================================================
// PSOLA stage-1: synthetic pitch marks, jittered grain relocation, shimmer EQ pair
class PsolaStage
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    // per-sample state feed (state 1/2/3 = mark class at that sample, 0 = none)
    void pushSample (const float* audio, int numChannels, int state, double lfo);

    // process marks that became complete; writes delayed wet samples for the block
    void process (const Params& p, double f0, juce::AudioBuffer<float>& out);
    void setGrainSpectral (class SpectralStage* s) { grainSpec = s; }   // per-grain spectral (ref arch)
    void setExtraDelay (int d) { extraDelay = d; }

    int getOutputDelay() const { return outputDelay; }
    int64_t getHistWrite() const { return histWrite; }   // absolute sample counter

private:
    double fs = 44100.0;
    int numChans = 2;
    static constexpr int historyLen = 16384;   // per-channel audio history (absolute-time ring)
    static constexpr int outputDelay = 1536;  // wet emission delay (STFT legacy); grain mode adds 2048 (see R15)

    std::vector<std::vector<float>> history;
    class SpectralStage* grainSpec = nullptr;
    int extraDelay = 0;                            // grain-mode emission offset
    int64_t histWrite = 0;                        // absolute sample counter

    std::vector<int64_t> marks;                   // absolute positions of fired marks
    int64_t lastProcessedMark = -1;
    int64_t emittedUpTo = 0;

    // wet accumulator indexed absolutely over a sliding window
    std::vector<std::vector<float>> wetWindow;
    int64_t wetBase = 0;

    std::vector<float> grain;
    std::vector<double> lfoAtMark;
    PeakingEQ eqA, eqB;
};

//==============================================================================
// Stage-2 spectral: STFT → envelope → formant warp (mode 4) → harmonic boost → IFFT → AddGain
class SpectralStage
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;    // 2048 (STFT approximation; ref = per-grain 1024 FFT - see R15)
    static constexpr int hopSize  = fftSize / 4;      // 512

    void prepare (double sampleRate, int numChannels);
    void reset();
    void process (juce::AudioBuffer<float>& buffer, const Params& p, double f0);

    // ---- grain-driven spectral stage (ref architecture, R15) ----
    static constexpr int grainOrder = 10;
    static constexpr int grainN     = 1 << grainOrder;   // 1024 (env/flat = 513 bins)
    static constexpr int grainBins  = grainN / 2 + 1;    // 513
    void beginGrain();                                   // call once per placed grain (MT reset)
    void endGrainBlock();                                // flush the last-grain env probe
    void processGrain (float* data, int len, const Params& p, double f0, double levelMod = 1.0);   // in-place
    // adaptive 3-pass dual-output separation (sub_18051F4E0 family, ref stage-2 analyzer):
    // A envA=sep(mag,2500), flatA=mag/envA; B soft-expander cepstrum re-weight mag2
    // C env=sep(mag2,2500)·sep(mag2,2500)/sep(mag2,25000); flat=mag/env
    void separateAdaptive3 (const double* mag, int bins, double* env, double* flat);
    bool grainSpectralOn() const { return grainOn; }

private:
    bool grainOn = true;
    juce::dsp::FFT fftG { grainOrder };
    std::vector<float> gBuf, gWork;
    float gRoundTripInv = 1.0f;
    std::vector<double> gMag, gPhase, gEnv, gWarpScratch;
    SpectralSeparator sepG;                             // grain-size separator (1024)
    int grainChannel = 0;                               // channel index within the current grain
    bool sepReady = false;
    std::vector<double> lastEnv;                        // last-grain env probe (block-end flush)
    std::vector<double> lastFlat;                       // last-grain flat probe (block-end flush)
    double fs = 44100.0;
    int numChans = 2;

    juce::dsp::FFT fft { fftOrder };
    std::vector<double> window;
    std::vector<float> inFifo, outFifo;                // per channel
    std::vector<std::vector<float>> inFi, outFi;       // per-channel fifos
    std::vector<std::vector<float>> fftWork;           // per channel
    std::vector<double> mag, env, newEnv, phaseV;
    std::vector<double> boostCep, boostFlat, boostLifter, boostDbNew, boostDbRef;  // NI-Harmonics buffers
    std::vector<double> boostEnv;
    std::vector<double> frameNoise;                   // per-frame shared MT draws (L==R in ref)
    int boostDrawCount = 0;
    int flatHopCount = 0;                             // per-hop flat-track record index
    std::vector<float> boostCepBuf;                    // 2N float scratch for juce FFT
    SpectralSeparator sep;
    std::mt19937 boostNoise { 5489u };
    int64_t totalIn = 0;                                 // input sample counter (hop scheduler)

public:
    // grain mode wiring: the PSOLA grain loop calls these (VD_GRAIN=0 disables)
    void setGrainOn (bool on) { grainOn = on; }
    float roundTripInv = 1.0f;                           // 1/(forward*inverse scale), set in prepare()
};

//==============================================================================
inline void resampleCatmullRom (const double* src, int srcLen, double ratio,
                                std::vector<double>& dst, int dstLen)
{
    dst.resize ((size_t) dstLen);
    for (int i = 0; i < dstLen; ++i)
    {
        double x = (double) i / ratio;
        int i1 = (int) x;
        double t = x - i1;
        auto c = [&] (int idx) { return src[juce::jlimit (0, srcLen - 1, idx)]; };
        double a0 = -0.5 * c (i1 - 1) + 1.5 * c (i1) - 1.5 * c (i1 + 1) + 0.5 * c (i1 + 2);
        double a1 = c (i1 - 1) - 2.5 * c (i1) + 2.0 * c (i1 + 1) - 0.5 * c (i1 + 2);
        double a2 = -0.5 * c (i1 - 1) + 0.5 * c (i1 + 1);
        double a3 = c (i1);
        dst[(size_t) i] = a3 + t * (a2 + t * (a1 + t * a0));
    }
}

//==============================================================================
class VoiceDriveDSP
{
public:
    void prepare (double sampleRate, juce::uint32 maxBlock, juce::uint32 numChannels);
    void reset();
    // state snapshot for A/B subsystem comparison (see tools/sweep_ab / state diff)
    struct StateSnapshot
    {
        juce::uint32 numSamples = 0;
        std::vector<double> lfo;        // per-sample vibration LFO
        std::vector<int32_t> state;     // per-sample mark state (0 / 1 / 2 / 3)
        std::vector<int32_t> marks;     // absolute positions of NEW marks this block
        double f0 = 0.0;                // current F0 estimate at block end
    };
    StateSnapshot getStateSnapshot (juce::uint32 n) const;
    void overrideF0 (double f0) { currentF0 = f0; f0OverrideActive = true; }   // A/B probe (per block)

    void process (juce::AudioBuffer<float>& buffer, const Params& p);
    int  getLatencySamples() const;

private:
    std::vector<double> lastLfo;
    std::vector<int32_t> lastState;
    std::vector<int32_t> lastNewMarks;
    double f0OverrideNext = -1.0;
    bool f0OverrideActive = false;

    void runEstimator (const Params& p);
    double sampleRate = 44100.0;
    juce::uint32 channels = 2;

    // F0 front-end (resampled to 44100)
    static constexpr int f0Frame = 1024;   // original: [proc+544]=1024, 50% overlap
    static constexpr int f0Hop   = 512;    // original: hop = frame/2 (dword @ proc+552)
    std::vector<double> f0Fifo;
    int f0Fill = 0;
    F0Estimator estimator;
    double currentF0 = 0.0;
    bool voiced = false, stable = false;
    bool wasSilent = true;

    // pitch-mark clocks + jitter noise
    double clockA = 0.0;
    RandomWave markJitter;

    VibrationBank vibration;
    PsolaStage psola;
    SpectralStage spectral;

    std::vector<double> monoScratch;
    std::vector<double> resampleBuf;
};

} // namespace voicedrive

