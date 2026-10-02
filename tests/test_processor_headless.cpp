// Headless test of the REAL BiteyProcessor plugin path.
//
// Compiles PluginProcessor.cpp with BITEY_HEADLESS (no GUI modules in the
// test binary's editor) and drives processBlock() exactly like a DAW would:
// parameter mapping, bus juggling, latency, bypass, meters, state round-trip.
//
// Build:
//   g++ -std=c++17 -O2 -DBITEY_HEADLESS -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
//       -DJUCE_USE_CURL=0 -I<juce>/modules -Ijuce -I. \
//       tests/test_processor_headless.cpp juce/PluginProcessor.cpp dsp/BiteyDsp.cpp \
//       build/juce_headless/*.o -o build/test_processor_headless -lpthread -ldl

#include "../juce/PluginProcessor.h"
#include <cmath>
#include <cstdio>
#include <vector>
#include <thread>
#include <chrono>

namespace {

int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

bool finiteBuf(const juce::AudioBuffer<float>& b) {
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n)
            if (!std::isfinite(d[n])) return false;
    }
    return true;
}

float peakBuf(const juce::AudioBuffer<float>& b) {
    float p = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::fabs(d[n]));
    }
    return p;
}

float rmsBuf(const juce::AudioBuffer<float>& b) {
    double s = 0;
    long n2 = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) {
        const float* d = b.getReadPointer(c);
        for (int n = 0; n < b.getNumSamples(); ++n) { s += d[n] * d[n]; ++n2; }
    }
    return float(std::sqrt(s / n2));
}

void setParam(BiteyProcessor& proc, const char* id, float v01) {
    if (auto* p = proc.apvts.getParameter(id)) {
        p->beginChangeGesture();
        p->setValueNotifyingHost(v01);
        p->endChangeGesture();
    }
}

void setParamNatural(BiteyProcessor& proc, const char* id, float natural) {
    if (auto* p = proc.apvts.getParameter(id))
        setParam(proc, id, p->convertTo0to1(natural));
}

// Render `seconds` of a sine through processBlock in host-sized blocks.
juce::AudioBuffer<float> render(BiteyProcessor& proc, double seconds,
                                float freq = 440.0f, float amp = 0.5f,
                                int block = 512, double sr = 44100.0) {
    const int total = int(seconds * sr);
    juce::AudioBuffer<float> out(2, total);
    juce::MidiBuffer midi;
    std::vector<float> phase{ 0.0f };
    int done = 0;
    while (done < total) {
        const int n = std::min(block, total - done);
        juce::AudioBuffer<float> blk(2, n);
        for (int c = 0; c < 2; ++c) {
            float* d = blk.getWritePointer(c);
            for (int i = 0; i < n; ++i) {
                d[i] = amp * std::sin(phase[0]);
                phase[0] += 2.0f * 3.14159265f * freq / float(sr);
            }
        }
        // keep L/R phase coherent: regenerate R identically
        blk.copyFrom(1, 0, blk.getReadPointer(0), n);
        proc.processBlock(blk, midi);
        for (int c = 0; c < 2; ++c)
            out.copyFrom(c, done, blk.getReadPointer(c), n);
        done += n;
    }
    return out;
}

} // namespace

int main() {
    BiteyProcessor proc;
    proc.prepareToPlay(44100.0, 512);

    check(proc.getLatencySamples() == 768, "host latency == 768 samples");

    // 1. Default render: finite, brickwall ceiling, meters alive.
    {
        auto out = render(proc, 2.0);
        check(finiteBuf(out), "default render: no NaN/Inf");
        const float pk = peakBuf(out);
        std::printf("  (default peak %.4f)\n", pk);
        check(pk <= 0.991f, "default render: true +/-0.99 ceiling");
        check(proc.getMainMeter() > 0.05f, "main VU meter alive");
        std::printf("  (main meter %.3f, reverb meter %.3f)\n",
                    proc.getMainMeter(), proc.getReverbMeter());
        check(proc.getReverbMeter() >= 0.0f, "reverb VU meter sane");
    }

    // 2. Bypass (power off): output == input delayed by exactly 768 samples.
    {
        setParam(proc, "power", 0.0f);
        const double sr = 44100.0;
        const int total = int(sr); // 1 s
        juce::AudioBuffer<float> in(2, total), out(2, total);
        for (int c = 0; c < 2; ++c) {
            float* d = in.getWritePointer(c);
            float ph = 0;
            for (int n = 0; n < total; ++n) {
                d[n] = 0.4f * std::sin(ph);
                ph += 2.0f * 3.14159265f * 220.0f / float(sr);
            }
        }
        juce::MidiBuffer midi;
        // warm up the smoothing, then measure
        juce::AudioBuffer<float> blk(2, 512);
        for (int i = 0; i < 200; ++i) { proc.processBlock(blk, midi); }
        int done = 0;
        while (done < total) {
            const int n = std::min(512, total - done);
            blk.setSize(2, n, false, false, true);
            for (int c = 0; c < 2; ++c)
                blk.copyFrom(c, 0, in.getReadPointer(c, done), n);
            proc.processBlock(blk, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom(c, done, blk.getReadPointer(c), n);
            done += n;
        }
        double err = 0;
        int cnt = 0;
        for (int c = 0; c < 2; ++c) {
            const float* o = out.getReadPointer(c);
            const float* ii = in.getReadPointer(c);
            for (int n = 2000; n < total; ++n) { err += std::fabs(o[n] - ii[n - 768]); ++cnt; }
        }
        err /= cnt;
        std::printf("  (bypass mean abs err %.2e)\n", err);
        check(err < 1e-4, "bypass: output == dry delayed 768 samples");
        setParam(proc, "power", 1.0f);
    }

    // 3. Level knob mapping through the host parameter path.
    {
        setParamNatural(proc, "ch1_level", 9.0f);
        setParamNatural(proc, "ch2_level", 9.0f);
        auto loud = render(proc, 1.0);
        setParamNatural(proc, "ch1_level", 2.0f);
        setParamNatural(proc, "ch2_level", 2.0f);
        auto quiet = render(proc, 1.0);
        const float rl = rmsBuf(loud), rq = rmsBuf(quiet);
        std::printf("  (level 9 RMS %.4f vs level 2 RMS %.4f)\n", rl, rq);
        // Level 9 slams the tube drive + brickwall limiter, so post-limiter
        // RMS compresses; it must still be clearly louder than level 2.
        check(rl > rq * 1.5f, "ch level params drive output via APVTS");
        setParamNatural(proc, "ch1_level", 5.0f);
        setParamNatural(proc, "ch2_level", 5.0f);
    }

    // 4. Tape speed choice mapping (7.5/15/30 -> 0/1/2).
    {
        setParamNatural(proc, "tape_mix", 6.0f);
        setParam(proc, "tape_speed", 0.0f); // 7.5 ips
        auto a = render(proc, 0.5);
        setParam(proc, "tape_speed", 1.0f); // 15 ips
        auto b = render(proc, 0.5);
        check(finiteBuf(a) && finiteBuf(b), "tape speed switch: finite");
        check(std::fabs(rmsBuf(a) - rmsBuf(b)) > 1e-6, "tape speed switch changes sound");
        setParamNatural(proc, "tape_mix", 0.0f);
    }

    // 5. Reverb time knob: the worker thread rebuilds the spring IR off the
    //    audio thread; a longer time must audibly lengthen the decay tail.
    //    Measured on silence AFTER the input stops (steady-state tone would
    //    mask the difference).
    {
        auto renderDecay = [&](double toneSec, double silenceSec) {
            const double sr = 44100.0;
            const int total = int((toneSec + silenceSec) * sr);
            juce::AudioBuffer<float> out(2, total);
            juce::MidiBuffer midi;
            float phase = 0;
            int done = 0;
            while (done < total) {
                const int n = std::min(512, total - done);
                juce::AudioBuffer<float> blk(2, n);
                for (int c = 0; c < 2; ++c) {
                    float* d = blk.getWritePointer(c);
                    for (int i = 0; i < n; ++i) {
                        const double t = double(done + i) / sr;
                        d[i] = (t < toneSec) ? 0.5f * std::sin(phase) : 0.0f;
                        phase += 2.0f * 3.14159265f * 440.0f / float(sr);
                    }
                }
                proc.processBlock(blk, midi);
                for (int c = 0; c < 2; ++c)
                    out.copyFrom(c, done, blk.getReadPointer(c), n);
                done += n;
            }
            return out;
        };
        auto tailRms = [](const juce::AudioBuffer<float>& b, double seconds) {
            const int n = b.getNumSamples(), m = int(seconds * 44100.0);
            const int skip = n - m;
            double s = 0; long c2 = 0;
            for (int c = 0; c < 2; ++c) {
                const float* d = b.getReadPointer(c) + skip;
                for (int i = 0; i < m; ++i) { s += d[i] * d[i]; ++c2; }
            }
            return std::sqrt(s / c2);
        };
        setParamNatural(proc, "rev_time", 1.0f); // ~0.9 s tank
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        auto outShort = renderDecay(1.0, 2.5);
        setParamNatural(proc, "rev_time", 10.0f); // 4.5 s tank
        // Give the background IR worker time to synthesize + partition.
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
        auto outLong = renderDecay(1.0, 2.5);
        check(finiteBuf(outLong), "reverb time change: finite after worker rebuild");
        const double tShort = tailRms(outShort, 1.0), tLong = tailRms(outLong, 1.0);
        std::printf("  (decay-tail RMS short %.4f vs long %.4f)\n", tShort, tLong);
        check(tLong > tShort * 3.0, "reverb time change: longer tank, longer tail");
        setParamNatural(proc, "rev_time", 5.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }

    // 6. State round-trip preserves parameters.
    {
        setParamNatural(proc, "rev_drive", 7.5f);
        setParam(proc, "tape_size", 0.5f); // 1/2"
        juce::MemoryBlock mb;
        proc.getStateInformation(mb);
        BiteyProcessor proc2;
        proc2.setStateInformation(mb.getData(), (int) mb.getSize());
        auto* a = proc.apvts.getParameter("rev_drive");
        auto* b = proc2.apvts.getParameter("rev_drive");
        auto* c = proc.apvts.getParameter("tape_size");
        auto* d = proc2.apvts.getParameter("tape_size");
        check(a && b && std::fabs(a->getValue() - b->getValue()) < 1e-6f, "state round-trip: rev_drive");
        check(c && d && std::fabs(c->getValue() - d->getValue()) < 1e-6f, "state round-trip: tape_size");
    }

    // 7. Odd block sizes (host may use any).
    {
        auto out = render(proc, 0.5, 440.0f, 0.5f, 137);
        check(finiteBuf(out), "137-sample blocks: finite");
    }

    // 8. Oversized host blocks (larger than prepareToPlay's samplesPerBlock):
    //    must chunk without allocating, stay finite.
    {
        auto out = render(proc, 0.5, 440.0f, 0.5f, 8192);
        check(finiteBuf(out), "8192-sample blocks: finite");
        check(peakBuf(out) <= 0.991f, "8192-sample blocks: ceiling holds");
    }

    // 9. Channel PAD switch changes gain staging as labeled. Measured with a
    //    quiet input and low channel level so the tube drive + limiter don't
    //    compress the 20 dB pad difference away; ch2 muted for isolation.
    {
        setParamNatural(proc, "ch1_level", 2.0f);
        setParamNatural(proc, "ch2_level", 0.0f);
        setParam(proc, "ch1_pad", 0.0f); // +10 dB
        auto hi = render(proc, 0.5, 440.0f, 0.1f);
        setParam(proc, "ch1_pad", 1.0f); // -10 dB (index 3 of 4)
        auto lo = render(proc, 0.5, 440.0f, 0.1f);
        const float rHi = rmsBuf(hi), rLo = rmsBuf(lo);
        std::printf("  (pad +10 RMS %.4f vs -10 RMS %.4f)\n", rHi, rLo);
        check(rHi > rLo * 3.0f, "pad switch: +10 dB audibly hotter than -10 dB");
        setParam(proc, "ch1_pad", 2.0f / 3.0f); // back to 0 dB
        setParamNatural(proc, "ch1_level", 5.0f);
        setParamNatural(proc, "ch2_level", 5.0f);
    }

    // 10. Phase invert flips output polarity. Reverb return + tape killed so
    //    the output is a deterministic function of the input (no tail from
    //    the previous render contaminating the correlation).
    {
        setParamNatural(proc, "rev_return", 0.0f);
        setParamNatural(proc, "tape_mix", 0.0f);
        setParam(proc, "m_phase", 0.0f);
        auto a = render(proc, 0.5);
        setParam(proc, "m_phase", 1.0f);
        auto b = render(proc, 0.5);
        const float* la = a.getReadPointer(0);
        const float* lb = b.getReadPointer(0);
        double num = 0, den = 0;
        for (int n = 5000; n < a.getNumSamples(); ++n) {
            num += la[n] * lb[n];
            den += la[n] * la[n];
        }
        const double corr = num / (den + 1e-12);
        std::printf("  (phase correlation %.3f)\n", corr);
        check(corr < -0.99, "phase invert: output polarity flipped");
        setParam(proc, "m_phase", 0.0f);
        setParamNatural(proc, "rev_return", 5.0f);
    }

    // 11. Master mid-frequency switch changes the EQ shape.
    {
        setParamNatural(proc, "m_mid", 10.0f);
        setParam(proc, "m_midfreq", 0.0f); // 0.7k
        auto a = render(proc, 0.5, 700.0f);
        setParam(proc, "m_midfreq", 1.0f); // 1.4k
        auto b = render(proc, 0.5, 700.0f);
        check(std::fabs(rmsBuf(a) - rmsBuf(b)) > 1e-4,
              "mid freq switch: EQ shape follows 0.7k/1.0k/1.4k");
        setParamNatural(proc, "m_mid", 0.0f);
        setParam(proc, "m_midfreq", 0.5f); // back to 1.0k
    }

    // 12. Parameter automation across blocks: no NaN/Inf, no blowups.
    {
        bool ok = true;
        for (int i = 0; i < 40 && ok; ++i) {
            setParamNatural(proc, "rev_drive", float(i % 11));
            setParamNatural(proc, "m_level", 2.0f + float(i % 7));
            auto out = render(proc, 0.1);
            ok = finiteBuf(out) && peakBuf(out) <= 0.991f;
        }
        check(ok, "automation sweep: finite, ceiling holds");
        setParamNatural(proc, "rev_drive", 5.0f);
        setParamNatural(proc, "m_level", 5.0f);
    }

    std::printf(failures == 0 ? "ALL HEADLESS PLUGIN TESTS PASSED\n"
                              : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
