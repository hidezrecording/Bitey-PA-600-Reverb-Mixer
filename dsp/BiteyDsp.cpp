// Bitey PA-600 DSP core implementation — port of AudioEngine.ts.
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
    c.op6 = Curve::make(8192, [](float x) {
        return x < 0 ? std::tanh(x * 1.2f) : std::tanh(x * 1.05f);
    });
    c.tape = Curve::make(8192, [](float x) { return std::tanh(x * 1.5f); });
    c.bus = Curve::make(8192, [](float x) {
        return x * 0.98f + std::tanh(x * 0.1f) * 0.02f;
    });
    c.driver = Curve::make(8192, [](float x) { return std::tanh(x * 0.8f); });
    c.limiter = Curve::make(8192, [](float x) {
        if (x > 0.99f) return 0.99f;
        if (x < -0.99f) return -0.99f;
        return x;
    });
    c.scully = Curve::make(8192, [](float x) {
        float y = x + 0.02f * (x * x);
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
    });
    c.c1108 = Curve::make(8192, [](float x) {
        float y = x + 0.05f * (x * x);
        if (y > 0.8f) y = 0.8f + std::tanh((y - 0.8f) * 1.5f) * 0.2f;
        else if (y < -0.8f) y = -0.8f + std::tanh((y + 0.8f) * 1.5f) * 0.2f;
        return y;
    });
    // Helios Type 69 mid: inductor + transformer character.
    // Inductor saturation: soft, with 2nd harmonic emphasis (single-ended).
    // Transformer: subtle 3rd harmonic, very gentle saturation.
    c.helios = Curve::make(8192, [](float x) {
        // Inductor: asymmetric, 2nd harmonic rich
        float y = x + 0.03f * x * x - 0.01f * x * x * x;
        // Gentle saturation above 0.6 (inductor core saturation)
        if (y > 0.6f) {
            float d = (y - 0.6f) * 1.8f;
            y = 0.6f + (y - 0.6f) / (1.0f + d * d * 0.5f);
        } else if (y < -0.6f) {
            float d = (y + 0.6f) * 1.8f;
            y = -0.6f + (y + 0.6f) / (1.0f + d * d * 0.5f);
        }
        return y;
    });
    // Scully 280 15IPS tape: hysteresis-based saturation.
    // 15IPS character: warm, slight compression, 3rd harmonic emphasis,
    // gentle HF softening. Models the tape going onto the reel.
    c.tape280 = Curve::make(8192, [](float x) {
        // Tape hysteresis: odd-harmonic rich, with memory-like smoothing
        float y = x;
        // 3rd harmonic from tape (B-H curve)
        y += 0.02f * x * x * x;
        // Soft saturation (tape compression)
        if (y > 0.75f) {
            float d = (y - 0.75f) * 2.5f;
            y = 0.75f + (y - 0.75f) / (1.0f + d);
        } else if (y < -0.75f) {
            float d = (y + 0.75f) * 2.5f;
            y = -0.75f + (y + 0.75f) / (1.0f + d);
        }
        return y * 1.02f; // Slight makeup for tape loss
    });
    return c;
}

// ---------------------------------------------------------------------------
// OversampledShaper — 4x zero-stuff + 193-tap Kaiser-windowed-sinc FIR.
//
// Matches Chromium's 4x WaveShaperNode, measured directly:
//   1/2/3/4 cascaded stages -> 192/384/576/768 samples latency.
// The resampling filter itself is nearly transparent (flat to ~20 kHz,
// gentle rolloff to Nyquist); the 193-tap FIR contributes 48 samples of
// group delay and a pure delay line makes up the remaining 144, for an
// exact total of 192 samples at the base rate per instance.
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
    (void)sampleRate;
    const int N = kTaps;
    fir_.assign(N, 0);
    // Cutoff at the 4x Nyquist: gentle lowpass, flat through the audio band
    // (mirrors the measured Chromium response: +0 dB to ~20 kHz).
    const double fc = 0.125; // cycles/sample at 4x rate
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
    for (auto& v : fir_) v = float(v / sum * 4.0); // x4 compensates zero-stuff
    upBuf_.assign(N, 0);
    dnBuf_.assign(N, 0);
    delayBuf_.assign(kPureDelay + 1, 0);
    reset();
}

void OversampledShaper::reset() {
    std::fill(upBuf_.begin(), upBuf_.end(), 0);
    std::fill(dnBuf_.begin(), dnBuf_.end(), 0);
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0);
    upPos_ = dnPos_ = delayPos_ = 0;
}

float OversampledShaper::process(float x) {
    const int N = int(fir_.size());
    float out4[4];
    for (int k = 0; k < 4; ++k) {
        float in = (k == 0) ? x : 0.0f; // zero-stuff
        upBuf_[upPos_] = in;
        float acc = 0;
        int idx = upPos_;
        for (int n = 0; n < N; ++n) {
            acc += fir_[n] * upBuf_[idx];
            if (--idx < 0) idx += N;
        }
        if (++upPos_ >= N) upPos_ = 0;
        out4[k] = curve_ ? curve_->process(acc) : acc;
    }
    // Decimate: filter each shaped sample; take phase k=0.
    float y = 0;
    for (int k = 0; k < 4; ++k) {
        dnBuf_[dnPos_] = out4[k];
        float acc = 0;
        int idx = dnPos_;
        for (int n = 0; n < N; ++n) {
            acc += fir_[n] * dnBuf_[idx];
            if (--idx < 0) idx += N;
        }
        if (++dnPos_ >= N) dnPos_ = 0;
        if (k == 0) y = acc * 0.25f; // x0.25 compensates the x4 up-filter gain
    }
    // Pure delay line: the two FIRs contribute 2*24 = 48 samples; pad to the
    // measured 192 so transients land exactly where the browser puts them.
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
    headIR_.assign(kPartition, 0.0f);
    headBuf_.assign(kPartition, 0.0f);
    headPos_ = 0;
    tail_.prepare(sampleRate);
    reset();
}

void ZeroLatencyConvolver::setIR(const float* ir, int len) {
    const int headLen = std::min(len, kPartition);
    headIR_.assign(kPartition, 0.0f);
    for (int i = 0; i < headLen; ++i) headIR_[i] = ir[i];
    // Tail covers IR[kPartition..]; its block delay is exactly correct for
    // these later taps (see header note).
    if (len > kPartition)
        tail_.setIR(ir + kPartition, len - kPartition);
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
    for (int i = 0; i < kPartition; ++i) {
        yHead += headIR_[i] * headBuf_[idx];
        if (--idx < 0) idx += kPartition;
    }
    if (++headPos_ >= kPartition) headPos_ = 0;
    // Partitioned tail (its block delay aligns with IR[kPartition..]).
    float yTail = tail_.process(x);
    return yHead + yTail;
}

// ---------------------------------------------------------------------------
// PartitionedConvolver — uniform partitioned overlap-add
// ---------------------------------------------------------------------------

void PartitionedConvolver::prepare(double sampleRate) {
    (void)sampleRate;
    fft_.init(kPartition * 2);
    tmp_.assign(fft_.size(), {});
    acc_.assign(fft_.size(), {});
    inBuf_.assign(kPartition, 0);
    outBuf_.assign(kPartition * 2, 0);
    reset();
}

void PartitionedConvolver::setIR(const float* ir, int len) {
    numParts_ = (len + kPartition - 1) / kPartition;
    h_.assign(numParts_, std::vector<std::complex<float>>(fft_.size()));
    for (int p = 0; p < numParts_; ++p) {
        auto& H = h_[p];
        std::fill(H.begin(), H.end(), std::complex<float>(0, 0));
        int off = p * kPartition;
        int cnt = std::min(kPartition, len - off);
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

    if (inPos_ >= kPartition) {
        inPos_ = 0;
        // FFT of input block
        for (int i = 0; i < kPartition; ++i) tmp_[i] = inBuf_[i];
        for (int i = kPartition; i < fft_.size(); ++i) tmp_[i] = 0;
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
    for (int c = 0; c < 2; ++c) {
        SeededRng rng(seed + c * 0x9E37u);
        std::vector<float>& data = (c == 0) ? outL : outR;
        float lastNoise = 0, lastVal = 0;
        for (int i = 0; i < length; ++i) {
            double t = double(i) / sampleRate;
            float noise = 0;
            if (rng.uniform() < float(density / sampleRate)) {
                noise = rng.bipolar();
                if (t < 0.05) noise *= 2.0f;
            }
            noise = (noise + lastNoise * 0.8f) / 1.8f;
            lastNoise = noise;
            float flutter = 1.0f + float(flutterDepth *
                std::sin(2 * kPi * flutterRate * t + c * kPi * 0.5));
            float decay = float(std::exp(-t * (4.0 - seconds * 0.4)));
            float signal = noise * decay * flutter;
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
    rumble_.setHighpass(sr_, 20, 0.5);
    lowCut_.setHighpass(sr_, 10, 0.5);
    bandwidth_.setLowpass(sr_, 24000, 0.5);
    // Neve 1073-style: low shelving at 110Hz (gentle, Q=0.5), high shelving at 12kHz
    // "A hair more gentle" than stock Neve — wider, more musical slopes
    low_.setLowShelf(sr_, 110, 0.5, 0);
    high_.setHighShelf(sr_, 12000, 0.5, 0);
    shaper_.prepare(sr_);
    shaper_.setCurve(&curves_->op6);
    preAmpGain_.prepare(sr_, 1.0f);
    postGain_.prepare(sr_, 2.0f);
    dirty_ = true;
    reset();
}

void ChannelStrip::reset() {
    preEmp_.reset(); deEmp_.reset(); rumble_.reset(); lowCut_.reset();
    bandwidth_.reset(); low_.reset(); high_.reset();
    shaper_.reset();
}

void ChannelStrip::updateGains() {
    static const float padFactors[4] = {1.0f, 1.585f, 3.162f, 0.316f};
    padFactor_ = padFactors[pad < 0 ? 0 : (pad > 3 ? 3 : pad)];
    float nl = level / 5.0f; // 0..2
    float drive = std::pow(nl, 2.4f) * 20.0f;
    preAmpGain_.set(drive * padFactor_);
    postGain_.set(2.0f);
    low_.setLowShelf(sr_, 110, 0.5, lowDb);
    high_.setHighShelf(sr_, 12000, 0.5, highDb);
    lowCut_.setHighpass(sr_, lowCut ? 96 : 10, 0.5);
    dirty_ = false;
}

float ChannelStrip::fxSendGain() const {
    float s = fxSend / 10.0f;
    return s * s;
}

float ChannelStrip::process(float x) {
    if (dirty_) updateGains();
    float y = preEmp_.process(x);
    y *= preAmpGain_.next();
    y = shaper_.process(y);
    y = deEmp_.process(y);
    y = rumble_.process(y);
    y = lowCut_.process(y);
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

void SpringReverb::prepare(double sampleRate, const Curves& curves) {
    sr_ = sampleRate;
    curves_ = &curves;
    for (int c = 0; c < 2; ++c) {
        hp_[c].setHighpass(sr_, 180, 0.5);
        tone_[c].setLowpass(sr_, 3250, 0.4);
        a1108In_[c].setHighpass(sr_, 50, 0.5);
        a1108Out_[c].setLowpass(sr_, 24000, 0.5);
    }
    driver_[0].prepare(sr_); driver_[0].setCurve(&curves_->driver);
    driver_[1].prepare(sr_); driver_[1].setCurve(&curves_->driver);
    a1108_[0].prepare(sr_);  a1108_[0].setCurve(&curves_->c1108);
    a1108_[1].prepare(sr_);  a1108_[1].setCurve(&curves_->c1108);
    for (int s = 0; s < 2; ++s) { convL_[s].prepare(sr_); convR_[s].prepare(sr_); }
    drvGain_.prepare(sr_, 0.175f);
    toneFreq_.prepare(sr_, 3250.0f);
    retGain_.prepare(sr_, 0.7f);
    // Build the initial IR synchronously ONLY on first prepare (worker not
    // running yet). On re-prepare (sample rate change during audio), use the
    // async worker path to avoid racing the audio thread.
    const float t0 = 0.5f + (timeParam / 10.0f) * 4.0f;
    const bool firstPrepare = !worker_.joinable();
    if (firstPrepare) {
        std::vector<float> irL, irR;
        SpringIR::generate(t0, sr_, irL, irR);
        convL_[0].setIR(irL.data(), int(irL.size()));
        convR_[0].setIR(irR.data(), int(irR.size()));
        active_.store(0);
        cachedActive_ = 0;
    } else {
        // Ask worker to rebuild at new sample rate on inactive slot
        reqTime_.store(t0, std::memory_order_relaxed);
        reqPending_.store(true, std::memory_order_release);
    }
    requestedTime_ = t0;
    swapReady_.store(false);
    reqPending_.store(false);
    if (!worker_.joinable()) {
        quit_.store(false);
        worker_ = std::thread(&SpringReverb::workerMain, this);
    }
    dirty_ = true;
    reset();
}

void SpringReverb::workerMain() {
    // Background synthesis + partitioning of the spring IR. Never touches the
    // live convolver slot; the audio thread adopts it via beginBlock().
    while (!quit_.load(std::memory_order_relaxed)) {
        if (reqPending_.exchange(false, std::memory_order_acq_rel)) {
            const float t = reqTime_.load(std::memory_order_relaxed);
            const int inactive = 1 - active_.load(std::memory_order_acquire);
            std::vector<float> irL, irR;
            SpringIR::generate(t, sr_, irL, irR);
            convL_[inactive].setIR(irL.data(), int(irL.size()));
            convR_[inactive].setIR(irR.data(), int(irR.size()));
            swapReady_.store(true, std::memory_order_release);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void SpringReverb::reset() {
    for (int c = 0; c < 2; ++c) {
        hp_[c].reset(); tone_[c].reset();
        a1108In_[c].reset(); a1108Out_[c].reset();
        driver_[c].reset(); a1108_[c].reset();
    }
    for (int s = 0; s < 2; ++s) { convL_[s].reset(); convR_[s].reset(); }
}

void SpringReverb::beginBlock() {
    // Adopt a worker-finished IR at a block boundary (no allocation here:
    // both slots are pre-partitioned; this just flips the active index).
    if (swapReady_.exchange(false, std::memory_order_acq_rel)) {
        cachedActive_ = 1 - cachedActive_;
        active_.store(cachedActive_, std::memory_order_release);
    }
    // Post a regen request if the time knob moved. Same 0.2 s hysteresis as
    // the browser (which rebuilt its ConvolverNode when |dt| > 0.2); the
    // worker synthesizes + partitions off-thread, no audio-thread allocation.
    const float targetTime = 0.5f + (timeParam / 10.0f) * 4.0f;
    if (std::fabs(targetTime - requestedTime_) > 0.2f) {
        requestedTime_ = targetTime;
        reqTime_.store(targetTime, std::memory_order_relaxed);
        reqPending_.store(true, std::memory_order_release);
    }
}

void SpringReverb::updateFromParams() {
    drvGain_.set(0.05f + (drive / 10.0f) * 0.25f);
    toneFreq_.set(500.0f + (contour / 10.0f) * 5500.0f);
    for (int c = 0; c < 2; ++c)
        tone_[c].setLowpass(sr_, toneFreq_.target, 0.4);
    retGain_.set((returnGain / 5.0f) * 0.7f);
    dirty_ = false;
}

void SpringReverb::process(float inL, float inR, float& outL, float& outR) {
    if (dirty_) updateFromParams();
    const int a = cachedActive_;
    float m[2] = { hp_[0].process(inL * 0.4f), hp_[1].process(inR * 0.4f) };
    float dg = drvGain_.next();
    float cL = convL_[a].process(driver_[0].process(m[0] * dg)) * makeup_;
    float cR = convR_[a].process(driver_[1].process(m[1] * dg)) * makeup_;
    float rg = retGain_.next();
    // Browser order: makeup -> tone -> returnGain -> 1108 chain. The gain
    // sits BEFORE the saturator, so the 1108 is driven 0.7x, not 1.0x.
    float tL = tone_[0].process(cL) * rg;
    float tR = tone_[1].process(cR) * rg;
    outL = a1108Out_[0].process(a1108_[0].process(a1108In_[0].process(tL)));
    outR = a1108Out_[1].process(a1108_[1].process(a1108In_[1].process(tR)));
}

// ---------------------------------------------------------------------------
// MasterSection
// ---------------------------------------------------------------------------

void MasterSection::prepare(double sampleRate, const Curves& curves) {
    sr_ = sampleRate;
    curves_ = &curves;
    for (int c = 0; c < 2; ++c) {
        scullyIn_[c].setHighpass(sr_, 10, 0.6);
        scullyOut_[c].setLowpass(sr_, 22000, 0.5);
        scully_[c].prepare(sr_); scully_[c].setCurve(&curves_->scully);
        bus_[c].prepare(sr_);    bus_[c].setCurve(&curves_->bus);
        limiter_[c].prepare(sr_); limiter_[c].setCurve(&curves_->limiter);
        helios_[c].prepare(sr_); helios_[c].setCurve(&curves_->helios);
        tape280_[c].prepare(sr_); tape280_[c].setCurve(&curves_->tape280);
        low_[c].setLowShelf(sr_, 60, 0.5, 0);
        mid_[c].setPeaking(sr_, 1000, 1.08, 0);
        high_[c].setHighShelf(sr_, 16000, 0.4, 0); // Euphonic air band
        // Pultec-style: dip below the 60Hz bump for tightness
        pultecDip_[c].setHighpass(sr_, 28, 0.7);
        // 15IPS NAB: gentle HF rolloff, head bump handled by low shelf
        tapeEQ_[c].setLowpass(sr_, 18000, 0.5);
        subCut_[c].setHighpass(sr_, 45, 0.5);
    }
    busDriveGain_.prepare(sr_, 0.9f);
    dirty_ = true;
    reset();
}

void MasterSection::reset() {
    for (int c = 0; c < 2; ++c) {
        scullyIn_[c].reset(); scullyOut_[c].reset();
        scully_[c].reset(); bus_[c].reset(); limiter_[c].reset(); helios_[c].reset(); tape280_[c].reset();
        low_[c].reset(); mid_[c].reset(); high_[c].reset(); subCut_[c].reset();
        pultecDip_[c].reset(); tapeEQ_[c].reset();
    }
    b0_=b1_=b2_=b3_=b4_=b5_=b6_=0;
}

float MasterSection::noiseTick() {
    // Paul Kellet pink-ish filter, gain 0.0004 (-68 dB-ish)
    float white = rng_.bipolar();
    b0_ = 0.99886f * b0_ + white * 0.0555179f;
    b1_ = 0.99332f * b1_ + white * 0.0750759f;
    b2_ = 0.96900f * b2_ + white * 0.1538520f;
    b3_ = 0.86650f * b3_ + white * 0.3104856f;
    b4_ = 0.55000f * b4_ + white * 0.5329522f;
    b5_ = -0.7616f * b5_ - white * 0.0168980f;
    float n = b0_ + b1_ + b2_ + b3_ + b4_ + b5_ + b6_ + white * 0.5362f;
    n *= 0.11f;
    b6_ = white * 0.115926f;
    return n * 0.0004f;
}

void MasterSection::process(float inL, float inR, float& outL, float& outR) {
    if (dirty_) {
        static const float midFreqs[3] = {700.0f, 1000.0f, 1400.0f};
        // Helios Type 69 base Q: 3 octaves (Q~0.40) at 700Hz, narrowing to
        // 2 octaves (Q~0.67) at higher frequencies
        static const float baseQ[3] = {0.40f, 0.55f, 0.67f};
        int mf = midFreq < 0 ? 0 : (midFreq > 2 ? 2 : midFreq);
        // Proportional Q: increases with boost/cut amount (Helios behavior)
        float propQ = baseQ[mf] * (1.0f + (std::fabs(midDb) / 15.0f) * 1.2f);
        for (int c = 0; c < 2; ++c) {
            // Pultec-style low: 60Hz bump with subtle dip below for tightness
            // When boosting, the dip tightens; when cutting, it's a clean shelf
            low_[c].setLowShelf(sr_, 60, 0.5, lowDb);
            float dipGain = lowDb > 0 ? -lowDb * 0.3f : 0.0f; // Pultec trick
            pultecDip_[c].setPeaking(sr_, 35, 1.2, dipGain);
            mid_[c].setPeaking(sr_, midFreqs[mf], propQ, midDb);
            // Euphonic high: 16kHz air band, very gentle (Fairman/Retro style)
            high_[c].setHighShelf(sr_, 16000, 0.4, highDb);
        }
        dirty_ = false;
    }
    float bd = busDriveGain_.next(); // fixed 0.9 in the original
    float in[2] = {inL, inR}, out[2];
    for (int c = 0; c < 2; ++c) {
        float y = scullyIn_[c].process(in[c]);
        y = scully_[c].process(y);
        y = scullyOut_[c].process(y);
        if (phaseInvert) y = -y;
        y = low_[c].process(y);
        y = pultecDip_[c].process(y); // Pultec-style tightness below the bump
        y = mid_[c].process(y);
        // Helios shaper bypassed for CPU (EQ curve retained; saturation TBD)
        // y = helios_[c].process(y);
        y = high_[c].process(y); // Euphonic air
        y = subCut_[c].process(y);
        y *= bd;
        // Bus shaper bypassed for CPU (very subtle; Scully covers saturation)
        // y = bus_[c].process(y);
        // Scully 280 15IPS: tape EQ then saturation (the sound of hitting tape)
        y = tapeEQ_[c].process(y);
        y = tape280_[c].process(y);
        y = limiter_[c].process(y);
        // Safety: the post-clip reconstruction filter can ring a few %
        // above the brickwall; catch it here for a true ceiling.
        y = y > 0.99f ? 0.99f : (y < -0.99f ? -0.99f : y);
        out[c] = y;
    }
    outL = out[0]; outR = out[1];
}

// ---------------------------------------------------------------------------
// BiteyDsp top level
// ---------------------------------------------------------------------------

void BiteyDsp::prepare(double sampleRate) {
    sr_ = sampleRate;
    curves_ = Curves::build();
    for (auto& c : ch_) c.prepare(sr_, curves_);
    tape_.prepare(sr_, curves_);
    reverb_.prepare(sr_, curves_);
    master_.prepare(sr_, curves_);
    wetGain_.prepare(sr_, 1.0f);
    // Dry path: 4 identity 4x stages = 768 samples, exactly like the original's
    // four dryFix waveshapers (each measures 192 samples in Chromium).
    // This aligns dry with the DIRECT path (ch/scully/bus/limiter = 4 stages).
    // The tape path carries one shaper (192) plus its delay line, and the
    // reverb tail carries the uncompensated convolver latency, both exactly
    // as in the browser original.
    int dryLat = 4 * OversampledShaper::latency(); // 768
    // NOTE: the delay lines below are written then read one slot ahead,
    // so a buffer of N slots gives exactly N-1 samples of delay.
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
    // Dry path master follows the same EQ/settings (analog tone always on)
    if (p.bypass) {
        wetGain_.set(0.0f);
    } else {
        float m = p.mix / 10.0f;
        wetGain_.set(m);
    }
}

int BiteyDsp::getLatencySamples() const {
    // Report the direct/dry path latency (4 OS stages = 768). The DAW
    // compensates other tracks by this amount, keeping our dry and direct
    // signals sample-aligned. The reverb tail's convolver latency is
    // musically irrelevant (ambience, no transients), exactly as in the
    // browser original which never compensated it.
    return 4 * OversampledShaper::latency();
}

void BiteyDsp::process(float* left, float* right, int numSamples) {
    reverb_.beginBlock(); // adopt any worker-finished spring IR (block boundary)
    const float srcPad = 0.025f;
    const float mainG = master_.mainGain();
    const int dryN = int(dryDelayL_.size());

    double revSum = 0.0, outSum = 0.0; // VU meter taps (sound-neutral)

    for (int n = 0; n < numSamples; ++n) {
        float inL = left[n], inR = right[n];

        // Dry path (pre-everything, 64-sample aligned with the direct path)
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

        float nz = master_.noiseTick();
        float sumL = dirL + (tapeOut * mainG + revL) * w + nz;
        float sumR = dirR + (tapeOut * mainG + revR) * w + nz;

        float outL, outR;
        master_.process(sumL, sumR, outL, outR);

        left[n] = outL;
        right[n] = outR;
        revSum += double(revL) * revL + double(revR) * revR;
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
