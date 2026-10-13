// Bitey PA-200 — framework-free C++17 DSP core.
//
// Faithful port of Nathan's Gemini Studio "Bitey PA-200 Reverb Mixer"
// (bitey-pa200-rev-1, AudioEngine.ts): a vintage Peavey PA-600-style
// powered-mixer emulator — 2 channel strips with preamp/EQ/sends,
// tape slap (7.5/15/30 ips), tube-driven spring reverb (synthesized IR,
// 1108-style return amp), and a master section (Scully 280 line amp,
// Pultec/Helios/Baxandall EQ, bus saturation).
//
// TEST BUILD 0.3.2-TEST1 (2026-10-13, private — NOT the public
// release): the refinement-research round on top of 0.3.1, all
// proposals from bitey-refinement-research-2026-10-13.md except the
// three flagged not-recommended (TS-B, HL-B, CC-B — excluded).
// OP6-A LEVEL-knob feedback coupling, OP6-B slow bias time constant,
// SC-A Scully transformer flux (level-tracking LF corner), SC-B
// two-segment Scully composite (same calibrated gain/THD), SR-A
// dispersive chirp spring IRs, SR-B hard-hit IR set with driver-
// envelope crossfade, SR-C driver pre/de-emphasis pair, HL-A Helios
// detector tracks boost amount, TS-A three-component transport
// modulation, TS-C slap hysteresis-lite, CC-A 4x oversampling on the
// OP-6 cascade (latency unchanged at 75 samples). Every dynamic is
// smooth, deterministic, level-monotonic and transparent at rest,
// per Nathan's predictable-pro-gear directive.
//
// TEST BUILD 0.3.0-test (2026-10-13, private — NOT the public release):
// authenticity pass per the 2026-10-12 audit, DN1000-style. Nathan's
// explicit removals: the master Scully tape stage (tape280 + tape EQ)
// and the output limiter are GONE. The tape slap is untouched, and the
// Scully line amp is untouched. Added: bias memory on the OP-6 / 12AT7
// driver / Scully / Helios / 1108 stages, one shared power supply, EQ
// glide with decimated coefficient refresh, decorrelated gain-tracking
// hiss, per-channel component tolerances, DN1000 power semantics
// (300 ms crossfade to latency-matched dry, warm-up settle), and true
// rate-aware latency reporting. Plus the approved audit item 2.4 #3:
// the shaper tables (all but the tape slap's) are built over a +-4
// domain, so driven stages saturate asymptotically instead of
// flat-topping at the old +-1 table edge; inside +-1 the tables are
// the same functions at the same resolution. Plus the 2026-10-13
// model-research round (Nathan authorised, all at once): OP-6 split
// into a two-stage cascade with dynamic transformer darkening and an
// LF-weighted knee (OP6-1/2/3), Scully split-band LF-first saturation
// with bias-coupled fuzz replacing the sin ripple (SC-1/2), spring IR
// band-split decay + density ramp + HF-lead attack and driver/return
// rebalancing (SR-1..5), Helios mid-band-driven saturation with
// bounded boost compression and micro-detune (HL-1/2/3). Every new
// dynamic is smooth, deterministic, level-monotonic and transparent
// at rest, per Nathan's predictable-pro-gear directive.
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

// One-pole smoothing coefficient for a time constant in SECONDS at fs
// (DN1000 pattern: every time constant is derived from the sample rate).
inline float tauCoef(float tauSec, double fs) {
    return 1.0f - float(std::exp(-1.0 / (double(tauSec) * fs)));
}
// One-pole lowpass pole for a corner frequency in Hz at fs.
inline float poleForCorner(float fcHz, double fs) {
    return float(std::exp(-2.0 * 3.14159265358979323846 * double(fcHz) / fs));
}

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
// Curve — waveshaper lookup table (linear interpolation, clamped to
// the table domain). Most curves are built over +-4 (0.3.0-test,
// audit 2.4 #3); the tape slap's `tape` curve keeps the original
// +-1 domain and 8192-point table exactly as shipped.
// ---------------------------------------------------------------------------

struct Curve {
    std::vector<float> t; // maps [-domain, +domain]
    float domain = 1.0f;

    float process(float x) const {
        float xc = x < -domain ? -domain : (x > domain ? domain : x);
        float pos = (xc / (2.0f * domain) + 0.5f) * float(t.size() - 1);
        int i = int(pos);
        if (i >= int(t.size()) - 1) return t.back();
        float frac = pos - float(i);
        return t[i] + frac * (t[i + 1] - t[i]);
    }

    template <typename Fn>
    static Curve make(int n, Fn fn, float domain = 1.0f) {
        Curve c; c.domain = domain; c.t.resize(n);
        for (int i = 0; i < n; ++i) {
            float x = ((float(i) * 2.0f) / float(n) - 1.0f) * domain;
            c.t[i] = fn(x);
        }
        return c;
    }
};

// All curves ported from AudioEngine.ts (make*Curve).
struct Curves {
    Curve op6;      // channel preamp (RCA OP-6 style) — reference curve
    Curve op6a;     // OP6-1: input-pentode stage of the cascade
    Curve op6b;     // OP6-1: output-pentode/transformer stage;
                    // op6b(op6a(x)) == op6(x) by construction
    Curve tape;     // tape saturation: tanh(1.5x)
    Curve bus;      // bus saturation: 0.98x + 0.02*tanh(0.1x)
    Curve driver;   // reverb driver tube (12AT7): tanh(0.8x)
    Curve scully;   // Scully 280 line amp model
    Curve c1108;    // 1108/1176-style FET return amp
    Curve helios;   // Helios Type 69 mid: inductor + transformer
    // 0.3.0-test: the limiter and tape280 curves are gone with their
    // stages (Nathan's removal order, 2026-10-13). The tape slap's
    // `tape` curve above is NOT affected and stays as shipped.
    static Curves build();
};

// ---------------------------------------------------------------------------
// OversampledShaper — zero-stuff + Kaiser FIR oversampling (47 taps at
// 2x by default; the tape-slap instance is trimmed to 27 — see below).
// 0.3.2-TEST1 (CC-A): the factor is per-instance — the OP-6 cascade
// slots run 4x with a 61-tap FIR, which keeps the stage latency at
// exactly 25 samples ((61-1)/4 = 15 FIR samples + 10 pure delay), so
// the total reported latency is unchanged. Latency: 25 samples at the
// base rate per instance (measured) at either factor.
// ---------------------------------------------------------------------------

class OversampledShaper {
public:
    // First call designs the (rate-independent) FIR and allocates the fixed
    // delay buffers. Later calls only flip the bypass flag, so re-prepare is
    // safe while the audio thread is running: no allocation, no vector
    // mutation, just an atomic flag the render thread reads.
    void prepare(double sampleRate);
    void setCurve(const Curve* c) { curve_ = c; }
    void setTaps(int t) { taps_ = t; } // call before first prepare()
    // Oversampling factor (2 or 4). Call before first prepare().
    // (taps-1) must be divisible by the factor for the stage latency
    // to stay exactly 25 samples: 47@2x, 27@2x and 61@4x all qualify.
    void setOsFactor(int f) { osFactor_ = f; }
    // Run the curve at base rate (no oversampling, no latency). Used
    // for the Helios stage (research brief S5: the EQ-associated amp
    // is the designated CPU sacrifice; after HL-1 its saturation is
    // band-driven and mostly low-order). Set before prepare().
    bool forceBaseRate = false;
    void reset();
    float process(float x);          // 25 samples latency (0 if bypassed)
    static constexpr int latency() { return 25; }
    // At high sample rates (>=88.2kHz), the base rate already provides
    // adequate bandwidth; oversampling is bypassed to save CPU.
    bool isBypassed() const { return bypass_.load(); }
private:
    // FIR length is per-instance (default 47 taps = the shipped
    // filter). Nathan's DSP priority ladder (2026-10-13): the OP-6,
    // Scully, spring driver/1108 and Helios shapers keep full 47-tap
    // fidelity; only the tape-slap shaper is trimmed (47 -> 31) as a
    // CPU lever. The pure-delay padding compensates per instance, so
    // the per-stage latency is exactly 25 samples at either length
    // (see BiteyDsp::getLatencySamples()).
    static constexpr int kTaps = 47;
    void designFilter(); // one-time FIR design + buffer allocation
    int taps_ = kTaps;
    int osFactor_ = 2;
    int pureDelay_ = 24 - 2 * ((kTaps - 1) / 4);
    std::vector<float> fir_;         // taps_ taps
    std::vector<float> upBuf_, dnBuf_;
    std::vector<float> delayBuf_;    // pure delay padding
    int upPos_ = 0, dnPos_ = 0, delayPos_ = 0;
    const Curve* curve_ = nullptr;
    std::atomic<bool> bypass_{ false }; // true when sampleRate >= 88200
    // Written on first prepare, read on later prepares (which may run on a
    // different thread); atomic for a race-free handoff.
    std::atomic<bool> designed_{ false };
};

// ---------------------------------------------------------------------------
// Bias memory (DN1000 pattern, audit F1): tracks a stage's recent signal
// level. Loud signal shifts the stage's operating point; it recovers with
// an ~80 ms time constant. At rest (silence/recent silence) env == 0 and
// the stage is EXACTLY its shipped static curve.
// ---------------------------------------------------------------------------

struct BiasMemory {
    float env = 0.0f;
    float process(float x, double fs) {
        const float a = std::fabs(x);
        const float c = (a > env) ? tauCoef(0.005f, fs)   // fast attack
                                  : tauCoef(0.040f, fs);  // ~40 ms recovery
        // Recovery was ~80 ms through TEST2. TEST3 (dryness
        // addendum, Nathan 2026-10-13): shortened toward the fast,
        // hardware-plausible end. Measured with the burst probe
        // (200 ms @ 0 dBFS, then a -26 dBFS tone): the probe tone's
        // gain is back within ~1.5% of rest by 10-25 ms and its THD
        // reaches rest values by ~100-200 ms, with lower absolute
        // post-burst THD than TEST2 at every window; the residual
        // settle is shaped by the shared PSU rail, not this tau.
        // 40 ms stays far too slow to follow audio waveforms (no
        // AM artefacts); the character starts and stops with the
        // signal.
        env += (a - env) * c;
        return env;
    }
    void reset() { env = 0.0f; }
};

// An OversampledShaper wrapped with bias memory (+ optional PSU rail
// drive modulation). The stage's input drive deepens with its own recent
// level while the post-scale restores the curve's exact low-level gain,
// and a ~0.2 dB gain droop accrues when slammed (DN1000 transistorStage
// behaviour). With env == 0 and railDrive == 1 the input reaches the
// shaper bit-identically and the output scale is exactly 1: the rest
// state is the shipped curve, unchanged.
struct BiasedShaper {
    OversampledShaper shaper;
    BiasMemory bias;
    double sr = 44100;
    float railDrive = 1.0f; // PSU modulation (1.0 = rail at nominal)
    // Drive multiplier with exact low-level gain normalization (the
    // output is divided back by it). 1.0 = shipped behaviour. Used by
    // the Scully LF band (SC-1: constant 1.41) and the Helios stage
    // (HL-1: modulated by mid-band energy).
    float driveMul = 1.0f;
    // SC-2 (Scully): bias-coupled asymmetry. When quadCoef != 0, an
    // x^2 term of quadCoef * clamp01(env/quadRef) is added to the
    // shaper input, so the curve's asymmetry grows with sustained
    // level (fuzz emerges from bias shift, not a static ripple).
    // Exactly zero at rest.
    float quadCoef = 0.0f;
    float quadRef = 0.8f;
    // OP6-3 (OP-6 stage A): LF-weighted bias detector. A parallel
    // detector on a ~120 Hz lowpassed copy of the input, weighted
    // lfWeight, deepens the knee for bass ahead of treble (output
    // transformer flux behaviour). effEnv = max(fullEnv, w * lfEnv):
    // bounded, monotonic, deterministic.
    bool lfDetect = false;
    float lfWeight = 1.5f;
    // OP6R-5 (OP-6 stage B, the output stage): the complementary
    // behaviour — the choke-loaded output gives LF content MORE
    // swing, so the same LF detector instead WIDENS this stage's
    // knee for bass (drive divided by `widen`, restored after the
    // curve, so low-level gain is exact and rest state is exact).
    // Bounded (<= 1 + depth); at extremes the core thickening still
    // arrives because the widening saturates while drive grows.
    bool lfWiden = false;
    float lfWidenDepth = 0.35f;
    // Bias deepen depth (DN1000 pattern). Default 0.6; cascaded
    // stages use less per stage so the COMPOSITE level-history effect
    // stays in the same gentle family instead of compounding.
    float biasDepth = 0.6f;
    // OP6-B (0.3.2-TEST1): a second, slower bias time constant — the
    // phrase-level "glow" timescale of a real pentode stage (fast
    // attack ~50 ms, slow ~2 s release), depth capped at a fifth of
    // the fast envelope's contribution, feeding the same deepen path.
    // Enabled on OP-6 stage A only. Zero at rest, like the fast env.
    bool slowBias = false;
    float slowDepth = 0.2f;

    void prepare(double sampleRate) {
        sr = sampleRate;
        lfA_ = 1.0f - poleForCorner(120.0f, sampleRate);
        shaper.prepare(sampleRate);
    }
    void setCurve(const Curve* c) { shaper.setCurve(c); }
    void reset() { shaper.reset(); bias.reset(); lfSig_ = 0; lfEnv_ = 0; env_ = 0; slowEnv_ = 0; }
    bool isBypassed() const { return shaper.isBypassed(); }
    // The effective bias envelope from the most recent process() call
    // (0 at rest). Used by OP6-2's dynamic transformer darkening.
    float lastEnv() const { return env_; }
    float process(float x) {
        float e = bias.process(x, sr);
        float widen = 1.0f;
        if (lfDetect || lfWiden) {
            lfSig_ += (x - lfSig_) * lfA_;
            const float a = std::fabs(lfSig_);
            const float c = (a > lfEnv_) ? tauCoef(0.005f, sr)
                                         : tauCoef(0.080f, sr);
            lfEnv_ += (a - lfEnv_) * c;
            const float w = lfEnv_ * lfWeight;
            if (lfDetect && w > e) e = w;
            if (lfWiden) {
                float wn = w > 1.2f ? 1.2f : w;
                widen = 1.0f + lfWidenDepth * wn / 1.2f;
            }
        }
        env_ = e;
        float b = e > 1.2f ? 1.2f : e;
        // OP6-B: the slow envelope deepens the same path, bounded so
        // fast + slow together never exceed the fast env's own clamp.
        float bd = b;
        if (slowBias) {
            const float a = std::fabs(x);
            const float c = (a > slowEnv_) ? tauCoef(0.050f, sr)
                                           : tauCoef(2.0f, sr);
            slowEnv_ += (a - slowEnv_) * c;
            const float se = slowEnv_ > 1.2f ? 1.2f : slowEnv_;
            bd = b + slowDepth * se;
            if (bd > 1.2f) bd = 1.2f;
        }
        float deepen = 1.0f + biasDepth * bd;
        float in = x * driveMul;
        float v = in * deepen * railDrive / widen;
        if (quadCoef != 0.0f) {
            float en = e / quadRef; if (en > 1.0f) en = 1.0f;
            v += quadCoef * en * in * in;
        }
        float y = shaper.process(v);
        return y * (1.0f - 0.02f * b) * widen / (deepen * driveMul);
    }
private:
    float lfSig_ = 0, lfEnv_ = 0, lfA_ = 0, env_ = 0;
    float slowEnv_ = 0; // OP6-B slow bias envelope
};

// ---------------------------------------------------------------------------
// Power supply (DN1000 pattern, audit F4): ONE per unit, shared by the
// channels, the spring driver and the master. Sags under the combined
// load and modulates stage drive — the sections breathe together. No
// audio signal crosses between sections; only the rail moves. At rest
// (or under light load) the rail is 1.0 and nothing changes.
// ---------------------------------------------------------------------------

struct PowerSupply {
    float env_ = 0.0f;  // demand envelope (sum of |section outputs|)
    float rail_ = 1.0f; // normalized rail (1.0 = nominal)

    void update(float demand, double fs) {
        const float c = (demand > env_) ? tauCoef(0.005f, fs)   // fast attack
                                        : tauCoef(0.060f, fs);  // slow recovery
        env_ += (demand - env_) * c;
        const float d = env_ > 1.5f ? 1.5f : env_;
        const float target = 1.0f - d * 0.045f; // up to ~6.75% sag, slammed
        rail_ += (target - rail_) * tauCoef(0.030f, fs);
    }
    float getRail() const { return rail_; }
    void reset() { env_ = 0.0f; rail_ = 1.0f; }
};

// ---------------------------------------------------------------------------
// Pink noise generator (audit F3/F9): Paul Kellett-style pink filter, but
// every pole is derived from its corner frequency at prepare time, so the
// noise spectrum is the same at any sample rate (the original used fixed
// per-sample constants tuned at 44.1 kHz). Corners below reproduce the
// original constants exactly at 44.1 kHz. One instance per channel with
// independent seeds -> decorrelated L/R hiss.
// ---------------------------------------------------------------------------

struct PinkNoise {
    SeededRng rng{0x9015Eu};
    float b0_ = 0, b1_ = 0, b2_ = 0, b3_ = 0, b4_ = 0, b5_ = 0, b6_ = 0;
    float p0_ = 0, p1_ = 0, p2_ = 0, p3_ = 0, p4_ = 0, p5_ = 0;

    void prepare(double fs, uint32_t seed) {
        rng.s = seed;
        // Corners derived from the original Kellett poles at 44.1 kHz:
        // 0.99886 / 0.99332 / 0.96900 / 0.86650 / 0.55000, and the
        // alternating pole -0.7616 (1910 Hz below Nyquist).
        p0_ = poleForCorner(8.0f, fs);
        p1_ = poleForCorner(47.0f, fs);
        p2_ = poleForCorner(221.0f, fs);
        p3_ = poleForCorner(1004.0f, fs);
        p4_ = poleForCorner(4197.0f, fs);
        p5_ = -float(std::exp(-2.0 * 3.14159265358979323846 * 1910.0 / fs));
        reset();
    }
    void reset() { b0_ = b1_ = b2_ = b3_ = b4_ = b5_ = b6_ = 0; }
    // Raw pink sample at the original filter's output scaling (the
    // caller applies the level: the old code multiplied by 0.0004).
    float process() {
        float white = rng.bipolar();
        b0_ = p0_ * b0_ + white * 0.0555179f;
        b1_ = p1_ * b1_ + white * 0.0750759f;
        b2_ = p2_ * b2_ + white * 0.1538520f;
        b3_ = p3_ * b3_ + white * 0.3104856f;
        b4_ = p4_ * b4_ + white * 0.5329522f;
        b5_ = p5_ * b5_ - white * 0.0168980f;
        float n = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
        n *= 0.11f;
        b6_ = white * 0.115926f;
        return n;
    }
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
    static constexpr int kPartition = 512; // default partition (samples)
    void prepare(double sampleRate) { prepare(sampleRate, kPartition); }
    void prepare(double sampleRate, int partition);
    void setIR(const float* ir, int len); // copies + partitions
    void reset();
    float process(float x);
    int latency() const { return part_; } // one partition of block delay
private:
    FFT fft_;
    std::vector<std::vector<std::complex<float>>> h_; // partitions, freq domain
    std::vector<std::vector<std::complex<float>>> xHist_;
    std::vector<float> inBuf_, outBuf_;
    int inPos_ = 0, outPos_ = 0, histPos_ = 0, numParts_ = 0;
    int part_ = kPartition;
    std::vector<std::complex<float>> tmp_, acc_;
};

// ---------------------------------------------------------------------------
// Gardner non-uniform partitioned convolver (0.3.1 hotfix).
//
// Drop-in replacement for the uniform tail above. The uniform engine's
// cost is proportional to the IR length (every 128 samples it sweeps
// every partition), so long spring tails (TIME past ~3.3 s) overran the
// audio thread and crackled. Gardner's scheme keeps the convolution sum
// mathematically identical but splits the IR into sections whose
// partition size doubles along the tail (128, 256, 512, ... capped):
// each section is an independent uniform partitioned convolver fed by
// the input delayed by (base + segStart - B), which aligns its block
// latency with its segment. Per-sample cost is then ~log(IR length)
// instead of linear, so the tail cost is essentially flat vs TIME.
// Total latency is still exactly one base partition, like the uniform
// engine it replaces — only the float summation order changes.
// ---------------------------------------------------------------------------

class GardnerConvolver {
public:
    static constexpr int kMaxPart = 16384;   // largest partition size
    static constexpr int kPartsPerSize = 4;  // partitions per size class
    void prepare(double sampleRate, int basePartition);
    void setIR(const float* ir, int len); // copies + partitions
    void reset();
    float process(float x);
    int latency() const { return base_; } // one base partition of block delay
private:
    struct Section {
        FFT fft;
        std::vector<std::vector<std::complex<float>>> h;     // partitions, freq domain
        std::vector<std::vector<std::complex<float>>> xHist; // input spectra ring
        std::vector<float> outBuf;                           // 2*B overlap-add ring
        std::vector<std::complex<float>> tmp, acc;
        int B = 0, numParts = 0, delay = 0, fftSize = 0;
        int outPos = 0, histPos = 0;
    };
    std::vector<Section> sections_;
    std::vector<float> hist_; // shared input history ring (power of two)
    int histMask_ = 0;
    int base_ = 128;
    long t_ = 0; // absolute index of the current sample
};

// ---------------------------------------------------------------------------
// Zero-latency hybrid convolver.
//
// Chromium's ConvolverNode measures 0 samples of latency. A uniform
// partitioned convolver inherently delays by one partition, so we split the
// IR: the first partition is convolved directly in the time domain (0
// latency) and the remainder runs through the partitioned FFT engine whose
// block delay is mathematically correct for IR[kHead..]. The sum is a true
// zero-latency convolution.
//
// 0.3.0-test (audit F2): the direct head shrank 512 -> 128 taps and the
// tail's block delay equals the head length so the split aligns. The
// convolution sum is mathematically identical — only the float
// summation order changes.
// 0.3.1: the tail engine is Gardner non-uniform partitioning (see
// GardnerConvolver above) instead of uniform 128-sample partitions, so
// long tails no longer cost proportionally to their length.
// ---------------------------------------------------------------------------

class ZeroLatencyConvolver {
public:
    static constexpr int kPartition = PartitionedConvolver::kPartition;
    static constexpr int kHead = 128; // direct head taps == tail partition
    void prepare(double sampleRate);
    void setIR(const float* ir, int len); // copies + partitions
    void reset();
    float process(float x);
    static constexpr int latency() { return 0; }
private:
    std::vector<float> headIR_;   // first kHead taps
    std::vector<float> headBuf_;  // input history for the direct head
    int headPos_ = 0;
    GardnerConvolver tail_;       // IR[kHead..], Gardner non-uniform partitions
};

// ---------------------------------------------------------------------------
// Spring reverb IR — port of loadSpringReverb()
// ---------------------------------------------------------------------------

struct SpringIR {
    // Generates a stereo spring-tank-ish impulse. Deterministic.
    // 0.3.2-TEST1 (SR-A): each reflection burst is synthesized as a
    // short downward-frequency chirp (dispersive spring behaviour),
    // not a filtered noise burst; sweep width grows with reflection
    // order. Decay calibration (band lambdas, density, seeds) is
    // unchanged from the shipped generator.
    // SR-B: hard=true generates the "hard-hit" companion set — same
    // per-band decay times (the TIME knob's calibrated meaning is
    // fenced), but higher early density, wider chirp spread and a
    // brighter HF band, for the driver-envelope crossfade.
    static void generate(float seconds, double sampleRate,
                         std::vector<float>& outL, std::vector<float>& outR,
                         uint32_t seed = 0xBA600u, bool hard = false);
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
    // Current preamp drive target (for the hiss gain-staging readout).
    float driveTarget() const { return preAmpGain_.target; }
    // Shared-PSU rail (audit F4): modulates the OP-6 drive (stage A,
    // the cascade's drive entry). Written by BiteyDsp on the audio
    // thread; 1.0 = rail at nominal.
    void setRail(float r) { shaper_.railDrive = r; }
    // Per-channel component tolerances (audit F6, DN1000 pattern): fixed
    // offsets around the nominal values, set once at prepare. Ch 1 keeps
    // exactly 1.0 everywhere (the nominal unit).
    void setTolerances(float lowFreqMul, float highFreqMul, float postGainMul) {
        tolLowFreq_ = lowFreqMul; tolHighFreq_ = highFreqMul; tolPostGain_ = postGainMul;
    }
private:
    void updateGains();      // targets from params (called when touched)
    void writeEqCoeffs();    // biquads from the GLIDED values
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    Biquad preEmp_, deEmp_, inLp_, rumble_, lowCut_, bandwidth_, low_, high_;
    BiasedShaper shaper_;    // OP-6 stage A (input pentode) + bias memory
    BiasedShaper shaper2_;   // OP-6 stage B (output pentode/transformer)
    // OP6-2: the bandwidth_ lowpass corner is modulated by the OP-6
    // bias envelope (stage A) (24 kHz at rest -> ~14 kHz under
    // sustained heavy drive; swing narrowed by OP6R-4) — dynamic
    // transformer darkening.
    // bwWritten_ tracks the last written corner; coefficients
    // refresh every 16 samples and only while the corner is moving.
    float bwWritten_ = 24000.0f;
    int bwCtr_ = 0;
    // OP6-A (0.3.2-TEST1): the LEVEL knob also moves the cascade's
    // operating point, as on the hardware where the gain control sits
    // inside stage 1's feedback loop — past 5 the effective feedback
    // falls (bias deepen deepens; the darkening swing narrows), below
    // 5 the stage runs a touch cleaner. darkenSwing_ is the current
    // OP6-2 swing (10 kHz at Level <= 5). Small-signal gain at every
    // knob position is untouched (bias env is 0 at rest).
    float darkenSwing_ = 10000.0f;
    Smoothed preAmpGain_, postGain_;
    float padFactor_ = 1.0f;
    bool dirty_ = true;
    // EQ glide (audit F5): params glide with a 15 ms tau; coefficients
    // are refreshed on a decimated schedule (every 32 samples) and only
    // while a glided value is still moving.
    float lowDbT_ = 0.0f, highDbT_ = 0.0f;   // targets
    float lowDbS_ = 0.0f, highDbS_ = 0.0f;   // glided values
    float lowCutFreqT_ = 10.0f, lowCutFreqS_ = 10.0f; // 10 (off) <-> 96 Hz
    float glideA_ = 0.0f;                    // per-sample glide coef
    int refreshCtr_ = 0;
    bool eqSnap_ = true;                     // snap glide on (re)prepare
    // Tolerances (nominal 1.0)
    float tolLowFreq_ = 1.0f, tolHighFreq_ = 1.0f, tolPostGain_ = 1.0f;
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
    // TS-A (0.3.2-TEST1): three-component transport modulation. The
    // shipped wow sine stays; added are a band-limited noise flutter
    // (5-20 Hz, ~1/10 of wow's depth) and a scrape component
    // (25-120 Hz, ~3% of wow's depth, gated by signal level — tape
    // friction jitter exists only while signal moves against the
    // head). Seeded/deterministic, re-seeded on reset.
    SeededRng modRng_{0x51A9u};
    float fltLp20_ = 0, fltLp5_ = 0, scrLp120_ = 0, scrLp25_ = 0;
    float aFlt20_ = 0, aFlt5_ = 0, aScr120_ = 0, aScr25_ = 0;
    float gateEnv_ = 0;
    // TS-C (0.3.2-TEST1): hysteresis-lite on the slap saturation —
    // a one-state level-history integrator around the tape curve.
    // Charges fast under loud signal, discharges slowly (~45 ms), and
    // pulls the shaper input 30% toward its state; at steady state
    // state == input and the curve's static shape is exact.
    float hyst_ = 0;
};

// ---------------------------------------------------------------------------
// Spring reverb — port of the reverb network.
//
// The spring IR is synthesized + partitioned OFF the audio thread: the audio
// thread only ever posts a lock-free regeneration request (atomics) and picks
// up the finished convolver at a block boundary. The browser original rebuilt
// its ConvolverNode on the main thread; this is the native equivalent.
//
// THREAD SAFETY (this struct is shared by three threads):
//   - Audio thread:  beginBlock() + process().
//   - Worker thread: synthesizes + partitions IRs in the background.
//   - Caller thread: prepare()/reset()/param setters (e.g. the host's
//     prepareToPlay path, which may race the render thread).
// The two convolver slots form a strict state machine (IDLE -> BUILDING ->
// READY -> ACTIVE -> IDLE). The worker CAS-claims an IDLE slot and only ever
// touches the BUILDING slot it claimed. The audio thread only reads the
// ACTIVE slot in process() and performs all READY/ACTIVE/IDLE transitions in
// beginBlock() at a block boundary. No slot is ever touched by two threads,
// so setIR()'s vector reallocations can never race process()'s reads.
// prepare() on re-prepare never touches convolver (or filter) state directly:
// it posts atomics and the audio thread applies them in beginBlock().
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
    // Audio thread, once per block before process(): applies deferred
    // prepare()/reset() work, adopts a finished worker IR, and posts a regen
    // request if the time knob moved (lock-free, wait-free, no allocation).
    void beginBlock();
    // Stereo in (already merged L/R sends) -> stereo out
    void process(float inL, float inR, float& outL, float& outR);
    // Cross-thread parameters: written by the caller/UI thread, read by the
    // audio thread. Atomic so the handoff is race-free (and TSan-clean).
    std::atomic<float> timeParam{ 5.0f };    // 0..10 -> 0.5..4.5 s
    std::atomic<float> drive{ 5.0f };        // 0..10 dwell
    std::atomic<float> contour{ 5.0f };      // 0..10 -> tone 500..6000 Hz
    std::atomic<float> returnGain{ 5.0f };   // 0..10 master reverb return
    void touch() { dirty_ = true; }
    // Shared-PSU rail (audit F4): modulates the spring driver drive.
    // Written by BiteyDsp on the audio thread; 1.0 = rail at nominal.
    float railDrive = 1.0f;
private:
    // Slot lifecycle. Only the audio thread assigns ACTIVE; only the worker
    // assigns BUILDING (via CAS from IDLE) and READY.
    enum class SlotState : int { IDLE = 0, BUILDING = 1, READY = 2, ACTIVE = 3 };
    void updateFromParams();
    void workerMain();
    // (Re)configures biquads + smoothed params for a sample rate. Called
    // synchronously on first prepare; deferred to the audio thread
    // (via beginBlock) on re-prepare so filter coefficients are never
    // written while the render thread reads them.
    void configureFilters(double sampleRate);
    // Deferred reset body: runs on the audio thread inside beginBlock().
    // Only zeroes the ACTIVE convolver slot (the worker owns BUILDING slots;
    // a freshly built slot is already clean because setIR() resets it).
    void resetOnAudioThread();
    std::atomic<double> sr_{ 44100.0 };
    const Curves* curves_ = nullptr;
    Biquad hp_[2];
    Smoothed drvGain_, toneFreq_, retGain_;
    BiasedShaper driver_[2]; // 12AT7 driver with bias memory (audit F1)
    // Double-buffered convolvers with the state machine above.
    ZeroLatencyConvolver convL_[2], convR_[2];
    // SR-B (0.3.2-TEST1): the hard-hit IR set, built alongside the
    // soft set in the SAME slot (same lifecycle, same worker build),
    // crossfaded in process() by a slow envelope of the driver input.
    // At the bottom of the envelope the output is the soft path alone,
    // bit-identical to the shipped build.
    ZeroLatencyConvolver convHardL_[2], convHardR_[2];
    // Split lifecycle for the hard set: a slot goes READY on its
    // SOFT pair alone (adoption latency stays at the shipped
    // build's), and the worker completes the hard pair afterwards,
    // publishing hardReady_[slot] (release) when its setIR lands.
    // The audio thread snapshots that flag in beginBlock() and only
    // ever calls the hard convolvers under a true snapshot, so the
    // worker's late setIR can never race a process() call. Same
    // ownership discipline as the slot machine itself.
    std::atomic<bool> hardReady_[2];
    bool audioHardReady_ = false; // audio thread's beginBlock snapshot
    float hitEnv_ = 0.0f;  // driver-input envelope (audio thread)
    float hitMix_ = 0.0f;  // 0 = soft set only, 1 = hard set only
    // CPU gate for the hard pair: the convolvers only run while the
    // hit envelope is anywhere near the crossfade window (hysteresis
    // 0.016 on / 0.008 off, 2 ms send fade). Below the window the
    // crossfade is 0 by definition, so an idle hard pair changes
    // nothing audible; the first hit re-opens the gate in ~1 ms,
    // inside the IR's own attack ramps.
    bool hardGateOn_ = false;
    float hardGate_ = 0.0f;
    // SR-C (0.3.2-TEST1): fixed pre/de-emphasis pair around the 12AT7
    // driver (+5 dB HF shelf in, complementary -5 dB after the
    // convolver) so driver saturation is frequency-dependent while
    // the low-level response stays net flat by construction (RBJ
    // shelves with reciprocal gains are exact inverses).
    Biquad preEmph_[2], deEmph_[2];
    std::atomic<SlotState> slotState_[2];
    int activeSlot_ = 0; // audio thread's view; written only in beginBlock()
    Biquad tone_[2];
    Biquad a1108In_[2], a1108Out_[2];
    BiasedShaper a1108_[2]; // 1108 return amp with bias memory (audit F1)
    float makeup_ = 6.0f;
    // Contour glide wiring (audit F5): toneFreq_ is advanced per sample
    // in process(); the tone biquads are rewritten on a decimated
    // schedule while the glided frequency is still moving.
    float toneFreqWritten_ = -1.0f;
    int toneCtr_ = 0;
    float requestedTime_ = -1.0f; // audio thread's last regen request
    // Worker thread's record of the tank time it last built (or is
    // building). The worker also WATCHES timeParam itself (see
    // workerMain): with the 0.3.2 chirp IRs a build takes a few
    // hundred ms, so waiting for the audio thread to post the
    // request at the next block would start the build a render late.
    float lastBuiltTime_ = -1.0f;
    // Worker handoff (all atomics; no locks on the audio thread).
    // All use the default seq_cst ordering for clarity; none are hot-path.
    std::thread worker_;
    std::atomic<bool> quit_{ false };
    std::atomic<bool> reqPending_{ false };
    std::atomic<float> reqTime_{ -1.0f };
    std::atomic<double> reqSr_{ -1.0 };
    std::atomic<bool> resetPending_{ false };
    std::atomic<bool> reconfigPending_{ false };
    std::atomic<double> pendingSr_{ 44100.0 };
    bool dirty_ = true; // audio-thread only (set via touch() on the audio thread)
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
    // True when the Helios shaper is running at base rate (its stage
    // then contributes 0 latency). Read by getLatencySamples().
    bool heliosBaseRate() const { return helios_[0].isBypassed(); }
    // Analog noise floor: two decorrelated pink generators (audit F3).
    // `level` is the staging-scaled amplitude (BiteyDsp computes it from
    // the channel drive + MAIN); at the shipped defaults it reproduces
    // the original fixed hiss level.
    void noisePair(float level, float& nl, float& nr);
    // Shared-PSU rail (audit F4): modulates the Scully line-amp drive
    // (both split-band instances). Written by BiteyDsp on the audio
    // thread; 1.0 = rail at nominal.
    void setScullyRail(float r) {
        scully_[0].railDrive = r; scully_[1].railDrive = r;
        scullyLf_[0].railDrive = r; scullyLf_[1].railDrive = r;
    }
private:
    void updateTargets();    // param targets (called when touched)
    void writeEqCoeffs();    // biquads from the GLIDED values
    double sr_ = 44100;
    const Curves* curves_ = nullptr;
    Biquad scullyIn_[2], scullyOut_[2];
    BiasedShaper scully_[2];   // Scully 280 line amp + bias memory (F1)
    // SC-1: split-band Scully. A one-pole 150 Hz crossover feeds the
    // LF component through scullyLf_ at 1.41x drive (nickel output
    // transformer saturates LF first); the HF remainder uses scully_.
    // The split sums exactly and both stages share the 25-sample
    // latency, so the recombined response is flat at low level.
    BiasedShaper scullyLf_[2];
    float scLp_[2] = {0.0f, 0.0f};
    float scLpA_ = 0.0f;
    // SC-A (0.3.2-TEST1): output-transformer flux behaviour. A
    // one-pole high-pass in the Scully path whose corner tracks the
    // LF band's bias envelope (the existing LF flux proxy): resting
    // ~6 Hz, rising toward ~30 Hz only while LF flux is high. One
    // mechanism produces both the power-tracking LF slope reduction
    // and the associated phase rotation the transformer literature
    // describes — behaviour the amplitude-only model missed. At rest
    // (env == 0) the corner sits at 6 Hz: -0.06 dB at 50 Hz, inside
    // the staging tolerance the TEST3 round measured (-0.82 dB @ 50).
    float fluxLp_[2] = {0.0f, 0.0f};
    float fluxFc_[2] = {6.0f, 6.0f};
    // HL-1/2/3: mid-band energy detector (bandpass tap at the selected
    // mid frequency + bias-family envelope). Drives the Helios stage
    // depth (HL-1), bounded mid boost compression (HL-2) and a <=3%
    // mid detune (HL-3). Coefficients are stereo-shared, so the
    // compression/detune use the LINKED (max) envelope.
    Biquad midTap_[2];
    BiasMemory midEnv_[2];
    float midEnvNorm_ = 0.0f;  // linked, 0..1
    float midCompDb_ = 0.0f;   // current compression amount (dB, >= 0)
    float compWritten_ = -1.0f, detuneWritten_ = -1.0f;
    OversampledShaper bus_[2]; // kept (still bypassed in process, as shipped)
    BiasedShaper helios_[2];   // Helios Type 69 mid + bias memory (F1)
    // 0.3.0-test REMOVALS (Nathan, 2026-10-13): the tape280_ shapers,
    // the tapeEQ_ biquads and the limiter_ shapers are gone, along with
    // the base-rate +/-0.99 post-clamp in process(). The master chain
    // now ends at the tape-EQ position: Scully -> EQ -> Helios -> bus.
    Biquad low_[2], mid_[2], high_[2], subCut_[2];
    Biquad pultecDip_[2]; // Pultec-style low-end dip (below the 60Hz bump)
    Smoothed busDriveGain_;
    PinkNoise noise_[2]; // independent seeds -> decorrelated L/R
    bool dirty_ = true;
    // EQ glide (audit F5): 15 ms tau, decimated refresh (every 32
    // samples) only while a glided value is still moving.
    float lowDbT_ = 0.0f, midDbT_ = 0.0f, highDbT_ = 0.0f;
    float lowDbS_ = 0.0f, midDbS_ = 0.0f, highDbS_ = 0.0f;
    int midFreqT_ = 1;
    float glideA_ = 0.0f;
    int refreshCtr_ = 0;
    bool eqSnap_ = true;
    // L/R tolerance trim (audit F6): R runs +0.4% hot, like a real pair.
    float outTrim_[2] = {1.0f, 1.004f};
};

// ---------------------------------------------------------------------------
// Top level — Bitey PA-200
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
    MasterSection master_;      // Single master: dry + effects through Scully/EQ
    Smoothed wetGain_;
    PowerSupply psu_;           // one shared rail for the whole unit (F4)
    std::vector<float> dryDelayL_, dryDelayR_;
    int dryPos_ = 0;
    BiteyParams params_;
    float revMeter_ = 0.0f, mainMeter_ = 0.0f; // VU meter taps
    // Power semantics (audit F10, DN1000 build-98 pattern): powerGain_
    // crossfades (~300 ms) between the processed output and the dry
    // input delayed by exactly the reported latency. The DSP keeps
    // running while off (stays warm; the spring tail keeps decaying).
    float powerGain_ = 1.0f;
    bool powerSnap_ = true;     // snap the fade on (re)prepare
    // Warm-up settle (DN1000 item 8): a cold unit sits ~0.26 dB down
    // and settles over ~2.5 s after power-on; while off it cools slowly
    // toward a lukewarm state (0.25) instead of resetting.
    float warmth_ = 0.0f;
};

} // namespace bitey
