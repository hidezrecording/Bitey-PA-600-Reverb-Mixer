// Bitey PA-200 0.3.2-TEST1 refinement-round tests (section 7 updated
// for 0.3.2-TEST2: HL-A reverted, asserts the shipped 0.3.1 law).
//
// Targeted checks for the refinement proposals (OP6-A/B, SC-A/B,
// SR-A/B/C, HL-A, TS-A/C, CC-A) plus the invariants they must not
// break: reported latency 75, per-stage oversampler latency 25,
// rest-state transparency. DSP-level (no JUCE), like the race test.
//
// Build:
//   g++ -std=c++17 -O2 tests/test_refinements.cpp dsp/BiteyDsp.cpp \
//       -o build/test_refinements -lpthread

#include "../dsp/BiteyDsp.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>

using namespace bitey;

static int failures = 0;
static void check(bool ok, const char* name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
static void info(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::printf("  (");
    std::vprintf(fmt, ap);
    std::printf(")\n");
    va_end(ap);
}

static const double SR = 44100.0;

// DFT band energy of a steady segment (for foldback measurement).
static double bandEnergy(const std::vector<float>& x, int start, double f0, double f1) {
    double e = 0;
    const int n = int(x.size()) - start;
    for (double f = f0; f <= f1; f += 50.0) {
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i) {
            double ph = 2 * M_PI * f * i / SR;
            re += x[start + i] * std::cos(ph);
            im -= x[start + i] * std::sin(ph);
        }
        e += re * re + im * im;
    }
    return e;
}

static double rmsOf(const std::vector<float>& x, int i0, int i1) {
    double s = 0; int n = 0;
    for (int i = i0; i < i1 && i < int(x.size()); ++i) { s += double(x[i]) * x[i]; ++n; }
    return n ? std::sqrt(s / n) : 0;
}

int main() {
    Curves curves = Curves::build();

    // ---------------------------------------------------------------
    // 1. Reported latency unchanged (CC-A must not move it).
    {
        BiteyDsp dsp; dsp.prepare(SR);
        check(dsp.getLatencySamples() == 75, "getLatencySamples() == 75");
    }

    // ---------------------------------------------------------------
    // 2. CC-A: 4x/93-tap oversampler — same 25-sample stage latency,
    //    transparent with a linear curve, less foldback than 2x.
    {
        Curve lin = Curve::make(32768, [](float x) { return x; }, 4.0f);
        auto impulseDelay = [&](int factor, int taps) {
            OversampledShaper sh;
            sh.setOsFactor(factor); sh.setTaps(taps);
            sh.prepare(SR); sh.setCurve(&lin);
            int peakAt = -1; float peak = 0;
            for (int i = 0; i < 200; ++i) {
                float y = sh.process(i == 0 ? 1.0f : 0.0f);
                if (std::fabs(y) > peak) { peak = std::fabs(y); peakAt = i; }
            }
            return peakAt;
        };
        int d2 = impulseDelay(2, 47), d4 = impulseDelay(4, 61);
        info("OS impulse peak at %d (2x) / %d (4x)", d2, d4);
        check(d2 == 25 && d4 == 25, "CC-A: stage latency 25 at 2x and 4x");

        auto foldback = [&](int factor, int taps) {
            OversampledShaper sh;
            sh.setOsFactor(factor); sh.setTaps(taps);
            sh.prepare(SR); sh.setCurve(&curves.op6a);
            std::vector<float> out((size_t)(SR));
            for (int i = 0; i < int(SR); ++i)
                out[i] = sh.process(2.0f * std::sin(2 * M_PI * 15000.0 * i / SR));
            // Aliased products of a 15 kHz drive land at 0.9k, 1.8k, ...
            return bandEnergy(out, int(SR / 2), 300.0, 6000.0);
        };
        double e2 = foldback(2, 47), e4 = foldback(4, 61);
        info("foldback energy 2x %.3e vs 4x %.3e (%.1f dB better)",
             e2, e4, 10 * std::log10(e2 / (e4 + 1e-20)));
        check(e4 < e2 * 0.55, "CC-A: 4x foldback energy roughly halved vs 2x");
    }

    // ---------------------------------------------------------------
    // 3. OP6-B: slow bias. The deepen path scales the curve input and
    //    divides the output back, so its signature is DISTORTION, not
    //    gain: after a sustained loud passage + a short silence (fast
    //    env fully decayed), a mid-level probe still reads hotter
    //    with slowBias on than off; off returns to its rest THD.
    {
        auto probeThd = [&](bool slow, bool afterLoud) {
            BiasedShaper bs;
            bs.prepare(SR); bs.setCurve(&curves.op6a);
            bs.slowBias = slow; bs.biasDepth = 0.6f;
            if (afterLoud) {
                for (int i = 0; i < int(SR * 2); ++i) // 2 s loud
                    bs.process(0.9f * std::sin(2 * M_PI * 440.0 * i / SR));
                for (int i = 0; i < int(SR * 0.3); ++i) bs.process(0.0f);
            }
            // Probe: 0.6-amp 490 Hz, integer cycles; THD h2..8.
            const int N = 22050; // 0.5 s = 245 cycles at 490 Hz
            std::vector<float> y(N);
            for (int i = 0; i < N; ++i)
                y[i] = bs.process(0.6f * std::sin(2 * M_PI * 490.0 * i / SR));
            double mag[9] = {0};
            for (int h = 1; h <= 8; ++h) {
                double re = 0, im = 0;
                for (int i = N / 2; i < N; ++i) {
                    double ph = 2 * M_PI * 490.0 * h * i / SR;
                    re += y[i] * std::cos(ph); im -= y[i] * std::sin(ph);
                }
                mag[h] = std::sqrt(re * re + im * im);
            }
            double harm = 0; for (int h = 2; h <= 8; ++h) harm += mag[h] * mag[h];
            return 100.0 * std::sqrt(harm) / mag[1];
        };
        double restOff = probeThd(false, false);
        double afterOff = probeThd(false, true);
        double afterOn = probeThd(true, true);
        info("probe THD rest %.3f%%, after loud (slow off) %.3f%%, after loud (slow on) %.3f%%",
             restOff, afterOff, afterOn);
        check(afterOn > afterOff * 1.03, "OP6-B: slow bias keeps the stage hotter after loud passages");
        check(std::fabs(afterOff - restOff) < restOff * 0.15 + 0.02,
              "OP6-B: fast-only stage returns to rest THD after silence");
    }

    // ---------------------------------------------------------------
    // 4. OP6-A: LEVEL knob coupling, isolated from the drive law.
    //    The burst/probe INPUT amplitudes are scaled per level so the
    //    OP-6 stage sees identical material at every knob position
    //    (input = target / drive(level)); the only remaining
    //    level-dependent term is the coupling itself — stage A's
    //    bias depth (0.46 / 0.60 / 0.84 at L2 / L5 / L10). A 440 Hz
    //    probe keeps OP6-2's darkening (a >=14 kHz effect) out of
    //    the measurement. Post-burst probe THD must grow with LEVEL.
    {
        auto thdAt = [&](float level) {
            const double drive = std::pow(level / 5.0, 2.4) * 10.0; // pad 0 -> x1
            ChannelStrip ch;
            ch.prepare(SR, curves);
            ch.level = level; ch.pad = 0; ch.touch();
            auto feed = [&](double stageIn, double sec) {
                const int n = int(SR * sec);
                std::vector<float> out(n);
                for (int i = 0; i < n; ++i)
                    out[i] = ch.process(float(stageIn / drive)
                                        * std::sin(2 * M_PI * 490.0 * i / SR));
                return out;
            };
            feed(3.0, 0.7);                    // burst (stage-in 3.0)
            auto probe = feed(0.5, 0.4);       // probe (stage-in 0.5)
            const int N = int(probe.size());
            double mag[9] = {0};
            for (int h = 1; h <= 8; ++h) {
                double re = 0, im = 0;
                for (int i = N / 2; i < N; ++i) {
                    double ph = 2 * M_PI * 490.0 * h * i / SR;
                    re += probe[i] * std::cos(ph); im -= probe[i] * std::sin(ph);
                }
                mag[h] = std::sqrt(re * re + im * im);
            }
            double harm = 0; for (int h = 2; h <= 8; ++h) harm += mag[h] * mag[h];
            return 100.0 * std::sqrt(harm) / mag[1];
        };
        double t2 = thdAt(2.0f), t5 = thdAt(5.0f), t10 = thdAt(10.0f);
        info("post-burst probe THD at matched stage drive: L2 %.3f%% L5 %.3f%% L10 %.3f%%",
             t2, t5, t10);
        check(t10 > t5 * 1.01 && t5 > t2 * 1.01,
              "OP6-A: bias response grows with LEVEL at matched drive");
    }

    // ---------------------------------------------------------------
    // 5. SC-B: Scully composite calibration (measured targets from
    //    the shipped curve: gain 1.1764; THD 1.13% @ a=0.5).
    {
        auto sineGain = [&](const Curve& c, float a) {
            const int N = 16384; double re = 0, im = 0;
            for (int i = 0; i < N; ++i) {
                double ph = 2 * M_PI * i / N;
                double v = c.process(float(a * std::sin(ph)));
                re += v * std::cos(ph); im -= v * std::sin(ph);
            }
            return std::sqrt(re * re + im * im) * 2.0 / N / a;
        };
        auto thd = [&](const Curve& c, float a) {
            const int N = 16384; double mag[17] = {0};
            for (int h = 1; h <= 16; ++h) {
                double re = 0, im = 0;
                for (int i = 0; i < N; ++i) {
                    double v = c.process(float(a * std::sin(2 * M_PI * i / N)));
                    double ph = 2 * M_PI * i * h / N;
                    re += v * std::cos(ph); im -= v * std::sin(ph);
                }
                mag[h] = std::sqrt(re * re + im * im);
            }
            double harm = 0; for (int h = 2; h <= 16; ++h) harm += mag[h] * mag[h];
            return 100.0 * std::sqrt(harm) / mag[1];
        };
        double g = sineGain(curves.scully, 0.01f);
        double t05 = thd(curves.scully, 0.5f), t10 = thd(curves.scully, 1.0f);
        info("scully gain %.5f THD@0.5 %.3f%% THD@1.0 %.3f%%", g, t05, t10);
        check(std::fabs(g - 1.17642) < 0.002, "SC-B: small-signal gain preserved");
        check(std::fabs(t05 - 1.125) < 0.06, "SC-B: THD at nominal (a=0.5) preserved");
        check(t10 > 4.5 && t10 < 7.5, "SC-B: THD at a=1.0 in the shipped family");
    }

    // ---------------------------------------------------------------
    // 6. SC-A: rest state nearly flat at 50 Hz; under sustained LF
    //    load the 50 Hz gain falls (flux corner rises).
    {
        auto gainAt = [&](double freq, float amp, double loudSec) {
            MasterSection ms;
            ms.prepare(SR, curves);
            for (int i = 0; i < int(SR * loudSec); ++i) {
                float o1, o2;
                float x = 0.9f * std::sin(2 * M_PI * 55.0 * i / SR);
                ms.process(x, x, o1, o2);
            }
            double in = 0, out = 0;
            // Probe IMMEDIATELY after the load stops: the flux corner
            // relaxes in ~100 ms, so measure the first ~100 ms of the
            // probe (after 35 ms of filter settling).
            const int n = int(SR * 0.14);
            for (int i = 0; i < n; ++i) {
                float x = amp * std::sin(2 * M_PI * freq * i / SR);
                float o1, o2;
                ms.process(x, x, o1, o2);
                if (i > 1500) { in += x * x; out += o1 * o1; }
            }
            return std::sqrt(out / in);
        };
        double rest50 = gainAt(50, 0.02f, 0.0), rest1k = gainAt(1000, 0.02f, 0.0);
        // The flux corner lives at 6-30 Hz; its load effect is
        // printed at 20 Hz for the before/after comparison (the
        // section's own bias-memory gain change rides along in BOTH
        // builds, so the SC-A term is read differentially — see the
        // verification log; the in-binary guard is the rest shape).
        double rest20 = gainAt(20, 0.02f, 0.0), load20 = gainAt(20, 0.02f, 1.5);
        info("master 50 Hz rest %.4f, 1 kHz rest %.4f, 20 Hz rest %.4f, 20 Hz after LF load %.4f",
             rest50, rest1k, rest20, load20);
        check(std::fabs(20 * std::log10(rest50 / rest1k)) < 0.85,
              "SC-A: rest LF shape sane (no gross resting-corner error)");
    }

    // ---------------------------------------------------------------
    // 7. HL-A REVERTED (0.3.2-TEST2): the Helios detector law is the
    //    shipped 0.3.1 law (band energy only — HL-2 compression from
    //    in-band level, no boost-amount tracking). With a +10 mid
    //    boost, the settled effective boost at a working level must
    //    match the shipped build (~9.4 dB), i.e. measurably ABOVE
    //    the 8.13 dB the TEST1 boost-aware law produced at the same
    //    settings, while HL-2's bounded compression still applies
    //    (settled boost stays below the +10 knob value).
    {
        auto settledGain = [&](float midDb) {
            MasterSection ms;
            ms.prepare(SR, curves);
            ms.midDb = midDb; ms.midFreq = 1; ms.touch();
            double in = 0, out = 0;
            const int n = int(SR * 2);
            for (int i = 0; i < n; ++i) {
                float x = 0.30f * std::sin(2 * M_PI * 1000.0 * i / SR);
                float o1, o2;
                ms.process(x, x, o1, o2);
                if (i > n - int(SR / 2)) { in += x * x; out += o1 * o1; }
            }
            return std::sqrt(out / in);
        };
        double boostDb = 20 * std::log10(settledGain(10.0f) / settledGain(0.0f));
        info("settled effective mid boost at 0.30 amp: %.2f dB (knob +10)", boostDb);
        check(boostDb > 9.15 && boostDb < 9.80, "TEST2: HL-A reverted - settled boost matches shipped 0.3.1 law (~9.4 dB)");
        check(boostDb < 10.0, "TEST2: HL-2 bounded compression still applies (settled boost below knob value)");
    }

    // ---------------------------------------------------------------
    // 8. SR-B/SR-C: soft hits keep the shipped character (tail
    //    present at the expected scale), hard hits excite a denser,
    //    brighter tail. Brightness = band-energy ratio (0.9-2.8 kHz
    //    over 150-600 Hz) of the early tail; density = fraction of
    //    10 ms blocks in the first 300 ms whose RMS exceeds 40% of
    //    the window's loudest block.
    {
        SpringReverb rev;
        rev.prepare(SR, curves);
        rev.beginBlock();
        auto tail = [&](float amp, double& bright, double& density) {
            rev.reset(); rev.beginBlock();
            std::vector<float> outL(int(SR * 2), 0), outR(int(SR * 2), 0);
            for (int i = 0; i < int(SR * 2); ++i) {
                float o1, o2;
                // 30 ms burst: a musical "hit" that can charge the
                // SR-B driver-input envelope (8 ms attack).
                const float x = (i >= 100 && i < 100 + 1323)
                    ? amp * std::sin(2 * M_PI * 220.0 * (i - 100) / SR) : 0.0f;
                rev.process(x, x, o1, o2);
                outL[i] = o1; outR[i] = o2;
            }
            const int i0 = int(SR * 0.03), i1 = int(SR * 0.35);
            auto seg = [&](double f0, double f1) {
                double e = 0;
                for (double f = f0; f <= f1; f += 25.0) {
                    double re = 0, im = 0;
                    for (int i = i0; i < i1; ++i) {
                        double ph = 2 * M_PI * f * (i - i0) / SR;
                        re += outL[i] * std::cos(ph); im -= outL[i] * std::sin(ph);
                    }
                    e += re * re + im * im;
                }
                return e;
            };
            bright = seg(900, 2800) / (seg(150, 600) + 1e-20);
            const int B = 441, nb = int(SR * 0.3) / B;
            double mx = 0; std::vector<double> br(nb);
            for (int b = 0; b < nb; ++b) {
                br[b] = rmsOf(outL, int(SR * 0.05) + b * B, int(SR * 0.05) + (b + 1) * B);
                mx = std::max(mx, br[b]);
            }
            int above = 0;
            for (double v : br) if (v > 0.4 * mx) ++above;
            density = nb ? double(above) / nb : 0;
            return rmsOf(outL, int(SR * 0.3), int(SR * 1.5));
        };
        double brSoft, deSoft, brHard, deHard;
        double eSoft = tail(0.25f, brSoft, deSoft);
        double eHard = tail(3.0f, brHard, deHard);
        info("reverb tail rms soft %.5f hard %.5f; brightness soft %.3f hard %.3f; density soft %.2f hard %.2f",
             eSoft, eHard, brSoft, brHard, deSoft, deHard);
        check(eSoft > 0.0005, "SR-B: soft impulse tail present");
        check(deHard > deSoft + 0.05, "SR-B: hard hit tail is denser");
        // IR-level: the hard set itself must carry the energy — much
        // denser early field at at-least-equal brightness (share of
        // the >2.5 kHz band in the first 300 ms).
        auto irEarly = [&](bool hard, double& rmsOut) {
            std::vector<float> L, R;
            SpringIR::generate(2.5f, float(SR), L, R, 0xBA600u, hard);
            const float a250 = 1.0f - poleForCorner(250.0f, SR);
            const float a2500 = 1.0f - poleForCorner(2500.0f, SR);
            float l1 = 0, l2 = 0; double sT = 0, sH = 0, sR = 0;
            for (int i = 0; i < int(0.3 * SR); ++i) {
                float x = L[i];
                l1 += (x - l1) * a250; l2 += (x - l2) * a2500;
                float hf = x - l2;
                sT += double(x) * x; sH += double(hf) * hf; sR += double(x) * x;
            }
            rmsOut = std::sqrt(sR / int(0.3 * SR));
            return sT > 0 ? sH / sT : 0;
        };
        double rmsS, rmsH;
        double hfS = irEarly(false, rmsS), hfH = irEarly(true, rmsH);
        info("hard IR early: RMS %.5f vs soft %.5f; HF share %.3f vs %.3f",
             rmsH, rmsS, hfH, hfS);
        check(rmsH > rmsS * 1.35, "SR-B: hard IR early field is much more energetic");
        check(hfH > hfS - 0.01, "SR-B: hard IR is at least as bright as soft");
    }

    // ---------------------------------------------------------------
    // 9. TS-A/TS-C: slap is deterministic across instances, peak stays
    //    at the calibrated 134 ms, steady-state level unchanged in
    //    character (finite, bounded, alive).
    {
        auto renderSlap = [&](bool burstFirst) {
            TapeSlap ts;
            ts.prepare(SR, curves);
            ts.mix = 8.0f; ts.speed = 0; ts.size = 0; ts.touch();
            std::vector<float> out(int(SR * 1.5), 0);
            for (int i = 0; i < int(SR * 1.5); ++i) {
                float x = 0.0f;
                if (burstFirst && i < int(SR * 0.4))
                    x = 0.8f * std::sin(2 * M_PI * 220.0 * i / SR);
                if (i == int(SR * 0.6)) x = 1.0f;
                if (!burstFirst && i == 100) x = 1.0f;
                out[i] = ts.process(x);
            }
            return out;
        };
        auto a = renderSlap(false), b = renderSlap(false);
        double err = 0;
        for (size_t i = 0; i < a.size(); ++i) err = std::max(err, double(std::fabs(a[i] - b[i])));
        check(err == 0.0, "TS-A: deterministic across instances (seeded transport noise)");
        // slap peak near 134 ms after the t=100 impulse
        int pk = 0; float pv = 0;
        for (int i = 100; i < int(SR * 0.5); ++i)
            if (std::fabs(a[i]) > pv) { pv = std::fabs(a[i]); pk = i; }
        double ms = (pk - 100) / SR * 1000.0;
        info("slap peak at %.1f ms", ms);
        check(ms > 119 && ms < 149, "TS-A: slap peak within +/-15 ms of 134 ms");
        bool finite = true;
        for (float v : a) finite = finite && std::isfinite(v);
        check(finite && pv < 1.5f, "TS-C: slap output finite and bounded");
    }

    std::printf(failures == 0 ? "ALL REFINEMENT TESTS PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
