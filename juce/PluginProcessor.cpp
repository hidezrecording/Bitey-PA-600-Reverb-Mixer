#include "PluginProcessor.h"
// BITEY_HEADLESS: compile the DSP + parameter path without any GUI modules,
// so the real processBlock() can be exercised in a console test.
#ifndef BITEY_HEADLESS
#include "PluginEditor.h"
#endif

namespace {

using ParamPtr = std::unique_ptr<juce::RangedAudioParameter>;

// Maps a 0..10 knob to an AudioParameterFloat.
ParamPtr knob10(const juce::String& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, 1}, name,
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.01f), def);
}

// Maps a +/-10 dB EQ control (Peavey hardware scale, Nathan 2026-10-07).
ParamPtr eqDb(const juce::String& id, const juce::String& name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, 1}, name,
        juce::NormalisableRange<float>(-10.0f, 10.0f, 0.01f), def,
        juce::AudioParameterFloatAttributes().withLabel("dB"));
}

ParamPtr choice(const juce::String& id, const juce::String& name,
                juce::StringArray options, int def) {
    return std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{id, 1}, name, std::move(options), def);
}

ParamPtr toggle(const juce::String& id, const juce::String& name, bool def) {
    return std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{id, 1}, name, def);
}

// Browser UI order for the pad switch: +10, +4, 0, -10.
// DSP pad index: 0:+0dB 1:+4dB 2:+10dB 3:-10dB.
int padChoiceToDsp(int choiceIdx) {
    static const int map[4] = {2, 1, 0, 3};
    return map[choiceIdx < 0 ? 0 : (choiceIdx > 3 ? 3 : choiceIdx)];
}
int padDspToChoice(int dspIdx) {
    static const int map[4] = {2, 1, 0, 3};
    return map[dspIdx < 0 ? 0 : (dspIdx > 3 ? 3 : dspIdx)];
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
BiteyProcessor::createLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int c = 1; c <= 2; ++c) {
        const juce::String pfx = "ch" + juce::String(c) + "_";
        const juce::String nm  = "Ch " + juce::String(c) + " ";
        layout.add(knob10(pfx + "level",  nm + "Level",  5.0f));  // Nathan 2026-10-08: nominal
        layout.add(choice(pfx + "pad",    nm + "Pad",
                          juce::StringArray{"+10 dB", "+4 dB", "0 dB", "-10 dB"}, 2));
        layout.add(toggle(pfx + "lowcut", nm + "96Hz Cut", false));
        layout.add(eqDb(pfx + "low",      nm + "Low",    0.0f));   // Nathan 2026-10-08: nominal
        layout.add(eqDb(pfx + "high",     nm + "High",   0.0f));   // Nathan 2026-10-08: nominal
        layout.add(knob10(pfx + "fx",     nm + "FX Send", 5.0f));  // Nathan 2026-10-08: nominal
    }

    layout.add(choice("tape_speed", "Tape Speed",
                      juce::StringArray{"7.5 ips", "15 ips", "30 ips"}, 0));
    layout.add(choice("tape_size", "Tape Size",
                      juce::StringArray{"1/4\"", "1/2\"", "1\""}, 0));  // Nathan v2: 1/4"
    layout.add(knob10("tape_mix", "Tape Mix", 5.0f));  // Nathan 2026-10-08: nominal

    layout.add(knob10("rev_drive",   "Reverb Drive",   5.0f));  // Nathan 2026-10-08: nominal
    layout.add(knob10("rev_contour", "Reverb Contour", 5.0f));  // Nathan 2026-10-08: nominal
    layout.add(knob10("rev_time",    "Reverb Time",    5.0f));  // Nathan 2026-10-08: nominal (2.5s)
    layout.add(knob10("rev_return",  "Reverb Return",  5.0f));  // Nathan 2026-10-08: nominal

    layout.add(eqDb("m_low",  "Master Low",  0.0f));   // Nathan 2026-10-08: nominal
    layout.add(eqDb("m_mid",  "Master Mid",  0.0f));  // Nathan 2026-10-08: nominal
    layout.add(eqDb("m_high", "Master High", 0.0f)); // Nathan 2026-10-08: nominal
    layout.add(choice("m_midfreq", "Master Mid Freq",
                      juce::StringArray{"0.7 kHz", "1.0 kHz", "1.4 kHz"}, 0));  // Nathan: 0.7k
    layout.add(knob10("m_level", "Main Level", 5.0f));  // Nathan 2026-10-08: nominal
    layout.add(knob10("m_mix",   "Mix",        5.0f));  // Nathan 2026-10-08: nominal
    layout.add(toggle("m_phase", "Phase Invert", false));
    layout.add(toggle("power",   "Power",        true));

    return layout;
}

BiteyProcessor::BiteyProcessor()
    : AudioProcessor(juce::AudioProcessor::BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "BiteyPA600", createLayout()) {
    setLatencySamples(64); // 4x oversampled stages on the direct path

    // Cache every parameter pointer once; the audio thread must not hit
    // the APVTS lookup map or build strings per block.
    auto raw = [this](const juce::String& id) {
        return apvts.getRawParameterValue(id);
    };
    auto ch = [this](const juce::String& id) {
        return dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
    };
    for (int c = 0; c < 2; ++c) {
        const juce::String pfx = "ch" + juce::String(c + 1) + "_";
        cache_.chLevel[c]  = raw(pfx + "level");
        cache_.chPad[c]    = ch(pfx + "pad");
        cache_.chLowCut[c] = raw(pfx + "lowcut");
        cache_.chLow[c]    = raw(pfx + "low");
        cache_.chHigh[c]   = raw(pfx + "high");
        cache_.chFx[c]     = raw(pfx + "fx");
    }
    cache_.tapeSpeed  = ch("tape_speed");
    cache_.tapeSize   = ch("tape_size");
    cache_.tapeMix    = raw("tape_mix");
    cache_.revDrive   = raw("rev_drive");
    cache_.revContour = raw("rev_contour");
    cache_.revTime    = raw("rev_time");
    cache_.revReturn  = raw("rev_return");
    cache_.mLow    = raw("m_low");
    cache_.mMid    = raw("m_mid");
    cache_.mHigh   = raw("m_high");
    cache_.mMidFreq = ch("m_midfreq");
    cache_.mLevel = raw("m_level");
    cache_.mMix   = raw("m_mix");
    cache_.mPhase = raw("m_phase");
    cache_.power  = raw("power");

    jassert(cache_.tapeSpeed != nullptr && cache_.tapeSize != nullptr &&
            cache_.mMidFreq != nullptr && cache_.chPad[0] != nullptr &&
            cache_.chPad[1] != nullptr);
}

void BiteyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    dsp_.prepare(sampleRate);
    dsp_.reset();
    scratch_.setSize(2, samplesPerBlock);
    setLatencySamples(dsp_.getLatencySamples());
}

bitey::BiteyParams BiteyProcessor::readParamsFromHost() const {
    bitey::BiteyParams p;
    for (int c = 0; c < 2; ++c) {
        auto& ch = p.ch[c];
        ch.level  = cache_.chLevel[c]->load();
        ch.pad    = padChoiceToDsp(cache_.chPad[c]->getIndex());
        ch.lowCut = cache_.chLowCut[c]->load() > 0.5f;
        ch.lowDb  = cache_.chLow[c]->load();
        ch.highDb = cache_.chHigh[c]->load();
        ch.fxSend = cache_.chFx[c]->load();
    }
    p.tapeSpeed = cache_.tapeSpeed->getIndex();
    p.tapeSize  = cache_.tapeSize->getIndex();
    p.tapeMix   = cache_.tapeMix->load();
    p.revDrive   = cache_.revDrive->load();
    p.revContour = cache_.revContour->load();
    p.revTime    = cache_.revTime->load();
    p.revReturn  = cache_.revReturn->load();
    p.mLowDb  = cache_.mLow->load();
    p.mMidDb  = cache_.mMid->load();
    p.mHighDb = cache_.mHigh->load();
    p.midFreq   = cache_.mMidFreq->getIndex();
    p.mainLevel = cache_.mLevel->load();
    p.mix       = cache_.mMix->load();
    p.phaseInvert = cache_.mPhase->load() > 0.5f;
    p.bypass      = !(cache_.power->load() > 0.5f);
    return p;
}

void BiteyProcessor::applyParamsToHost(const bitey::BiteyParams& p) {
    auto setF = [this](const juce::String& id, float v) {
        if (auto* par = apvts.getParameter(id))
            par->setValueNotifyingHost(par->convertTo0to1(v));
    };
    auto setI = [this](const juce::String& id, int i) {
        if (auto* par = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id)))
            par->setValueNotifyingHost(par->convertTo0to1((float)i));
    };
    auto setB = [this](const juce::String& id, bool b) {
        if (auto* par = apvts.getParameter(id))
            par->setValueNotifyingHost(b ? 1.0f : 0.0f);
    };
    for (int c = 0; c < 2; ++c) {
        const juce::String pfx = "ch" + juce::String(c + 1) + "_";
        const auto& ch = p.ch[c];
        setF(pfx + "level", ch.level);
        setI(pfx + "pad", padDspToChoice(ch.pad));
        setB(pfx + "lowcut", ch.lowCut);
        setF(pfx + "low", ch.lowDb);
        setF(pfx + "high", ch.highDb);
        setF(pfx + "fx", ch.fxSend);
    }
    setI("tape_speed", p.tapeSpeed);
    setI("tape_size", p.tapeSize);
    setF("tape_mix", p.tapeMix);
    setF("rev_drive", p.revDrive);
    setF("rev_contour", p.revContour);
    setF("rev_time", p.revTime);
    setF("rev_return", p.revReturn);
    setF("m_low", p.mLowDb);
    setF("m_mid", p.mMidDb);
    setF("m_high", p.mHighDb);
    setI("m_midfreq", p.midFreq);
    setF("m_level", p.mainLevel);
    setF("m_mix", p.mix);
    setB("m_phase", p.phaseInvert);
    setB("power", !p.bypass);
}

void BiteyProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                  juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    dsp_.setParams(readParamsFromHost());
    dsp_.beginBlock(); // Adopt worker-finished reverb IRs at block boundary

    const int numIn  = juce::jmin(getTotalNumInputChannels(), 2);
    const int numOut = juce::jmin(getTotalNumOutputChannels(), 2);

    // scratch_ is sized in prepareToPlay; if a host hands us a larger block
    // than prepared for, process in chunks instead of allocating here.
    const int cap = juce::jmax(scratch_.getNumSamples(), 64);

    // Gather: mono input feeds both channel strips (dual mono).
    const float* in0 = buffer.getReadPointer(0);
    const float* in1 = numIn > 1 ? buffer.getReadPointer(1) : in0;

    int done = 0;
    while (done < numSamples) {
        const int n = juce::jmin(cap, numSamples - done);
        scratch_.copyFrom(0, 0, in0 + done, n);
        scratch_.copyFrom(1, 0, in1 + done, n);

        dsp_.process(scratch_.getWritePointer(0), scratch_.getWritePointer(1), n);

        for (int c = 0; c < numOut; ++c)
            buffer.copyFrom(c, done, scratch_.getReadPointer(c), n);
        done += n;
    }
    for (int c = numOut; c < buffer.getNumChannels(); ++c)
        buffer.clear(c, 0, numSamples);

    reverbMeter_.store(dsp_.getReverbMeter());
    mainMeter_.store(dsp_.getMainMeter());
}

void BiteyProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        AudioProcessor::copyXmlToBinary(*xml, destData);
}

void BiteyProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = AudioProcessor::getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* BiteyProcessor::createEditor() {
#ifdef BITEY_HEADLESS
    return nullptr;
#else
    return new BiteyEditor(*this);
#endif
}

// ---------------------------------------------------------------------------
// Factory presets — starting points for common sounds.
// ---------------------------------------------------------------------------

bitey::BiteyParams BiteyProcessor::presetGrittyDrums() {
    bitey::BiteyParams p; // defaults = browser prototype state
    p.ch[0].level = 7.0f; p.ch[1].level = 7.0f;
    p.ch[0].lowDb = 2.0f; p.ch[1].lowDb = 2.0f;
    p.tapeSpeed = 1; p.tapeSize = 0; p.tapeMix = 4.0f;
    p.revDrive = 6.0f; p.revContour = 4.0f; p.revTime = 4.0f; p.revReturn = 6.0f;
    p.mLowDb = 1.5f; p.mainLevel = 6.0f;
    return p;
}

bitey::BiteyParams BiteyProcessor::presetDarkSpring() {
    bitey::BiteyParams p;
    p.ch[0].level = 6.5f; p.ch[1].level = 6.5f;
    p.ch[0].fxSend = 7.0f; p.ch[1].fxSend = 7.0f;
    p.tapeSpeed = 0; p.tapeSize = 0; p.tapeMix = 7.0f;
    p.revDrive = 7.0f; p.revContour = 3.0f; p.revTime = 7.0f; p.revReturn = 7.0f;
    p.mLowDb = 2.0f; p.mHighDb = -1.5f;
    return p;
}

bitey::BiteyParams BiteyProcessor::presetRoomGlue() {
    bitey::BiteyParams p;
    p.ch[0].level = 5.5f; p.ch[1].level = 5.5f;
    p.tapeSpeed = 2; p.tapeSize = 1; p.tapeMix = 2.0f;
    p.revDrive = 4.0f; p.revContour = 6.0f; p.revTime = 3.0f; p.revReturn = 4.0f;
    return p;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new BiteyProcessor();
}
