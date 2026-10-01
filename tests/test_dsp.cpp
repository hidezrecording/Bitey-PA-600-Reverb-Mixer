// Bitey PA-600 DSP test harness.
// Runs the ported PA-600 engine through invariant checks and renders
// dry/wet demo WAVs.
#include "../dsp/BiteyDsp.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <vector>

using namespace bitey;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS  %s\n", msg); } \
    else { printf("  FAIL  %s\n", msg); ++failures; } \
} while (0)

static bool hasNonFinite(const std::vector<float>& v) {
    for (float x : v) if (!std::isfinite(x)) return true;
    return false;
}
static float peakAbs(const std::vector<float>& v) {
    float p = 0; for (float x : v) p = std::max(p, std::fabs(x)); return p;
}
static float rms(const std::vector<float>& v) {
    double s = 0; for (float x : v) s += x * x; return float(std::sqrt(s / v.size()));
}

// Musical-ish test signal: picked notes + strum transient, stereo.
static void makeLoop(std::vector<float>& L, std::vector<float>& R, int sr) {
    int n = int(sr * 3.0);
    L.assign(n, 0); R.assign(n, 0);
    const float notes[] = {110, 138.59f, 164.81f, 220, 164.81f, 138.59f};
    for (int k = 0; k < 6; ++k) {
        int start = int(k * sr * 0.45);
        float f = notes[k];
        int len = int(sr * 0.9);
        for (int i = 0; i < len && start + i < n; ++i) {
            float env = std::exp(-i / (sr * 0.25f));
            float s = std::sin(2 * 3.14159265f * f * i / sr)
                    + 0.4f * std::sin(2 * 3.14159265f * f * 2 * i / sr)
                    + 0.15f * std::sin(2 * 3.14159265f * f * 3.01f * i / sr);
            s *= env * 0.5f;
            L[start + i] += s;
            R[start + i] += s * (k % 2 ? 0.8f : 1.0f);
        }
    }
    // strum transient
    for (int i = 0; i < 2000; ++i) {
        float s = (std::sin(i * 12.9f) * 0.5f + std::sin(i * 3.7f) * 0.5f)
                * std::exp(-i / 300.0f) * 0.6f;
        L[i] += s; R[i] += s * 0.9f;
    }
}

static void writeWav(const char* path, const std::vector<float>& L,
                     const std::vector<float>& R, int sr) {
    FILE* f = std::fopen(path, "wb");
    int n = int(L.size());
    uint32_t dataBytes = n * 2 * 2;
    uint32_t riffSize = 36 + dataBytes;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riffSize, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtLen = 16; std::fwrite(&fmtLen, 4, 1, f);
    uint16_t fmt = 1, ch = 2; std::fwrite(&fmt, 2, 1, f); std::fwrite(&ch, 2, 1, f);
    uint32_t u32 = sr; std::fwrite(&u32, 4, 1, f);
    u32 = sr * 4; std::fwrite(&u32, 4, 1, f);
    uint16_t u16 = 4; std::fwrite(&u16, 2, 1, f);
    u16 = 16; std::fwrite(&u16, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&dataBytes, 4, 1, f);
    for (int i = 0; i < n; ++i) {
        float a = std::max(-1.0f, std::min(1.0f, L[i]));
        float b = std::max(-1.0f, std::min(1.0f, R[i]));
        int16_t sa = int16_t(std::lrint(a * 32767)), sb = int16_t(std::lrint(b * 32767));
        std::fwrite(&sa, 2, 1, f); std::fwrite(&sb, 2, 1, f);
    }
    std::fclose(f);
}

static void runThrough(BiteyDsp& dsp, std::vector<float>& L, std::vector<float>& R) {
    const int block = 512;
    int n = int(L.size());
    for (int i = 0; i < n; i += block)
        dsp.process(L.data() + i, R.data() + i, std::min(block, n - i));
}

int main() {
    const int sr = 44100;
    printf("Bitey PA-600 DSP tests @ %d Hz\n", sr);

    std::vector<float> dryL, dryR;
    makeLoop(dryL, dryR, sr);

    // 1. Default settings: finite, bounded, alive.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; dsp.setParams(p);
        auto t0 = std::chrono::steady_clock::now();
        std::vector<float> L = dryL, R = dryR;
        runThrough(dsp, L, R);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        printf("  (3 s render took %lld ms)\n", (long long)ms);
        CHECK(!hasNonFinite(L) && !hasNonFinite(R), "default: no NaN/Inf");
        CHECK(peakAbs(L) <= 1.05f && peakAbs(R) <= 1.05f, "default: output ceiling holds");
        CHECK(rms(L) > 0.005f, "default: output is alive");
        printf("  (default wet RMS %.4f, peak %.3f)\n", rms(L), peakAbs(L));
        writeWav("demo/wet.wav", L, R, sr);
        writeWav("demo/dry.wav", dryL, dryR, sr);
        printf("  wrote demo/dry.wav and demo/wet.wav\n");
    }

    // 2. Extreme settings: finite + bounded.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p;
        p.ch[0].level = 10; p.ch[0].pad = 2; p.ch[0].lowDb = 15; p.ch[0].highDb = 15;
        p.ch[0].fxSend = 10; p.ch[0].lowCut = true;
        p.ch[1] = p.ch[0];
        p.tapeMix = 10; p.tapeSpeed = 2; p.tapeSize = 2;
        p.revTime = 10; p.revDrive = 10; p.revContour = 10; p.revReturn = 10;
        p.mainLevel = 10; p.mLowDb = 15; p.mMidDb = 15; p.mHighDb = 15;
        p.mix = 10;
        dsp.setParams(p);
        std::vector<float> L = dryL, R = dryR;
        // hot input too
        for (auto& x : L) x *= 2.0f;
        for (auto& x : R) x *= 2.0f;
        runThrough(dsp, L, R);
        float pkE = peakAbs(L);
        printf("  (extreme peak %.3f)\n", pkE);
        CHECK(!hasNonFinite(L) && !hasNonFinite(R), "extreme: no NaN/Inf");
        CHECK(pkE <= 1.05f && peakAbs(R) <= 1.05f, "extreme: limiter ceiling holds");
    }

    // 3. Bypass (power off) ~= dry, delayed by the dry-path latency (80).
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; p.bypass = true; dsp.setParams(p);
        std::vector<float> L = dryL, R = dryR;
        runThrough(dsp, L, R);
        // NB: bypass engages with a ~50 ms glide (click-free); compare after it settles.
        double err = 0; int cnt = 0;
        for (size_t i = 70000; i + 64 < L.size() && i < 110000; ++i) {
            err += std::fabs(L[i + 64] - dryL[i]); ++cnt;
        }
        err /= cnt;
        CHECK(err < 1e-4, "bypass: output == dry delayed 64 samples");
        printf("  (bypass mean abs err %.2e)\n", err);
    }

    // 4. Mix = 0 behaves like bypass.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; p.mix = 0; dsp.setParams(p);
        std::vector<float> L = dryL, R = dryR;
        runThrough(dsp, L, R);
        double err = 0; int cnt = 0;
        for (size_t i = 70000; i + 64 < L.size() && i < 110000; ++i) {
            err += std::fabs(L[i + 64] - dryL[i]); ++cnt;
        }
        err /= cnt;
        CHECK(err < 1e-4, "mix=0: output == dry");
    }

    // 5. Reverb tail: isolate the reverb (return=5 minus return=0) and
    // measure its own -40 dB tail.
    {
        int n = sr * 5;
        std::vector<float> L(n, 0), R(n, 0), L0(n, 0), R0(n, 0);
        L[100] = 1.0f; R[100] = 1.0f;
        L0[100] = 1.0f; R0[100] = 1.0f;
        { BiteyDsp dsp; dsp.prepare(sr); BiteyParams p; dsp.setParams(p);
          runThrough(dsp, L, R); }
        { BiteyDsp dsp; dsp.prepare(sr); BiteyParams p; p.revReturn = 0; dsp.setParams(p);
          runThrough(dsp, L0, R0); }
        CHECK(!hasNonFinite(L), "reverb: no NaN/Inf");
        for (int i = 0; i < n; ++i) L[i] -= L0[i]; // reverb-only
        float pk = peakAbs(L);
        int last = 0;
        for (int i = 0; i < n; ++i)
            if (std::fabs(L[i]) > pk * 0.01f) last = i;
        float tailSec = (last - 100) / float(sr);
        printf("  (isolated reverb -40 dB tail: %.2f s, rel peak %.4f)\n", tailSec, pk);
        CHECK(tailSec > 1.0f && tailSec < 3.5f, "reverb tail in plausible range");
        CHECK(pk > 1e-4f, "reverb return contributes audible energy");
    }

    // 6. Tape slap: with tapeMix up, a repeat appears near the ips delay.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; p.tapeMix = 10; p.revReturn = 0; dsp.setParams(p);
        int n = sr * 2;
        std::vector<float> L(n, 0), R(n, 0);
        L[1000] = 1.0f; R[1000] = 1.0f;
        runThrough(dsp, L, R);
        // subtract the tape-less version to isolate the slap
        BiteyDsp dsp0; dsp0.prepare(sr);
        BiteyParams p0; p0.tapeMix = 0; p0.revReturn = 0; dsp0.setParams(p0);
        std::vector<float> L0(n, 0), R0(n, 0);
        L0[1000] = 1.0f; R0[1000] = 1.0f;
        runThrough(dsp0, L0, R0);
        int peakIdx = 0; float peakV = 0;
        for (int i = 2000; i < n; ++i) {
            float v = std::fabs(L[i] - L0[i]);
            if (v > peakV) { peakV = v; peakIdx = i; }
        }
        float slapMs = (peakIdx - 1000) / float(sr) * 1000.0f;
        printf("  (tape slap at %.1f ms, expected ~134 ms @7.5ips)\n", slapMs);
        CHECK(std::fabs(slapMs - 134.0f) < 40.0f, "tape slap timing ~= 134 ms");
    }

    // 7. Level knob actually drives the preamp.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; p.mix = 10; p.revReturn = 0; p.ch[0].level = 2; p.ch[1].level = 2;
        dsp.setParams(p);
        std::vector<float> L = dryL, R = dryR; runThrough(dsp, L, R);
        float rLo = rms(L);
        BiteyDsp dsp2; dsp2.prepare(sr);
        BiteyParams p2 = p; p2.ch[0].level = 9; p2.ch[1].level = 9; dsp2.setParams(p2);
        std::vector<float> L2 = dryL, R2 = dryR; runThrough(dsp2, L2, R2);
        printf("  (level 2 RMS %.4f vs level 9 RMS %.4f)\n", rLo, rms(L2));
        CHECK(rms(L2) > rLo * 1.5f, "level knob drives output");
    }

    // 8. Determinism: two fresh instances, identical output (seeded IR).
    {
        BiteyDsp a, b; a.prepare(sr); b.prepare(sr);
        BiteyParams p; a.setParams(p); b.setParams(p);
        std::vector<float> L1 = dryL, R1 = dryR, L2 = dryL, R2 = dryR;
        runThrough(a, L1, R1); runThrough(b, L2, R2);
        double err = 0;
        for (size_t i = 0; i < L1.size(); ++i) err += std::fabs(L1[i] - L2[i]);
        err /= L1.size();
        CHECK(err == 0.0, "deterministic across instances");
    }

    // 9. Reverb time change regenerates the IR without exploding.
    {
        BiteyDsp dsp; dsp.prepare(sr);
        BiteyParams p; dsp.setParams(p);
        std::vector<float> L = dryL, R = dryR; runThrough(dsp, L, R);
        BiteyParams p2 = p; p2.revTime = 9; dsp.setParams(p2);
        std::vector<float> L2 = dryL, R2 = dryR; runThrough(dsp, L2, R2);
        CHECK(!hasNonFinite(L2) && peakAbs(L2) <= 1.05f, "revTime change: stable");
    }

    // 10. Reported latency == dry/direct path (4 OS stages = 64).
    {
        BiteyDsp dsp; dsp.prepare(sr);
        CHECK(dsp.getLatencySamples() == 64, "getLatencySamples() == 64");
    }

    printf(failures ? "\n%d FAILURES\n" : "\nALL TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
