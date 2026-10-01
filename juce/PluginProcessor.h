#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/BiteyDsp.h"

// Bitey PA-600 — JUCE wrapper.
//
// The entire sound lives in ../dsp/BiteyDsp.{h,cpp} (framework-free C++17).
// This class only bridges DAW parameters/audio to bitey::BiteyParams and the
// bitey::BiteyDsp engine. Parameter IDs below are stable: hosts store
// automation and presets against them, so do not rename once released.

class BiteyProcessor : public juce::AudioProcessor {
public:
    BiteyProcessor();
    ~BiteyProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bitey"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Push a full BiteyParams set into the hosted parameters (presets, etc.).
    void applyParamsToHost(const bitey::BiteyParams& p);

    // Named starting points, after Nathan Sabatino records.
    static bitey::BiteyParams presetBeTheVoid();
    static bitey::BiteyParams presetPsychedelicSwamp();
    static bitey::BiteyParams presetBRoom();

private:
    // Parameter pointers cached once in the constructor so the audio thread
    // never touches the APVTS lookup map or allocates strings.
    struct ParamCache {
        std::atomic<float>* chLevel[2]{};
        juce::AudioParameterChoice* chPad[2]{};
        std::atomic<float>* chLowCut[2]{};
        std::atomic<float>* chLow[2]{};
        std::atomic<float>* chHigh[2]{};
        std::atomic<float>* chFx[2]{};
        juce::AudioParameterChoice* tapeSpeed{};
        juce::AudioParameterChoice* tapeSize{};
        std::atomic<float>* tapeMix{};
        std::atomic<float>* revDrive{};
        std::atomic<float>* revContour{};
        std::atomic<float>* revTime{};
        std::atomic<float>* revReturn{};
        std::atomic<float>* mLow{};
        std::atomic<float>* mMid{};
        std::atomic<float>* mHigh{};
        juce::AudioParameterChoice* mMidFreq{};
        std::atomic<float>* mLevel{};
        std::atomic<float>* mMix{};
        std::atomic<float>* mPhase{};
        std::atomic<float>* power{};
    };
    ParamCache cache_;

    // Read the cached parameters into a BiteyParams struct (audio thread,
    // allocation-free).
    bitey::BiteyParams readParamsFromHost() const;

    bitey::BiteyDsp dsp_;
    juce::AudioBuffer<float> scratch_; // stereo working buffer for bus juggling

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BiteyProcessor)
};
