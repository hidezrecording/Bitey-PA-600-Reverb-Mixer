#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Bitey PA-600 — visual editor.
//
// Faithful port of the current browser prototype's panel artwork
// (prototype/App.tsx, components/Knob.tsx, components/VUMeter.tsx).
// Geometry notes:
//
//  - Editor is 1050 x 520: the mixer unit exactly as in the browser
//    (the waveform player strip below is a browser-only concern).
//  - Channel strips: REVERB / HIGH / LOW (50px), skirted LEVEL (90px),
//    96Hz low-cut switch + channel number + 4-way PAD switch.
//  - MASTER: HIGH / MID / LOW, 3-way mid-freq switch, skirted MAIN.
//  - REVERB: DRIVE / CONTOUR / TIME, skirted REVERB return.
//  - Center: IPS + ECHO / TAPE cluster, twin VU meters, board tape,
//    small DRY/WET knob, POWER jewel + PHASE (normal/reverse) bat.
//  - Knob scale: 11 ticks over -135..+135 deg; tick/label radii match
//    Knob.tsx (standard: 1.25x/1.45x radius; skirted: 1.15x/1.30x).
//  - VU meter: direct port of the canvas code (teal face, red zone from
//    0.72, needle ballistics 0.2 attack / 0.05 release).
//
// Fonts (Michroma, Metal Mania, Permanent Marker — all OFL) are embedded
// via BinaryData (see CMakeLists juce_add_binary_data).

namespace BiteyFonts {
juce::Font robotoCondensed(float sizePx, bool bold = true);
juce::Font michroma(float sizePx);
juce::Font metalMania(float sizePx);
juce::Font permanentMarker(float sizePx);
} // namespace BiteyFonts

// ---------------------------------------------------------------------------
// BiteyKnob — rotary slider with the prototype's exact knob artwork:
// panel tick scale + labels, drop shadow, optional metal skirt, knurled
// rotating body with pointer and specular highlight.
// ---------------------------------------------------------------------------
class BiteyKnob : public juce::Component {
public:
    enum class Scale { ZeroToTen, Eq, None };

    BiteyKnob(BiteyProcessor& proc, const juce::String& paramID,
              int knobSizePx, bool skirted, Scale scale);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void setKnobLabel(const juce::String& label) { knobLabel_ = label; }

private:
    struct Look : public juce::LookAndFeel_V4 {
        int knobSize = 50;
        bool skirted = false;
        void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                              float sliderPos, float rotaryStartAngle,
                              float rotaryEndAngle, juce::Slider& slider) override;
    };

    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att_;
    Look look_;
    int knobSize_;
    bool skirted_;
    Scale scale_;
    float half_ = 0.0f; // half-width of the component (labels included)
    float cy_ = 0.0f;   // y of the knob circle centre
    juce::String knobLabel_; // label drawn in the bottom scale gap

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BiteyKnob)
};

// ---------------------------------------------------------------------------
// MetalToggle — the prototype's MetalToggleSwitch: N-position bat with
// mounting nut, bat tip, and a label column. Click cycles through values.
// The active value glows green (#4ade80) like the prototype.
// iconMode == 1 draws the 96Hz low-cut icons (flat line / filter curve)
// instead of text labels.
// ---------------------------------------------------------------------------
class MetalToggle : public juce::Component {
public:
    MetalToggle(BiteyProcessor& proc, const juce::String& paramID,
                const juce::String& caption, bool captionBelow,
                const std::vector<juce::String>& labels,
                bool labelsOnRight, int iconMode = 0);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void syncFromParam();

private:
    float tipOffset() const; // -6 .. +6 like the prototype's translateY
    void drawLabelColumn(juce::Graphics& g, juce::Rectangle<float> area);

    BiteyProcessor& proc_;
    juce::String paramID_;
    juce::String caption_;
    bool captionBelow_;
    std::vector<juce::String> labels_;
    bool labelsOnRight_;
    int iconMode_;
    int index_ = 0;
    bool isBool_ = false;
    float body_ = 30.0f; // switch body diameter

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MetalToggle)
};

// ---------------------------------------------------------------------------
// BatToggle — the prototype's ToggleSwitch (POWER / PHASE): bat lever with
// hex nut, shadow, and a caption above.
// ---------------------------------------------------------------------------
class BatToggle : public juce::Component {
public:
    BatToggle(BiteyProcessor& proc, const juce::String& paramID,
              const juce::String& title);
    void setTitle(const juce::String& t);
    void setInverted(bool inv); // phase switch: bat UP = normal (param 0)
    bool paramToVisual(bool paramOn) const;
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void syncFromParam();

private:
    BiteyProcessor& proc_;
    juce::String paramID_;
    juce::String title_;
    bool isOn_ = false;
    bool inverted_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BatToggle)
};

// ---------------------------------------------------------------------------
// PowerJewel — amber pilot light, glowing when powered.
// ---------------------------------------------------------------------------
class PowerJewel : public juce::Component, private juce::Timer {
public:
    PowerJewel();
    void paint(juce::Graphics& g) override;
    void setOn(bool on);

private:
    void timerCallback() override;
    bool isOn_ = true;
    float pulse_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PowerJewel)
};

// ---------------------------------------------------------------------------
// ClipBulb — UA 1108 style round level indicator. Single bulb transitions
// green -> yellow -> red based on signal level (0.0 to 1.0+).
// ---------------------------------------------------------------------------
class ClipBulb : public juce::Component, private juce::Timer {
public:
    ClipBulb();
    void paint(juce::Graphics& g) override;
    void setLevel(float level);  // 0.0 (silent) to 1.0+ (clipping)

private:
    void timerCallback() override;
    std::atomic<float> level_{0.0f};
    float displayLevel_ = 0.0f;  // smoothed for the bulb

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClipBulb)
};

// ---------------------------------------------------------------------------
// VUMeterComp — port of VUMeter.tsx: teal face, scale arc with red zone,
// BITEY / MOD. 600 branding, VU/dB corner labels, glass reflection,
// ballistics-matched needle.
// ---------------------------------------------------------------------------
class VUMeterComp : public juce::Component, private juce::Timer {
public:
    explicit VUMeterComp(BiteyProcessor& proc, bool reverbMeter);
    void paint(juce::Graphics& g) override;
    void setDimmed(bool dimmed);

private:
    void timerCallback() override;
    float readLevel();

    BiteyProcessor& proc_;
    bool reverbMeter_;
    bool dimmed_ = false;
    float smoothed_ = 0.0f;
    juce::Image noise_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VUMeterComp)
};

// ---------------------------------------------------------------------------
// BoardTape — masking-tape strip reading "reverb mixer".
// ---------------------------------------------------------------------------
class BoardTape : public juce::Component {
public:
    BoardTape();
    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoardTape)
};

// ---------------------------------------------------------------------------
// PanelBox — the 6px light-bordered dark rounded panel with title plaque.
// ---------------------------------------------------------------------------
class PanelBox : public juce::Component {
public:
    explicit PanelBox(const juce::String& title,
                      juce::Colour bg = juce::Colour(0xff1a1a1a));
    void paint(juce::Graphics& g) override;
    void resized() override;

protected:
    juce::String title_;
    juce::Colour bg_;
};

// ---------------------------------------------------------------------------
// ChannelStrip — LEFT / RIGHT: REVERB, HIGH, LOW + skirted LEVEL,
// 96Hz cut switch + channel number + PAD switch.
// ---------------------------------------------------------------------------
class ChannelStrip : public PanelBox {
public:
    ChannelStrip(BiteyProcessor& proc, int index, const juce::String& title,
                 const juce::String& number);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void syncToggles();

private:
    BiteyProcessor& proc_;
    juce::String number_;
    std::unique_ptr<BiteyKnob> kReverb_, kHigh_, kLow_, kLevel_;
    std::unique_ptr<MetalToggle> lowCut_, pad_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

// ---------------------------------------------------------------------------
// MasterStrip — HIGH / MID / LOW + mid-freq switch + skirted MAIN.
// ReverbStrip — DRIVE / CONTOUR / TIME + skirted REVERB return.
// ---------------------------------------------------------------------------
class MasterStrip : public PanelBox, private juce::Timer {
public:
    explicit MasterStrip(BiteyProcessor& proc);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void syncToggles();

private:
    void timerCallback() override;
    BiteyProcessor& proc_;
    std::unique_ptr<BiteyKnob> kHigh_, kMid_, kLow_, kMain_;
    std::unique_ptr<MetalToggle> midFreq_;
    std::unique_ptr<ClipBulb> clipBulb_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStrip)
};

class ReverbStrip : public PanelBox, private juce::Timer {
public:
    explicit ReverbStrip(BiteyProcessor& proc);
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    BiteyProcessor& proc_;
    std::unique_ptr<BiteyKnob> kDrive_, kContour_, kTime_, kReturn_;
    std::unique_ptr<ClipBulb> clipBulb_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ReverbStrip)
};

// ---------------------------------------------------------------------------
// CenterPanel — tape cluster, VU meters, board tape + dry/wet, power.
// ---------------------------------------------------------------------------
class CenterPanel : public PanelBox {
public:
    explicit CenterPanel(BiteyProcessor& proc);
    void paint(juce::Graphics& g) override;
    void resized() override;
    void syncPower(bool on);
    void syncToggles();

private:

private:
    BiteyProcessor& proc_;
    std::unique_ptr<MetalToggle> ips_, tapeSize_;
    std::unique_ptr<BiteyKnob> echo_, dryWet_;
    std::unique_ptr<VUMeterComp> vuReverb_, vuMain_;
    std::unique_ptr<BoardTape> tape_;
    std::unique_ptr<MetalToggle> power_, phase_;
    std::unique_ptr<PowerJewel> jewel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CenterPanel)
};

// ---------------------------------------------------------------------------
// WoodCheek — golden-brown wooden end cheek with grain.
// ---------------------------------------------------------------------------
class WoodCheek : public juce::Component {
public:
    explicit WoodCheek(bool left);
    void paint(juce::Graphics& g) override;

private:
    bool left_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WoodCheek)
};

// ---------------------------------------------------------------------------
class BiteyEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit BiteyEditor(BiteyProcessor&);
    ~BiteyEditor() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    BiteyProcessor& proc_;
    std::unique_ptr<WoodCheek> cheekL_, cheekR_;
    std::unique_ptr<ChannelStrip> chL_, chR_;
    std::unique_ptr<CenterPanel> center_;
    std::unique_ptr<MasterStrip> master_;
    std::unique_ptr<ReverbStrip> reverb_;
    bool lastPower_ = true;
    juce::Image grain_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BiteyEditor)
};
