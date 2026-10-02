// Bitey PA-600 — visual editor implementation.
//
// Faithful port of the current browser prototype's panel artwork
// (prototype/App.tsx, components/Knob.tsx, components/VUMeter.tsx).
// See PluginEditor.h for geometry notes.

#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

namespace {

constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;

juce::Typeface::Ptr loadEmbedded(const void* data, size_t size) {
    return juce::Typeface::createSystemTypefaceFor(data, size);
}

juce::Colour col(uint32_t argb) { return juce::Colour(argb); }

} // namespace

namespace BiteyFonts {

juce::Font robotoCondensed(float sizePx, bool bold) {
    static juce::Typeface::Ptr tf =
        loadEmbedded(BinaryData::RobotoCondensed_ttf, BinaryData::RobotoCondensed_ttfSize);
    auto opts = juce::FontOptions(tf).withHeight(sizePx);
    if (bold) opts = opts.withStyle("Bold");
    return juce::Font(opts);
}
juce::Font michroma(float sizePx) {
    static juce::Typeface::Ptr tf =
        loadEmbedded(BinaryData::MichromaRegular_ttf, BinaryData::MichromaRegular_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(sizePx));
}
juce::Font metalMania(float sizePx) {
    static juce::Typeface::Ptr tf =
        loadEmbedded(BinaryData::MetalManiaRegular_ttf, BinaryData::MetalManiaRegular_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(sizePx));
}
juce::Font permanentMarker(float sizePx) {
    static juce::Typeface::Ptr tf = loadEmbedded(BinaryData::PermanentMarkerRegular_ttf,
                                                 BinaryData::PermanentMarkerRegular_ttfSize);
    return juce::Font(juce::FontOptions(tf).withHeight(sizePx));
}

} // namespace BiteyFonts

// ---------------------------------------------------------------------------
// BiteyKnob
// ---------------------------------------------------------------------------

BiteyKnob::BiteyKnob(BiteyProcessor& proc, const juce::String& paramID,
                     int knobSizePx, bool skirted, Scale scale)
    : knobSize_(knobSizePx), skirted_(skirted), scale_(scale) {
    look_.knobSize = knobSizePx;
    look_.skirted = skirted;
    slider_.setLookAndFeel(&look_);
    slider_.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    slider_.setRotaryParameters(-135.0f * kDeg2Rad, 135.0f * kDeg2Rad, true);
    slider_.setMouseDragSensitivity(100); // prototype: 100px = full range
    if (auto* p = proc.apvts.getParameter(paramID))
        slider_.setDoubleClickReturnValue(true, p->getDefaultValue());
    addAndMakeVisible(slider_);
    att_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, paramID, slider_);

    const float r = knobSizePx * 0.5f;
    if (scale == Scale::None) {
        half_ = r + 8.0f;
        cy_ = r + 8.0f;
    } else {
        const float textR = r * (skirted ? 1.30f : 1.45f);
        half_ = textR + 13.0f; // label anchor + glyph extent + padding
        cy_ = textR + 11.0f;   // room for the top arc labels
    }
    // Height includes room for the knob label below the scale (14px + padding)
    setSize(int(std::ceil(half_ * 2.0f)), int(std::ceil(cy_ + textR + 18.0f)));
}

void BiteyKnob::resized() {
    const int s = knobSize_;
    slider_.setBounds(int(half_ - s * 0.5f), int(cy_ - s * 0.5f), s, s);
}

void BiteyKnob::paint(juce::Graphics& g) {
    if (scale_ == Scale::None) return; // ECHO / DRY/WET: bare knob, no scale
    const auto bounds = slider_.getBounds();
    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();
    const float radius = knobSize_ * 0.5f;
    const float tickR = radius * (skirted_ ? 1.15f : 1.25f);
    const float textR = radius * (skirted_ ? 1.30f : 1.45f);

    g.setFont(BiteyFonts::robotoCondensed(7.0f));

    for (int i = 0; i <= 10; ++i) {
        const float angleDeg = -135.0f + (i / 10.0f) * 270.0f;
        const float a = (angleDeg - 90.0f) * kDeg2Rad;
        const float ca = std::cos(a), sa = std::sin(a);

        // Tick mark
        const float tickH = (scale_ == Scale::Eq && i == 5) ? 5.0f : 3.0f;
        const float x1 = cx + ca * tickR, y1 = cy + sa * tickR;
        const float x2 = cx + ca * (tickR - tickH), y2 = cy + sa * (tickR - tickH);
        g.setColour(col(0xffffffff));
        g.drawLine(x1, y1, x2, y2, 1.5f);

        // Numeric labels
        bool show = false;
        juce::String label;
        if (scale_ == Scale::ZeroToTen) {
            show = (i % 2 == 0);
            label = juce::String(i);
        } else {
            if (i == 0) { label = "-15"; show = true; }
            if (i == 5) { label = "0"; show = true; }
            if (i == 10) { label = "+15"; show = true; }
        }
        if (show) {
            const float lx = cx + ca * textR, ly = cy + sa * textR;
            g.setColour(col(0xffffffff));
            g.drawText(label, int(lx - 16), int(ly - 7), 32, 14,
                       juce::Justification::centred);
        }
    }

    // Knob label in the bottom scale gap (between the end markers, under the pot)
    if (knobLabel_.isNotEmpty()) {
        g.setFont(BiteyFonts::robotoCondensed(10.0f));
        // Shadow
        g.setColour(col(0x80000000));
        g.drawText(knobLabel_, 0, int(cy + textR - 2), getWidth(), 14,
                   juce::Justification::centred);
        // Main text
        g.setColour(col(0xffffffff));
        g.drawText(knobLabel_, 0, int(cy + textR - 3), getWidth(), 14,
                   juce::Justification::centred);
    }
}

void BiteyKnob::Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider&) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float size = float(knobSize);

    // Drop shadow (soft, offset down-right)
    g.setColour(col(0xaa000000));
    g.fillEllipse(cx - size * 0.44f, cy - size * 0.38f + 3.0f, size * 0.88f, size * 0.88f);

    // Skirt (big knobs): dark bakelite ring with chunky scalloped edge
    if (skirted) {
        // Bakelite base (dark charcoal, hint of warmth)
        juce::ColourGradient skirt(col(0xff232022), cx - size * 0.35f, cy - size * 0.35f,
                                   col(0xff0b0a09), cx + size * 0.4f, cy + size * 0.4f, true);
        g.setGradientFill(skirt);
        g.fillEllipse(cx - size * 0.5f, cy - size * 0.5f, size, size);

        // Chunky scallops: just a few large ridges (vintage RCA style)
        g.setColour(col(0x50000000));
        for (int i = 0; i < 8; ++i) {
            const float a = i * juce::MathConstants<float>::twoPi / 8.0f;
            const float x1 = cx + std::cos(a) * size * 0.44f;
            const float y1 = cy + std::sin(a) * size * 0.44f;
            const float x2 = cx + std::cos(a) * size * 0.50f;
            const float y2 = cy + std::sin(a) * size * 0.50f;
            g.drawLine(x1, y1, x2, y2, 4.0f);
        }
        // Highlight on scallops (top-left)
        g.setColour(col(0x28ffffff));
        for (int i = 4; i < 7; ++i) {
            const float a = i * juce::MathConstants<float>::twoPi / 8.0f;
            const float x1 = cx + std::cos(a) * size * 0.44f;
            const float y1 = cy + std::sin(a) * size * 0.44f;
            const float x2 = cx + std::cos(a) * size * 0.50f;
            const float y2 = cy + std::sin(a) * size * 0.50f;
            g.drawLine(x1, y1, x2, y2, 3.0f);
        }

        g.setColour(col(0xff121110));
        g.fillEllipse(cx - size * 0.45f, cy - size * 0.45f, size * 0.9f, size * 0.9f);
    }

    const float bodyR = (skirted ? 0.35f : 0.425f) * size;

    // Rotating body
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle), cx, cy));

    // RCA bakelite: dark charcoal with hint of warmth, matte finish
    juce::ColourGradient body(col(0xff35322e), cx - bodyR * 0.6f, cy - bodyR * 0.75f,
                              col(0xff141210), cx + bodyR * 0.7f, cy + bodyR * 0.8f, true);
    body.addColour(0.35, col(0xff242220));
    body.addColour(0.85, col(0xff0e0c0a));
    g.setGradientFill(body);
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Chunky scalloped knurling (just a few large ridges, vintage RCA style)
    g.setColour(col(0x60000000));
    const int flutes = 8;
    for (int i = 0; i < flutes; ++i) {
        const float a = i * juce::MathConstants<float>::twoPi / flutes;
        const float x1 = cx + std::cos(a) * bodyR * 0.86f;
        const float y1 = cy + std::sin(a) * bodyR * 0.86f;
        const float x2 = cx + std::cos(a) * bodyR * 0.99f;
        const float y2 = cy + std::sin(a) * bodyR * 0.99f;
        g.drawLine(x1, y1, x2, y2, 5.0f);
    }
    // Highlight on upper scallops
    g.setColour(col(0x3aaaaaaa));
    for (int i = 2; i < 5; ++i) {
        const float a = i * juce::MathConstants<float>::twoPi / flutes;
        const float x1 = cx + std::cos(a) * bodyR * 0.86f;
        const float y1 = cy + std::sin(a) * bodyR * 0.86f;
        const float x2 = cx + std::cos(a) * bodyR * 0.99f;
        const float y2 = cy + std::sin(a) * bodyR * 0.99f;
        g.drawLine(x1, y1, x2, y2, 3.0f);
    }

    // Domed cap (slightly raised center)
    const float capR = bodyR * 0.68f;
    juce::ColourGradient cap(col(0xff3d3a35), cx - capR * 0.5f, cy - capR * 0.6f,
                             col(0xff1a1815), cx + capR * 0.5f, cy + capR * 0.5f, true);
    g.setGradientFill(cap);
    g.fillEllipse(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    // Chunky ivory pointer (RCA broadcast style - wide and visible)
    g.setColour(col(0xf5e6c8));  // aged ivory
    const float pw = bodyR * 0.14f;  // pointer width
    const float pl = bodyR * 0.92f;  // pointer length (almost to edge)
    g.fillRoundedRectangle(cx - pw * 0.5f, cy - pl, pw, pl * 0.55f, pw * 0.3f);
    // Pointer shadow for depth
    g.setColour(col(0x66000000));
    g.fillRoundedRectangle(cx - pw * 0.5f + 1.5f, cy - pl + 1.5f, pw, pl * 0.55f, pw * 0.3f);

    g.restoreState();
}

// ---------------------------------------------------------------------------
// MetalToggle — the prototype's MetalToggleSwitch.
// ---------------------------------------------------------------------------

MetalToggle::MetalToggle(BiteyProcessor& proc, const juce::String& paramID,
                         const juce::String& caption, bool captionBelow,
                         const std::vector<juce::String>& labels,
                         bool labelsOnRight, int iconMode)
    : proc_(proc), paramID_(paramID), caption_(caption), captionBelow_(captionBelow),
      labels_(labels), labelsOnRight_(labelsOnRight), iconMode_(iconMode) {
    if (auto* p = proc.apvts.getParameter(paramID_))
        isBool_ = (dynamic_cast<juce::AudioParameterBool*>(p) != nullptr);

    // Measure the label column
    auto font = BiteyFonts::robotoCondensed(8.0f);
    float labelW = 0.0f;
    for (auto& l : labels_)
        labelW = juce::jmax(labelW, juce::GlyphArrangement::getStringWidth(font, l));
    if (iconMode_ == 1) labelW = 20.0f;

    const float labelColH = juce::jmax(body_, float(labels_.size()) * 11.0f);
    const float w = body_ + 6.0f + labelW + 4.0f;
    const float h = (caption_.isNotEmpty() ? 12.0f : 0.0f) + labelColH +
                    (captionBelow_ ? 12.0f : 0.0f);
    setSize(int(std::ceil(w)), int(std::ceil(h)));
    syncFromParam();
}

float MetalToggle::tipOffset() const {
    const int n = (int) labels_.size();
    if (n < 2) return 0.0f;
    // prototype: translateY(-6 + idx * (12 / (n - 1))) px on a 36px body
    return (-6.0f + index_ * (12.0f / (n - 1))) * (body_ / 36.0f);
}

void MetalToggle::syncFromParam() {
    int idx = 0;
    if (auto* p = proc_.apvts.getParameter(paramID_)) {
        if (isBool_) idx = (p->getValue() > 0.5f) ? 1 : 0;
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
            idx = c->getIndex();
    }
    idx = juce::jlimit(0, (int) labels_.size() - 1, idx);
    if (idx != index_) { index_ = idx; repaint(); }
}

void MetalToggle::mouseDown(const juce::MouseEvent&) {
    const int next = (index_ + 1) % (int) labels_.size();
    if (auto* p = proc_.apvts.getParameter(paramID_)) {
        p->beginChangeGesture();
        if (isBool_) p->setValueNotifyingHost(next > 0 ? 1.0f : 0.0f);
        else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(p))
            c->setValueNotifyingHost(c->convertTo0to1(next));
        p->endChangeGesture();
        index_ = next;
        repaint();
    }
}

void MetalToggle::resized() {}

void MetalToggle::drawLabelColumn(juce::Graphics& g, juce::Rectangle<float> area) {
    const int n = (int) labels_.size();
    const float rowH = area.getHeight() / n;
    for (int i = 0; i < n; ++i) {
        juce::Rectangle<float> row(area.getX(), area.getY() + i * rowH,
                                   area.getWidth(), rowH);
        const bool active = (i == index_);
        g.setColour(active ? col(0xff4ade80) : col(0xb3ffffff));
        if (iconMode_ == 1) {
            // 96Hz icons: flat line (OFF) / high-pass bode curve (ON)
            const float cx = row.getCentreX(), cy = row.getCentreY();
            if (i == 0) {
                g.drawLine(cx - 8.0f, cy, cx + 8.0f, cy, 2.0f);
            } else {
                juce::Path bode;
                bode.startNewSubPath(cx - 10.0f, cy + 4.0f);
                bode.lineTo(cx - 2.0f, cy + 4.0f);
                bode.quadraticTo(cx + 2.0f, cy + 4.0f, cx + 4.0f, cy);
                bode.quadraticTo(cx + 6.0f, cy - 4.0f, cx + 10.0f, cy - 4.0f);
                g.strokePath(bode, juce::PathStrokeType(1.5f));
            }
        } else {
            g.setFont(BiteyFonts::robotoCondensed(8.0f));
            auto just = labelsOnRight_ ? juce::Justification::centredLeft
                                       : juce::Justification::centredRight;
            g.drawText(labels_[i], row, just);
        }
    }
}

void MetalToggle::paint(juce::Graphics& g) {
    const float W = getWidth();
    float y = 0.0f;

    // Caption above (IPS / TAPE / FREQ style)
    if (caption_.isNotEmpty() && !captionBelow_) {
        g.setFont(BiteyFonts::robotoCondensed(7.0f));
        g.setColour(col(0xe6ffffff));
        g.drawText(caption_, 0, 0, int(W), 12, juce::Justification::centred);
        y += 12.0f;
    }

    const int n = (int) labels_.size();
    const float labelColH = juce::jmax(body_, float(n) * 11.0f);
    const float bodyY = y + (labelColH - body_) * 0.5f;

    auto font = BiteyFonts::robotoCondensed(8.0f);
    float labelW = 0.0f;
    for (auto& l : labels_)
        labelW = juce::jmax(labelW, juce::GlyphArrangement::getStringWidth(font, l));
    if (iconMode_ == 1) labelW = 20.0f;

    const float bodyX = labelsOnRight_ ? 0.0f : labelW + 6.0f;
    const float labelX = labelsOnRight_ ? body_ + 6.0f : 0.0f;
    const float bcx = bodyX + body_ * 0.5f;
    const float bcy = bodyY + body_ * 0.5f;

    // Mounting nut (hexagonal, like real toggle switches)
    const float nutR = body_ * 0.5f;
    juce::Path hex;
    for (int i = 0; i < 6; ++i) {
        const float a = i * juce::MathConstants<float>::twoPi / 6.0f + juce::MathConstants<float>::pi / 6.0f;
        const float px = bcx + std::cos(a) * nutR;
        const float py = bcy + std::sin(a) * nutR;
        if (i == 0) hex.startNewSubPath(px, py);
        else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient nut(col(0xffd8d8d8), bcx - nutR, bcy - nutR,
                             col(0xff707070), bcx + nutR, bcy + nutR, true);
    g.setGradientFill(nut);
    g.fillPath(hex);
    g.setColour(col(0xff505050));
    g.strokePath(hex, juce::PathStrokeType(1.5f));

    // Dark well (recessed)
    g.setColour(col(0xff0d0d0d));
    g.fillEllipse(bcx - body_ * 0.38f, bcy - body_ * 0.38f, body_ * 0.76f, body_ * 0.76f);
    // Inner shadow ring for depth
    g.setColour(col(0x66000000));
    g.drawEllipse(bcx - body_ * 0.38f, bcy - body_ * 0.38f, body_ * 0.76f, body_ * 0.76f, 3.0f);

    // Toggle bat: 3D metal lever (not just a tip)
    // The bat pivots at center and tilts up/down based on state
    const float batLen = body_ * 0.42f;
    const float batW = body_ * 0.16f;
    const float tilt = tipOffset(); // positive = down, negative = up

    g.saveState();
    // Bat base (pivot point)
    juce::ColourGradient pivot(col(0xffa0a0a0), bcx - 6.0f, bcy - 6.0f,
                               col(0xff404040), bcx + 6.0f, bcy + 6.0f, true);
    g.setGradientFill(pivot);
    g.fillEllipse(bcx - 6.0f, bcy - 6.0f, 12.0f, 12.0f);

    // Bat lever: rounded rectangle with 3D shading, tilted by state
    const float batX = bcx - batW * 0.5f;
    const float batY = bcy - batLen + tilt * 0.3f; // lever extends up, tilts with state

    // Bat shadow
    g.setColour(col(0x77000000));
    g.fillRoundedRectangle(batX + 2.0f, batY + 2.0f, batW, batLen, batW * 0.5f);

    // Bat body: chrome/metal gradient
    juce::ColourGradient bat(col(0xfff0f0f0), batX, batY,
                             col(0xff909090), batX + batW, batY, false);
    bat.addColour(0.5, col(0xffc8c8c8));
    g.setGradientFill(bat);
    g.fillRoundedRectangle(batX, batY, batW, batLen, batW * 0.5f);

    // Bat highlight (left edge)
    g.setColour(col(0xaaffffff));
    g.fillRoundedRectangle(batX + 1.0f, batY + 2.0f, 2.0f, batLen - 4.0f, 1.0f);

    // Bat tip: slightly larger, rounded
    const float tipY = batY - 2.0f;
    juce::ColourGradient tipGrad(col(0xffffffff), bcx - batW, tipY,
                                 col(0xffa0a0a0), bcx + batW, tipY + 8.0f, true);
    g.setGradientFill(tipGrad);
    g.fillEllipse(bcx - batW * 0.7f, tipY, batW * 1.4f, 10.0f);

    g.restoreState();

    // Labels
    drawLabelColumn(g, juce::Rectangle<float>(labelX, y, labelW, labelColH));

    // Caption below (PAD / 96Hz style)
    if (caption_.isNotEmpty() && captionBelow_) {
        g.setFont(BiteyFonts::robotoCondensed(7.0f));
        g.setColour(col(0xe6ffffff));
        g.drawText(caption_, 0, int(y + labelColH), int(W), 12,
                   juce::Justification::centred);
    }
}
// ---------------------------------------------------------------------------
// BatToggle — the prototype's ToggleSwitch.
// ---------------------------------------------------------------------------

BatToggle::BatToggle(BiteyProcessor& proc, const juce::String& paramID,
                     const juce::String& title)
    : proc_(proc), paramID_(paramID), title_(title) {
    setSize(64, 64);
    syncFromParam();
}

void BatToggle::setTitle(const juce::String& t) {
    if (t != title_) { title_ = t; repaint(); }
}

void BatToggle::setInverted(bool inv) {
    if (inv != inverted_) { inverted_ = inv; syncFromParam(); }
}

bool BatToggle::paramToVisual(bool paramOn) const {
    return inverted_ ? !paramOn : paramOn;
}

void BatToggle::syncFromParam() {
    if (auto* p = proc_.apvts.getParameter(paramID_)) {
        const bool on = paramToVisual(p->getValue() > 0.5f);
        if (on != isOn_) { isOn_ = on; repaint(); }
    }
}

void BatToggle::mouseDown(const juce::MouseEvent&) {
    if (auto* p = proc_.apvts.getParameter(paramID_)) {
        const bool nextVisual = !paramToVisual(p->getValue() > 0.5f);
        const bool nextParam = inverted_ ? !nextVisual : nextVisual;
        p->beginChangeGesture();
        p->setValueNotifyingHost(nextParam ? 1.0f : 0.0f);
        p->endChangeGesture();
        isOn_ = nextVisual;
        repaint();
    }
}

void BatToggle::paint(juce::Graphics& g) {
    const int W = getWidth();
    g.setFont(BiteyFonts::robotoCondensed(7.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText(title_, 0, 0, W, 12, juce::Justification::centred);

    const float cx = W * 0.5f;
    const float cy = 14.0f + 20.0f; // body centre

    // Contact shadow
    g.setColour(col(0x80000000));
    g.fillEllipse(cx - 6.0f, cy + (isOn_ ? 10.0f : -14.0f), 12.0f, 24.0f);

    // Mounting ring
    juce::ColourGradient ring(col(0xff555555), cx - 12.0f, cy - 16.0f,
                              col(0xff1a1a1a), cx + 12.0f, cy + 16.0f, true);
    g.setGradientFill(ring);
    g.fillEllipse(cx - 16.0f, cy - 16.0f, 32.0f, 32.0f);
    g.setColour(col(0xff666666));
    g.drawEllipse(cx - 16.0f, cy - 16.0f, 32.0f, 32.0f, 1.0f);

    // Hex nut
    juce::Path hex;
    for (int i = 0; i < 6; ++i) {
        const float a = (60.0f * i - 90.0f) * kDeg2Rad;
        const float px = cx + std::cos(a) * 12.0f, py = cy + std::sin(a) * 12.0f;
        if (i == 0) hex.startNewSubPath(px, py);
        else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient hexG(col(0xffbbbbbb), cx - 8.0f, cy - 8.0f,
                              col(0xff444444), cx + 8.0f, cy + 8.0f, true);
    hexG.addColour(0.5, col(0xff666666));
    g.setGradientFill(hexG);
    g.fillPath(hex);
    g.setColour(col(0xff111111));
    g.fillEllipse(cx - 8.0f, cy - 8.0f, 16.0f, 16.0f);

    // Bat lever: tilted bar, offset up/down
    const float tilt = isOn_ ? -16.0f : 16.0f; // degrees, visual approx
    const float yOff = isOn_ ? -8.0f : 8.0f;
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(tilt * kDeg2Rad, cx, cy)
                       .translated(0.0f, yOff * 0.4f));
    juce::ColourGradient lever(col(0xff888888), cx - 6.0f, cy - 20.0f,
                               col(0xffe0e0e0), cx, cy - 20.0f, false);
    lever.addColour(1.0, col(0xff888888));
    g.setGradientFill(lever);
    const float lw = 12.0f, lh = 40.0f;
    g.fillRoundedRectangle(cx - lw / 2, cy - lh / 2 + yOff * 0.6f, lw, lh, 6.0f);
    // highlight stripe
    g.setColour(col(0x99ffffff));
    g.fillRoundedRectangle(cx - 2.0f, cy - lh / 2 + 8.0f + yOff * 0.6f, 4.0f, 24.0f, 2.0f);
    g.restoreState();
}

// ---------------------------------------------------------------------------
// PowerJewel
// ---------------------------------------------------------------------------

PowerJewel::PowerJewel() {
    setSize(56, 56);
    startTimerHz(12);
}

void PowerJewel::setOn(bool on) {
    if (on != isOn_) { isOn_ = on; repaint(); }
}

void PowerJewel::timerCallback() {
    if (isOn_) {
        pulse_ += 0.25f;
        repaint();
    }
}

void PowerJewel::paint(juce::Graphics& g) {
    const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
    const float r = 25.0f;

    // Bezel
    g.setColour(col(0xff000000));
    g.fillEllipse(cx - r - 3, cy - r - 3, (r + 3) * 2, (r + 3) * 2);
    g.setColour(col(0xff555555));
    g.drawEllipse(cx - r - 3, cy - r - 3, (r + 3) * 2, (r + 3) * 2, 3.0f);

    // Lens
    juce::ColourGradient lens(
        isOn_ ? col(0xffffdd88) : col(0xff552200), cx - r * 0.4f, cy - r * 0.4f,
        isOn_ ? col(0xffcc5500) : col(0xff110500), cx + r * 0.5f, cy + r * 0.5f, true);
    if (isOn_) lens.addColour(0.4, col(0xffffaa00));
    else lens.addColour(0.6, col(0xff331100));
    g.setGradientFill(lens);
    g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

    // Pulse glow when on
    if (isOn_) {
        const float a = 0.35f + 0.25f * std::sin(pulse_);
        g.setColour(col(0xffffaa00).withAlpha(a * 0.5f));
        g.fillEllipse(cx - r * 0.6f, cy - r * 0.6f, r * 1.2f, r * 1.2f);
    }

    // Specular
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-45.0f * kDeg2Rad, cx, cy));
    g.setColour(col(0xb3ffffff));
    g.fillEllipse(cx - 14.0f, cy - 20.0f, 10.0f, 5.0f);
    g.restoreState();
}

// ---------------------------------------------------------------------------
// VUMeterComp — direct port of VUMeter.tsx canvas rendering.
// ---------------------------------------------------------------------------

VUMeterComp::VUMeterComp(BiteyProcessor& proc, bool reverbMeter)
    : proc_(proc), reverbMeter_(reverbMeter) {
    // 220x110: 200x85 face + padding for the drop shadow (prototype CSS
    // box-shadow overflows the 200x85 layout box; JUCE clips to bounds).
    setSize(220, 110);

    // Pre-generated static noise texture (face-sized)
    noise_ = juce::Image(juce::Image::ARGB, 200, 85, true);
    juce::Random rng(0x600d);
    for (int y = 0; y < 85; ++y)
        for (int x = 0; x < 200; ++x)
            if (rng.nextFloat() > 0.5f) {
                const int n = int(rng.nextFloat() * 30.0f);
                noise_.setPixelAt(x, y, juce::Colour((juce::uint8) n, (juce::uint8) n, (juce::uint8) n, (juce::uint8) 20));
            }

    startTimerHz(60);
}

void VUMeterComp::setDimmed(bool dimmed) {
    if (dimmed != dimmed_) { dimmed_ = dimmed; setAlpha(dimmed ? 0.4f : 1.0f); }
}

float VUMeterComp::readLevel() {
    if (auto* p = proc_.apvts.getParameter("power"))
        if (!(p->getValue() > 0.5f)) return 0.0f; // browser forces 0 when off
    return reverbMeter_ ? proc_.getReverbMeter() : proc_.getMainMeter();
}

void VUMeterComp::timerCallback() {
    const float target = readLevel();
    if (target > smoothed_) smoothed_ += (target - smoothed_) * 0.2f;
    else smoothed_ += (target - smoothed_) * 0.05f;
    repaint();
}

void VUMeterComp::paint(juce::Graphics& g) {
    // UAD-style VU meter: wide black plastic bezel with screws, recessed face under glass,
    // backlit, realistic needle with pivot cap. Component is 220x110.
    const float bx = 2.0f, by = 2.0f;          // bezel outer
    const float bw = 216.0f, bh = 106.0f;
    const float bezelThick = 20.0f;            // FAT black plastic bezel covering blue edges
    const float fx = bx + bezelThick, fy = by + bezelThick;
    const float w = bw - bezelThick * 2, h = bh - bezelThick * 2; // face: 176x66
    const float s = w / 300.0f;
    juce::Rectangle<float> bezel(bx, by, bw, bh);
    juce::Rectangle<float> face(fx, fy, w, h);

    // Drop shadow under bezel
    {
        juce::Path sp; sp.addRectangle(bezel);
        juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 12, juce::Point<int>(0, 6))
            .drawForPath(g, sp);
    }

    // Bezel: WIDE black plastic frame (like real VU meter housing)
    // The face sits deep inside, recessed under glass
    {
        // Black plastic with subtle texture
        juce::ColourGradient bg(col(0xff1e1e1e), bx, by, col(0xff0a0a0a), bx, by + bh, false);
        bg.addColour(0.5, col(0xff151515));
        g.setGradientFill(bg);
        g.fillRect(bezel);

        // Top edge highlight (plastic sheen)
        g.setColour(col(0x44ffffff));
        g.fillRect(juce::Rectangle<float>(bx + 1, by + 1, bw - 2, 3));

        // Inner bevel: the glass sits in a recessed channel
        // Outer bevel (light catching the top edge of the recess)
        g.setColour(col(0x88333333));
        g.drawRect(face.expanded(3.0f), 2.0f);
        // Deep inner shadow (the face is pushed back)
        g.setColour(col(0xee000000));
        g.drawRect(face.expanded(1.0f), 4.0f);
        // Inner highlight at bottom (light bouncing inside the recess)
        g.setColour(col(0x33ffffff));
        g.drawLine(face.getX(), face.getBottom() + 2.0f,
                   face.getRight(), face.getBottom() + 2.0f, 1.5f);
    }

    // Corner screws
    {
        const float sr = 4.0f;
        const juce::Point<float> corners[] = {
            {bx + bezelThick * 0.5f, by + bezelThick * 0.5f},
            {bx + bw - bezelThick * 0.5f, by + bezelThick * 0.5f},
            {bx + bezelThick * 0.5f, by + bh - bezelThick * 0.5f},
            {bx + bw - bezelThick * 0.5f, by + bh - bezelThick * 0.5f}
        };
        for (int i = 0; i < 4; ++i) {
            // Screw head: dark metallic circle
            juce::ColourGradient sg(col(0xff555555), corners[i].x - sr, corners[i].y - sr,
                                    col(0xff1a1a1a), corners[i].x + sr, corners[i].y + sr, false);
            g.setGradientFill(sg);
            g.fillEllipse(corners[i].x - sr, corners[i].y - sr, sr * 2, sr * 2);
            // Slot (rotated per screw for realism)
            const float ang = (i * 0.7f) + 0.3f;
            const float sl = sr * 0.75f;
            g.setColour(col(0xff0a0a0a));
            g.drawLine(corners[i].x - std::cos(ang) * sl, corners[i].y - std::sin(ang) * sl,
                       corners[i].x + std::cos(ang) * sl, corners[i].y + std::sin(ang) * sl, 1.5f);
        }
    }

    // Backlight glow behind face (warm, like incandescent VU backlight)
    {
        juce::ColourGradient glow(col(0x33ffdd88), fx + w/2, fy + h/2,
                                   col(0x00000000), fx + w/2, fy + h/2 + h, true);
        g.setGradientFill(glow);
        g.fillRect(face);
    }

    // Clip everything below to the face
    g.saveState();
    {
        juce::Path clip;
        clip.addRectangle(face);
        g.reduceClipRegion(clip);
    }

    // Face background: vertical teal gradient
    juce::ColourGradient bg(col(0xff0088aa), fx, fy,
                            col(0xff002233), fx, fy + h, false);
    bg.addColour(0.5, col(0xff005577));
    g.setGradientFill(bg);
    g.fillRect(face);

    // Static noise (prototype: 'overlay' blend at 0.5 alpha; JUCE has no
    // overlay mode, so pre-darkened speckle at low opacity approximates it)
    g.setOpacity(0.5f);
    g.drawImageAt(noise_, int(fx), int(fy));
    g.setOpacity(1.0f);

    // Vignette: prototype is a radial gradient centred at (w/2, h/1.5)
    // with inner radius w*0.1 and outer radius w*0.95.
    // Stops: 0 -> rgba(255,255,255,0.3), 0.3 -> rgba(100,220,255,0.1),
    //        1 -> rgba(0,0,0,0.5).
    // Inside the inner radius is transparent (no vignette).
    // JUCE radial gradients run centre -> radius, so remap stops:
    //   0.1/0.95 = 0.105, (0.1+0.3*0.85)/0.95 = 0.374.
    {
        const float vcx = fx + w * 0.5f, vcy = fy + h / 1.5f;
        const float vr = w * 0.95f;
        juce::ColourGradient vig(col(0x00ffffff), vcx, vcy,
                                 col(0x80000000), vcx + vr, vcy, true);
        vig.addColour(0.105, col(0x4dffffff));
        vig.addColour(0.374, col(0x1a64dcff));
        g.setGradientFill(vig);
        g.fillRect(face);
    }

    const float cx = fx + w / 2, cy = fy + h * 1.50f, r = h * 1.42f;
    const float startAngle = -3.14159265f * 0.75f;
    const float endAngle = -3.14159265f * 0.25f;
    const float totalAngle = endAngle - startAngle;
    const float zeroPos = 0.72f;
    const float redStartAngle = startAngle + zeroPos * totalAngle;

    // Red zone arc
    {
        juce::Path p;
        p.addArc(cx - r, cy - r, r * 2, r * 2, redStartAngle, endAngle, true);
        g.setColour(col(0xe6dc3232));
        g.strokePath(p, juce::PathStrokeType(6.0f * s));
    }
    // Black arc
    {
        juce::Path p;
        p.addArc(cx - r, cy - r, r * 2, r * 2, startAngle, redStartAngle, true);
        g.setColour(col(0xe6141414));
        g.strokePath(p, juce::PathStrokeType(2.0f * s));
    }

    // Scale ticks
    struct Tick { float pos; const char* label; bool big; bool red; };
    const Tick ticks[] = {
        {0.00f, "20", true, false}, {0.30f, "10", true, false},
        {0.42f, "", false, false}, {0.52f, "5", true, false},
        {0.61f, "3", true, false}, {0.65f, "", false, false},
        {0.685f, "", false, false}, {0.72f, "0", true, true},
        {0.79f, "", false, true}, {0.86f, "", false, true},
        {1.00f, "3", true, true},
    };
    g.setFont(BiteyFonts::robotoCondensed(12.0f * s));
    for (const auto& t : ticks) {
        const float a = startAngle + t.pos * totalAngle;
        const float ca = std::cos(a), sa = std::sin(a);
        const float tickLen = (t.big ? 12.0f : 7.0f) * s;
        g.setColour(col(t.red ? 0xffcc3333 : 0xff111111));
        g.drawLine(cx + ca * r, cy + sa * r,
                   cx + ca * (r - tickLen), cy + sa * (r - tickLen),
                   (t.big ? 2.5f : 1.5f) * s);
        if (t.label[0] != '\0') {
            const float td = 28.0f * s;
            g.drawText(t.label, int(cx + ca * (r - td)) - 20, int(cy + sa * (r - td)) - 10,
                       40, 20, juce::Justification::centred);
        }
    }

    // Branding: Nathan's hand-drawn BITEY logo (embedded PNG with transparency)
    {
        static juce::Image logoImg;
        if (!logoImg.isValid()) {
            logoImg = juce::ImageCache::getFromMemory(BinaryData::biteylogo_png,
                                                      BinaryData::biteylogo_pngSize);
        }
        if (logoImg.isValid()) {
            const float maxW = w * 0.55f;
            const float maxH = h * 0.42f;
            const float imgAspect = (float)logoImg.getWidth() / (float)logoImg.getHeight();
            float dw = maxW, dh = dw / imgAspect;
            if (dh > maxH) { dh = maxH; dw = dh * imgAspect; }
            const float dx = fx + (w - dw) * 0.5f;
            const float dy = fy + h * 0.68f;
            g.drawImage(logoImg, juce::Rectangle<float>(dx, dy, dw, dh));
        }
    }
    g.setFont(BiteyFonts::robotoCondensed(8.0f * s));
    g.setColour(col(0xcc000000));
    g.drawText("MOD. 600", int(fx), int(fy + h * 0.65f + 15 * s) - 8, int(w), 16,
               juce::Justification::centred);

    // Prototype draws VU left-aligned-ish and dB right-aligned at the top.
    g.setFont(BiteyFonts::robotoCondensed(11.0f * s));
    g.setColour(col(0xff111111));
    g.drawText("VU", int(fx + w * 0.12f) - 20, int(fy + h * 0.25f) - 10, 40, 20,
               juce::Justification::centred);
    g.setColour(col(0xffcc3333));
    g.drawText("dB", int(fx + w * 0.88f) - 40, int(fy + h * 0.25f) - 10, 40, 20,
               juce::Justification::centredRight);

    // Needle (2px for visibility on high-DPI; prototype is 1.5*s ≈ 1px)
    const float targetPos = juce::jlimit(-0.05f, 1.05f, smoothed_ * 0.8f);
    const float na = startAngle + targetPos * totalAngle;
    const float nca = std::cos(na), nsa = std::sin(na);
    const float shadowOff = 4.0f * s;
    g.setColour(col(0x66000000));
    g.drawLine(cx + shadowOff, cy + shadowOff,
               cx + nca * (r - 10.0f) + shadowOff, cy + nsa * (r - 10.0f) + shadowOff,
               2.5f);
    g.setColour(col(0xff1a1a1a));
    g.drawLine(cx, cy, cx + nca * (r - 5.0f), cy + nsa * (r - 5.0f), 2.0f);
    const float tipR = r - 25.0f * s;
    g.setColour(col(0xffcc3333));
    g.drawLine(cx + nca * tipR, cy + nsa * tipR,
               cx + nca * (r - 5.0f), cy + nsa * (r - 5.0f), 1.5f * s);

    // Pivot cover (dome)
    {
        juce::Path dome;
        dome.addArc(cx - 40.0f * s, fy + h + 10.0f - 40.0f * s, 80.0f * s, 80.0f * s,
                    3.14159265f, 6.2831853f, true);
        dome.closeSubPath();
        g.setColour(col(0xff111111));
        g.fillPath(dome);
    }

    // Glass: top sheen (prototype: white/10 gradient over top 45%)
    {
        juce::ColourGradient sheen(col(0x1affffff), fx, fy,
                                   col(0x00ffffff), fx, fy + h * 0.45f, false);
        g.setGradientFill(sheen);
        g.fillRect(juce::Rectangle<float>(fx, fy, w, h * 0.45f));
    }

    // Inner dark edge (prototype: inset 0 0 20px rgba(0,0,0,0.9))
    {
        juce::ColourGradient inner(col(0x00000000), fx, fy,
                                   col(0xe6000000), fx, fy + 14.0f, false);
        g.setGradientFill(inner);
        juce::Path ip;
        ip.addRectangle(face);
        g.saveState();
        g.reduceClipRegion(ip);
        g.fillRect(juce::Rectangle<float>(fx, fy, w, 14.0f));
        g.fillRect(juce::Rectangle<float>(fx, fy + h - 14.0f, w, 14.0f));
        g.fillRect(juce::Rectangle<float>(fx, fy, 14.0f, h));
        g.fillRect(juce::Rectangle<float>(fx + w - 14.0f, fy, 14.0f, h));
        g.restoreState();
    }

    g.restoreState(); // unclip face
    // GLASS COVER: wide bezel + glass makes scale/logo look recessed underneath
    // Diagonal reflection (top-left) - the key "under glass" cue
    {
        g.saveState();
        juce::Path gp;
        gp.addRectangle(face);
        g.reduceClipRegion(gp);

        // Glass tint (subtle blue-grey like real meter glass)
        g.setColour(col(0x0a8aa8c8));
        g.fillRect(face);

        // Main diagonal reflection
        juce::Path refl;
        refl.startNewSubPath(fx, fy);
        refl.lineTo(fx + w * 0.45f, fy);
        refl.lineTo(fx + w * 0.15f, fy + h);
        refl.lineTo(fx, fy + h);
        refl.closeSubPath();
        juce::ColourGradient rg(col(0x35ffffff), fx, fy,
                                col(0x00ffffff), fx + w * 0.3f, fy + h * 0.5f, false);
        g.setGradientFill(rg);
        g.fillPath(refl);

        // Faint secondary reflection (bottom-right)
        g.setColour(col(0x18ffffff));
        g.fillRect(juce::Rectangle<float>(fx + w * 0.7f, fy + h * 0.75f, w * 0.3f, h * 0.25f));

        // Glass edge where it meets the bezel
        g.setColour(col(0x66ffffff));
        g.drawRect(face, 1.5f);

        g.restoreState();
    }
}

// ---------------------------------------------------------------------------
// BoardTape
// ---------------------------------------------------------------------------

BoardTape::BoardTape() { setSize(112, 40); }

void BoardTape::paint(juce::Graphics& g) {
    const float w = getWidth(), h = getHeight();
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-2.0f * kDeg2Rad, w / 2, h / 2));

    // Torn masking-tape polygon (from the CSS clip-path)
    juce::Path tape;
    const float px[8] = { 0.02f, 0.05f, 0.95f, 0.98f, 1.00f, 0.96f, 0.04f, 0.00f };
    const float py[8] = { 0.04f, 0.00f, 0.02f, 0.05f, 0.95f, 1.00f, 0.98f, 0.90f };
    for (int i = 0; i < 8; ++i) {
        const float x = px[i] * w, y = py[i] * h;
        if (i == 0) tape.startNewSubPath(x, y);
        else tape.lineTo(x, y);
    }
    tape.closeSubPath();
    g.setColour(col(0xe6e8e0c8)); // 90% opacity like the prototype
    g.fillPath(tape);

    // Paper fibre creases
    g.setColour(col(0x1a000000));
    g.drawLine(w * 0.21f, 0.0f, w * 0.21f, h, 1.0f);
    g.setColour(col(0x0d000000));
    g.drawLine(w * 0.75f, 0.0f, w * 0.75f, h, 2.0f);

    // Handwritten label
    g.setFont(BiteyFonts::permanentMarker(h * 0.32f));
    g.setColour(col(0xd9333333));
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-1.0f * kDeg2Rad, w / 2, h / 2));
    g.drawText("reverb mixer", 0, 0, int(w), int(h), juce::Justification::centred);
    g.restoreState();

    g.restoreState();
}

// ---------------------------------------------------------------------------
// PanelBox
// ---------------------------------------------------------------------------

PanelBox::PanelBox(const juce::String& title, juce::Colour bg)
    : title_(title), bg_(bg) {}

void PanelBox::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat();
    g.setColour(bg_);
    g.fillRoundedRectangle(r, 4.0f);
    // Thin crisp white outline like the original PA-600
    g.setColour(col(0xffe8e8e8));
    g.drawRoundedRectangle(r.reduced(1.5f), 3.0f, 1.5f);

    if (title_.isNotEmpty()) {
        auto font = BiteyFonts::robotoCondensed(10.0f);
        g.setFont(font);
        const float tw = juce::GlyphArrangement::getStringWidth(font, title_) + 18.0f;
        const float cx = r.getCentreX();
        juce::Rectangle<float> plaque(cx - tw / 2, 7.0f, tw, 19.0f);
        g.setColour(bg_);
        g.fillRect(plaque);
        g.setColour(col(0xff333333));
        g.drawRect(plaque, 1.0f);
        g.setColour(col(0xffffffff));
        g.drawText(title_, plaque, juce::Justification::centred);
    }
}

void PanelBox::resized() {}

// ---------------------------------------------------------------------------
// Strips
// ---------------------------------------------------------------------------

namespace {
// Nathan's hand-drawn BITEY logo (shared helper)
void drawBiteyLogo(juce::Graphics& g, float x, float y, float w, float h) {
    static juce::Image logoImg;
    if (!logoImg.isValid()) {
        logoImg = juce::ImageCache::getFromMemory(BinaryData::biteylogo_png,
                                                  BinaryData::biteylogo_pngSize);
    }
    if (logoImg.isValid()) {
        const float imgAspect = (float)logoImg.getWidth() / (float)logoImg.getHeight();
        float dw = w, dh = dw / imgAspect;
        if (dh > h) { dh = h; dw = dh * imgAspect; }
        const float dx = x + (w - dw) * 0.5f;
        const float dy = y + (h - dh) * 0.5f;
        g.drawImage(logoImg, juce::Rectangle<float>(dx, dy, dw, dh));
    }
}

void drawKnobLabel(juce::Graphics& g, const juce::String& text, int x, int y, int w) {
    g.setFont(BiteyFonts::robotoCondensed(10.0f));
    g.setColour(col(0x80000000));
    g.drawText(text, x, y + 1, w, 14, juce::Justification::centred);
    g.setColour(col(0xffffffff));
    g.drawText(text, x, y, w, 14, juce::Justification::centred);
}

// Screen divider line (the prototype's ScreenLine)
void drawScreenLine(juce::Graphics& g, int x, int y, int w) {
    g.setColour(col(0xfff0f0f0));
    g.fillRect(x, y, w, 4);
    g.setColour(col(0x80000000));
    g.fillRect(x, y + 4, w, 2);
}
} // namespace

// ---------------------------------------------------------------------------
// ChannelStrip
// ---------------------------------------------------------------------------

ChannelStrip::ChannelStrip(BiteyProcessor& proc, int index,
                           const juce::String& title, const juce::String& number)
    : PanelBox(title, col(0xff1a1a1a)), proc_(proc), number_(number) {
    const juce::String p = (index == 0) ? "ch1_" : "ch2_";
    kReverb_ = std::make_unique<BiteyKnob>(proc, p + "fx", 68, false, BiteyKnob::Scale::ZeroToTen);
    kHigh_ = std::make_unique<BiteyKnob>(proc, p + "high", 68, false, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, p + "low", 68, false, BiteyKnob::Scale::Eq);
    kLevel_ = std::make_unique<BiteyKnob>(proc, p + "level", 110, true, BiteyKnob::Scale::ZeroToTen);
    kReverb_->setKnobLabel("REVERB");
    kHigh_->setKnobLabel("HIGH");
    kLow_->setKnobLabel("LOW");
    kLevel_->setKnobLabel("LEVEL");
    lowCut_ = std::make_unique<MetalToggle>(proc, p + "lowcut", "96Hz", true,
                                            std::vector<juce::String>{"", ""},
                                            true, 1 /* icons */);
    pad_ = std::make_unique<MetalToggle>(proc, p + "pad", "PAD", true,
                                        std::vector<juce::String>{"+10", "+4", "0", "-10"},
                                        false /* labels left */);
    addAndMakeVisible(*kReverb_); addAndMakeVisible(*kHigh_);
    addAndMakeVisible(*kLow_); addAndMakeVisible(*kLevel_);
    addAndMakeVisible(*lowCut_); addAndMakeVisible(*pad_);
}

void ChannelStrip::syncToggles() {
    lowCut_->syncFromParam();
    pad_->syncFromParam();
}

void ChannelStrip::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();
    // Knob labels are drawn by the knobs themselves (in the scale gap)
    drawScreenLine(g, 10, 325, W - 20);
    // Channel number
    g.setFont(BiteyFonts::robotoCondensed(22.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText(number_, 58, 458, 29, 40, juce::Justification::centred);
}

void ChannelStrip::resized() {
    kReverb_->setCentrePosition(72, 25 + 40);
    kHigh_->setCentrePosition(72, 145 + 40);
    kLow_->setCentrePosition(72, 265 + 40);
    kLevel_->setCentrePosition(72, 330 + 50);
    lowCut_->setTopLeftPosition(4, 448);
    pad_->setTopLeftPosition(81, 434);
}

// ---------------------------------------------------------------------------
// MasterStrip
// ---------------------------------------------------------------------------

MasterStrip::MasterStrip(BiteyProcessor& proc)
    : PanelBox("MASTER", col(0xff1a1a1a)), proc_(proc) {
    kHigh_ = std::make_unique<BiteyKnob>(proc, "m_high", 68, false, BiteyKnob::Scale::Eq);
    kMid_  = std::make_unique<BiteyKnob>(proc, "m_mid", 68, false, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, "m_low", 68, false, BiteyKnob::Scale::Eq);
    kHigh_->setKnobLabel("HIGH");
    kMid_->setKnobLabel("MID");
    kLow_->setKnobLabel("LOW");
    midFreq_ = std::make_unique<MetalToggle>(proc, "m_midfreq", "FREQ", false,
                                             std::vector<juce::String>{"0.7k", "1.0k", "1.4k"},
                                             false /* labels left */);
    kMain_ = std::make_unique<BiteyKnob>(proc, "m_level", 110, true, BiteyKnob::Scale::ZeroToTen);
    kMain_->setKnobLabel("LEVEL");
    addAndMakeVisible(*kHigh_); addAndMakeVisible(*kMid_); addAndMakeVisible(*kLow_);
    addAndMakeVisible(*midFreq_); addAndMakeVisible(*kMain_);
}

void MasterStrip::syncToggles() { midFreq_->syncFromParam(); }

void MasterStrip::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();
    // Knob labels drawn by knobs themselves
    drawScreenLine(g, 10, 325, W - 20);
    // Large MAIN label at bottom (same height as channel numbers: 22pt)
    g.setFont(BiteyFonts::robotoCondensed(22.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("MAIN", 0, 458, W, 40, juce::Justification::centred);
}

void MasterStrip::resized() {
    kHigh_->setCentrePosition(72, 25 + 40);
    kMid_->setCentrePosition(72, 145 + 40);
    kLow_->setCentrePosition(72, 265 + 40);
    midFreq_->setTopLeftPosition(79, 158);
    kMain_->setCentrePosition(72, 330 + 50);
}

// ---------------------------------------------------------------------------
// ReverbStrip
// ---------------------------------------------------------------------------

ReverbStrip::ReverbStrip(BiteyProcessor& proc)
    : PanelBox("REVERB", col(0xff1a1a1a)), proc_(proc) {
    kDrive_ = std::make_unique<BiteyKnob>(proc, "rev_drive", 68, false, BiteyKnob::Scale::ZeroToTen);
    kContour_ = std::make_unique<BiteyKnob>(proc, "rev_contour", 68, false, BiteyKnob::Scale::ZeroToTen);
    kTime_  = std::make_unique<BiteyKnob>(proc, "rev_time", 68, false, BiteyKnob::Scale::ZeroToTen);
    kReturn_   = std::make_unique<BiteyKnob>(proc, "rev_return", 110, true, BiteyKnob::Scale::ZeroToTen);
    kDrive_->setKnobLabel("DRIVE");
    kContour_->setKnobLabel("CONTOUR");
    kTime_->setKnobLabel("TIME");
    kReturn_->setKnobLabel("LEVEL");
    addAndMakeVisible(*kDrive_); addAndMakeVisible(*kContour_);
    addAndMakeVisible(*kTime_); addAndMakeVisible(*kReturn_);
}

void ReverbStrip::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();
    // Knob labels drawn by knobs themselves
    drawScreenLine(g, 10, 325, W - 20);
    // Large REVERB label at bottom (same height as channel numbers: 22pt)
    g.setFont(BiteyFonts::robotoCondensed(22.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("REVERB", 0, 458, W, 40, juce::Justification::centred);
}

void ReverbStrip::resized() {
    kDrive_->setCentrePosition(72, 25 + 40);
    kContour_->setCentrePosition(72, 145 + 40);
    kTime_->setCentrePosition(72, 265 + 40);
    kReturn_->setCentrePosition(72, 330 + 50);
}

// ---------------------------------------------------------------------------
// CenterPanel
// ---------------------------------------------------------------------------

CenterPanel::CenterPanel(BiteyProcessor& proc)
    : PanelBox("", col(0xff1a1a1a)), proc_(proc) {
    ips_ = std::make_unique<MetalToggle>(proc, "tape_speed", "IPS", false,
                                        std::vector<juce::String>{"7.5", "15", "30"},
                                        true /* labels right */);
    tapeSize_ = std::make_unique<MetalToggle>(proc, "tape_size", "TAPE", false,
                                             std::vector<juce::String>{"1/4\"", "1/2\"", "1\""},
                                             true /* labels right */);
    echo_ = std::make_unique<BiteyKnob>(proc, "tape_mix", 40, false,
                                       BiteyKnob::Scale::None);
    dryWet_ = std::make_unique<BiteyKnob>(proc, "m_mix", 28, false,
                                         BiteyKnob::Scale::None);
    vuReverb_ = std::make_unique<VUMeterComp>(proc, true);
    vuMain_ = std::make_unique<VUMeterComp>(proc, false);
    tape_ = std::make_unique<BoardTape>();
    power_ = std::make_unique<BatToggle>(proc, "power", "POWER");
    phase_ = std::make_unique<BatToggle>(proc, "m_phase", "Ø NOR");
    phase_->setInverted(true); // browser: bat UP = normal phase
    jewel_ = std::make_unique<PowerJewel>();
    addAndMakeVisible(*ips_); addAndMakeVisible(*tapeSize_);
    addAndMakeVisible(*echo_); addAndMakeVisible(*dryWet_);
    addAndMakeVisible(*vuReverb_); addAndMakeVisible(*vuMain_);
    addAndMakeVisible(*tape_); addAndMakeVisible(*power_);
    addAndMakeVisible(*phase_); addAndMakeVisible(*jewel_);
    syncPhaseTitle();
}

void CenterPanel::syncToggles() {
    ips_->syncFromParam();
    tapeSize_->syncFromParam();
    power_->syncFromParam();
    phase_->syncFromParam();
    syncPhaseTitle();
}

void CenterPanel::syncPhaseTitle() {
    bool inv = false;
    if (auto* p = proc_.apvts.getParameter("m_phase"))
        inv = p->getValue() > 0.5f;
    phase_->setTitle(inv ? "Ø REV" : "Ø NOR");
}

void CenterPanel::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();

    // Tape cluster: black strip with top/bottom seams
    g.setColour(col(0x66000000)); // black/40
    g.fillRect(6, 16, W - 12, 100);
    g.setColour(col(0xff333333));
    g.fillRect(6, 16, W - 12, 1);
    g.fillRect(6, 115, W - 12, 1);

    // ECHO caption below the echo knob (centered at 60,70)
    g.setFont(BiteyFonts::robotoCondensed(7.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("ECHO", 30, 98, 60, 14, juce::Justification::centred);

    // VU meter captions (below each meter)
    g.drawText("REVERB", 0, 242, W, 14, juce::Justification::centred);
    g.drawText("MAIN", 0, 364, W, 14, juce::Justification::centred);

    // Board tape: masking tape strip with "REVERB MIXER" in sharpie style
    // Positioned under the VU meters
    const float tapeY = 385.0f;
    const float tapeH = 28.0f;
    const float tapeW = W - 24.0f;
    const float tapeX = 12.0f;

    g.saveState();
    // Slight rotation for realism (-1 degree)
    g.addTransform(juce::AffineTransform::rotation(-0.017f, tapeX + tapeW/2, tapeY + tapeH/2));

    // Masking tape base (beige/tan with slight texture)
    juce::ColourGradient tape(col(0xffe8d5a3), tapeX, tapeY,
                              col(0xffd4b87a), tapeX, tapeY + tapeH, false);
    g.setGradientFill(tape);
    g.fillRect(tapeX, tapeY, tapeW, tapeH);

    // Tape edges (slightly darker)
    g.setColour(col(0xffb8945a));
    g.fillRect(tapeX, tapeY, tapeW, 2.0f);
    g.fillRect(tapeX, tapeY + tapeH - 2.0f, tapeW, 2.0f);

    // Sharpie text: bold, slightly irregular, dark blue-black
    g.setFont(BiteyFonts::robotoCondensed(16.0f));
    g.setColour(col(0xff1a1a2e));  // Sharpie dark blue-black
    // Slight offset for hand-drawn feel
    g.drawText("REVERB MIXER", tapeX + 1.0f, tapeY + 1.0f, tapeW, tapeH,
               juce::Justification::centred);

    g.restoreState();

    // DRY/WET caption below the small knob (centered at 180,70)
    g.drawText("DRY/WET", 150, 94, 60, 12, juce::Justification::centred);

    // Power section divider
    g.setColour(col(0x0dffffff)); // white/5
    g.fillRect(6, 416, W - 12, 1);

    // ON caption above the jewel
    g.setFont(BiteyFonts::robotoCondensed(7.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("ON", 92, 420, 56, 12, juce::Justification::centred);
}

void CenterPanel::resized() {
    ips_->setTopLeftPosition(4, 55);
    tapeSize_->setTopLeftPosition(196, 55);
    // Row 1: ECHO and DRY/WET side by side, aligned with row 1 pots (y=70)
    // ECHO (40px, None): component 56x56, center (60,70) -> top-left (32,42)
    echo_->setTopLeftPosition(32, 42);
    // DRY/WET (28px, None): component 44x44, center (180,70) -> top-left (158,48)
    dryWet_->setTopLeftPosition(158, 48);
    vuReverb_->setTopLeftPosition(10, 110);
    vuMain_->setTopLeftPosition(10, 222);
    tape_->setTopLeftPosition(14, 346);
    power_->setTopLeftPosition(8, 432);
    jewel_->setTopLeftPosition(92, 436);
    phase_->setTopLeftPosition(168, 432);
}

void CenterPanel::syncPower(bool on) {
    jewel_->setOn(on);
    vuReverb_->setDimmed(!on);
    vuMain_->setDimmed(!on);
}

// ---------------------------------------------------------------------------
// WoodCheek
// ---------------------------------------------------------------------------

WoodCheek::WoodCheek(bool left) : left_(left) {}

void WoodCheek::paint(juce::Graphics& g) {
    const int W = getWidth(), H = getHeight();
    juce::ColourGradient wood(col(0xffd89a55), 0.0f, 0.0f,
                              col(0xffa06a35), float(W), 0.0f, false);
    g.setGradientFill(wood);
    g.fillRect(0, 0, W, H);
    // grain streaks
    juce::Random rng(left_ ? 0x1E57 : 0xBEEF);
    g.setColour(col(0x334a2c10));
    for (int i = 0; i < 9; ++i) {
        const float x = rng.nextFloat() * W;
        juce::Path p;
        p.startNewSubPath(x, 0);
        p.cubicTo(x + rng.nextFloat() * 6 - 3, H * 0.3f,
                  x + rng.nextFloat() * 6 - 3, H * 0.6f, x, float(H));
        g.strokePath(p, juce::PathStrokeType(1.2f));
    }
    // edge shading: dark seam toward the panel, light outer edge
    const float sx = left_ ? float(W) : 0.0f;
    juce::ColourGradient seam(col(0xcc000000), sx, 0.0f, col(0x00000000),
                              left_ ? float(W - 8) : 8.0f, 0.0f, false);
    g.setGradientFill(seam);
    g.fillRect(0, 0, W, H);
    g.setColour(col(0x66ffffff));
    g.fillRect(left_ ? 0 : W - 2, 0, 2, H);
}

// ---------------------------------------------------------------------------
// BiteyEditor
// ---------------------------------------------------------------------------

BiteyEditor::BiteyEditor(BiteyProcessor& p)
    : AudioProcessorEditor(p), proc_(p) {
    // NB: create components BEFORE setSize — setSize triggers resized(),
    // which dereferences the strips.
    cheekL_ = std::make_unique<WoodCheek>(true);
    cheekR_ = std::make_unique<WoodCheek>(false);
    chL_ = std::make_unique<ChannelStrip>(p, 0, "LEFT", "1");
    chR_ = std::make_unique<ChannelStrip>(p, 1, "RIGHT", "2");
    center_ = std::make_unique<CenterPanel>(p);
    master_ = std::make_unique<MasterStrip>(p);
    reverb_ = std::make_unique<ReverbStrip>(p);
    addAndMakeVisible(*cheekL_);
    addAndMakeVisible(*cheekR_);
    addAndMakeVisible(*chL_);
    addAndMakeVisible(*chR_);
    addAndMakeVisible(*center_);
    addAndMakeVisible(*master_);
    addAndMakeVisible(*reverb_);

    // film-grain texture for the distress overlay
    grain_ = juce::Image(juce::Image::ARGB, 128, 128, true);
    juce::Random rng(0xD157E55);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            grain_.setPixelAt(x, y, juce::Colour((juce::uint8) int(rng.nextFloat() * 255.0f),
                                                (juce::uint8) int(rng.nextFloat() * 255.0f),
                                                (juce::uint8) int(rng.nextFloat() * 255.0f), (juce::uint8) 14));

    startTimerHz(30);
    timerCallback();
    setSize(1050, 520);
}

void BiteyEditor::resized() {
    // Guard: setSize() in the ctor triggers resized() before strips exist.
    if (!cheekL_ || !chL_ || !center_) return;
    // 38–1012 content, strips 145 wide, center 240, space-between (38.5px gaps)
    cheekL_->setBounds(0, 0, 30, 520);
    cheekR_->setBounds(1020, 0, 30, 520);
    chL_->setBounds(38, 8, 145, 504);
    chR_->setBounds(222, 8, 145, 504);
    center_->setBounds(405, 8, 240, 504);
    master_->setBounds(684, 8, 145, 504);
    reverb_->setBounds(867, 8, 145, 504);
}

void BiteyEditor::timerCallback() {
    chL_->syncToggles();
    chR_->syncToggles();
    master_->syncToggles();
    center_->syncToggles();

    bool on = true;
    if (auto* p = proc_.apvts.getParameter("power"))
        on = p->getValue() > 0.5f;
    if (on != lastPower_) {
        lastPower_ = on;
        const float a = on ? 1.0f : 0.6f;
        chL_->setAlpha(a);
        chR_->setAlpha(a);
        master_->setAlpha(a);
        reverb_->setAlpha(a);
        center_->syncPower(on);
    }
}

void BiteyEditor::paint(juce::Graphics& g) {
    // chassis
    g.setColour(col(0xff161616));
    g.fillRect(getLocalBounds());

    // distress: film grain + vignette
    g.setOpacity(0.5f);
    g.setTiledImageFill(grain_, 0, 0, 0.6f);
    g.fillRect(getLocalBounds());
    g.setOpacity(1.0f);
    juce::ColourGradient vig(col(0x00000000), 525.0f, 260.0f,
                             col(0x66000000), 525.0f, 260.0f, true);
    g.setGradientFill(vig);
    g.fillRect(getLocalBounds());
}
