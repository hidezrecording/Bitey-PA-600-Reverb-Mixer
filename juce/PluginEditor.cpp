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
    float textR = r;  // default for Scale::None (no scale labels)
    if (scale == Scale::None) {
        half_ = r + 8.0f;
        cy_ = r + 8.0f;
    } else {
        // Website: .pk height 9.2cqw (97px) for 66px knob, 11cqw (115px) for 86px
        // Compact layout matching website proportions
        textR = r * (skirted ? 1.15f : 1.25f);
        half_ = textR + 10.0f;
        cy_ = textR + 6.0f;
    }
    // Height includes room for the knob label below (website: absolute positioned)
    setSize(int(std::ceil(half_ * 2.0f)), int(std::ceil(cy_ + textR + 10.0f)));
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
    const float textR = radius * (skirted_ ? 1.15f : 1.25f); // Must match constructor!

    g.setFont(BiteyFonts::robotoCondensed(7.0f));

    // Numeric labels - website shows only min/center/max, not all numbers
    // ZeroToTen: "0" left, "10" right. Eq: "-15" left, "0" top, "+15" right.
    for (int i = 0; i <= 10; ++i) {
        const float angleDeg = -135.0f + (i / 10.0f) * 270.0f;
        const float a = (angleDeg - 90.0f) * kDeg2Rad;
        const float ca = std::cos(a), sa = std::sin(a);

        // Tick mark - thin, elegant white (website style)
        const float tickH = (scale_ == Scale::Eq && i == 5) ? 4.0f : 2.5f;
        const float x1 = cx + ca * tickR, y1 = cy + sa * tickR;
        const float x2 = cx + ca * (tickR - tickH), y2 = cy + sa * (tickR - tickH);
        g.setColour(col(0xe8ffffff));
        g.drawLine(x1, y1, x2, y2, 1.2f);

        // Numeric labels - only at key positions like the website
        bool show = false;
        juce::String label;
        if (scale_ == Scale::ZeroToTen) {
            if (i == 0) { label = "0"; show = true; }
            if (i == 10) { label = "10"; show = true; }
        } else { // Eq
            if (i == 0) { label = "-15"; show = true; }
            if (i == 5) { label = "0"; show = true; }
            if (i == 10) { label = "+15"; show = true; }
        }
        if (show) {
            const float lx = cx + ca * textR, ly = cy + sa * textR;
            g.setColour(col(0xffffffff));
            g.setFont(BiteyFonts::robotoCondensed(8.0f));
            g.drawText(label, int(lx - 20), int(ly - 8), 40, 16,
                       juce::Justification::centred);
        }
    }

    // Knob label: website exact - Roboto Condensed Bold, 1.08cqw (11.3px), uppercase
    // Positioned at bottom of component with clear separation from scale
    if (knobLabel_.isNotEmpty()) {
        juce::Font labelFont(BiteyFonts::robotoCondensed(11.5f));
        labelFont.setBold(true);
        g.setFont(labelFont);
        g.setColour(col(0xffffffff));
        const float labelH = 12.0f;
        const float labelY = float(getHeight()) - labelH - 1.0f;
        g.drawText(knobLabel_, 0, int(labelY), getWidth(), int(labelH),
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

    // Skirt (big knobs): website exact CSS
    // radial-gradient(circle at 35% 28%, #3b352e, #1a1611 58%, #070605 100%)
    if (skirted) {
        juce::ColourGradient skirt(col(0xff3b352e), cx - size * 0.3f, cy - size * 0.44f,
                                   col(0xff070605), cx + size * 0.4f, cy + size * 0.4f, true);
        skirt.addColour(0.58, col(0xff1a1611));
        g.setGradientFill(skirt);
        g.fillEllipse(cx - size * 0.5f, cy - size * 0.5f, size, size);

        // Subtle edge highlight (smooth, not fluted)
        g.setColour(col(0x2a9a8a76));
        g.drawEllipse(cx - size * 0.48f, cy - size * 0.48f, size * 0.96f, size * 0.96f, 2.0f);

        g.setColour(col(0xff151412));
        g.fillEllipse(cx - size * 0.45f, cy - size * 0.45f, size * 0.9f, size * 0.9f);
    }

    const float bodyR = (skirted ? 0.35f : 0.425f) * size;

    // Rotating body: smooth rounded doorknob shape (spherical, not industrial)
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle), cx, cy));

    // Doorknob sphere: website exact CSS
    // radial-gradient(circle at 38% 30%, #4d463d, #28221b 48%, #120e0a 78%, #050403 100%)
    // plus top highlight: radial-gradient(circle at 34% 24%, rgba(255,242,216,.34), transparent 44%)
    juce::ColourGradient body(col(0xff4d463d), cx - bodyR * 0.24f, cy - bodyR * 0.4f,
                              col(0xff050403), cx + bodyR * 0.3f, cy + bodyR * 0.35f, true);
    body.addColour(0.48, col(0xff28221b));
    body.addColour(0.78, col(0xff120e0a));
    g.setGradientFill(body);
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Top highlight (warm light catching the knob)
    juce::ColourGradient hi(col(0x57fff2d8), cx - bodyR * 0.32f, cy - bodyR * 0.52f,
                            col(0x00fff2d8), cx, cy, true);
    hi.addColour(0.44, col(0x00fff2d8));
    g.setGradientFill(hi);
    g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    // Subtle rim light (bottom-right, like light catching the curve)
    g.setColour(col(0x1a8a7a66));
    juce::Path rim;
    rim.addArc(cx - bodyR * 0.95f, cy - bodyR * 0.95f, bodyR * 1.9f, bodyR * 1.9f,
               0.6f, 1.8f, true);
    g.strokePath(rim, juce::PathStrokeType(2.5f));

    // Domed top: smaller, smoother highlight
    const float capR = bodyR * 0.62f;
    juce::ColourGradient cap(col(0xff54514b), cx - capR * 0.45f, cy - capR * 0.5f,
                             col(0xff201d1a), cx + capR * 0.3f, cy + capR * 0.35f, true);
    g.setGradientFill(cap);
    g.fillEllipse(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);

    // Thin ivory pointer: website exact - linear-gradient(180deg, #fff8e6, #eadfc2)
    const float pw = 2.0f;  // thin line
    const float pl = bodyR * 0.88f;
    juce::ColourGradient ptr(col(0xfffff8e6), cx, cy - pl,
                             col(0xffeadfc2), cx, cy - pl * 0.5f, false);
    g.setGradientFill(ptr);
    g.fillRoundedRectangle(cx - pw * 0.5f, cy - pl, pw, pl * 0.5f, pw * 0.5f);

    g.restoreState();
}

// ---------------------------------------------------------------------------
// MetalToggle — the prototype's MetalToggleSwitch.
// ---------------------------------------------------------------------------

MetalToggle::MetalToggle(BiteyProcessor& proc, const juce::String& paramID,
                         const juce::String& caption, bool captionBelow,
                         const std::vector<juce::String>& labels,
                         bool labelsOnRight, int iconMode, bool invertBool)
    : proc_(proc), paramID_(paramID), caption_(caption), captionBelow_(captionBelow),
      labels_(labels), labelsOnRight_(labelsOnRight), iconMode_(iconMode),
      invertBool_(invertBool) {
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
        if (isBool_) {
            bool on = (p->getValue() > 0.5f);
            // invertBool: value 1.0 maps to index 0 (top label, bat UP)
            // e.g. POWER with labels {"ON","OFF"}: ON=1.0 -> "ON" (top) active
            idx = invertBool_ ? (on ? 0 : 1) : (on ? 1 : 0);
        }
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
        if (isBool_) {
            // invertBool: index 0 = ON (value 1.0), index 1 = OFF (value 0.0)
            float v = invertBool_ ? (next == 0 ? 1.0f : 0.0f)
                                  : (next > 0 ? 1.0f : 0.0f);
            p->setValueNotifyingHost(v);
        }
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
        juce::Font capFont(BiteyFonts::robotoCondensed(8.6f));
        capFont.setBold(true);
        capFont.setExtraKerningFactor(0.06f);
        g.setFont(capFont);
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

    // Toggle bat: 3D metal lever that ROTATES around the pivot (like a real switch)
    // The bat pivots ±25 degrees based on state (not just tiny translation)
    const float batLen = body_ * 0.42f;
    const float batW = body_ * 0.16f;
    const int nStates = (int) labels_.size();
    // Map index to angle: first = up (-25°), last = down (+25°)
    float angleDeg = 0.0f;
    if (nStates > 1) {
        float t = float(index_) / float(nStates - 1); // 0..1
        angleDeg = -25.0f + t * 50.0f; // -25° to +25°
    }
    const float angleRad = angleDeg * 3.14159265f / 180.0f;

    g.saveState();
    // Bat base (pivot point)
    juce::ColourGradient pivot(col(0xffa0a0a0), bcx - 6.0f, bcy - 6.0f,
                               col(0xff404040), bcx + 6.0f, bcy + 6.0f, true);
    g.setGradientFill(pivot);
    g.fillEllipse(bcx - 6.0f, bcy - 6.0f, 12.0f, 12.0f);

    // Bat lever: rounded rectangle with 3D shading, tilted by state
    // Bat lever: rotated around pivot (realistic toggle action)
    g.addTransform(juce::AffineTransform::rotation(angleRad, bcx, bcy));
    const float batX = bcx - batW * 0.5f;
    const float batY = bcy - batLen; // lever extends up from pivot

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
        // Website: .tgl-cap - 0.82cqw (8.6px), weight 600, letter-spacing .06em
        juce::Font capFont(BiteyFonts::robotoCondensed(8.6f));
        capFont.setBold(true);
        capFont.setExtraKerningFactor(0.06f);
        g.setFont(capFont);
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
    setSize(36, 36);
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
    const float r = 18.0f;  // Fits in 56x56 component with bezel

    // Outer glow (amber halo when on)
    if (isOn_) {
        juce::ColourGradient halo(col(0x66ff9500), cx, cy,
                                  col(0x00ff9500), cx, cy + r * 2.2f, true);
        g.setGradientFill(halo);
        g.fillEllipse(cx - r * 2.2f, cy - r * 2.2f, r * 4.4f, r * 4.4f);
    }

    // Chrome bezel (Fender-style)
    juce::ColourGradient bezel(col(0xffe8e8e8), cx - r, cy - r,
                               col(0xff707070), cx + r, cy + r, true);
    bezel.addColour(0.5, col(0xffa0a0a0));
    g.setGradientFill(bezel);
    g.fillEllipse(cx - r - 5, cy - r - 5, (r + 5) * 2, (r + 5) * 2);
    g.setColour(col(0xff333333));
    g.drawEllipse(cx - r - 5, cy - r - 5, (r + 5) * 2, (r + 5) * 2, 2.0f);

    // Jewel lens: bright orange-yellow amber (Fender pilot light)
    juce::ColourGradient lens(
        isOn_ ? col(0xffffe066) : col(0xff6b3a00), cx - r * 0.4f, cy - r * 0.5f,
        isOn_ ? col(0xffff8800) : col(0xff241100), cx + r * 0.4f, cy + r * 0.5f, true);
    if (isOn_) {
        lens.addColour(0.3, col(0xffffcc33));
        lens.addColour(0.65, col(0xffffaa00));
    } else {
        lens.addColour(0.6, col(0xff3d2000));
    }
    g.setGradientFill(lens);
    g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

    // Faceted jewel cuts (like a real Fender jewel)
    if (isOn_) {
        g.setColour(col(0x88ffffff));
        for (int i = 0; i < 6; ++i) {
            const float a = i * juce::MathConstants<float>::twoPi / 6.0f;
            g.drawLine(cx, cy,
                       cx + std::cos(a) * r * 0.85f, cy + std::sin(a) * r * 0.85f, 1.0f);
        }
    }

    // Bright specular highlight
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(-35.0f * kDeg2Rad, cx, cy));
    g.setColour(isOn_ ? col(0xd6ffffff) : col(0x55ffffff));
    g.fillEllipse(cx - r * 0.55f, cy - r * 0.75f, r * 0.45f, r * 0.22f);
    g.restoreState();
}

// ---------------------------------------------------------------------------
// ClipBulb — UA 1108 style: single round bulb, green -> yellow -> red.
// ---------------------------------------------------------------------------
ClipBulb::ClipBulb() {
    startTimerHz(30);
}

void ClipBulb::setLevel(float level) {
    level_.store(level);
}

void ClipBulb::timerCallback() {
    // Smooth the level for a nice bulb response
    const float target = level_.load();
    displayLevel_ += (target - displayLevel_) * 0.3f;
    if (std::abs(target - displayLevel_) > 0.001f)
        repaint();
}

void ClipBulb::paint(juce::Graphics& g) {
    const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
    const float r = 7.0f;  // Small LED like the website

    const float lvl = juce::jlimit(0.0f, 1.2f, displayLevel_);

    // Color: green (0.0-0.6) -> yellow (0.6-0.85) -> red (0.85+)
    juce::Colour bulbCol;
    if (lvl < 0.6f) {
        const float b = 0.35f + 0.65f * (lvl / 0.6f);
        bulbCol = juce::Colour::fromFloatRGBA(0.2f * b, 1.0f * b, 0.25f * b, 0.85f);
    } else if (lvl < 0.85f) {
        const float t = (lvl - 0.6f) / 0.25f;
        bulbCol = juce::Colour::fromFloatRGBA(0.35f + 0.65f * t, 1.0f, 0.2f * (1.0f - t), 0.9f);
    } else {
        const float t = juce::jmin(1.0f, (lvl - 0.85f) / 0.35f);
        const float pulse = 0.85f + 0.15f * std::sin(juce::Time::getMillisecondCounter() * 0.012f);
        bulbCol = juce::Colour::fromFloatRGBA(1.0f * pulse, 0.2f * (1.0f - t * 0.5f), 0.12f, 0.95f);
    }

    // Hexagonal outer (same as toggle screws)
    const float hexR = r + 8.0f;
    juce::Path hex;
    for (int i = 0; i < 6; ++i) {
        const float a = i * juce::MathConstants<float>::twoPi / 6.0f + juce::MathConstants<float>::pi / 6.0f;
        const float px = cx + std::cos(a) * hexR;
        const float py = cy + std::sin(a) * hexR;
        if (i == 0) hex.startNewSubPath(px, py);
        else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient hexG(col(0xffc8c8c8), cx - hexR, cy - hexR,
                              col(0xff555555), cx + hexR, cy + hexR, true);
    g.setGradientFill(hexG);
    g.fillPath(hex);
    g.setColour(col(0xff333333));
    g.strokePath(hex, juce::PathStrokeType(1.5f));

    // Dark recess inside hex
    g.setColour(col(0xff0a0a0a));
    g.fillEllipse(cx - r - 2, cy - r - 2, (r + 2) * 2, (r + 2) * 2);

    // Translucent bulb (smaller, glowing from within)
    // Outer glow
    const float glowA = 0.2f + 0.5f * juce::jmin(1.0f, lvl);
    juce::ColourGradient glow(bulbCol.withAlpha(glowA * 0.6f), cx, cy,
                              col(0x00000000), cx, cy + r * 1.8f, true);
    g.setGradientFill(glow);
    g.fillEllipse(cx - r * 1.8f, cy - r * 1.8f, r * 3.6f, r * 3.6f);

    // Bulb glass (translucent)
    juce::ColourGradient glass(bulbCol.brighter(0.5f).withAlpha(0.9f), cx - r * 0.4f, cy - r * 0.5f,
                               bulbCol.darker(0.4f).withAlpha(0.75f), cx + r * 0.3f, cy + r * 0.4f, true);
    g.setGradientFill(glass);
    g.fillEllipse(cx - r, cy - r, r * 2, r * 2);

    // Inner bright core
    g.setColour(bulbCol.brighter(0.6f).withAlpha(0.5f));
    g.fillEllipse(cx - r * 0.45f, cy - r * 0.45f, r * 0.9f, r * 0.9f);

    // Specular
    g.setColour(col(0x99ffffff));
    g.fillEllipse(cx - r * 0.4f, cy - r * 0.55f, r * 0.3f, r * 0.15f);
}

// ---------------------------------------------------------------------------
// VUMeterComp — direct port of VUMeter.tsx canvas rendering.
// ---------------------------------------------------------------------------

VUMeterComp::VUMeterComp(BiteyProcessor& proc, bool reverbMeter)
    : proc_(proc), reverbMeter_(reverbMeter) {
    // Website: VU meter face has 200:104 aspect (1.92:1) from the SVG viewBox
    // Component 263x140: fits in center strip layout
    setSize(263, 140);

    // Load website's exact VU scale (rendered from the site's SVG)
    scaleImg_ = juce::ImageCache::getFromMemory(BinaryData::vu_scale_png,
                                               BinaryData::vu_scale_pngSize);

    // Pre-generated static noise texture (face-sized)
    noise_ = juce::Image(juce::Image::ARGB, 231, 108, true);
    juce::Random rng(0x600d);
    for (int y = 0; y < 108; ++y)
        for (int x = 0; x < 231; ++x)
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
    // UAD-style VU meter: wide black plastic bezel, recessed face under glass,
    // backlit, realistic needle with pivot cap. Uses actual component bounds.
    const float bw = (float)getWidth();
    const float bh = (float)getHeight();
    // Guard against zero/invalid bounds (crash fix)
    if (bw < 50.0f || bh < 50.0f) return;
    const float bx = 2.0f, by = 2.0f;          // bezel outer
    const float bezelThick = 14.0f;            // black plastic bezel
    const float fx = bx + bezelThick, fy = by + bezelThick;
    const float w = bw - 4.0f - bezelThick * 2, h = bh - 4.0f - bezelThick * 2;
    if (w < 10.0f || h < 10.0f) return;
    const float s = w / 300.0f;
    juce::Rectangle<float> bezel(bx, by, bw - 4.0f, bh - 4.0f);
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

    // Zero-adjust screw: ONE small slotted trim screw centered below the meter
    // (like a real VU meter's zero adjustment, not a mounting screw)
    {
        const float sx = bx + bw * 0.5f;  // center horizontally
        const float sy = by + bh - bezelThick * 0.5f;  // centered in bottom bezel
        const float sr = 5.0f;

        // Screw head: small brass/dark metal circle
        juce::ColourGradient sg(col(0xff8a7a5a), sx - sr, sy - sr,
                                col(0xff2a241a), sx + sr, sy + sr, false);
        g.setGradientFill(sg);
        g.fillEllipse(sx - sr, sy - sr, sr * 2, sr * 2);
        // Dark ring around screw
        g.setColour(col(0xff0a0a0a));
        g.drawEllipse(sx - sr, sy - sr, sr * 2, sr * 2, 1.0f);
        // Slot (horizontal, like a trim pot)
        g.setColour(col(0xff0d0b08));
        g.drawLine(sx - sr * 0.7f, sy, sx + sr * 0.7f, sy, 2.0f);
        // Highlight on slot edge
        g.setColour(col(0x44ffffff));
        g.drawLine(sx - sr * 0.7f, sy + 1.0f, sx + sr * 0.7f, sy + 1.0f, 1.0f);
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

    // Face background: website exact CSS
    // linear-gradient(180deg, #0b9dc4 0%, #00779e 38%, #004d6b 78%, #00293d 100%)
    juce::ColourGradient bg(col(0xff0b9dc4), fx, fy,
                            col(0xff00293d), fx, fy + h, false);
    bg.addColour(0.38, col(0xff00779e));
    bg.addColour(0.78, col(0xff004d6b));
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

    const float cx = fx + w / 2, cy = fy + h * 0.846f, r = w * 0.54f;
    const float startAngle = -3.14159265f * 0.75f;
    const float endAngle = -3.14159265f * 0.25f;
    const float totalAngle = endAngle - startAngle;

    // Website-exact VU scale: rendered from the site's own SVG
    // (includes scale arc, ticks, numbers, VU text, zero screw)
    if (scaleImg_.isValid()) {
        // SVG viewBox is 200x104; draw to fill the face
        g.drawImage(scaleImg_, fx, fy, w, h, 0, 0, 
                    scaleImg_.getWidth(), scaleImg_.getHeight());
    }

    // Bitey logo (website: HTML img overlay, centered below scale)
    {
        juce::Image logoImg = juce::ImageCache::getFromMemory(BinaryData::biteylogo_png,
                                                             BinaryData::biteylogo_pngSize);
        if (logoImg.isValid()) {
            // Tint to glowing green like the website
            juce::Image greenLogo(juce::Image::ARGB, logoImg.getWidth(), logoImg.getHeight(), true);
            for (int y = 0; y < logoImg.getHeight(); ++y) {
                for (int x = 0; x < logoImg.getWidth(); ++x) {
                    juce::Colour px = logoImg.getPixelAt(x, y);
                    float b = px.getBrightness();
                    float boost = 0.3f + 0.7f * b;
                    greenLogo.setPixelAt(x, y, juce::Colour::fromFloatRGBA(
                        0.2f * boost, 1.0f * boost, 0.25f * boost, px.getAlpha()));
                }
            }
            // Website: logo centered, about 35% of face width (not covering scale)
            float lw = w * 0.35f;
            float lh = lw * float(greenLogo.getHeight()) / float(greenLogo.getWidth());
            float lx = fx + (w - lw) * 0.5f;
            float ly = fy + h * 0.52f; // Below the scale arc
            g.drawImage(greenLogo, lx, ly, lw, lh, 0, 0,
                        greenLogo.getWidth(), greenLogo.getHeight());
        }
    }


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

    // (Pivot screw is in the SVG scale image; no separate dome needed)

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
    // Website exact: linear-gradient(180deg,#1b1b1b 0%,#141414 55%,#101010 100%)
    juce::ColourGradient bg(col(0xff1b1b1b), 0, 0,
                            col(0xff101010), 0, r.getHeight(), false);
    bg.addColour(0.55, col(0xff141414));
    g.setGradientFill(bg);
    g.fillRoundedRectangle(r, 11.0f);  // website: border-radius 1.05cqw ≈ 11px
    // No white outline — silver chassis shows through as dividers

    // Corner screws (website: .scr, width .95cqw ≈ 10px, at .65cqw ≈ 7px from edges)
    // radial-gradient(circle at 35% 30%, #e4e4e4, #7d7d7d 55%, #3a3a3a)
    const float sr = 5.0f; // screw radius
    const float so = 7.0f + sr; // offset from edge to center
    const float W = r.getWidth(), H = r.getHeight();
    float sx[4] = {so, W - so, so, W - so};
    float sy[4] = {so, so, H - so, H - so};
    for (int i = 0; i < 4; ++i) {
        juce::ColourGradient screw(col(0xffe4e4e4), sx[i] - sr * 0.3f, sy[i] - sr * 0.4f,
                                   col(0xff3a3a3a), sx[i] + sr * 0.4f, sy[i] + sr * 0.4f, true);
        screw.addColour(0.55, col(0xff7d7d7d));
        g.setGradientFill(screw);
        g.fillEllipse(sx[i] - sr, sy[i] - sr, sr * 2, sr * 2);
        // Slot
        g.setColour(col(0xff242424));
        g.drawLine(sx[i] - sr * 0.7f, sy[i], sx[i] + sr * 0.7f, sy[i], 1.5f);
    }

    if (title_.isNotEmpty()) {
        auto font = BiteyFonts::robotoCondensed(10.0f);
        g.setFont(font);
        const float tw = juce::GlyphArrangement::getStringWidth(font, title_) + 18.0f;
        const float cx = r.getCentreX();
        juce::Rectangle<float> plaque(cx - tw / 2, 7.0f, tw, 19.0f);
        g.setColour(col(0xff181818));
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
    kReverb_ = std::make_unique<BiteyKnob>(proc, p + "fx", 66, false, BiteyKnob::Scale::ZeroToTen);
    kHigh_ = std::make_unique<BiteyKnob>(proc, p + "high", 66, false, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, p + "low", 66, false, BiteyKnob::Scale::Eq);
    kLevel_ = std::make_unique<BiteyKnob>(proc, p + "level", 86, true, BiteyKnob::Scale::ZeroToTen);
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
    const int H = getHeight();
    // Knob labels are drawn by the knobs themselves (in the scale gap)
    // Channel label: "CHANNEL 1" or "CHANNEL 2", centered (same size as MAIN/REVERB)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f)); bf.setBold(true); bf.setExtraKerningFactor(0.01f); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    juce::String label = (number_ == "1") ? "CHANNEL 1" : "CHANNEL 2";
    g.drawText(label, 0, H - 44, W, 40, juce::Justification::centred);
}

void ChannelStrip::resized() {
    const int cx = 86; // center x for 173px wide strip (173/2=86.5)
    // Website proportions: 99px tall regular knobs, 115px LEVEL
    kReverb_->setCentrePosition(cx, 60);
    kHigh_->setCentrePosition(cx, 160);
    kLow_->setCentrePosition(cx, 260);
    kLevel_->setCentrePosition(cx, 370); // 86px knob, 115px component
    // 96Hz/PAD toggles: below LEVEL, above CHANNEL label (no overlap)
    lowCut_->setTopLeftPosition(18, 415);
    pad_->setTopLeftPosition(98, 415);
}

// ---------------------------------------------------------------------------
// MasterStrip
// ---------------------------------------------------------------------------

MasterStrip::MasterStrip(BiteyProcessor& proc)
    : PanelBox("", col(0xff1a1a1a)), proc_(proc) {
    kHigh_ = std::make_unique<BiteyKnob>(proc, "m_high", 66, false, BiteyKnob::Scale::Eq);
    kMid_  = std::make_unique<BiteyKnob>(proc, "m_mid", 66, false, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, "m_low", 66, false, BiteyKnob::Scale::Eq);
    kHigh_->setKnobLabel("HIGH");
    kMid_->setKnobLabel("MID");
    kLow_->setKnobLabel("LOW");
    midFreq_ = std::make_unique<MetalToggle>(proc, "m_midfreq", "", false,
                                             std::vector<juce::String>{"0.7k", "1.0k", "1.4k"},
                                             false /* labels left */);
    kMain_ = std::make_unique<BiteyKnob>(proc, "m_level", 86, true, BiteyKnob::Scale::ZeroToTen);
    kMain_->setKnobLabel("LEVEL");
    clipBulb_ = std::make_unique<ClipBulb>();
    addAndMakeVisible(*kHigh_); addAndMakeVisible(*kMid_); addAndMakeVisible(*kLow_);
    addAndMakeVisible(*midFreq_); addAndMakeVisible(*kMain_); addAndMakeVisible(*clipBulb_);
}

void MasterStrip::syncToggles() { midFreq_->syncFromParam(); }

void MasterStrip::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();
    const int H = getHeight();
    // Knob labels drawn by knobs themselves
    // CLIP label above the bulb (bulb centered at 86,435)
    g.setFont(BiteyFonts::robotoCondensed(10.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("CLIP", 0, 395, W, 20, juce::Justification::centred);
    // Large MAIN label at bottom (same size as CHANNEL labels: 20pt)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f)); bf.setBold(true); bf.setExtraKerningFactor(0.01f); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("MAIN", 0, H - 44, W, 40, juce::Justification::centred);
}

void MasterStrip::resized() {
    const int cx = 86; // center x for 173px wide strip
    kHigh_->setCentrePosition(cx, 60);
    kMid_->setCentrePosition(cx, 160);
    kLow_->setCentrePosition(cx, 260);
    midFreq_->setTopLeftPosition(99, 180);
    kMain_->setCentrePosition(cx, 370); // 86px LEVEL knob
    clipBulb_->setCentrePosition(cx, 435);
    clipBulb_->setSize(20, 20);
}

// ---------------------------------------------------------------------------
// ReverbStrip
// ---------------------------------------------------------------------------

ReverbStrip::ReverbStrip(BiteyProcessor& proc)
    : PanelBox("", col(0xff1a1a1a)), proc_(proc) {
    kDrive_ = std::make_unique<BiteyKnob>(proc, "rev_drive", 66, false, BiteyKnob::Scale::ZeroToTen);
    kContour_ = std::make_unique<BiteyKnob>(proc, "rev_contour", 66, false, BiteyKnob::Scale::ZeroToTen);
    kTime_  = std::make_unique<BiteyKnob>(proc, "rev_time", 66, false, BiteyKnob::Scale::ZeroToTen);
    kReturn_   = std::make_unique<BiteyKnob>(proc, "rev_return", 86, true, BiteyKnob::Scale::ZeroToTen);
    kDrive_->setKnobLabel("DRIVE");
    kContour_->setKnobLabel("CONTOUR");
    kTime_->setKnobLabel("TIME");
    kReturn_->setKnobLabel("LEVEL");
    clipBulb_ = std::make_unique<ClipBulb>();
    addAndMakeVisible(*kDrive_); addAndMakeVisible(*kContour_);
    addAndMakeVisible(*kTime_); addAndMakeVisible(*kReturn_); addAndMakeVisible(*clipBulb_);
}

void ReverbStrip::paint(juce::Graphics& g) {
    PanelBox::paint(g);
    const int W = getWidth();
    const int H = getHeight();
    // Knob labels drawn by knobs themselves
    // CLIP label above the bulb (bulb centered at 86,435)
    g.setFont(BiteyFonts::robotoCondensed(10.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("CLIP", 0, 395, W, 20, juce::Justification::centred);
    // Large REVERB label at bottom (same size as CHANNEL labels: 20pt)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f)); bf.setBold(true); bf.setExtraKerningFactor(0.01f); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("REVERB", 0, H - 44, W, 40, juce::Justification::centred);
}

void ReverbStrip::resized() {
    const int cx = 86; // center x for 173px wide strip
    kDrive_->setCentrePosition(cx, 60);
    kContour_->setCentrePosition(cx, 160);
    kTime_->setCentrePosition(cx, 260);
    kReturn_->setCentrePosition(cx, 370); // 86px LEVEL knob
    clipBulb_->setCentrePosition(cx, 435);
    clipBulb_->setSize(20, 20);
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
    echo_ = std::make_unique<BiteyKnob>(proc, "tape_mix", 53, false,
                                       BiteyKnob::Scale::None);
    dryWet_ = std::make_unique<BiteyKnob>(proc, "m_mix", 53, false,
                                         BiteyKnob::Scale::None);
    vuReverb_ = std::make_unique<VUMeterComp>(proc, true);
    vuMain_ = std::make_unique<VUMeterComp>(proc, false);
    power_ = std::make_unique<MetalToggle>(proc, "power", "", true,
                                         std::vector<juce::String>{"ON", "OFF"},
                                         true /* labels right */, 0, true /* invert */);
    phase_ = std::make_unique<MetalToggle>(proc, "m_phase", "", true,
                                          std::vector<juce::String>{"0", "180"},
                                          true /* labels right */);
    jewel_ = std::make_unique<PowerJewel>();
    addAndMakeVisible(*ips_); addAndMakeVisible(*tapeSize_);
    addAndMakeVisible(*echo_); addAndMakeVisible(*dryWet_);
    addAndMakeVisible(*vuReverb_); addAndMakeVisible(*vuMain_);
    addAndMakeVisible(*power_);
    addAndMakeVisible(*phase_); addAndMakeVisible(*jewel_);
}

void CenterPanel::syncToggles() {
    ips_->syncFromParam();
    tapeSize_->syncFromParam();
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

    // ECHO/DRY/WET captions are drawn by the BiteyKnob components themselves
    // (no duplicate drawing here)

    // VU meter captions (below each meter)
    // vuReverb: y=100-240, vuMain: y=250-390
    g.setFont(BiteyFonts::robotoCondensed(11.5f));
    juce::Font capFont(BiteyFonts::robotoCondensed(11.5f));
    capFont.setBold(true);
    g.setFont(capFont);
    g.setColour(col(0xffffffff));
    g.drawText("REVERB", 0, 242, W, 14, juce::Justification::centred);
    g.drawText("MAIN", 0, 392, W, 14, juce::Justification::centred);

    // Board tape: masking tape strip with "REVERB MIXER"
    // Website: 92% width, 2.75cqw (29px) height, torn edges via clip-path
    const float tapeW = W * 0.92f;
    const float tapeH = 29.0f;
    const float tapeX = (W - tapeW) / 2.0f;
    const float tapeY = 400.0f;

    g.saveState();
    // Slight rotation for realism (-1.6 degrees like website)
    g.addTransform(juce::AffineTransform::rotation(-0.028f, tapeX + tapeW/2, tapeY + tapeH/2));

    // Torn tape edges (website clip-path polygon)
    juce::Path tapePath;
    tapePath.startNewSubPath(tapeX, tapeY + tapeH * 0.18f);
    tapePath.lineTo(tapeX + tapeW * 0.02f, tapeY);
    tapePath.lineTo(tapeX + tapeW * 0.97f, tapeY + tapeH * 0.04f);
    tapePath.lineTo(tapeX + tapeW, tapeY + tapeH * 0.22f);
    tapePath.lineTo(tapeX + tapeW * 0.99f, tapeY + tapeH * 0.82f);
    tapePath.lineTo(tapeX + tapeW * 0.96f, tapeY + tapeH);
    tapePath.lineTo(tapeX + tapeW * 0.03f, tapeY + tapeH * 0.96f);
    tapePath.lineTo(tapeX, tapeY + tapeH * 0.78f);
    tapePath.closeSubPath();

    // Masking tape base (website: #ead9ae to #d8bf87)
    juce::ColourGradient tape(col(0xffead9ae), tapeX, tapeY,
                              col(0xffd8bf87), tapeX, tapeY + tapeH, false);
    g.setGradientFill(tape);
    g.fillPath(tapePath);

    // Tape text: website serif, 1.55cqw (16px), #1c1a17, letter-spacing .07em
    juce::Font tapeFont(juce::Font::getDefaultSerifFontName(), 16.0f, juce::Font::bold);
    tapeFont.setExtraKerningFactor(0.07f);
    g.setFont(tapeFont);
    g.setColour(col(0xff1c1a17));
    g.drawText("REVERB MIXER", tapeX, tapeY, tapeW, tapeH,
               juce::Justification::centred);

    g.restoreState();

    // (DRY/WET label drawn by the BiteyKnob itself; no duplicate here)

    // Power section divider
    g.setColour(col(0x0dffffff)); // white/5
    g.fillRect(6, 416, W - 12, 1);

    // ON caption above the jewel
    g.setFont(BiteyFonts::robotoCondensed(7.0f));
    g.setColour(col(0xe6ffffff));
    g.drawText("ON", 92, 420, 56, 12, juce::Justification::centred);

    // Bottom labels: POWER and PHASE, 20pt like other strips (website: 1.9cqw)
    const int ch = getHeight();
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f)); bf.setBold(true); bf.setExtraKerningFactor(0.01f); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("POWER", 0, ch - 52, 143, 40, juce::Justification::centred);
    g.drawText("PHASE", 143, ch - 52, 143, 40, juce::Justification::centred);
}

void CenterPanel::resized() {
    // 286px wide center panel (website exact)
    // Top row: IPS (left), ECHO, DRY/WET, TAPE (right)
    ips_->setTopLeftPosition(8, 50);
    tapeSize_->setTopLeftPosition(238, 50);
    // ECHO (53px knob): center (85,65)
    echo_->setTopLeftPosition(58, 38);
    // DRY/WET (53px knob): center (200,65)
    dryWet_->setTopLeftPosition(173, 38);
    // VU meters: 263px wide (92% of 286), 140px tall
    vuReverb_->setTopLeftPosition(12, 100);
    vuReverb_->setSize(263, 140);
    vuMain_->setTopLeftPosition(12, 250);
    vuMain_->setSize(263, 140);
    // Bottom row: POWER, jewel, PHASE
    power_->setTopLeftPosition(12, 435);
    jewel_->setTopLeftPosition(117, 439);
    phase_->setTopLeftPosition(202, 435);
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
    // Website exact: linear-gradient(90deg,#e2a45e 0%, #d89a55 30%, #bd813f 62%, #a06a35 100%)
    juce::ColourGradient wood(col(0xffe2a45e), 0.0f, 0.0f,
                              col(0xffa06a35), float(W), 0.0f, false);
    wood.addColour(0.30, col(0xffd89a55));
    wood.addColour(0.62, col(0xffbd813f));
    g.setGradientFill(wood);
    g.fillRoundedRectangle(0, 0, W, H, 4.0f); // website: border-radius .35cqw ≈ 4px
    // Wood grain: subtle vertical streaks (website has repeating gradients)
    juce::Random rng(left_ ? 0x1E57 : 0xBEEF);
    for (int i = 0; i < 12; ++i) {
        const float x = rng.nextFloat() * W;
        g.setColour(col(0x1960340c)); // rgba(96,52,12,.10)
        juce::Path p;
        p.startNewSubPath(x, 0);
        p.cubicTo(x + rng.nextFloat() * 4 - 2, H * 0.3f,
                  x + rng.nextFloat() * 4 - 2, H * 0.6f, x, float(H));
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }
    // Inner shadow for depth (website: inset box-shadows)
    juce::ColourGradient shade(col(0x8c462608), 0, 0, col(0x00000000), 12.0f, 0, false);
    g.setGradientFill(shade);
    g.fillRoundedRectangle(0, 0, W, H, 4.0f);
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
    chL_ = std::make_unique<ChannelStrip>(p, 0, "", "1");
    chR_ = std::make_unique<ChannelStrip>(p, 1, "", "2");
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
    // Website exact: 1050x520, grid 30fr/145fr/145fr/240fr/145fr/145fr/30fr
    // 880fr total, 1fr=1.193px: cheek 36px, strip 173px, center 286px
    cheekL_->setBounds(0, 0, 36, 520);
    cheekR_->setBounds(1014, 0, 36, 520);
    chL_->setBounds(38, 8, 173, 504);
    chR_->setBounds(213, 8, 173, 504);
    center_->setBounds(388, 8, 286, 504);
    master_->setBounds(676, 8, 173, 504);
    reverb_->setBounds(851, 8, 173, 504);
}

void BiteyEditor::timerCallback() {
    chL_->syncToggles();
    chR_->syncToggles();
    master_->syncToggles();
    center_->syncToggles();
    master_->updateBulb();
    reverb_->updateBulb();

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
    // Chassis: website exact CSS
    // linear-gradient(180deg,#e6e7e3 0%,#cbccc8 35%,#b0b2ad 62%,#92948f 100%)
    // with brushed texture: repeating-linear-gradient(90deg, rgba(255,255,255,.07), rgba(0,0,0,.03))
    const float H = float(getHeight());
    juce::ColourGradient silver(col(0xffe6e7e3), 0, 0,
                                col(0xff92948f), 0, H, false);
    silver.addColour(0.35, col(0xffcbccc8));
    silver.addColour(0.62, col(0xffb0b2ad));
    g.setGradientFill(silver);
    g.fillRect(getLocalBounds());

    // Brushed metal texture: fine vertical lines (website: 90deg repeating gradient)
    // Light lines and dark lines alternating
    for (int x = 0; x < getWidth(); x += 6) {
        g.setColour(col(0x12ffffff)); // rgba(255,255,255,.07)
        g.drawLine(float(x), 0, float(x), H, 1.0f);
        g.setColour(col(0x08000000)); // rgba(0,0,0,.03)
        g.drawLine(float(x + 3), 0, float(x + 3), H, 1.0f);
    }
}
