// Bitey PA-600 convolver race stress test.
//
// Reproduces the LUNA crash scenario (EXC_BAD_ACCESS on the render thread in
// PartitionedConvolver::process while SpringReverb::workerMain ran
// PartitionedConvolver::setIR): rapid TIME-knob changes and sample-rate
// re-prepares racing the audio thread, plus plugin destruction while the
// worker is mid-build.
//
//   Thread A (audio):  changes reverb.timeParam, beginBlock(), process()
//                      random 32..2048-sample blocks of sine+noise.
//   Thread B (UI):     reverb.prepare() alternating 44100/48000/96000
//                      every ~50 ms (host sample-rate change racing audio).
//   Thread C (host):   destroys + recreates the SpringReverb every ~2 s
//                      while A/B run (worker teardown racing audio).
//
// Pass criteria: no crash, no hang, finite (non-NaN/Inf) output throughout.
// Run plain, then under -fsanitize=thread and -fsanitize=address; both
// sanitizer runs must report zero issues.

#include "../dsp/BiteyDsp.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace bitey;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS  %s\n", msg); } \
    else { printf("  FAIL  %s\n", msg); ++failures; } \
} while (0)

static std::mutex g_mu; // protects g_inst
static std::shared_ptr<SpringReverb> g_inst;
static Curves g_curves; // built once; shared by every prepare (deterministic)
static std::atomic<bool> g_stop{ false };
static std::atomic<long> g_blocks{ 0 };
static std::atomic<long> g_nonFinite{ 0 };

static std::shared_ptr<SpringReverb> acquireInstance() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_inst;
}

static void publishInstance(std::shared_ptr<SpringReverb> p) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_inst = std::move(p);
}

// Thread A: the "render thread". Random timeParam (atomic), block-boundary
// handoff, variable block sizes, finite-output assertion per block.
static void audioThread() {
    std::mt19937 rng(0xA91D0u);
    std::uniform_int_distribution<int> blockDist(32, 2048);
    std::uniform_real_distribution<float> paramDist(0.0f, 10.0f);
    std::uniform_real_distribution<float> noiseDist(-0.5f, 0.5f);
    double phase = 0.0;

    while (!g_stop.load()) {
        auto rev = acquireInstance();
        if (!rev) { std::this_thread::yield(); continue; }

        // Whip the TIME knob: posts regen requests with 0.2 s hysteresis.
        rev->timeParam.store(paramDist(rng));

        const int n = blockDist(rng);
        rev->beginBlock();
        for (int i = 0; i < n; ++i) {
            phase += 2.0 * 3.141592653589793 / 44100.0 * 220.0;
            const float in = float(std::sin(phase) * 0.4 + noiseDist(rng));
            float oL = 0, oR = 0;
            rev->process(in, in * 0.9f, oL, oR);
            if (!std::isfinite(oL) || !std::isfinite(oR))
                g_nonFinite.fetch_add(1);
        }
        g_blocks.fetch_add(1);
    }
}

// Thread B: the "UI/host thread". Sample-rate re-prepare racing audio.
static void uiThread() {
    const double rates[] = { 44100.0, 48000.0, 96000.0 };
    int idx = 0;
    while (!g_stop.load()) {
        auto rev = acquireInstance();
        if (rev) {
            rev->prepare(rates[idx % 3], g_curves);
            idx++;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// Thread C: the "host teardown thread". Destroy + recreate the whole
// SpringReverb (joining its worker mid-build) while A/B keep running.
static void lifecycleThread() {
    const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
    int idx = 0;
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        if (g_stop.load()) break;
        auto fresh = std::make_shared<SpringReverb>();
        fresh->prepare(rates[idx % 4], g_curves); // worker starts here
        idx++;
        publishInstance(std::move(fresh)); // old instance dies when thread A's
                                           // in-flight block releases it
    }
}

// Deterministic single-threaded check: hammer the state machine with rapid
// time changes and block boundaries; output must stay finite.
static void deterministicChurn() {
    SpringReverb rev;
    rev.prepare(48000.0, g_curves);
    std::mt19937 rng(1234u);
    std::uniform_real_distribution<float> paramDist(0.0f, 10.0f);
    long bad = 0;
    for (int k = 0; k < 60; ++k) {
        rev.timeParam.store(paramDist(rng));
        rev.beginBlock();
        for (int i = 0; i < 512; ++i) {
            float oL = 0, oR = 0;
            rev.process(0.3f, -0.2f, oL, oR);
            if (!std::isfinite(oL) || !std::isfinite(oR)) bad++;
        }
    }
    // Let any in-flight worker build finish, then process some more.
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    rev.beginBlock();
    for (int i = 0; i < 2048; ++i) {
        float oL = 0, oR = 0;
        rev.process(0.3f, -0.2f, oL, oR);
        if (!std::isfinite(oL) || !std::isfinite(oR)) bad++;
    }
    CHECK(bad == 0, "deterministic churn: finite output");
}

// Defensive guard check: an empty tail IR must not crash process().
static void emptyTailGuard() {
    ZeroLatencyConvolver c;
    c.prepare(44100.0);
    const float empty[1] = { 0.0f };
    c.setIR(empty, 0); // tail gets a zero-length IR -> numParts_ == 0
    bool bad = false;
    for (int i = 0; i < 1200; ++i) {
        const float y = c.process(0.5f);
        if (!std::isfinite(y)) bad = true;
    }
    CHECK(!bad, "empty tail IR: no crash, finite output");
}

int main() {
    printf("== Bitey convolver race stress test ==\n");
    g_curves = Curves::build();

    emptyTailGuard();
    deterministicChurn();

    printf("-- 3-thread race (audio + re-prepare + destroy/recreate) --\n");
    {
        auto first = std::make_shared<SpringReverb>();
        first->prepare(44100.0, g_curves);
        publishInstance(std::move(first));
    }

    std::thread a(audioThread);
    std::thread b(uiThread);
    std::thread c(lifecycleThread);

    // ~12 s wall clock: enough for dozens of worker IR builds, hundreds of
    // re-prepares racing audio, and several destroy/recreate cycles.
    std::this_thread::sleep_for(std::chrono::milliseconds(12000));
    g_stop.store(true);
    a.join(); b.join(); c.join();

    const long blocks = g_blocks.load();
    const long bad = g_nonFinite.load();
    printf("   blocks processed: %ld, non-finite samples: %ld\n", blocks, bad);
    CHECK(blocks > 100, "race: audio thread made progress");
    CHECK(bad == 0, "race: finite output throughout");

    publishInstance(nullptr); // final teardown (joins worker)
    printf(blocks > 0 && failures == 0 ? "RACE TEST PASSED\n" : "RACE TEST FAILED\n");
    return failures == 0 ? 0 : 1;
}
