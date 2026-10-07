// Quick CPU test at different buffer sizes
#include "../juce/PluginProcessor.h"
#include <chrono>
#include <cstdio>
#include <vector>
#include <cmath>

int main() {
    const double sr = 44100.0;
    const int testSizes[] = {64, 128, 256, 512};
    
    for (int bs : testSizes) {
        BiteyProcessor proc;
        proc.prepareToPlay(sr, bs);
        
        juce::AudioBuffer<float> buf(2, bs);
        // Fill with sine
        for (int c = 0; c < 2; ++c) {
            auto* d = buf.getWritePointer(c);
            for (int n = 0; n < bs; ++n)
                d[n] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * n / sr);
        }
        
        juce::MidiBuffer midi;
        // Warm up
        for (int i = 0; i < 10; ++i) proc.processBlock(buf, midi);
        
        auto start = std::chrono::high_resolution_clock::now();
        const int iters = 200;
        for (int i = 0; i < iters; ++i) proc.processBlock(buf, midi);
        auto end = std::chrono::high_resolution_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        
        double rtUs = iters * (bs / sr) * 1e6;
        double cpu = (us / rtUs) * 100.0;
        std::printf("Buffer %4d: %.1f%% CPU\n", bs, cpu);
    }
    return 0;
}
