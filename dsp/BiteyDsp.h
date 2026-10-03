// Bitey PA-600 — framework-free C++17 DSP core.
//
// Faithful port of Nathan's Gemini Studio "Bitey PA-600 Reverb Mixer"
// (bitey-pa-600-rev-1, AudioEngine.ts): a vintage Peavey PA-600-style
// powered-mixer emulator — 2 channel strips with preamp/EQ/sends,
// tape slap (7.5/15/30 ips), tube-driven spring reverb (synthesized IR,
// 1108-style return amp), and a master section (Scully 280 line amp,
// Pultec/Helios/Baxandall EQ, bus saturation, brickwall limiter).
//
// Notes on the port:
//  - Web Audio BiquadFilterNodes are RBJ cookbook filters; matched here.
//  - WaveShaper curves are ported sample-for-sample from the TS math.
//  - 4x-oversampled shapers: Web Audio's opaque OS latency is replaced by
//    an explicit 65-tap FIR 4x oversampler with EXACT integer latency
//    (16 samples @1x per stage); parallel paths are delay-aligned.
//  - The spring IR uses a seeded PRNG so it is deterministic (the web
//    version used Math.random()).
//  - The monitor bus has no meaning in a VST effect insert; monitor sends
//    and aux monitor controls are dropped. Everything else is kept.

#pragma once

#include <vector>
#include <complex>
#include <cstdint>
#include <cmath>
#include <thread>
#include <atomic>
#include <chrono>

namespace bitey {

// ---------------------------------------------------------------------------
// Small utilities
// ---------------------------------------------------------------------------

inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }

// Deterministic PRNG (splitmix32) for the spring IR generator.
struct SeededRng {
    uint32_t s;
    explicit SeededRng(uint32_t seed = 0xB17E7u) : s(seed) {}
    uint32_t next() {
        uint32_t z = (s += 0x9E3779B9u);
        z = (z ^ (z >> 16)) * 0x21f0aaadu;
        z = (z ^ (z >> 15)) * 0x735a2d97u;
        return z ^ (z >> 15);
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); } // [0,1)
    float bipolar() { return uniform() * 2.0f - 1.0f; }               // [-1,1)
};

// ---------------------------------------------------------------------------
// Biquad — RBJ cookbook, Direct Form I
// ---------------------------------------------------------------------------

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;

    void reset() { x1 = x2 = y1 = y2 = 0; }

    void setPeaking(double fs, double f, double q, double gainDb);
    void setLowShelf(double fs, double f, double s, double gainDb);
    void setHighShelf(double fs, double f, double s, double gainDb);
    void setLowpass(double fs, double f, double q);
    void setHighpass(double fs, double f, double q);

    float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// ---------------------------------------------------------------------------
// Curve — waveshaper lookup table (linear interpolation, hard-clamped)
// ---------------------------------------------------------------------------

struct Curve {
    std::vector<float> t; // maps [-1,1]

    float process(float x) const {
        float xc = x < -1.0f ? -1.0f : (x > 1.0f ? 1.0f : x);
        float pos = (xc * 0.5f + 0.5f) * float(t.size() - 1);
        int i = int(pos);
        if (i >= int(t.size()) - 1) return t.back();
        float frac = pos - float(i);
        return t[i] + frac * (t[i + 1] - t[i]);
    }

    template <typename Fn>
    static Curve make(int n, Fn fn) {
        Curve c; c.t.resize(n);
        for (int i = 0; i < n; ++i) {
            float x = (float(i) * 2.0f) / float(n) - 1.0f;
            c.t[i] = fn(x);
        }
        return c;
    }
};

// All curves ported from AudioEngine.ts (make*Curve).
struct Curves {
    Curve op6;      // channel preamp (RCA OP-6 style)
    Curve tape;     // tape saturation: tanh(1.5x)
    Curve bus;      // bus saturation: 0.98x + 0.02*tanh(0.1x)
    Curve driver;   // reverb driver tube (12AT7): tanh(0.8x)
    Curve limiter;  // brickwall: hard clip +-0.99
    Curve scully;   // Scully 280 line amp model
    Curve c1108;    // 1108/1176-style FET return amp
    Curve helios;   // Helios Type 69 mid: inductor + transformer
    Curve tape280;  // Scully 280 15IPS: tape hysteresis + saturation
    static Curves build();
};

// ---------------------------------------------------------------------------
// OversampledShaper — 4x zero-stuff + 193-tap Kaiser FIR.
// Exact latency: 192 samples at the base rate per instance, matching
// Chromium's measured 4x WaveShaperNode latency.
// ---------------------------------------------------------------------------

class OversampledShaper {
public:
    void prepare(double sampleRate);
    void setCurve(const Curve* c) { curve_ = c; }
    void reset();
    float process(float x);          // 192 samples latency
    static constexpr int latency() { return 192; }
private:
    static constexpr int kTaps = 193;
    // FIR group delay per filter: (193-1)/2 @4x = 24 @1x; two filters = 48.
    static constexpr int kPureDelay = 192 - 2 * ((kTaps - 1) / 8);
    std::vector<float> fir_;         // 193 taps
    std::vector<float> upBuf_, dnBuf_;
    std::vector<float> delayBuf_;    // pure delay padding to 192
    int upPos_ = 0, dnPos_ = 0, delayPos_ = 0;
    const Curve* curve_ = nullptr;
};

// ---------------------------------------------------------------------------
// FFT (radix-2, complex) + uniform partitioned convolver
// ---------------------------------------------------------------------------

class FFT {
public:
    void init(int n); // n = power of two
    void forward(std::vector<std::complex<float>>& d) const;
    void inverse(std::vector<std::complex<float>>& d) const;
    int size() const { return n_; }
private:
    int n_ = 0, logN_ = 0;
    std::vector<int> rev_;
    std::vector<std::complex<float>> tw_;
};

class PartitionedConvolver {
public:
    static constexpr int kPartition = 512; // ~11.6 ms latency @44.1k
    void prepare(double sampleRate);
    void setIR(const float* ir, int len); // copies + partitions
    void reset();
    float process(float x);
    int latency() const { return kPartition; }
private:
    FFT fft_;
    std::vector<std::vector<std::complex<float>>> h_; // partitions, freq domain
    std::vector<std::vector<std::complex<float>>> xHist_;
    std::vector<float> inBuf_, outBuf_;
    int inPos_ = 0, outPos_ = 0, histPos_ = 0, numParts_ = 0;
    std::vector<std::complex<float>> tmp_, acc_;
};

// ---------------------------------------------------------------------------
// Zero-latency hybrid convolver.
//
// Chromium's ConvolverNode measures 0 samples of latency. A uniform
// partitioned convolver inherently delays by one partition, so we split the
// IR: the first partition is convolved directly in the time domain (0
// latency) and the remainder runs through the partitioned FFT engine whose
// block delay is mathematically correct for IR[512..]. The sum is a true
// zero-latency convolution.
// ---------------------------------------------------------------------------

class ZeroLatencyConvolver {
public:
    static constexpr int kPartition = PartitionedConvolver::kPartition;
    void prepare(double sampleRate);
    void setIR(const float* ir, int len); // copies + partitions
    void reset();
    float process(float x);
    static constexpr int latency() { return 0; }
private:
    std::vector<float> headIR_;   // first kPartition taps
    std::vector<float> headBuf_;  // input history for the direct head
    int headPos_ = 0;
    PartitionedConvolver tail_;   // IR[kPartition..]
};

// ---------------------------------------------------------------------------
// Spring reverb IR — port of loadSpringReverb()
// ---------------------------------------------------------------------------

struct SpringIR {
    // Generates a stereo spring-tank-ish impulse. Deterministic.
    static void generate(float seconds, double sampleRate,
                         std::vector<float>& outL, std::vector<float>& outR,
                         uint32_t seed = 0xBA600u);
};

// ---------------------------------------------------------------------------
// Simple one-pole smoothed value (mirrors setTargetAtTime tau ~= 0.05)
// ---------------------------------------------------------------------------

struct Smoothed {
    float v = 0, target = 0, a = 0;
    void prepare(double sr, float init, float tau = 0.05f) {
        v = target = init;
        a = float(1.0 - std::exp(-1.0 / (sr * tau)));
    }
    void set(float t) { target = t; }
    void snap(float t) { v = target = t; }
    float next() { v += (target - v) * a; return v; }
};

// ---------------------------------------------------------------------------
// Channel strip — port of allocateChannels() channel graph
// ---------------------------------------------------------------------------

struct ChannelStrip {
    void prepare(double sampleRate, const Curves& curves);
    void reset();
    // Full per-sample path: input -> postGain output.
    float process(float x);
    // Parameter setters (call when UI changes)
    float level = 5.0f;      // 0..10
    int   pad = 0;           // 0:+0dB 1:+4dB 2:+10dB 3:-10dB
    bool  lowCut = false;    // 96 Hz HPF
    float lowDb = 0.0f;      // -15..+15
    float highDb = 0.0f;     // -15..+15
    float fxSend = 5.0f;     // 0..10
    float fxSendGain() const; // (fxSend/10)^2
    void touch() { dirty_ = true; }
private:
    void updateGains();
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    Biquad preEmp_, deEmp_, rumble_, lowCut_, bandwidth_, low_, high_;
    OversampledShaper shaper_;
    Smoothed preAmpGain_, postGain_;
    float padFactor_ = 1.0f;
    bool dirty_ = true;
};

// ---------------------------------------------------------------------------
// Tape slap — port of the tape network
// ---------------------------------------------------------------------------

struct TapeSlap {
    void prepare(double sampleRate, const Curves& curves);
    void reset();
    float process(float x); // includes wet gain; 0 when bypassed/mix=0
    int speed = 0;          // 0: 7.5ips, 1: 15ips, 2: 30ips
    int size = 0;           // 0: 1/4", 1: 1/2", 2: 1"
    float mix = 0.0f;       // 0..10
    bool bypass = false;
    float lastOut() const { return lastOut_; } // tap for reverb bleed (x0.12)
    void touch() { dirty_ = true; }
private:
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    std::vector<float> delay_;
    int writePos_ = 0;
    double phase_ = 0;
    Biquad lp_, body_, outLp_;
    OversampledShaper shaper_;
    Smoothed wet_, driveGain_, delayTime_, flutFreq_, flutDepth_;
    float lastOut_ = 0;
    bool dirty_ = true;
    void updateFromParams();
};

// ---------------------------------------------------------------------------
// Spring reverb — port of the reverb network.
//
// The spring IR is synthesized + partitioned OFF the audio thread: the audio
// thread only ever posts a lock-free regeneration request (atomics) and picks
// up the finished convolver at a block boundary. The browser original rebuilt
// its ConvolverNode on the main thread; this is the native equivalent.
//
// The tank is always stereo, exactly like the browser original (which builds
// a 2-channel impulse buffer and has no mono switch).

struct SpringReverb {
    SpringReverb() = default;
    ~SpringReverb();
    SpringReverb(const SpringReverb&) = delete;
    SpringReverb& operator=(const SpringReverb&) = delete;

    void prepare(double sampleRate, const Curves& curves);
    void reset();
    // Audio thread, once per block before process(): adopts a finished worker
    // IR and posts a regen request if the time knob moved (lock-free).
    void beginBlock();
    // Stereo in (already merged L/R sends) -> stereo out
    void process(float inL, float inR, float& outL, float& outR);
    float timeParam = 5.0f;    // 0..10 -> 0.5..4.5 s
    float drive = 5.0f;        // 0..10 dwell
    float contour = 5.0f;      // 0..10 -> tone 500..6000 Hz
    float returnGain = 5.0f;   // 0..10 master reverb return
    void touch() { dirty_ = true; }
private:
    void updateFromParams();
    void workerMain();
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    Biquad hp_[2];
    Smoothed drvGain_, toneFreq_, retGain_;
    OversampledShaper driver_[2];
    // Double-buffered convolvers: the worker builds the inactive slot.
    ZeroLatencyConvolver convL_[2], convR_[2];
    std::atomic<int> active_{ 0 };
    int cachedActive_ = 0; // audio thread's per-block view of active_
    Biquad tone_[2];
    Biquad a1108In_[2], a1108Out_[2];
    OversampledShaper a1108_[2];
    float makeup_ = 6.0f;
    float requestedTime_ = -1.0f; // audio thread's last regen request
    // Worker handoff (all atomics; no locks on the audio thread)
    std::thread worker_;
    std::atomic<bool> quit_{ false };
    std::atomic<bool> reqPending_{ false };
    std::atomic<float> reqTime_{ -1.0f };
    std::atomic<bool> swapReady_{ false };
    bool dirty_ = true;
};

// ---------------------------------------------------------------------------
// Master section — port of the master chain
// ---------------------------------------------------------------------------

struct MasterSection {
    void prepare(double sampleRate, const Curves& curves);
    void reset();
    void process(float inL, float inR, float& outL, float& outR);
    float mainLevel = 5.0f;  // 0..10 -> /5 gain
    float lowDb = 0.0f, midDb = 0.0f, highDb = 0.0f; // -15..+15
    int midFreq = 1;         // 0: 0.7k, 1: 1.0k, 2: 1.4k
    bool phaseInvert = false;
    float mainGain() const { return mainLevel / 5.0f; }
    void touch() { dirty_ = true; }
    // Pink-ish analog noise floor, ported from startNoiseFloor()
    float noiseTick();
private:
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    Biquad scullyIn_[2], scullyOut_[2];
    OversampledShaper scully_[2], bus_[2], limiter_[2];
    OversampledShaper helios_[2]; // Helios Type 69 mid inductor/transformer
    OversampledShaper tape280_[2]; // Scully 280 15IPS tape saturation
    Biquad low_[2], mid_[2], high_[2], subCut_[2];
    Biquad pultecDip_[2]; // Pultec-style low-end dip (below the 60Hz bump)
    Biquad tapeEQ_[2]; // 15IPS NAB EQ curve
    Smoothed busDriveGain_;
    SeededRng rng_{0x9015Eu};
    float b0_=0,b1_=0,b2_=0,b3_=0,b4_=0,b5_=0,b6_=0;
    bool dirty_ = true;
};

// ---------------------------------------------------------------------------
// Top level — Bitey PA-600
// ---------------------------------------------------------------------------

struct BiteyParams {
    struct Ch {
        float level = 5.0f;
        int pad = 0;          // 0:+0dB 1:+4dB 2:+10dB 3:-10dB
        bool lowCut = false;
        float lowDb = 0.0f, highDb = 0.0f;
        float fxSend = 5.0f;
    };
    Ch ch[2];
    int tapeSpeed = 0;        // 0:7.5 1:15 2:30
    int tapeSize = 0;         // 0:1/4" 1:1/2" 2:1"
    float tapeMix = 0.0f;     // 0..10
    float revTime = 5.0f, revDrive = 5.0f, revContour = 5.0f, revReturn = 5.0f;
    float mainLevel = 5.0f;
    float mLowDb = 0.0f, mMidDb = 0.0f, mHighDb = 0.0f;
    int midFreq = 1;          // 0:0.7k 1:1.0k 2:1.4k
    float mix = 10.0f;        // 0..10 global dry/wet
    bool phaseInvert = false;
    bool bypass = false;      // power switch
};

class BiteyDsp {
public:
    void prepare(double sampleRate);
    void reset();
    void setParams(const BiteyParams& p); // applies (smoothing inside)
    // Call once per audio block before process(): adopts worker-finished IRs
    void beginBlock() { reverb_.beginBlock(); }
    // In-place stereo processing.
    void process(float* left, float* right, int numSamples);
    // Total I/O latency in samples (for the host).
    int getLatencySamples() const;
    // Meter taps (not part of the sound): per-block RMS of the reverb bus
    // and the final output, scaled exactly like the browser prototype's
    // calculateRMS() so the VU meters behave identically.
    float getReverbMeter() const { return revMeter_; }
    float getMainMeter() const { return mainMeter_; }
private:
    double sr_ = 44100;
    Curves curves_;
    ChannelStrip ch_[2];
    TapeSlap tape_;
    SpringReverb reverb_;
    MasterSection master_;      // Wet path: dry+effects through Scully/EQ
    MasterSection masterDry_;   // Dry path: dry only through Scully/EQ
    Smoothed wetGain_, dryGain_;
    std::vector<float> dryDelayL_, dryDelayR_;
    int dryPos_ = 0;
    BiteyParams params_;
    float revMeter_ = 0.0f, mainMeter_ = 0.0f; // VU meter taps
};

} // namespace bitey
