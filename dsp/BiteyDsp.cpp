// Bitey PA-200 DSP core implementation — port of AudioEngine.ts.
#include "BiteyDsp.h"
#include <algorithm>

namespace bitey {
namespace {
constexpr double kPi = 3.14159265358979323846;

inline double clampF(double f, double fs) {
    // Keep filter frequencies sane at any sample rate.
    if (f < 5.0) f = 5.0;
    double nyq = fs * 0.499;
    if (f > nyq) f = nyq;
    return f;
}
} // namespace

// ---------------------------------------------------------------------------
// Biquad (RBJ cookbook — matches Web Audio BiquadFilterNode)
// ---------------------------------------------------------------------------

void Biquad::setPeaking(double fs, double f, double q, double gainDb) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * clampF(f, fs) / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double b0 = 1 + alpha * A, b1 = -2 * cw, b2 = 1 - alpha * A;
    double a0 = 1 + alpha / A, a1 = -2 * cw, a2 = 1 - alpha / A;
    this->b0 = float(b0 / a0); this->b1 = float(b1 / a0); this->b2 = float(b2 / a0);
    this->a1 = float(a1 / a0); this->a2 = float(a2 / a0);
}

void Biquad::setLowShelf(double fs, double f, double s, double gainDb) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * clampF(f, fs) / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / 2 * std::sqrt((A + 1 / A) * (1 / s - 1) + 2);
    double b0 = A * ((A + 1) - (A - 1) * cw + 2 * std::sqrt(A) * alpha);
    double b1 = 2 * A * ((A - 1) - (A + 1) * cw);
    double b2 = A * ((A + 1) - (A - 1) * cw - 2 * std::sqrt(A) * alpha);
    double a0 = (A + 1) + (A - 1) * cw + 2 * std::sqrt(A) * alpha;
    double a1 = -2 * ((A - 1) + (A + 1) * cw);
    double a2 = (A + 1) + (A - 1) * cw - 2 * std::sqrt(A) * alpha;
    this->b0 = float(b0 / a0); this->b1 = float(b1 / a0); this->b2 = float(b2 / a0);
    this->a1 = float(a1 / a0); this->a2 = float(a2 / a0);
}

void Biquad::setHighShelf(double fs, double f, double s, double gainDb) {
    double A = std::pow(10.0, gainDb / 40.0);
    double w0 = 2 * kPi * clampF(f, fs) / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / 2 * std::sqrt((A + 1 / A) * (1 / s - 1) + 2);
    double b0 = A * ((A + 1) + (A - 1) * cw + 2 * std::sqrt(A) * alpha);
    double b1 = -2 * A * ((A - 1) + (A + 1) * cw);
    double b2 = A * ((A + 1) + (A - 1) * cw - 2 * std::sqrt(A) * alpha);
    double a0 = (A + 1) - (A - 1) * cw + 2 * std::sqrt(A) * alpha;
    double a1 = 2 * ((A - 1) - (A + 1) * cw);
    double a2 = (A + 1) - (A - 1) * cw - 2 * std::sqrt(A) * alpha;
    this->b0 = float(b0 / a0); this->b1 = float(b1 / a0); this->b2 = float(b2 / a0);
    this->a1 = float(a1 / a0); this->a2 = float(a2 / a0);
}

void Biquad::setLowpass(double fs, double f, double q) {
    double w0 = 2 * kPi * clampF(f, fs) / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double b0 = (1 - cw) / 2, b1 = 1 - cw, b2 = (1 - cw) / 2;
    double a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    this->b0 = float(b0 / a0); this->b1 = float(b1 / a0); this->b2 = float(b2 / a0);
    this->a1 = float(a1 / a0); this->a2 = float(a2 / a0);
}

void Biquad::setHighpass(double fs, double f, double q) {
    double w0 = 2 * kPi * clampF(f, fs) / fs;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2 * q);
    double b0 = (1 + cw) / 2, b1 = -(1 + cw), b2 = (1 + cw) / 2;
    double a0 = 1 + alpha, a1 = -2 * cw, a2 = 1 - alpha;
    this->b0 = float(b0 / a0); this->b1 = float(b1 / a0); this->b2 = float(b2 / a0);
    this->a1 = float(a1 / a0); this->a2 = float(a2 / a0);
}

// ---------------------------------------------------------------------------
// Curves — exact ports of the TS make*Curve functions
// ---------------------------------------------------------------------------

Curves Curves::build() {
    Curves c;
    // RCA OP-6 (OP6R round, 2026-10-13 — full research brief:
    // goals/bitey-pa-600-sellable-plugin/files/op6-research-2026-10-13.md).
    // The hardware: three single-ended 1620 pentode stages,
    // resistance-coupled, TWO feedback loops (a variable loop around
    // stage 1 tied to the gain control; a fixed loop around stages
    // 2-3), choke-loaded output stage, 88 dB max gain (RCA catalog,
    // 1948). Published anchors: <0.5% THD at normal output (+8 dBm),
    // <1% at maximum (+18 dBm) — distortion roughly doubles across
    // the top 10 dB, then the unit is done. (OP6R-7: the PA-600 drum
    // H2/H3 figures that used to head this block were the plugin's
    // voicing history, NOT an OP-6 measurement, and are retired as
    // this stage's target — no OP-6 spectrum exists in any source;
    // the recipe below is topology inference per the brief, anchored
    // on the published THD totals, for Nathan's ears to pass.)
    //
    // OP6R-1: the cascade is now statically REAL — three genuine
    // stage functions with staggered saturation onsets (stage 1
    // latest: it is the feedback-protected one; stages 2-3
    // progressively earlier), pre-composed into the two existing
    // oversampled shaper slots (A = stages 1 o 2, B = stage 3), so
    // the statically-real variant costs zero added CPU. This
    // supersedes the OP6-1 identity split, whose static composite
    // was the old single curve by construction.
    // OP6R-2: pentode harmonic recipe — per-sign rate asymmetry (a
    // level-independent 2nd-harmonic floor), a windowed quadratic
    // (H2 that grows gently with level, then stops) and a windowed
    // cubic (H3 present at working levels, self-limiting): mixed
    // H2+H3 at nominal instead of engineered H2 dominance.
    // OP6R-3 + OP6R-6 land here as ONE calibration together with
    // the TEST3 drive trim (x10) and post makeup (x4): composite
    // ~0.3-0.6% THD at the nominal operating point (Level 5,
    // -18 dBFS in), rising <= ~2.5x at +10 dB, soft ceiling above.
    // The composite small-signal gain is calibrated against the
    // previous curve (op6Prev, kept only as the gain reference), so
    // channel unity is preserved exactly.
    struct Op6Stage { float k, rp, rn, alpha, gamma, k0, s; };
    auto op6Core = [](float z, float k0, float s) {
        // Soft-knee saturator: exactly linear below k0, tanh tail
        // above (C1-continuous) — the long clean reach the feedback
        // fingerprint implies, with a soft ceiling, never a clamp.
        float az = std::fabs(z);
        if (az <= k0) return z;
        float t = k0 + std::tanh((az - k0) * s) / s;
        return z < 0 ? -t : t;
    };
    auto op6Stage = [op6Core](const Op6Stage& P) {
        return [P, op6Core](float x) {
            const float w = 1.3f, zc = 0.5f; // shared window widths
            float z = P.k * x;
            float arg = z + P.alpha * (z * z / (1.0f + (z / w) * (z / w)))
                          + P.gamma * (z * z * z / (1.0f + (z / zc) * (z / zc)));
            float rbar = 0.5f * (P.rp + P.rn);
            arg *= (arg >= 0 ? P.rp : P.rn) / rbar;
            return op6Core(arg, P.k0, P.s) / P.k;
        };
    };
    // Calibrated stage set (static-composite harness, 2026-10-13):
    // nominal THD 0.58%, H3 ~-9.6 dB rel H2, +10 dB ratio ~2.0x,
    // 0 dBFS-in THD 3.5% with a smooth bounded peak.
    const Op6Stage op6S1{0.510f, 1.000f, 1.014f, 0.0238f, 0.0217f, 0.80f, 1.20f};
    const Op6Stage op6S2{0.789f, 1.004f, 1.022f, 0.0486f, 0.1108f, 0.80f, 1.20f};
    const Op6Stage op6S3{1.070f, 1.008f, 1.039f, 0.0687f, 0.1852f, 0.85f, 1.05f};
    const float op6G1 = 2.7f, op6G2 = 2.15f; // interstage gains
    auto op6S1f = op6Stage(op6S1), op6S2f = op6Stage(op6S2), op6S3f = op6Stage(op6S3);
    auto op6Raw = [&](float u) { return op6S3f(op6G2 * op6S2f(op6G1 * op6S1f(u))); };
    auto op6Prev = [](float x) {
        float y = x < 0 ? std::tanh(x * 1.2f) : std::tanh(x * 1.05f);
        y += 0.30f * x * x;
        if (y > 0.5f) y = 0.5f + (y - 0.5f) * 0.92f;
        return y;
    };
    auto sineGain = [](auto&& f, float a) {
        const int N = 4096; double re = 0, im = 0;
        for (int i = 0; i < N; ++i) {
            double ph = 2 * kPi * i / N;
            double v = f(float(a * std::sin(ph)));
            re += v * std::cos(ph); im -= v * std::sin(ph);
        }
        return float(std::sqrt(re * re + im * im) * 2.0 / N / a);
    };
    const float op6Trim = sineGain(op6Prev, 0.006f) / sineGain(op6Raw, 0.006f);
    c.op6 = Curve::make(32768, [&](float u) { return op6Raw(u) * op6Trim; }, 4.0f);
    c.op6a = Curve::make(32768, [&](float u) { return op6S2f(op6G1 * op6S1f(u)); }, 4.0f);
    c.op6b = Curve::make(32768, [&](float z) { return op6S3f(op6G2 * z) * op6Trim; }, 4.0f);
    c.tape = Curve::make(8192, [](float x) { return std::tanh(x * 1.5f); });
    c.bus = Curve::make(32768, [](float x) {
        return x * 0.98f + std::tanh(x * 0.1f) * 0.02f;
    }, 4.0f);
    c.driver = Curve::make(32768, [](float x) {
        // 12AT7 reverb driver: gentle saturation with 2nd harmonic warmth.
        return std::tanh(x * 0.9f) + 0.20f * x * x;
    }, 4.0f);
    c.scully = Curve::make(32768, [](float x) {
        // Scully 280: GERMANIUM transistors (1960s), UTC/Freed nickel transformers.
        // "Warm, with a slight fuzzy bite when pushed just below or slightly
        // into clipping (then it gets angry in a way that's still useful)."
        // Germanium: lower forward voltage (~0.3V) = earlier, softer clipping
        // than silicon. The "fuzz" is higher harmonics emerging when pushed hard.
        // Three nickel transformers add clarity on top of the warmth.
        // TEST3 fatness bend: static x^2 asymmetry 0.04 -> 0.045, so
        // the germanium stage carries gentle 2nd-harmonic body at
        // working levels, not only when pushed. Origin slope is
        // unchanged (x^2 has none) — master unity is untouched.
        // (0.045, not more: the master runs at ~unity scale, and the
        // bend's H2 grows with the curve argument — the rebalance
        // measurements set this value.)
        float y = x + 0.045f * (x * x);
        // Germanium soft knee: starts earlier (0.5 vs 0.7), more gradual
        // SC-2 (research round): the old +0.02*sin(y*25) "fuzzy bite"
        // ripple is REMOVED — it was a static wiggle and an aliasing
        // source (audit F8). The fuzz now emerges from bias-coupled
        // asymmetry: BiasedShaper adds up to +0.03 to this x^2
        // coefficient under sustained level near the knee (0.045 -> 0.075),
        // exactly the level window users describe for germanium 280s.
        if (y > 0.5f) {
            float d = (y - 0.5f) * 1.5f;
            y = 0.5f + (y - 0.5f) / (1.0f + d * d * 0.4f);
        }
        if (y > 0.7f) {
            float d = (y - 0.7f) * 2.0f;
            y = 0.7f + (y - 0.7f) / (1.0f + d * d);
        }
        if (y < -0.7f) {
            float d = (y + 0.7f) * 2.0f;
            y = -0.7f + (y + 0.7f) / (1.0f + d * d);
        }
        y /= 0.85f;
        return std::max(-1.0f, std::min(1.0f, y));
    }, 4.0f);
    c.c1108 = Curve::make(32768, [](float x) {
        float y = x + 0.05f * (x * x);
        if (y > 0.8f) y = 0.8f + std::tanh((y - 0.8f) * 1.5f) * 0.2f;
        else if (y < -0.8f) y = -0.8f + std::tanh((y + 0.8f) * 1.5f) * 0.2f;
        return y;
    }, 4.0f);
    // Helios Type 69 mid: inductor + transformer character.
    // Inductor saturation: soft, with 2nd harmonic emphasis (single-ended).
    // Transformer: subtle 3rd harmonic, very gentle saturation.
    c.helios = Curve::make(32768, [](float x) {
        // Helios Type 69: Lustraphone transformer + inductor EQ.
        // "Mid-range growl", "fat", "assertive". The inductor saturates
        // aggressively when pushed — this is the "passive/aggressive" character:
        // bold EQ moves stay musical because the inductor rounds them.
        // Stronger 2nd harmonic than before, with 3rd emerging when driven hard.
        float y = x + 0.06f * x * x - 0.02f * x * x * x;
        // Inductor core saturation: more aggressive knee (the "growl")
        if (y > 0.5f) {
            float d = (y - 0.5f) * 2.2f;
            y = 0.5f + (y - 0.5f) / (1.0f + d * d * 0.7f);
        } else if (y < -0.5f) {
            float d = (y + 0.5f) * 2.2f;
            y = -0.5f + (y + 0.5f) / (1.0f + d * d * 0.7f);
        }
        return y;
    }, 4.0f);
    // 0.3.0-test: the limiter curve and the Scully 280 tape (tape280)
    // curve builds are removed with their stages (Nathan, 2026-10-13).
    return c;
}

// ---------------------------------------------------------------------------
// OversampledShaper — 2x zero-stuff + Kaiser-windowed-sinc FIR.
//
// Reduced from 4x/95-tap for CPU (was 92% of real-time, need <50%).
// 2x provides adequate aliasing suppression for the saturation curves.
// The resampling filter is nearly transparent (flat to ~20 kHz,
// gentle rolloff to Nyquist). 0.3.0-test: the pure delay line pads
// the per-stage total to exactly 25 samples at either FIR length
// (measured — see getLatencySamples()).
// ---------------------------------------------------------------------------

// Modified Bessel I0 (Kaiser window).
static double besselI0(double x) {
    double sum = 1.0, term = 1.0;
    const double x2 = x * x / 4.0;
    for (int k = 1; k < 40; ++k) {
        term *= x2 / (k * k);
        sum += term;
        if (term < 1e-16 * sum) break;
    }
    return sum;
}

void OversampledShaper::prepare(double sampleRate) {
    // At 88.2kHz and above, the base sample rate already provides enough
    // bandwidth that 2x oversampling gives diminishing returns. Bypass the
    // oversampling to save CPU (critical for Luna at 96kHz).
    //
    // The FIR design and buffer sizes are rate-independent, so they are
    // built once. Re-prepare only flips the atomic bypass flag: no
    // allocation and no vector mutation, safe while audio is running.
    bypass_.store(forceBaseRate || sampleRate >= 88200.0);
    bool expected = false;
    if (designed_.compare_exchange_strong(expected, true)) {
        designFilter();
    }
}

void OversampledShaper::designFilter() {
    const int N = taps_;
    pureDelay_ = 24 - 2 * ((N - 1) / 4);
    fir_.assign(N, 0);
    // Cutoff at the 2x Nyquist: gentle lowpass, flat through the audio band.
    const double fc = 0.25; // cycles/sample at 2x rate
    const double beta = 8.0;
    const double i0beta = besselI0(beta);
    const int M = N - 1;
    double sum = 0;
    for (int n = 0; n < N; ++n) {
        double m = n - M / 2.0;
        double sinc = (m == 0) ? 2 * kPi * fc : std::sin(2 * kPi * fc * m) / m;
        double r = (2.0 * n / M) - 1.0;
        double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0beta;
        fir_[n] = float(sinc * w);
        sum += fir_[n];
    }
    for (auto& v : fir_) v = float(v / sum * 2.0); // x2 compensates zero-stuff
    upBuf_.assign(N, 0);
    dnBuf_.assign(N, 0);
    delayBuf_.assign(pureDelay_ + 1, 0);
    reset();
}

void OversampledShaper::reset() {
    std::fill(upBuf_.begin(), upBuf_.end(), 0);
    std::fill(dnBuf_.begin(), dnBuf_.end(), 0);
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0);
    upPos_ = dnPos_ = delayPos_ = 0;
}

float OversampledShaper::process(float x) {
    // Bypass mode (high sample rates): just apply the curve directly.
    // No oversampling, no latency.
    if (bypass_.load()) {
        return curve_ ? curve_->process(x) : x;
    }
    const int N = int(fir_.size());
    float out2[2];
    for (int k = 0; k < 2; ++k) {
        float in = (k == 0) ? x : 0.0f; // zero-stuff
        upBuf_[upPos_] = in;
        float acc = 0;
        int idx = upPos_;
        for (int n = 0; n < N; ++n) {
            acc += fir_[n] * upBuf_[idx];
            if (--idx < 0) idx += N;
        }
        if (++upPos_ >= N) upPos_ = 0;
        out2[k] = curve_ ? curve_->process(acc) : acc;
    }
    // Decimate: filter each shaped sample; take phase k=0.
    float y = 0;
    for (int k = 0; k < 2; ++k) {
        dnBuf_[dnPos_] = out2[k];
        float acc = 0;
        int idx = dnPos_;
        for (int n = 0; n < N; ++n) {
            acc += fir_[n] * dnBuf_[idx];
            if (--idx < 0) idx += N;
        }
        if (++dnPos_ >= N) dnPos_ = 0;
        if (k == 0) y = acc * 0.5f; // x0.5 compensates the x2 up-filter gain
    }
    // Pure delay line: the two FIRs contribute 2*12 = 24 samples; pad to 24
    // total so transients land consistently.
    delayBuf_[delayPos_] = y;
    int rp = delayPos_ + 1;
    if (rp >= int(delayBuf_.size())) rp = 0;
    float out = delayBuf_[rp];
    if (++delayPos_ >= int(delayBuf_.size())) delayPos_ = 0;
    return out;
}

// ---------------------------------------------------------------------------
// FFT
// ---------------------------------------------------------------------------

void FFT::init(int n) {
    n_ = n; logN_ = 0;
    for (int t = n; t > 1; t >>= 1) ++logN_;
    rev_.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < logN_; ++b)
            if (i & (1 << b)) r |= 1 << (logN_ - 1 - b);
        rev_[i] = r;
    }
    tw_.assign(n / 2, {});
    for (int i = 0; i < n / 2; ++i) {
        double a = -2 * kPi * i / n;
        tw_[i] = {float(std::cos(a)), float(std::sin(a))};
    }
}

void FFT::forward(std::vector<std::complex<float>>& d) const {
    int n = n_;
    for (int i = 0; i < n; ++i)
        if (rev_[i] > i) std::swap(d[i], d[rev_[i]]);
    for (int len = 2; len <= n; len <<= 1) {
        int half = len / 2, step = n / len;
        for (int i = 0; i < n; i += len)
            for (int j = 0; j < half; ++j) {
                auto u = d[i + j];
                auto v = d[i + j + half] * tw_[j * step];
                d[i + j] = u + v;
                d[i + j + half] = u - v;
            }
    }
}

void FFT::inverse(std::vector<std::complex<float>>& d) const {
    for (auto& v : d) v = std::conj(v);
    forward(d);
    float inv = 1.0f / n_;
    for (auto& v : d) v = std::conj(v) * inv;
}

// ---------------------------------------------------------------------------
// ZeroLatencyConvolver — direct head + partitioned tail
// ---------------------------------------------------------------------------

void ZeroLatencyConvolver::prepare(double sampleRate) {
    headIR_.assign(kHead, 0.0f);
    headBuf_.assign(kHead, 0.0f);
    headPos_ = 0;
    tail_.prepare(sampleRate, kHead);
    reset();
}

void ZeroLatencyConvolver::setIR(const float* ir, int len) {
    const int headLen = std::min(len, kHead);
    headIR_.assign(kHead, 0.0f);
    for (int i = 0; i < headLen; ++i) headIR_[i] = ir[i];
    // Tail covers IR[kHead..]; its block delay (one tail partition ==
    // kHead) is exactly correct for these later taps (see header note).
    if (len > kHead)
        tail_.setIR(ir + kHead, len - kHead);
    else
        tail_.setIR(headIR_.data(), 0); // empty tail
    reset();
}

void ZeroLatencyConvolver::reset() {
    std::fill(headBuf_.begin(), headBuf_.end(), 0.0f);
    headPos_ = 0;
    tail_.reset();
}

float ZeroLatencyConvolver::process(float x) {
    // Direct-form head (0 latency).
    headBuf_[headPos_] = x;
    float yHead = 0.0f;
    int idx = headPos_;
    for (int i = 0; i < kHead; ++i) {
        yHead += headIR_[i] * headBuf_[idx];
        if (--idx < 0) idx += kHead;
    }
    if (++headPos_ >= kHead) headPos_ = 0;
    // Partitioned tail (its block delay aligns with IR[kHead..]).
    float yTail = tail_.process(x);
    return yHead + yTail;
}

// ---------------------------------------------------------------------------
// PartitionedConvolver — uniform partitioned overlap-add
// ---------------------------------------------------------------------------

void PartitionedConvolver::prepare(double sampleRate, int partition) {
    (void)sampleRate;
    part_ = partition;
    fft_.init(part_ * 2);
    tmp_.assign(fft_.size(), {});
    acc_.assign(fft_.size(), {});
    inBuf_.assign(part_, 0);
    outBuf_.assign(part_ * 2, 0);
    reset();
}

void PartitionedConvolver::setIR(const float* ir, int len) {
    numParts_ = (len + part_ - 1) / part_;
    h_.assign(numParts_, std::vector<std::complex<float>>(fft_.size()));
    for (int p = 0; p < numParts_; ++p) {
        auto& H = h_[p];
        std::fill(H.begin(), H.end(), std::complex<float>(0, 0));
        int off = p * part_;
        int cnt = std::min(part_, len - off);
        for (int i = 0; i < cnt; ++i) H[i] = ir[off + i];
        fft_.forward(H);
    }
    xHist_.assign(numParts_, std::vector<std::complex<float>>(fft_.size(), {0, 0}));
    reset();
}

void PartitionedConvolver::reset() {
    std::fill(inBuf_.begin(), inBuf_.end(), 0);
    std::fill(outBuf_.begin(), outBuf_.end(), 0);
    for (auto& h : xHist_) std::fill(h.begin(), h.end(), std::complex<float>(0, 0));
    inPos_ = outPos_ = histPos_ = 0;
}

float PartitionedConvolver::process(float x) {
    inBuf_[inPos_++] = x;
    float y = outBuf_[outPos_];
    outBuf_[outPos_] = 0; // consumed; tail writes accumulate below
    if (++outPos_ >= int(outBuf_.size())) outPos_ = 0;

    if (inPos_ >= part_) {
        inPos_ = 0;
        // Defensive: setIR() with an empty IR leaves numParts_ == 0 and the
        // history empty; skip the block FFT rather than indexing xHist_[0].
        if (numParts_ > 0) {
        // FFT of input block
        for (int i = 0; i < part_; ++i) tmp_[i] = inBuf_[i];
        for (int i = part_; i < fft_.size(); ++i) tmp_[i] = 0;
        fft_.forward(tmp_);
        xHist_[histPos_] = tmp_;
        // Sum over partitions
        std::fill(acc_.begin(), acc_.end(), std::complex<float>(0, 0));
        for (int p = 0; p < numParts_; ++p) {
            int hb = histPos_ - p;
            if (hb < 0) hb += numParts_;
            const auto& X = xHist_[hb];
            const auto& H = h_[p];
            for (int i = 0; i < fft_.size(); ++i) acc_[i] += X[i] * H[i];
        }
        if (++histPos_ >= numParts_) histPos_ = 0;
        fft_.inverse(acc_);
        // Overlap-add into outBuf (length 2*P)
        for (int i = 0; i < fft_.size(); ++i) {
            int idx = outPos_ + i;
            if (idx >= int(outBuf_.size())) idx -= int(outBuf_.size());
            outBuf_[idx] += acc_[i].real();
        }
        }
    }
    return y;
}

// ---------------------------------------------------------------------------
// GardnerConvolver — non-uniform partitioned overlap-add (0.3.1).
//
// Sections tile the IR from segStart = 0 with partition sizes base,
// 2*base, 4*base, ... (capped at kMaxPart; the capped size takes all
// remaining partitions). A section covering IR[seg .. seg+P*B) with
// partition size B is fed the input delayed by D = base + seg - B, so
// its intrinsic one-partition block delay lands its first tap exactly
// at base + seg — the same alignment the uniform engine produced.
// All sections read their input blocks from one shared history ring;
// each fires on its own real-time cadence (every B samples).
// ---------------------------------------------------------------------------

void GardnerConvolver::prepare(double sampleRate, int basePartition) {
    (void)sampleRate;
    base_ = basePartition;
    sections_.clear();
    hist_.assign(1024, 0.0f);
    histMask_ = 1023;
    t_ = 0;
}

void GardnerConvolver::setIR(const float* ir, int len) {
    sections_.clear();
    if (len > 0) {
        int seg = 0, B = base_;
        while (seg < len) {
            const int remain = len - seg;
            const int partsHere = (B >= kMaxPart)
                ? (remain + B - 1) / B
                : std::min(kPartsPerSize, (remain + B - 1) / B);
            Section s;
            s.B = B;
            s.numParts = partsHere;
            s.delay = base_ + seg - B;
            s.fftSize = B * 2;
            s.fft.init(s.fftSize);
            s.h.assign(partsHere, std::vector<std::complex<float>>(s.fftSize));
            for (int p = 0; p < partsHere; ++p) {
                auto& H = s.h[p];
                std::fill(H.begin(), H.end(), std::complex<float>(0, 0));
                const int off = seg + p * B;
                const int cnt = std::min(B, len - off);
                for (int i = 0; i < cnt; ++i) H[i] = ir[off + i];
                s.fft.forward(H);
            }
            s.xHist.assign(partsHere,
                           std::vector<std::complex<float>>(s.fftSize, {0, 0}));
            s.outBuf.assign(B * 2, 0.0f);
            s.tmp.assign(s.fftSize, {});
            s.acc.assign(s.fftSize, {});
            sections_.push_back(std::move(s));
            seg += partsHere * B;
            B = std::min(B * 2, kMaxPart);
        }
    }
    // The shared history ring must reach back D + B <= base + len from
    // the current sample (see the alignment note above); size with
    // generous margin, power of two for mask indexing.
    const long need = long(len) + base_ + kMaxPart + 64;
    int sz = 1024;
    while (sz < need) sz <<= 1;
    hist_.assign(sz, 0.0f);
    histMask_ = sz - 1;
    reset();
}

void GardnerConvolver::reset() {
    std::fill(hist_.begin(), hist_.end(), 0.0f);
    for (auto& s : sections_) {
        std::fill(s.outBuf.begin(), s.outBuf.end(), 0.0f);
        for (auto& xh : s.xHist)
            std::fill(xh.begin(), xh.end(), std::complex<float>(0, 0));
        s.outPos = 0;
        s.histPos = 0;
    }
    t_ = 0;
}

float GardnerConvolver::process(float x) {
    if (hist_.empty()) return 0.0f;
    hist_[size_t(t_ & histMask_)] = x;
    float y = 0.0f;
    for (auto& s : sections_) {
        // Consume this sample's output (written by earlier firings).
        y += s.outBuf[s.outPos];
        s.outBuf[s.outPos] = 0.0f;
        if (++s.outPos >= int(s.outBuf.size())) s.outPos = 0;
        // A delayed-stream block completes on the real-time cadence,
        // exactly as in the uniform engine.
        if (((t_ + 1) % s.B) == 0) {
            // Block = delayed-stream samples [t_+1-B, t_+1), i.e.
            // original input times shifted back by the section delay.
            const long t0 = t_ + 1 - s.B - s.delay;
            for (int i = 0; i < s.B; ++i) {
                const long tt = t0 + i;
                s.tmp[i] = (tt >= 0) ? hist_[size_t(tt & histMask_)] : 0.0f;
            }
            for (int i = s.B; i < s.fftSize; ++i) s.tmp[i] = 0;
            s.fft.forward(s.tmp);
            s.xHist[s.histPos] = s.tmp;
            std::fill(s.acc.begin(), s.acc.end(), std::complex<float>(0, 0));
            for (int p = 0; p < s.numParts; ++p) {
                int hb = s.histPos - p;
                if (hb < 0) hb += s.numParts;
                const auto& X = s.xHist[hb];
                const auto& H = s.h[p];
                for (int i = 0; i < s.fftSize; ++i) s.acc[i] += X[i] * H[i];
            }
            if (++s.histPos >= s.numParts) s.histPos = 0;
            s.fft.inverse(s.acc);
            for (int i = 0; i < s.fftSize; ++i) {
                int idx = s.outPos + i;
                if (idx >= int(s.outBuf.size())) idx -= int(s.outBuf.size());
                s.outBuf[idx] += s.acc[i].real();
            }
        }
    }
    ++t_;
    return y;
}

// ---------------------------------------------------------------------------
// SpringIR — port of loadSpringReverb()
// ---------------------------------------------------------------------------

void SpringIR::generate(float seconds, double sampleRate,
                        std::vector<float>& outL, std::vector<float>& outR,
                        uint32_t seed) {
    int length = int(sampleRate * seconds);
    outL.assign(length, 0);
    outR.assign(length, 0);
    const double flutterRate = 3.5, flutterDepth = 0.02, density = 2200.0;
    // SR-1 (research round): frequency-dependent decay like every real
    // spring (AKG published exactly this shape for the BX 10): the
    // impulse train is split into three bands by one-pole crossovers
    // (~250 Hz / ~2.5 kHz) and each band decays at its own rate —
    // LF lambda x0.75 (rings ~33% longer), mid as before, HF x1.5
    // (dies ~33% sooner). IR-side only: zero runtime CPU, architecture
    // and per-channel decorrelation (seeds) untouched.
    const double lambda = 4.0 - seconds * 0.4;
    // Design RT60s at knob 5 (lambda = 3.0): LF 3.07 s / mid 2.30 s /
    // HF 1.54 s — the BX published shape. (A single-number RT60
    // measurement of the finished IR reads HF longer, ~2.2 s, because
    // the generator's own soft clip regenerates HF following the mid
    // envelope while the IR is hot; the band's own decay is as
    // designed and the tail's top audibly dies first.)
    const double lamLf = lambda * 0.75, lamHf = lambda * 1.5;
    const float a250 = 1.0f - poleForCorner(250.0f, sampleRate);
    const float a2500 = 1.0f - poleForCorner(2500.0f, sampleRate);
    // SR-3: a whisper of the spring's HF-leads-LF dispersion — band
    // attack ramps (raised cosine): HF immediate, mid +6 ms, LF +12 ms.
    auto attack = [](double t, double t0, double t1) {
        if (t <= t0) return 0.0f;
        if (t >= t1) return 1.0f;
        double u = (t - t0) / (t1 - t0);
        return float(0.5 - 0.5 * std::cos(kPi * u));
    };
    for (int c = 0; c < 2; ++c) {
        SeededRng rng(seed + c * 0x9E37u);
        std::vector<float>& data = (c == 0) ? outL : outR;
        float lastNoise = 0, lastVal = 0;
        float lp250 = 0, lp2500 = 0; // band-split states (SR-1)
        for (int i = 0; i < length; ++i) {
            double t = double(i) / sampleRate;
            float noise = 0;
            // SR-2: density RAMPS 45% -> 100% over the first 80 ms (a
            // spring arriving), replacing the old front-loaded x2
            // density burst in the first 50 ms.
            const double ramp = t < 0.08 ? 0.45 + 0.55 * (t / 0.08) : 1.0;
            if (rng.uniform() < float(density * ramp / sampleRate))
                noise = rng.bipolar();
            noise = (noise + lastNoise * 0.8f) / 1.8f;
            lastNoise = noise;
            float flutter = 1.0f + float(flutterDepth *
                std::sin(2 * kPi * flutterRate * t + c * kPi * 0.5));
            lp250 += (noise - lp250) * a250;
            lp2500 += (noise - lp2500) * a2500;
            const float bandLf = lp250;
            const float bandMid = lp2500 - lp250;
            const float bandHf = noise - lp2500;
            float signal = bandLf * float(std::exp(-t * lamLf)) * attack(t, 0.012, 0.024)
                         + bandMid * float(std::exp(-t * lambda)) * attack(t, 0.006, 0.018)
                         + bandHf * float(std::exp(-t * lamHf));
            signal *= flutter;
            signal = (signal + lastVal * 0.9f) / 1.9f;
            lastVal = signal;
            if (signal > 0.8f) signal = 0.8f + (signal - 0.8f) * 0.5f;
            if (signal < -0.8f) signal = -0.8f + (signal + 0.8f) * 0.5f;
            data[i] = signal * 1.5f;
        }
    }
}

// ---------------------------------------------------------------------------
// ChannelStrip
// ---------------------------------------------------------------------------

void ChannelStrip::prepare(double sampleRate, const Curves& curves) {
    sr_ = sampleRate;
    curves_ = &curves;
    preEmp_.setLowShelf(sr_, 100, 1.0, 0.5);
    deEmp_.setLowShelf(sr_, 100, 1.0, -0.5);
    // OP6R-4: a gentle static lowpass in the INPUT-transformer
    // position, ahead of stage A — extreme HF is band-limited
    // before any waveshaping can multiply it (era program band;
    // the hardware's spike-smoothing is per-sample saturation plus
    // this, not a post-hoc tone control). Corner ~18 kHz,
    // Butterworth Q: measured -0.06 dB at 12 kHz (rest response
    // preserved) with real band-limiting at 19 kHz and above.
    inLp_.setLowpass(sr_, 18000, 0.707);
    rumble_.setHighpass(sr_, 20, 0.5);
    lowCut_.setHighpass(sr_, 10, 0.5);
    bandwidth_.setLowpass(sr_, 24000, 0.5);
    // Neve 1073-style: low shelving at 110Hz, high shelving at 10kHz.
    // TEST3 rebalance (Nathan, 2026-10-13): the LOW shelf slope
    // steepens (S 0.5 -> 0.9) so a +10 boost actually lands in the
    // audible lows; the HIGH shelf keeps its gentle shipped slope
    // (S 0.5) and only its corner moves down, 12 -> 10 kHz.
    low_.setLowShelf(sr_, 110, 0.9, 0);
    high_.setHighShelf(sr_, 10000, 0.5, 0);
    shaper_.prepare(sr_);
    shaper_.setCurve(&curves_->op6a); // OP6R-1 slot A = pentode stages 1 o 2
    shaper_.lfDetect = true;          // OP6-3 LF-weighted knee
    shaper2_.prepare(sr_);
    shaper2_.setCurve(&curves_->op6b); // OP6R-1 slot B = pentode stage 3 (output)
    // Stage B carries NO bias memory of its own: the OP-6's single
    // bias memory lives on stage A (with the OP6-3 LF detector), so
    // the cascade's level-history depth stays exactly the scope-1
    // build's, and OP6-2's darkening is driven from stage A's env.
    shaper2_.biasDepth = 0.0f;
    // OP6R-5: stage B gets the choke's LF headroom — its LF detector
    // widens (not deepens) the knee for bass.
    shaper2_.lfWiden = true;
    bwWritten_ = 24000.0f;
    bwCtr_ = 0;
    preAmpGain_.prepare(sr_, 1.0f);
    postGain_.prepare(sr_, 4.0f); // TEST3 rebalance (was 2.0)
    glideA_ = tauCoef(0.015f, sr_); // 15 ms EQ glide (DN1000 item 5)
    eqSnap_ = true;
    dirty_ = true;
    reset();
}

void ChannelStrip::reset() {
    preEmp_.reset(); deEmp_.reset(); inLp_.reset(); rumble_.reset(); lowCut_.reset();
    bandwidth_.reset(); low_.reset(); high_.reset();
    shaper_.reset();
    shaper2_.reset();
}

// Param targets (audit F5): gains go to their Smoothed targets as
// before; EQ values become glide TARGETS — coefficients are no longer
// rewritten in one step at a block boundary.
void ChannelStrip::updateGains() {
    static const float padFactors[4] = {1.0f, 1.585f, 3.162f, 0.316f};
    padFactor_ = padFactors[pad < 0 ? 0 : (pad > 3 ? 3 : pad)];
    float nl = level / 5.0f; // 0..2
    // TEST3 drive-law rebalance (Nathan, 2026-10-13): the OP-6 drive
    // halves (x20 -> x10) and the post gain doubles (x2 -> x4), so
    // small-signal gain at every Level is EXACTLY as before (unity at
    // Level 5 preserved bit-for-bit in the linear region) while the
    // cascade runs one notch cooler at defaults and saturates
    // progressively instead of hitting a wall. Level 10 still drives
    // the cascade past 1.3 at 0 dBFS in — wild, but smooth (the +-4
    // tables have the room).
    float drive = std::pow(nl, 2.4f) * 10.0f;
    preAmpGain_.set(drive * padFactor_);
    postGain_.set(4.0f * tolPostGain_);
    lowDbT_ = lowDb;
    highDbT_ = highDb;
    lowCutFreqT_ = lowCut ? 96.0f : 10.0f;
    if (eqSnap_) { // (re)prepare: start exactly on the targets
        lowDbS_ = lowDbT_; highDbS_ = highDbT_; lowCutFreqS_ = lowCutFreqT_;
        writeEqCoeffs();
        eqSnap_ = false;
    }
    dirty_ = false;
}

void ChannelStrip::writeEqCoeffs() {
    // Frequencies are the nominal EQ points, scaled only by the fixed
    // per-channel tolerance offsets (audit F6).
    low_.setLowShelf(sr_, 110.0 * tolLowFreq_, 0.9, lowDbS_);
    high_.setHighShelf(sr_, 10000.0 * tolHighFreq_, 0.5, highDbS_);
    lowCut_.setHighpass(sr_, lowCutFreqS_, 0.5);
}

float ChannelStrip::fxSendGain() const {
    float s = fxSend / 10.0f;
    return s * s;
}

float ChannelStrip::process(float x) {
    if (dirty_) updateGains();
    // EQ glide: advance the glided values per sample; refresh the
    // biquad coefficients every 32 samples, only while still moving.
    lowDbS_ += (lowDbT_ - lowDbS_) * glideA_;
    highDbS_ += (highDbT_ - highDbS_) * glideA_;
    lowCutFreqS_ += (lowCutFreqT_ - lowCutFreqS_) * glideA_;
    if (++refreshCtr_ >= 32) {
        refreshCtr_ = 0;
        if (std::fabs(lowDbT_ - lowDbS_) > 1e-4f ||
            std::fabs(highDbT_ - highDbS_) > 1e-4f ||
            std::fabs(lowCutFreqT_ - lowCutFreqS_) > 0.01f)
            writeEqCoeffs();
    }
    float y = preEmp_.process(x);
    y = inLp_.process(y); // OP6R-4 input-transformer band-limit
    y *= preAmpGain_.next();
    y = shaper_.process(y);   // OP-6 stage A (input pentode)
    y = shaper2_.process(y);  // OP-6 stage B (output pentode/transformer)
    y = deEmp_.process(y);
    y = rumble_.process(y);
    y = lowCut_.process(y);
    // OP6-2: dynamic transformer darkening — the bandwidth lowpass
    // corner follows the OP-6 bias envelope (stage A): exactly 24 kHz
    // at rest (the shipped static filter, bit-identical coefficients)
    // down to ~14 kHz under sustained heavy drive (OP6R-4 narrowed
    // the swing from 24->9 kHz: the hardware's darkening is a driven
    // seasoning, not a 15 kHz tone swing). Decimated coefficient
    // refresh (every 16 samples), only while moving.
    {
        float en = shaper_.lastEnv() / 1.2f; if (en > 1.0f) en = 1.0f;
        const float corner = 24000.0f - 10000.0f * en;
        if (++bwCtr_ >= 16) {
            bwCtr_ = 0;
            if (std::fabs(corner - bwWritten_) > 25.0f) {
                bwWritten_ = corner;
                bandwidth_.setLowpass(sr_, corner, 0.5);
            }
        }
    }
    y = bandwidth_.process(y);
    y = low_.process(y);
    y = high_.process(y);
    y *= postGain_.next();
    return y;
}

// ---------------------------------------------------------------------------
// TapeSlap
// ---------------------------------------------------------------------------

void TapeSlap::prepare(double sampleRate, const Curves& curves) {
    sr_ = sampleRate;
    curves_ = &curves;
    delay_.assign(int(sr_ * 0.5) + 8, 0);
    lp_.setLowpass(sr_, 1000, 0.5);
    body_.setPeaking(sr_, 350, 0.8, 4.0);
    outLp_.setLowpass(sr_, 1100, 0.7);
    // 0.3.0-test: tape-slap shaper FIR trimmed 47 -> 31 taps as a CPU
    // lever (authorised by Nathan's DSP priority ladder — the slap is
    // last to be protected; every other shaper keeps 47 taps). The
    // slap's model, curve and voicing are otherwise byte-untouched,
    // and the per-stage delay stays exactly 25 samples.
    shaper_.setTaps(31);
    shaper_.prepare(sr_);
    shaper_.setCurve(&curves_->tape);
    wet_.prepare(sr_, 0.0f);
    driveGain_.prepare(sr_, 1.5f);
    delayTime_.prepare(sr_, 0.134f);
    flutFreq_.prepare(sr_, 0.8f);
    flutDepth_.prepare(sr_, 0.0004f);
    dirty_ = true;
    reset();
}

void TapeSlap::reset() {
    std::fill(delay_.begin(), delay_.end(), 0);
    writePos_ = 0; phase_ = 0; lastOut_ = 0;
    lp_.reset(); body_.reset(); outLp_.reset();
    shaper_.reset();
}

void TapeSlap::updateFromParams() {
    static const float dTimes[3] = {0.134f, 0.085f, 0.042f};
    static const float fFreqs[3] = {0.8f, 1.2f, 2.5f};
    static const float fDepths[3] = {0.0004f, 0.0002f, 0.0001f};
    static const float drives[3] = {1.5f, 1.1f, 0.8f};
    static const float lpFreqs[3] = {1000.0f, 2200.0f, 4000.0f};
    int sp = speed < 0 ? 0 : (speed > 2 ? 2 : speed);
    int sz = size < 0 ? 0 : (size > 2 ? 2 : size);
    delayTime_.set(dTimes[sp]);
    flutFreq_.set(fFreqs[sp]);
    flutDepth_.set(fDepths[sp]);
    driveGain_.set(drives[sz]);
    lp_.setLowpass(sr_, lpFreqs[sz], 0.5);
    outLp_.setLowpass(sr_, 1100, 0.7);
    float w = bypass ? 0.0f : (mix / 10.0f) * 0.8f;
    wet_.set(w);
    dirty_ = false;
}

float TapeSlap::process(float x) {
    if (dirty_) updateFromParams();
    float w = wet_.next();
    // Write input (tapeSum gain 0.5 lives at the top level)
    delay_[writePos_] = x;
    // Modulated read
    double dt = delayTime_.next();
    double ff = flutFreq_.next();
    double fd = flutDepth_.next();
    phase_ += 2 * kPi * ff / sr_;
    if (phase_ > 2 * kPi) phase_ -= 2 * kPi;
    double readPos = double(writePos_) - (dt + std::sin(phase_) * fd) * sr_;
    int N = int(delay_.size());
    while (readPos < 0) readPos += N;
    int i0 = int(readPos) % N;
    int i1 = (i0 + 1) % N;
    float frac = float(readPos - std::floor(readPos));
    float d = delay_[i0] + frac * (delay_[i1] - delay_[i0]);
    if (++writePos_ >= N) writePos_ = 0;

    float y = d * driveGain_.next();
    y = shaper_.process(y);
    y = lp_.process(y);
    y = body_.process(y);
    // feedback = 0 (true slap, single repeat) — path kept for fidelity
    y *= w;
    y = outLp_.process(y);
    lastOut_ = y;
    return y;
}

// ---------------------------------------------------------------------------
// SpringReverb
// ---------------------------------------------------------------------------

SpringReverb::~SpringReverb() {
    quit_.store(true);
    if (worker_.joinable()) worker_.join();
}

void SpringReverb::configureFilters(double sampleRate) {
    // No allocation here: biquad coefficient writes + smoothed setup only.
    // Runs synchronously on first prepare; on re-prepare it is deferred to
    // the audio thread (via beginBlock) so coefficients are never written
    // while the render thread reads them.
    for (int c = 0; c < 2; ++c) {
        hp_[c].setHighpass(sampleRate, 180, 0.5);
        tone_[c].setLowpass(sampleRate, 3250, 0.4);
        a1108In_[c].setHighpass(sampleRate, 50, 0.5);
        a1108Out_[c].setLowpass(sampleRate, 24000, 0.5);
    }
    drvGain_.prepare(sampleRate, 0.175f);
    toneFreq_.prepare(sampleRate, 3250.0f);
    retGain_.prepare(sampleRate, 0.7f);
    toneFreqWritten_ = 3250.0f; // matches the tone_ writes above
    toneCtr_ = 0;
    dirty_ = true;
}

void SpringReverb::prepare(double sampleRate, const Curves& curves) {
    curves_ = &curves; // prepare-thread only; never dereferenced on audio
    // Shaper (re)prepare is allocation-free after the first call: it only
    // flips the atomic bypass flag, so this is safe while audio is running.
    // Curves are deterministic, so setCurve() is only needed once.
    driver_[0].prepare(sampleRate);
    driver_[1].prepare(sampleRate);
    a1108_[0].prepare(sampleRate);
    a1108_[1].prepare(sampleRate);

    const float t0 = 0.5f + (timeParam.load() / 10.0f) * 4.0f;
    const bool firstPrepare = !worker_.joinable();
    if (firstPrepare) {
        // No audio thread exists yet: do everything synchronously.
        configureFilters(sampleRate);
        sr_.store(sampleRate);
        driver_[0].setCurve(&curves_->driver);
        driver_[1].setCurve(&curves_->driver);
        a1108_[0].setCurve(&curves_->c1108);
        a1108_[1].setCurve(&curves_->c1108);
        for (int s = 0; s < 2; ++s) {
            convL_[s].prepare(sampleRate);
            convR_[s].prepare(sampleRate);
            slotState_[s].store(SlotState::IDLE);
        }
        // Build the initial IR synchronously into slot 0 and activate it.
        // setIR() ends with reset(), so the slot starts with clean state.
        // The IR is deterministic (seeded PRNG), same as the old path.
        std::vector<float> irL, irR;
        SpringIR::generate(t0, sampleRate, irL, irR);
        convL_[0].setIR(irL.data(), int(irL.size()));
        convR_[0].setIR(irR.data(), int(irR.size()));
        slotState_[0].store(SlotState::ACTIVE);
        activeSlot_ = 0;
        requestedTime_ = t0;
        quit_.store(false);
        worker_ = std::thread(&SpringReverb::workerMain, this);
    } else {
        // Audio may be running: NEVER touch convolver slots, biquad
        // coefficients, or smoothed state here. Post atomics only:
        //  - the audio thread applies filter reconfiguration + reset in
        //    beginBlock();
        //  - the worker rebuilds an IDLE slot at the new rate and the audio
        //    thread adopts it at a block boundary.
        // The request is intentionally left pending until the worker can
        // CAS-claim a slot (it is NOT cancelled).
        pendingSr_.store(sampleRate);
        reconfigPending_.store(true);
        reqTime_.store(t0);
        reqSr_.store(sampleRate);
        reqPending_.store(true);
        reset(); // deferred: only sets resetPending_
    }
}

void SpringReverb::workerMain() {
    // Background synthesis + partitioning of the spring IR.
    //
    // The worker CAS-claims an IDLE slot (IDLE -> BUILDING) and only ever
    // touches that slot. The audio thread adopts finished slots at block
    // boundaries (READY -> ACTIVE, old ACTIVE -> IDLE) and only reads the
    // ACTIVE slot in process(). If no IDLE slot is free (the audio thread
    // hasn't adopted the last build yet), the request stays pending and we
    // retry: we never rebuild the slot the audio thread is reading. This
    // closes the race that crashed LUNA, where the worker's setIR()
    // reallocated the live slot's vectors while the render thread was
    // iterating them in PartitionedConvolver::process().
    while (!quit_.load()) {
        if (reqPending_.load()) {
            int slot = -1;
            for (int s = 0; s < 2; ++s) {
                SlotState expected = SlotState::IDLE;
                if (slotState_[s].compare_exchange_strong(expected, SlotState::BUILDING)) {
                    slot = s;
                    break;
                }
            }
            if (slot < 0) {
                // Audio hasn't adopted the previous build yet; keep the
                // request pending and retry shortly.
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            // A build is definitely starting: consume the request, then read
            // its parameters. A newer post racing this sequence either lands
            // before the clear (we build the newest values) or re-arms the
            // flag (we build again next iteration); the built time/rate never
            // goes stale.
            reqPending_.store(false);
            const float t = reqTime_.load();
            const double buildSr = reqSr_.load();
            std::vector<float> irL, irR;
            SpringIR::generate(t, buildSr, irL, irR);
            if (quit_.load()) {
                // Teardown raced the build: release the slot, publish nothing.
                slotState_[slot].store(SlotState::IDLE);
                break;
            }
            convL_[slot].setIR(irL.data(), int(irL.size()));
            convR_[slot].setIR(irR.data(), int(irR.size()));
            slotState_[slot].store(SlotState::READY);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void SpringReverb::reset() {
    // Deferred: the calling thread (host prepare/release path) must not
    // touch DSP state the render thread may be using. beginBlock() performs
    // the actual reset on the audio thread.
    resetPending_.store(true);
}

void SpringReverb::resetOnAudioThread() {
    for (int c = 0; c < 2; ++c) {
        hp_[c].reset(); tone_[c].reset();
        a1108In_[c].reset(); a1108Out_[c].reset();
        driver_[c].reset(); a1108_[c].reset();
    }
    // Only the ACTIVE slot: the worker exclusively owns BUILDING slots, and
    // every READY slot was already reset by setIR() when the worker built it.
    convL_[activeSlot_].reset();
    convR_[activeSlot_].reset();
    dirty_ = true;
}

void SpringReverb::beginBlock() {
    // Wait-free: a handful of atomic loads/stores/CAS, no locks, no waiting,
    // no allocation.
    if (reconfigPending_.exchange(false)) {
        const double sr = pendingSr_.load();
        sr_.store(sr);
        configureFilters(sr);
    }
    if (resetPending_.exchange(false)) {
        resetOnAudioThread();
    }
    // Adopt a worker-finished IR at a block boundary. Retire the old ACTIVE
    // slot to IDLE first so the worker can reclaim it; the worker can never
    // observe a torn handoff because only this thread performs READY/ACTIVE
    // transitions and the worker only CAS-claims IDLE slots.
    for (int s = 0; s < 2; ++s) {
        if (slotState_[s].load() == SlotState::READY) {
            const int old = activeSlot_;
            slotState_[old].store(SlotState::IDLE);
            slotState_[s].store(SlotState::ACTIVE);
            activeSlot_ = s;
        }
    }
    // Post a regen request if the time knob moved. Same 0.2 s hysteresis as
    // the browser (which rebuilt its ConvolverNode when |dt| > 0.2); the
    // worker synthesizes + partitions off-thread, no audio-thread allocation.
    const float targetTime = 0.5f + (timeParam.load() / 10.0f) * 4.0f;
    if (std::fabs(targetTime - requestedTime_) > 0.2f) {
        requestedTime_ = targetTime;
        reqTime_.store(targetTime);
        reqSr_.store(sr_.load());
        reqPending_.store(true);
    }
}

void SpringReverb::updateFromParams() {
    // SR-4: DRIVE reaches the 12AT7 knee at settings 8-10. The law is
    // unchanged up to knob 6 (0.05 + (k/10)*0.25, as shipped), then
    // steepens to a 0.45 top (was 0.30, pre-knee) — predictable,
    // knob-proportional grit on transients only when cranked.
    {
        const float k = drive.load() / 10.0f;
        drvGain_.set(k <= 0.6f ? 0.05f + k * 0.25f
                               : 0.20f + (k - 0.6f) * 0.625f);
    }
    toneFreq_.set(500.0f + (contour.load() / 10.0f) * 5500.0f);
    // Audit F5 fix: the tone biquads are NOT written here anymore. They
    // used to be written from toneFreq_.target while toneFreq_.next()
    // was never called — the contour's smoothing was dead code and every
    // change stepped instantly. process() now advances the Smoothed and
    // rewrites the biquads from the glided value (see below).
    retGain_.set((returnGain.load() / 5.0f) * 0.7f);
    dirty_ = false;
}

void SpringReverb::process(float inL, float inR, float& outL, float& outR) {
    if (dirty_) updateFromParams();
    // Contour glide (audit F5): advance the existing Smoothed per
    // sample; rewrite the tone biquads every 16 samples while the
    // glided frequency is still moving.
    const float tf = toneFreq_.next();
    if (++toneCtr_ >= 16) {
        toneCtr_ = 0;
        if (std::fabs(tf - toneFreqWritten_) > 0.5f) {
            toneFreqWritten_ = tf;
            for (int c = 0; c < 2; ++c)
                tone_[c].setLowpass(sr_.load(), tf, 0.4);
        }
    }
    const int a = activeSlot_; // audio thread's view; written only in beginBlock()
    float m[2] = { hp_[0].process(inL * 0.4f), hp_[1].process(inR * 0.4f) };
    float dg = drvGain_.next() * railDrive; // shared PSU sags the driver
    float cL = convL_[a].process(driver_[0].process(m[0] * dg)) * makeup_;
    float cR = convR_[a].process(driver_[1].process(m[1] * dg)) * makeup_;
    float rg = retGain_.next();
    // Browser order: makeup -> tone -> returnGain -> 1108 chain.
    // SR-5: +4 dB of the return gain now sits BEFORE the 1108 and is
    // compensated by -4 dB after it, so the REVERB level at knob 5 is
    // unchanged while the top third of the knob genuinely drives the
    // 1108's FET knee (previously ~0.7x, nearly decorative).
    const float kPre1108 = 1.5849f;  // +4 dB
    const float kPost1108 = 0.6310f; // -4 dB
    float tL = tone_[0].process(cL) * rg * kPre1108;
    float tR = tone_[1].process(cR) * rg * kPre1108;
    outL = a1108Out_[0].process(a1108_[0].process(a1108In_[0].process(tL))) * kPost1108;
    outR = a1108Out_[1].process(a1108_[1].process(a1108In_[1].process(tR))) * kPost1108;
}

// ---------------------------------------------------------------------------
// MasterSection
// ---------------------------------------------------------------------------

void MasterSection::prepare(double sampleRate, const Curves& curves) {
    sr_ = sampleRate;
    curves_ = &curves;
    scLpA_ = 1.0f - poleForCorner(150.0f, sr_); // SC-1 crossover pole
    for (int c = 0; c < 2; ++c) {
        scullyIn_[c].setHighpass(sr_, 10, 0.6);
        scullyOut_[c].setLowpass(sr_, 22000, 0.5);
        scully_[c].prepare(sr_); scully_[c].setCurve(&curves_->scully);
        scully_[c].quadCoef = 0.03f;    // SC-2 bias-coupled asymmetry
        // TEST3 drive-law rebalance: the Scully pair runs at 0.55x
        // drive (BiasedShaper normalises the low-level gain back
        // exactly, so master unity is untouched) and its bias memory
        // deepens less (0.6 -> 0.4) — at master scale the deepen
        // overshoot, not the static curve, was the wall Nathan hit.
        scully_[c].driveMul = 0.55f;
        scully_[c].biasDepth = 0.4f;
        scullyLf_[c].prepare(sr_); scullyLf_[c].setCurve(&curves_->scully);
        scullyLf_[c].quadCoef = 0.03f;  // SC-2 (LF band instance)
        scullyLf_[c].driveMul = 1.41f * 0.55f;  // SC-1: LF ~+3 dB deeper, on the rebalanced base
        scullyLf_[c].biasDepth = 0.4f;
        bus_[c].prepare(sr_);    bus_[c].setCurve(&curves_->bus);
        // Research brief S5 CPU lever (pre-authorised): the Helios
        // shaper runs at base rate — after HL-1 its saturation is
        // driven by mid-band energy and is mostly low-order, so it is
        // the designated first sacrifice. This also removes its
        // 25-sample stage latency (see getLatencySamples()).
        helios_[c].shaper.forceBaseRate = true;
        helios_[c].prepare(sr_); helios_[c].setCurve(&curves_->helios);
        helios_[c].biasDepth = 0.4f; // TEST3 rebalance (was 0.6): less deepen overshoot at master scale
        low_[c].setLowShelf(sr_, 60, 0.9, 0);
        mid_[c].setPeaking(sr_, 1000, 1.08, 0);
        high_[c].setHighShelf(sr_, 14000, 0.4, 0); // Euphonic air band
        // Pultec-style: dip below the 60Hz bump for tightness
        // TEST3 rebalance (Nathan, 2026-10-13): the sub-bass stack is
        // relaxed so the bottom octave survives flat — the safety HP
        // moves 28 -> 8 Hz and the sub cut 45 -> 10 Hz. (Briefed
        // starting points were ~15/~22 Hz; measured on the full
        // chain, 22/15 still gave -3.7/-2.0 dB at 40/50 Hz and 16/12
        // gave -2.7/-1.6 — the rest of the chain, not just these two
        // corners, shapes the bottom. 10/8 lands the targets:
        // -2.4 dB @ 40 Hz, -1.2 dB @ 50 Hz. Subsonic protection is
        // retained where it matters: channel rumble 20 Hz, this
        // section's own 10 Hz input HP, and the spring-send HPs are
        // all unchanged. The 35 Hz Pultec peaking dip in
        // writeEqCoeffs is unchanged.)
        pultecDip_[c].setHighpass(sr_, 8, 0.7);
        subCut_[c].setHighpass(sr_, 10, 0.5);
        // Decorrelated hiss: independent seed per channel (audit F3).
        noise_[c].prepare(sr_, c == 0 ? 0x9015Eu : 0x3D7A9u);
    }
    // TEST3: 0.9 / kMasterPad (see process()) — the pad's exact makeup.
    busDriveGain_.prepare(sr_, 0.9f / 0.55f);
    glideA_ = tauCoef(0.015f, sr_); // 15 ms EQ glide (DN1000 item 5)
    eqSnap_ = true;
    dirty_ = true;
    reset();
}

void MasterSection::reset() {
    for (int c = 0; c < 2; ++c) {
        scullyIn_[c].reset(); scullyOut_[c].reset();
        scully_[c].reset(); scullyLf_[c].reset(); scLp_[c] = 0.0f;
        midTap_[c].reset(); midEnv_[c].reset();
        bus_[c].reset(); helios_[c].reset();
        low_[c].reset(); mid_[c].reset(); high_[c].reset(); subCut_[c].reset();
        pultecDip_[c].reset();
        noise_[c].reset();
    }
    midEnvNorm_ = 0.0f; midCompDb_ = 0.0f;
}

// Param targets (audit F5): EQ values become glide targets.
void MasterSection::updateTargets() {
    lowDbT_ = lowDb; midDbT_ = midDb; highDbT_ = highDb;
    midFreqT_ = midFreq < 0 ? 0 : (midFreq > 2 ? 2 : midFreq);
    if (eqSnap_) {
        lowDbS_ = lowDbT_; midDbS_ = midDbT_; highDbS_ = highDbT_;
        writeEqCoeffs();
        eqSnap_ = false;
    }
    dirty_ = false;
}

void MasterSection::writeEqCoeffs() {
    static const float midFreqs[3] = {700.0f, 1000.0f, 1400.0f};
    // Helios Type 69 base Q: 3 octaves (Q~0.40) at 700Hz, narrowing to
    // 2 octaves (Q~0.67) at higher frequencies
    static const float baseQ[3] = {0.40f, 0.55f, 0.67f};
    // TEST3 (Nathan, 2026-10-13): the mid bell tightens slightly —
    // base Q x1.2 for the peaking band only. The proportional-Q law
    // and the HL-1 detector tap (which keeps the original baseQ
    // below) are unchanged.
    static const float baseQTight[3] = {0.48f, 0.66f, 0.804f};
    // Proportional Q: increases with boost/cut amount (Helios behavior)
    float propQ = baseQTight[midFreqT_] * (1.0f + (std::fabs(midDbS_) / 10.0f) * 1.2f);
    // HL-2: inductor boost compression — under heavy sustained in-band
    // level the effective mid gain is pulled toward 0 by up to ~2 dB
    // (never past 0; the hardware's self-limiting boldness). HL-3: the
    // peak nudges up <=3% with the same envelope (inductance drop).
    float comp = midCompDb_;
    if (comp > std::fabs(midDbS_)) comp = std::fabs(midDbS_);
    const float midEff = midDbS_ - (midDbS_ >= 0 ? comp : -comp);
    const float midFreqEff = midFreqs[midFreqT_] * (1.0f + 0.03f * midEnvNorm_);
    compWritten_ = midCompDb_;
    detuneWritten_ = midEnvNorm_;
    for (int c = 0; c < 2; ++c) {
        // Pultec-style low: 60Hz bump with subtle dip below for tightness
        // When boosting, the dip tightens; when cutting, it's a clean shelf
        // TEST3: shelf slope steepens (S 0.5 -> 0.9) and the Pultec dip
        // factor halves (0.3 -> 0.15) so the boost is no longer fought.
        low_[c].setLowShelf(sr_, 60, 0.9, lowDbS_);
        float dipGain = lowDbS_ > 0 ? -lowDbS_ * 0.15f : 0.0f; // Pultec trick
        pultecDip_[c].setPeaking(sr_, 35, 1.2, dipGain);
        mid_[c].setPeaking(sr_, midFreqEff, propQ, midEff);
        // HL-1 detector tap: RBJ bandpass (constant 0 dB peak) at the
        // NOMINAL mid frequency, the band's own Q.
        {
            const double w0 = 2.0 * kPi * double(midFreqs[midFreqT_]) / sr_;
            const double alpha = std::sin(w0) / (2.0 * double(baseQ[midFreqT_]));
            const double a0 = 1.0 + alpha;
            Biquad& bp = midTap_[c];
            bp.b0 = float(alpha / a0); bp.b1 = 0.0f; bp.b2 = float(-alpha / a0);
            bp.a1 = float(-2.0 * std::cos(w0) / a0);
            bp.a2 = float((1.0 - alpha) / a0);
        }
        // Euphonic high: air band, very gentle (Fairman/Retro style).
        // TEST3 (Nathan's revised spec): corner 16 -> 14 kHz, slope
        // stays exactly as shipped (S 0.4).
        high_[c].setHighShelf(sr_, 14000, 0.4, highDbS_);
    }
}

void MasterSection::noisePair(float level, float& nl, float& nr) {
    nl = noise_[0].process() * level;
    nr = noise_[1].process() * level;
}

void MasterSection::process(float inL, float inR, float& outL, float& outR) {
    if (dirty_) updateTargets();
    // EQ glide: advance per sample; refresh coefficients every 32
    // samples, only while a glided value is still moving (audit F5).
    lowDbS_ += (lowDbT_ - lowDbS_) * glideA_;
    midDbS_ += (midDbT_ - midDbS_) * glideA_;
    highDbS_ += (highDbT_ - highDbS_) * glideA_;
    // HL-2/3 dynamics: the compression + detune move with the linked
    // band envelope even when the knobs are still, so coefficient
    // refresh also fires while those are moving.
    if (++refreshCtr_ >= 32) {
        refreshCtr_ = 0;
        if (std::fabs(lowDbT_ - lowDbS_) > 1e-4f ||
            std::fabs(midDbT_ - midDbS_) > 1e-4f ||
            std::fabs(highDbT_ - highDbS_) > 1e-4f ||
            std::fabs(midCompDb_ - compWritten_) > 0.01f ||
            std::fabs(midEnvNorm_ - detuneWritten_) > 0.002f)
            writeEqCoeffs();
    }
    float bd = busDriveGain_.next(); // 0.9 in the original; TEST3: 0.9/0.55 (pad makeup)
    // TEST3 drive-law rebalance: the whole master chain runs 0.55x
    // cooler (input pad here; exact makeup in busDriveGain_ at the
    // end of the chain, so small-signal master gain is bit-exact).
    // The master receives ~unity-scale signal, and its saturation
    // stack (Scully + Helios, each with bias-depth overshoot) was
    // the wall at defaults; the pad spreads the headroom across the
    // stack like the hardware's internal level drops.
    const float kMasterPad = 0.55f;
    float in[2] = {inL, inR}, out[2];
    float envMax = 0.0f;
    for (int c = 0; c < 2; ++c) {
        float y = scullyIn_[c].process(in[c] * kMasterPad);
        // SC-1: split-band Scully — LF (<150 Hz) through the deeper-
        // driven instance, the rest through the standard one; the
        // one-pole split sums exactly and both stages share the same
        // 25-sample latency, so recombination is phase-true.
        scLp_[c] += (y - scLp_[c]) * scLpA_;
        const float lf = scLp_[c];
        y = scullyLf_[c].process(lf) + scully_[c].process(y - lf);
        y = scullyOut_[c].process(y);
        if (phaseInvert) y = -y;
        y = low_[c].process(y);
        y = pultecDip_[c].process(y); // Pultec-style tightness below the bump
        // HL-1: mid-band energy detector on the mid EQ's input. The
        // envelope (bias-memory family: 5 ms / 80 ms) scales the
        // Helios stage's drive: at low in-band level the stage tends
        // to transparent; the growl arrives with the band, as in the
        // hardware's inductor.
        const float bandEnv = midEnv_[c].process(midTap_[c].process(y), sr_);
        float en = bandEnv / 0.6f; if (en > 1.0f) en = 1.0f;
        if (en > envMax) envMax = en;
        // TEST3 rebalance: HL-1's law keeps its 0.25 rest floor but
        // the driven top end caps at 0.75 (was 1.0) — same shape,
        // less deep into the inductor knee at defaults.
        helios_[c].driveMul = 0.25f + 0.50f * en;
        y = mid_[c].process(y);
        // Helios Type 69: inductor + transformer character, driven by
        // mid-band energy (HL-1) rather than full-band level.
        y = helios_[c].process(y);
        y = high_[c].process(y); // Euphonic air
        y = subCut_[c].process(y);
        y *= bd;
        // Bus shaper bypassed for CPU (very subtle; Scully covers saturation)
        // y = bus_[c].process(y);
        // 0.3.0-test: NO tape EQ, NO tape280, NO limiter, NO post-clamp
        // past this point — the master chain ends here (Nathan's
        // removal order, 2026-10-13). Peaks are bounded only by the
        // Scully/Helios saturation above, like the hardware.
        out[c] = y * outTrim_[c];
    }
    // Linked (stereo-shared coefficient) dynamics for HL-2/HL-3.
    midEnvNorm_ = envMax;
    midCompDb_ = 2.0f * envMax; // bounded: <= ~2 dB of boost compression
    outL = out[0]; outR = out[1];
}

// ---------------------------------------------------------------------------
// BiteyDsp top level
// ---------------------------------------------------------------------------

void BiteyDsp::prepare(double sampleRate) {
    sr_ = sampleRate;
    curves_ = Curves::build();
    for (auto& c : ch_) c.prepare(sr_, curves_);
    // Component tolerances (audit F6, DN1000 Ch-B pattern): Ch 2 runs a
    // hair off nominal — low shelf +1.5%, high shelf -1%, post gain
    // +0.6%. Ch 1 stays exactly nominal.
    ch_[1].setTolerances(1.015f, 0.99f, 1.006f);
    tape_.prepare(sr_, curves_);
    reverb_.prepare(sr_, curves_);
    master_.prepare(sr_, curves_);
    wetGain_.prepare(sr_, 1.0f);
    psu_.reset();
    powerSnap_ = true;
    warmth_ = 0.0f; // a freshly prepared unit starts cold (DN1000 item 8)
    // Dry path for the power crossfade: delayed by EXACTLY the reported
    // latency, so power-off is a latency-matched clean bypass (audit
    // F10/F11). After the 0.3.0-test removals the direct path holds 3
    // oversampled stages (channel OP-6 + master Scully + master Helios)
    // = 75 samples below 88.2 kHz, 0 at/above (shapers bypassed).
    // NOTE: the delay lines below are written then read one slot ahead,
    // so a buffer of N slots gives exactly N-1 samples of delay.
    const int dryLat = getLatencySamples();
    dryDelayL_.assign(dryLat + 1, 0);
    dryDelayR_.assign(dryLat + 1, 0);
    dryPos_ = 0;
    reset();
    setParams(BiteyParams{});
}

void BiteyDsp::reset() {
    for (auto& c : ch_) c.reset();
    tape_.reset();
    reverb_.reset();
    master_.reset();
    psu_.reset();
    std::fill(dryDelayL_.begin(), dryDelayL_.end(), 0);
    std::fill(dryDelayR_.begin(), dryDelayR_.end(), 0);
    dryPos_ = 0;
    revMeter_ = mainMeter_ = 0.0f;
}

void BiteyDsp::setParams(const BiteyParams& p) {
    params_ = p;
    for (int i = 0; i < 2; ++i) {
        ch_[i].level = p.ch[i].level;
        ch_[i].pad = p.ch[i].pad;
        ch_[i].lowCut = p.ch[i].lowCut;
        ch_[i].lowDb = p.ch[i].lowDb;
        ch_[i].highDb = p.ch[i].highDb;
        ch_[i].fxSend = p.ch[i].fxSend;
        ch_[i].touch();
    }
    tape_.speed = p.tapeSpeed;
    tape_.size = p.tapeSize;
    tape_.mix = p.tapeMix;
    tape_.bypass = false; // UI always runs the tape engaged; mix=0 silences it
    tape_.touch();
    reverb_.timeParam = p.revTime;
    reverb_.drive = p.revDrive;
    reverb_.contour = p.revContour;
    reverb_.returnGain = p.revReturn;
    reverb_.touch();
    master_.mainLevel = p.mainLevel;
    master_.lowDb = p.mLowDb;
    master_.midDb = p.mMidDb;
    master_.highDb = p.mHighDb;
    master_.midFreq = p.midFreq;
    master_.phaseInvert = p.phaseInvert;
    master_.touch();
    // Effects level only — power on/off no longer touches this (the
    // power crossfade at the output handles bypass now, audit F10).
    wetGain_.set(p.mix / 10.0f);
}

int BiteyDsp::getLatencySamples() const {
    // TRUE latency of the direct path (audit F11), rate-aware, and
    // MEASURED (impulse-response peak + cross-correlation, 2026-10-13):
    // one OversampledShaper delays 25 samples, not the nominal 24 its
    // latency() claims — the FIR pair contributes 23 and the pure-delay
    // padding 2. (The shipped build's true latency was 5 x 25 = 125
    // against a reported 96.) The 0.3.0-test direct path holds the
    // channel OP-6 stage A + stage B (OP6-1 cascade) and the master
    // Scully as oversampled stages (3 x 25); the Helios stage runs at
    // base rate (research S5 CPU lever) and adds 0. The Scully
    // split-band pair are parallel paths of equal latency, counted
    // once. Total 75 samples below 88.2 kHz, 0 at/above (all shapers
    // bypass there anyway). The reverb path carries its own
    // driver/1108 stages but is a parallel bus (ambience, no
    // transients), as in the original.
    if (sr_ >= 88200.0) return 0;
    return (master_.heliosBaseRate() ? 3 : 4) * 25;
}

void BiteyDsp::process(float* left, float* right, int numSamples) {
    reverb_.beginBlock(); // adopt any worker-finished spring IR (block boundary)
    const float srcPad = 0.025f;
    const float mainG = master_.mainGain();
    const int dryN = int(dryDelayL_.size());

    // Power crossfade (audit F10, DN1000 build-98 semantics): the DSP
    // ALWAYS runs — while off, the output crossfades (~300 ms) to the
    // dry input delayed by exactly the reported latency. No click, the
    // unit stays warm, and the spring tail keeps decaying underneath
    // instead of freezing. On (re)prepare the fade snaps to the current
    // power state so a freshly loaded instance starts settled.
    const float powerTarget = params_.bypass ? 0.0f : 1.0f;
    if (powerSnap_) { powerGain_ = powerTarget; powerSnap_ = false; }
    const float powerA = tauCoef(0.06f, sr_); // ~300 ms to settle

    // Warm-up settle (DN1000 item 8): while on, warmth rises toward 1
    // with a ~2.5 s tau; while off it cools toward 0.25 (lukewarm)
    // over ~60 s. Applied as a gentle trim on the processed path:
    // a cold unit sits ~0.26 dB down.
    if (numSamples > 0) {
        const double blockSec = double(numSamples) / sr_;
        if (!params_.bypass)
            warmth_ += (1.0f - warmth_) * float(1.0 - std::exp(-blockSec / 2.5));
        else
            warmth_ += (0.25f - warmth_) * float(1.0 - std::exp(-blockSec / 60.0));
    }
    const float warmTrim = 0.97f + 0.03f * warmth_;

    // Hiss level (audit F3): scaled by the actual gain staging — the
    // mean channel drive (relative to the Level-5 nominal of x20) and
    // MAIN. At the shipped defaults (Level 5, MAIN 5) this is exactly
    // 1.0 and the level equals the original fixed hiss. Crank the unit
    // and the hiss rises with it; MAIN at 0 silences it.
    // TEST3: the nominal Level-5 drive is now x10 (was x20) — the
    // normaliser follows so the hiss level at defaults is unchanged.
    const float driveNorm = 0.5f * (ch_[0].driveTarget() + ch_[1].driveTarget()) / 10.0f;
    const float noiseLevel = 0.0004f * std::sqrt(std::max(driveNorm, 0.0f)) * mainG;

    double revSum = 0.0, outSum = 0.0; // VU meter taps (sound-neutral)

    for (int n = 0; n < numSamples; ++n) {
        float inL = left[n], inR = right[n];

        // Shared PSU (audit F4): the rail from the previous samples'
        // combined load modulates this sample's stage drive.
        const float rail = psu_.getRail();
        ch_[0].setRail(rail);
        ch_[1].setRail(rail);
        reverb_.railDrive = rail;
        master_.setScullyRail(rail);

        // Dry path (latency-matched for the power crossfade)
        dryDelayL_[dryPos_] = inL;
        dryDelayR_[dryPos_] = inR;
        int rp = dryPos_ + 1;
        if (rp >= dryN) rp = 0;
        float dryL = dryDelayL_[rp], dryR = dryDelayR_[rp];
        if (++dryPos_ >= dryN) dryPos_ = 0;

        // Channel strips
        float postL = ch_[0].process(inL * srcPad);
        float postR = ch_[1].process(inR * srcPad);

        // Reverb sends (with 0.15 cross-bleed, as in the original merger)
        float s0 = ch_[0].fxSendGain(), s1 = ch_[1].fxSendGain();
        float revInL = postL * s0 + postR * s1 * 0.15f;
        float revInR = postR * s1 + postL * s0 * 0.15f;

        // Tape slap (mono sum in the original: tapeSum sums both posts)
        float tapeIn = (postL + postR) * 0.5f;
        float tapeOut = tape_.process(tapeIn);
        float tapeBleed = tape_.lastOut() * 0.12f;
        revInL += tapeBleed;
        revInR += tapeBleed;

        // Reverb return
        float revL, revR;
        reverb_.process(revInL, revInR, revL, revR);

        // Master sum: dry ALWAYS through Scully/EQ at unity.
        // DRY/WET controls the effects send level, not a crossfade.
        // This keeps the analog tone intact and halves CPU (single master chain).
        float dirL = postL * mainG, dirR = postR * mainG;

        float w = wetGain_.next(); // 0..1 effects level

        float nzL, nzR;
        master_.noisePair(noiseLevel, nzL, nzR);
        // ARCHITECTURE (Nathan 2026-10-07): MAIN and REVERB are INDEPENDENT
        // buses feeding the final stereo bus. They do NOT feed into each other.
        // - MAIN bus: dry + tape -> master chain (Scully, EQ; no tape
        //   stage, no limiter since 0.3.0-test)
        // - REVERB bus: reverb return -> final bus DIRECT (no master saturation).
        //   (Reverb already has its own 1108 drive chain.)
        // Cranking REVERB does NOT push the MAIN into distortion.
        float sumL = dirL + tapeOut * mainG * w + nzL;
        float sumR = dirR + tapeOut * mainG * w + nzR;

        float mainL, mainR;
        master_.process(sumL, sumR, mainL, mainR);

        // Independent reverb bus (LEVEL pot controls returnGain in SpringReverb)
        float revBusL = revL * w;
        float revBusR = revR * w;

        // Final stereo bus: sum of independent buses
        float wetL = mainL + revBusL;
        float wetR = mainR + revBusR;

        // PSU demand: combined channel + master load drives the next
        // samples' rail (DN1000: demand from outputs, one sample late).
        psu_.update(std::fabs(postL) + std::fabs(postR) +
                    std::fabs(wetL) + std::fabs(wetR), sr_);

        // Power crossfade + warm-up trim on the processed path.
        powerGain_ += (powerTarget - powerGain_) * powerA;
        if (std::fabs(powerTarget - powerGain_) < 1e-4f) powerGain_ = powerTarget;
        const float wet = powerGain_ * warmTrim;
        const float dry = 1.0f - powerGain_;
        float outL = wetL * wet + dryL * dry;
        float outR = wetR * wet + dryR * dry;

        left[n] = outL;
        right[n] = outR;
        revSum += double(revL) * revL * powerGain_ + double(revR) * revR * powerGain_;
        outSum += double(outL) * outL + double(outR) * outR;
    }

    // Browser calculateRMS() scaling: min(rms * 4.0, 1.4).
    if (numSamples > 0) {
        const float inv = 1.0f / float(2 * numSamples);
        revMeter_  = std::min(std::sqrt(float(revSum * inv)) * 4.0f, 1.4f);
        mainMeter_ = std::min(std::sqrt(float(outSum * inv)) * 4.0f, 1.4f);
    }
}

} // namespace bitey
