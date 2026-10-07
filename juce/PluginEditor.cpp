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

// Extra-heavy "stamped into the metal" text: draws the string multiple times
// with sub-pixel offsets for a double-bold industrial look.
inline void drawStamped(juce::Graphics& g, const juce::String& text,
                        int x, int y, int w, int h,
                        juce::Justification just = juce::Justification::centred) {
    for (float ox = -0.8f; ox <= 0.8f; ox += 0.8f)
        for (float oy = -0.8f; oy <= 0.8f; oy += 0.8f)
            g.drawText(text, int(x + ox), int(y + oy), w, h, just);
}

} // namespace

namespace BiteyFonts {

juce::Font robotoCondensed(float sizePx, bool bold) {
    static juce::Typeface::Ptr tf =
        loadEmbedded(BinaryData::RobotoCondensed_ttf, BinaryData::RobotoCondensed_ttfSize);
    auto opts = juce::FontOptions(tf).withHeight(sizePx);
    if (bold) opts = opts.withStyle("Bold");
    return juce::Font(opts);
}
// Hardware font: heavy industrial sans-serif like the physical Peavey PA-600
// (Arial Bold style — much heavier than Roboto Condensed)
juce::Font hardwareFont(float sizePx) {
    juce::Font f(juce::Font::getDefaultSansSerifFontName(), sizePx, juce::Font::bold);
    return f;
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

BiteyKnob::Dims BiteyKnob::dimsFor(Size s) {
    // Shrunk ~6% so tick hashes clear adjacent labels (Nathan 2026-10-07).
    switch (s) {
        case Size::Large: return { 108.0f, 93.0f, 80.5f, 126.0f, 118.0f };
        case Size::Small: return { 66.0f, 54.0f, 44.0f, 78.0f, 76.0f };
        default:          return { 91.0f, 74.0f, 62.0f, 109.0f, 102.0f };
    }
}

BiteyKnob::BiteyKnob(BiteyProcessor& proc, const juce::String& paramID,
                     Size size, Scale scale)
    : dims_(dimsFor(size)), scale_(scale) {
    look_.kringD = dims_.kringD;
    look_.pknobD = dims_.pknobD;
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

    setSize(int(std::ceil(dims_.compW)), int(std::ceil(dims_.compH)));
}

void BiteyKnob::resized() {
    const float cx = getWidth() * 0.5f;
    const float cy = dims_.tickD * 0.5f; // tick ring top at 0; component sits 8.9px above the web .pk box
    const float k = dims_.kringD;
    slider_.setBounds(int(cx - k * 0.5f), int(cy - k * 0.5f),
                      int(std::ceil(k)), int(std::ceil(k)));
}

void BiteyKnob::paint(juce::Graphics& g) {
    const float W = float(getWidth()), H = float(getHeight());
    const float cx = W * 0.5f;
    const float cy = dims_.tickD * 0.5f;
    const float tickR = dims_.tickD * 0.5f;

    // Tick ring: 21 ticks over -135..+135 deg, in the black band OUTSIDE the
    // kring (radius 43..48). Drawn as filled rotated rects so they render
    // on every backend (drawLine hairlines vanished on macOS).
    g.setColour(col(0xffffffff));
    const float tickLen = 5.5f, tickW = 2.2f;
    const float tickMid = tickR - tickLen * 0.5f - 0.5f;
    for (int i = 0; i <= 10; ++i) {
        const float a = (-135.0f + i * 27.0f) * kDeg2Rad;
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(a, cx, cy));
        g.fillRect(cx - tickW * 0.5f, cy - tickMid - tickLen * 0.5f, tickW, tickLen);
        g.restoreState();
    }

    // End markers "0" / "15" on the label baseline (web .end-l/.end-r)
    juce::Font endFont(BiteyFonts::robotoCondensed(8.4f));
    g.setFont(endFont);
    g.setColour(col(0xffffffff));
    const int markY = int(H - 19);
    g.drawText("0", 6, markY, 24, 13, juce::Justification::centredLeft);
    g.drawText("15", int(W - 30), markY, 24, 13, juce::Justification::centredRight);

    // Knob label: uppercase chip on #181818, 1.1cqw Roboto Condensed 700
    if (knobLabel_.isNotEmpty() || scale_ != Scale::None) {
        juce::Font labelFont(BiteyFonts::robotoCondensed(11.55f));
        g.setFont(labelFont);
        const juce::String txt = knobLabel_.toUpperCase();
        const float tw = juce::GlyphArrangement::getStringWidth(labelFont, txt);
        const float chipW = tw + 7.0f, chipH = 13.0f;
        const float chipX = cx - chipW * 0.5f, chipY = H - chipH - 5.0f;
        g.setColour(col(0xff181818));
        g.fillRoundedRectangle(chipX, chipY, chipW, chipH, 2.0f);
        g.setColour(col(0xffffffff));
        g.drawText(txt, int(chipX), int(chipY), int(chipW), int(chipH),
                   juce::Justification::centred);
    }
}

void BiteyKnob::Look::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider&) {
    const float cx = x + w * 0.5f, cy = y + h * 0.5f;
    const float kringR = kringD * 0.5f;
    const float pknobR = pknobD * 0.5f;

    // Drop shadow: 0 .45cqw .9cqw rgba(0,0,0,.72)
    g.setColour(col(0xb8000000));
    g.fillEllipse(cx - kringR, cy - kringR + 4.7f, kringR * 2.0f, kringR * 2.0f);

    // kring: radial-gradient(circle at 35% 28%, #3b352e, #1a1611 58%, #070605 100%)
    juce::ColourGradient kring(col(0xff3b352e), cx - kringR * 0.3f, cy - kringR * 0.44f,
                               col(0xff070605), cx + kringR * 0.4f, cy + kringR * 0.4f, true);
    kring.addColour(0.58, col(0xff1a1611));
    g.setGradientFill(kring);
    g.fillEllipse(cx - kringR, cy - kringR, kringR * 2.0f, kringR * 2.0f);
    // Inner top light + bottom shade (web inset shadows)
    g.setColour(col(0x24f0e2c6));
    g.drawEllipse(cx - kringR + 2.0f, cy - kringR + 2.0f,
                  (kringR - 2.0f) * 2.0f, (kringR - 2.0f) * 2.0f, 3.0f);
    g.setColour(col(0xd8000000));
    g.drawEllipse(cx - kringR + 1.0f, cy - kringR + 1.0f,
                  (kringR - 1.0f) * 2.0f, (kringR - 1.0f) * 2.0f, 5.0f);

    // pknob (rotates): base radial-gradient(circle at 38% 30%,
    //   #4d463d, #28221b 48%, #120e0a 78%, #050403 100%)
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(
        rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle), cx, cy));

    juce::ColourGradient pk(col(0xff4d463d), cx - pknobR * 0.24f, cy - pknobR * 0.4f,
                            col(0xff050403), cx + pknobR * 0.3f, cy + pknobR * 0.35f, true);
    pk.addColour(0.48, col(0xff28221b));
    pk.addColour(0.78, col(0xff120e0a));
    g.setGradientFill(pk);
    g.fillEllipse(cx - pknobR, cy - pknobR, pknobR * 2.0f, pknobR * 2.0f);

    // Top highlight: radial-gradient(circle at 34% 24%, rgba(255,242,216,.34), transparent 44%)
    juce::ColourGradient hi(col(0x57fff2d8), cx - pknobR * 0.32f, cy - pknobR * 0.52f,
                            col(0x00fff2d8), cx, cy, true);
    hi.addColour(0.44, col(0x00fff2d8));
    g.setGradientFill(hi);
    g.fillEllipse(cx - pknobR, cy - pknobR, pknobR * 2.0f, pknobR * 2.0f);

    // Inner shading: inset 0 .35cqw .7cqw rgba(255,240,210,.20),
    //   inset 0 -.8cqw 1.4cqw rgba(0,0,0,.82)
    g.setColour(col(0x33fff0d2));
    g.drawEllipse(cx - pknobR + 3.0f, cy - pknobR + 3.0f,
                  (pknobR - 3.0f) * 2.0f, (pknobR - 3.0f) * 2.0f, 3.5f);
    g.setColour(col(0xd2000000));
    g.drawEllipse(cx - pknobR + 1.5f, cy - pknobR + 1.5f,
                  (pknobR - 1.5f) * 2.0f, (pknobR - 1.5f) * 2.0f, 7.0f);

    // ::after inner dome (34% circle, subtle)
    const float domeR = pknobR * 0.34f;
    juce::ColourGradient dome(col(0x24ffeeCD), cx - domeR * 0.16f, cy - domeR * 0.36f,
                              col(0x57000000), cx + domeR * 0.3f, cy + domeR * 0.3f, true);
    g.setGradientFill(dome);
    g.fillEllipse(cx - domeR, cy - domeR, domeR * 2.0f, domeR * 2.0f);

    // Ivory pointer: width .17cqw, top 6%, height 39% of the pknob box
    const float pw = 1.8f;
    const float pTop = cy - pknobR + pknobD * 0.06f;
    const float pLen = pknobD * 0.39f;
    juce::ColourGradient ptr(col(0xfffff8e6), cx, pTop,
                             col(0xffeadfc2), cx, pTop + pLen, false);
    g.setGradientFill(ptr);
    g.fillRoundedRectangle(cx - pw * 0.5f, pTop, pw, pLen, pw * 0.5f);

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

    // Measure the label column (paint uses 10.7px)
    auto font = BiteyFonts::robotoCondensed(10.7f);
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
    // Web-build toggle: 2.05cqw (21.5px) hex body, chrome bat lever, label
    // column with the active value glowing green.
    const float W = getWidth();
    const float body = 21.5f;
    float y = 0.0f;

    // Caption above (IPS / TAPE style): .mtog-cap 0.86cqw
    if (caption_.isNotEmpty() && !captionBelow_) {
        juce::Font capFont(BiteyFonts::robotoCondensed(9.0f));
        capFont.setExtraKerningFactor(0.08f);
        g.setFont(capFont);
        g.setColour(col(0xe6ffffff));
        g.drawText(caption_.toUpperCase(), 0, 0, int(W), 12,
                   juce::Justification::centred);
        y += 12.0f;
    }

    const int n = (int) labels_.size();
    auto font = BiteyFonts::robotoCondensed(10.7f);
    float labelW = 0.0f;
    for (auto& l : labels_)
        labelW = juce::jmax(labelW, juce::GlyphArrangement::getStringWidth(font, l));
    if (iconMode_ == 1) labelW = 24.0f;

    const float labelColH = juce::jmax(body, float(n) * 12.5f);
    const float bodyY = y + (labelColH - body) * 0.5f;
    const float bodyX = labelsOnRight_ ? 0.0f : labelW + 6.0f;
    const float labelX = labelsOnRight_ ? body + 6.0f : 0.0f;
    const float bcx = bodyX + body * 0.5f;
    const float bcy = bodyY + body * 0.5f;

    // Hex body: clip-path polygon(25% 2%, 75% 2%, 98% 50%, 75% 98%, 25% 98%, 2% 50%)
    // with linear-gradient(145deg, #dedede, #878787 38%, #cfcfcf 58%, #686868)
    juce::Path hex;
    const float hx[6] = { 0.25f, 0.75f, 0.98f, 0.75f, 0.25f, 0.02f };
    const float hy[6] = { 0.02f, 0.02f, 0.50f, 0.98f, 0.98f, 0.50f };
    for (int i = 0; i < 6; ++i) {
        const float px = bodyX + hx[i] * body, py = bodyY + hy[i] * body;
        if (i == 0) hex.startNewSubPath(px, py);
        else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient hg(col(0xffdedede), bodyX, bodyY,
                            col(0xff686868), bodyX + body, bodyY + body, false);
    hg.addColour(0.38, col(0xff878787));
    hg.addColour(0.58, col(0xffcfcfcf));
    g.setGradientFill(hg);
    g.fillPath(hex);

    // Dark well: radial-gradient(ellipse at 50% 42%, #202020, #050505 72%)
    const float wellR = body * 0.37f;
    juce::ColourGradient well(col(0xff202020), bcx, bcy - wellR * 0.16f,
                              col(0xff050505), bcx, bcy + wellR, true);
    g.setGradientFill(well);
    g.fillEllipse(bcx - wellR, bcy - wellR, wellR * 2.0f, wellR * 2.0f);

    // Bat lever: .mbat 0.34 x 0.78cqw, chrome horizontal gradient.
    // UP (index 0) = bottom:44%; DOWN (last) = top:44%; interpolate between.
    const float batW = 3.6f, batH = 8.2f;
    float batY = bcy - batH * 0.5f;
    if (n > 1) {
        const float t = float(index_) / float(n - 1); // 0 = up, 1 = down
        const float upY = (bodyY + body) - body * 0.44f - batH;
        const float dnY = bodyY + body * 0.44f;
        batY = upY + t * (dnY - upY);
    }
    const float tilt = (n > 1) ? (-8.0f + 16.0f * float(index_) / float(n - 1)) : -8.0f;
    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(tilt * kDeg2Rad, bcx, bcy));
    juce::ColourGradient bat(col(0xfff5f5f5), bcx - batW * 0.5f, 0.0f,
                             col(0xff767676), bcx + batW * 0.5f, 0.0f, false);
    bat.addColour(0.38, col(0xffa3a3a3));
    bat.addColour(0.62, col(0xffececec));
    g.setGradientFill(bat);
    g.fillRoundedRectangle(bcx - batW * 0.5f, batY, batW, batH, batW * 0.5f);
    g.restoreState();

    // Label column: .mlabs 1.02cqw, active = #4dff7a with glow
    g.setFont(font);
    const float rowH = labelColH / juce::jmax(1, n);
    for (int i = 0; i < n; ++i) {
        const bool active = (i == index_);
        // Active = green text only (no background box)
        g.setColour(active ? col(0xff4dff7a) : col(0x61ffffff));
        auto just = labelsOnRight_ ? juce::Justification::centredLeft
                                   : juce::Justification::centredRight;
        if (iconMode_ == 1) {
            // 96Hz icons: flat line (off) / high-pass bode curve (on)
            const float icx = labelX + labelW * 0.5f, icy = y + i * rowH + rowH * 0.5f;
            if (i == 0) {
                g.drawLine(icx - 10.0f, icy, icx + 10.0f, icy, 2.6f);
            } else {
                juce::Path bode;
                bode.startNewSubPath(icx - 12.0f, icy + 4.0f);
                bode.lineTo(icx - 4.0f, icy + 4.0f);
                bode.quadraticTo(icx, icy + 4.0f, icx + 2.0f, icy);
                bode.quadraticTo(icx + 4.0f, icy - 4.0f, icx + 12.0f, icy - 4.0f);
                g.strokePath(bode, juce::PathStrokeType(2.2f));
            }
        } else {
            g.drawText(labels_[i], int(labelX), int(y + i * rowH),
                       int(std::ceil(labelW)) + 4, int(rowH), just);
        }
    }

    // Caption below (96 Hz / Pad style): .pk-label 1.08cqw static
    // When labels are on the right (PAD), left-align under the hex body
    // so the caption doesn't crash into the number column.
    if (caption_.isNotEmpty() && captionBelow_) {
        juce::Font capFont(BiteyFonts::robotoCondensed(11.3f));
        g.setFont(capFont);
        g.setColour(col(0xe6ffffff));
        auto capJust = labelsOnRight_ ? juce::Justification::centredLeft
                                      : juce::Justification::centred;
        const float capY = bodyY + body + 2.0f;
        g.drawText(caption_, 0, int(capY), int(W), 14, capJust);
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
    // Silver HEXAGON surround (matches toggle hardware), amber jewel, soft halo.
    const float cx = getWidth() * 0.5f, cy = getHeight() * 0.5f;
    const float hexR = 21.0f, jewelR = 13.0f;

    // Soft halo when on (radial, fades fully — no hard edge)
    if (isOn_) {
        juce::ColourGradient halo(col(0x55ffb020), cx, cy,
                                  col(0x00ffb020), cx, cy + 30.0f, true);
        g.setGradientFill(halo);
        g.fillEllipse(cx - 30.0f, cy - 30.0f, 60.0f, 60.0f);
    }

    // Hexagon: flat-top (matches toggle hardware), chrome gradient
    juce::Path hex;
    for (int i = 0; i < 6; ++i) {
        float a = (60.0f * i) * kDeg2Rad;  // 0,60,... -> flat top/bottom
        float px = cx + hexR * std::cos(a), py = cy + hexR * std::sin(a);
        if (i == 0) hex.startNewSubPath(px, py); else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient chrome(col(0xfff0f0f0), cx - hexR, cy - hexR,
                                col(0xff6a6a6a), cx + hexR, cy + hexR, false);
    chrome.addColour(0.5, col(0xffb8b8b8));
    g.setGradientFill(chrome);
    g.fillPath(hex);
    g.setColour(col(0x80000000));
    g.strokePath(hex, juce::PathStrokeType(1.5f));
    // Dark recessed center
    juce::Path hexIn;
    const float hexInR = hexR - 3.5f;
    for (int i = 0; i < 6; ++i) {
        float a = (60.0f * i) * kDeg2Rad;
        float px = cx + hexInR * std::cos(a), py = cy + hexInR * std::sin(a);
        if (i == 0) hexIn.startNewSubPath(px, py); else hexIn.lineTo(px, py);
    }
    hexIn.closeSubPath();
    g.setColour(col(0xff0a0a0a));
    g.fillPath(hexIn);

    // Jewel: radial-gradient(circle at 38% 30%, #fff8c8, #ffe066 28%,
    //   #ffb020 52%, #ff8800 74%, #c65300)
    juce::ColourGradient jewel(
        isOn_ ? col(0xfffff8c8) : col(0xff5a3a10),
        cx - jewelR * 0.24f, cy - jewelR * 0.4f,
        isOn_ ? col(0xffc65300) : col(0xff1a0e00),
        cx + jewelR * 0.3f, cy + jewelR * 0.35f, true);
    if (isOn_) {
        jewel.addColour(0.28, col(0xffffe066));
        jewel.addColour(0.52, col(0xffffb020));
        jewel.addColour(0.74, col(0xffff8800));
    }
    g.setGradientFill(jewel);
    g.fillEllipse(cx - jewelR, cy - jewelR, jewelR * 2.0f, jewelR * 2.0f);
    // Inner shading
    if (isOn_) {
        g.setColour(col(0x8ca03c00));
        g.drawEllipse(cx - jewelR + 1.5f, cy - jewelR + 1.5f,
                      (jewelR - 1.5f) * 2.0f, (jewelR - 1.5f) * 2.0f, 3.5f);
        g.setColour(col(0xbfffffdc));
        g.drawEllipse(cx - jewelR + 1.0f, cy - jewelR + 1.0f,
                      (jewelR - 1.0f) * 2.0f, (jewelR - 1.0f) * 2.0f, 1.8f);
    }
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
    // Web build .clip: 2.05cqw (21.5px) hex socket, 56% glowing core
    // (green at rest), "CLIP" word 1.05cqw to the right.
    const float hexD = 21.5f;
    const float cx = hexD * 0.5f + 2.0f, cy = getHeight() * 0.5f;

    const float lvl = juce::jlimit(0.0f, 1.2f, displayLevel_);
    juce::Colour coreCol;
    if (lvl < 0.6f) coreCol = col(0xff2fe07a);
    else if (lvl < 0.85f) coreCol = col(0xffffc020);
    else coreCol = col(0xffff3020);

    // Hex socket: linear-gradient(145deg, #dedede, #878787 38%, #cfcfcf 58%, #686868)
    juce::Path hex;
    const float hx[6] = { 0.25f, 0.75f, 0.98f, 0.75f, 0.25f, 0.02f };
    const float hy[6] = { 0.02f, 0.02f, 0.50f, 0.98f, 0.98f, 0.50f };
    for (int i = 0; i < 6; ++i) {
        const float px = cx - hexD * 0.5f + hx[i] * hexD;
        const float py = cy - hexD * 0.5f + hy[i] * hexD;
        if (i == 0) hex.startNewSubPath(px, py);
        else hex.lineTo(px, py);
    }
    hex.closeSubPath();
    juce::ColourGradient hg(col(0xffdedede), cx - hexD * 0.5f, cy - hexD * 0.5f,
                            col(0xff686868), cx + hexD * 0.5f, cy + hexD * 0.5f, false);
    hg.addColour(0.38, col(0xff878787));
    hg.addColour(0.58, col(0xffcfcfcf));
    g.setGradientFill(hg);
    g.fillPath(hex);

    // Core: 56% circle, radial highlight, green glow
    const float coreR = hexD * 0.28f;
    const float glowA = 0.25f + 0.55f * juce::jmin(1.0f, lvl);
    juce::ColourGradient glow(coreCol.withAlpha(glowA), cx, cy,
                              col(0x00000000), cx, cy + coreR * 2.2f, true);
    g.setGradientFill(glow);
    g.fillEllipse(cx - coreR * 2.2f, cy - coreR * 2.2f, coreR * 4.4f, coreR * 4.4f);
    juce::ColourGradient core(col(0xe6ffffff), cx - coreR * 0.28f, cy - coreR * 0.44f,
                              coreCol.withAlpha(0.85f), cx + coreR * 0.3f, cy + coreR * 0.4f, true);
    core.addColour(0.34, col(0x40ffffff));
    core.addColour(0.58, col(0x00ffffff));
    g.setGradientFill(core);
    g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);
    g.setColour(col(0x61000000));
    g.drawEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);

    // "CLIP" word: 1.05cqw Roboto Condensed 700, letter-spacing .08em
    juce::Font wf(BiteyFonts::robotoCondensed(11.0f));
    wf.setExtraKerningFactor(0.08f);
    g.setFont(wf);
    g.setColour(col(0xffffffff));
    g.drawText("CLIP", int(cx + hexD * 0.5f + 6.0f), 0,
               int(getWidth() - (cx + hexD * 0.5f + 6.0f)), getHeight(),
               juce::Justification::centredLeft);
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
    // Web-build VU meter, exact replica of the inline SVG (viewBox 0 0 200 104):
    // teal face, white/amber scale arc, tick marks, numerals, green Bitey logo,
    // white needle on a round pivot cap, "VU" corner label, zero-adjust screw.
    const float bw = float(getWidth()), bh = float(getHeight());
    if (bw < 40.0f || bh < 40.0f) return;

    // .vum: black plastic housing, wider bezel, subtle top highlight
    const float border = 4.2f, padV = 7.9f, padH = 9.5f;
    juce::ColourGradient housing(col(0xff3a3a3a), 0.0f, 0.0f,
                                 col(0xff0a0a0a), 0.0f, bh, false);
    g.setGradientFill(housing);
    g.fillRoundedRectangle(0.0f, 0.0f, bw, bh, 11.5f);
    // Plastic top highlight
    g.setColour(col(0x40ffffff));
    g.drawRoundedRectangle(1.0f, 1.0f, bw - 2.0f, bh * 0.5f, 10.0f, 1.2f);
    g.setColour(col(0xff000000));
    g.drawRoundedRectangle(border * 0.5f, border * 0.5f,
                           bw - border, bh - border, 11.5f, border);

    // Face rect (inside padding)
    const float fx = border + padH, fy = border + padV;
    const float fw = bw - 2.0f * (border + padH);
    const float fh = bh - 2.0f * (border + padV);
    if (fw < 20.0f || fh < 20.0f) return;

    // .vuface: teal gradient + warm top glow + diagonal glass
    juce::ColourGradient faceBg(col(0xff0b9dc4), fx, fy,
                                col(0xff00293d), fx, fy + fh, false);
    faceBg.addColour(0.38, col(0xff00779e));
    faceBg.addColour(0.78, col(0xff004d6b));
    g.setGradientFill(faceBg);
    g.fillRoundedRectangle(fx, fy, fw, fh, 2.3f);
    // Warm bulb glow from top: radial ellipse 75%x45% at (50%,-6%)
    {
        juce::ColourGradient warm(col(0x57ffd082), fx + fw * 0.5f, fy - fh * 0.28f,
                                  col(0x00ffb25c), fx + fw * 0.5f, fy + fh * 0.2f, true);
        warm.addColour(0.55, col(0x1affb25c));
        g.setGradientFill(warm);
        g.fillRoundedRectangle(fx, fy, fw, fh, 2.3f);
    }
    // Inset depth shadows
    g.setColour(col(0xd2000000));
    g.drawRoundedRectangle(fx + 1.0f, fy + 1.0f, fw - 2.0f, fh - 2.0f, 2.3f, 3.0f);

    // Map SVG 200x104 onto the face
    g.saveState();
    {
        juce::Path clip;
        clip.addRoundedRectangle(fx, fy, fw, fh, 2.3f);
        g.reduceClipRegion(clip);
    }
    const float sc = juce::jmin(fw / 200.0f, fh / 104.0f);
    const float ox = fx + (fw - 200.0f * sc) * 0.5f;
    const float oy = fy + (fh - 104.0f * sc) * 0.5f;
    auto X = [&](float x) { return ox + x * sc; };
    auto Y = [&](float y) { return oy + y * sc; };
    auto S = [&](float v) { return v * sc; };

    // Incandescent bulbs: half-circles poking up from behind the lower bezel,
    // either side of the pivot (100,88). Centered ON the face bottom edge
    // (y=104) so the clip region hides the bottom half — only the top shows.
    // Warm glow arcs upward (bottom clipped by face edge).
    {
        const float bulbXs[2] = { 68.0f, 132.0f };
        for (int bi = 0; bi < 2; ++bi) {
            const float bx = bulbXs[bi];
            const float by = 104.0f;  // ON the bottom edge -> top half visible
            const float br = 7.0f;
            // Warm glow (radial, bottom half clipped by face edge -> arc effect)
            juce::ColourGradient glow(col(0x55ffb545), X(bx), Y(by),
                                      col(0x00ffb545), X(bx), Y(by - 26.0f), true);
            glow.addColour(0.6, col(0x22ff9a2a));
            g.setGradientFill(glow);
            g.fillEllipse(X(bx - 24.0f), Y(by - 24.0f), S(48.0f), S(48.0f));
            // Bulb glass: top half visible (bottom clipped)
            juce::ColourGradient glass(col(0xffffe8a0), X(bx - br*0.3f), Y(by - br),
                                       col(0xffc77800), X(bx), Y(by), true);
            glass.addColour(0.7, col(0xffffb545));
            g.setGradientFill(glass);
            g.fillEllipse(X(bx - br), Y(by - br), S(br*2.0f), S(br*2.0f));
            // Filament highlight (top of bulb)
            g.setColour(col(0xbfffffff));
            g.fillEllipse(X(bx - br*0.3f), Y(by - br*0.7f), S(br*0.45f), S(br*0.3f));
        }
    }

    // Scale arc: radius 108 centered at (100,114); white -146deg..-70.2deg,
    // amber -70.2deg..-43deg (SVG y-down, JUCE angles clockwise from +x)
    {
        juce::Path white, amber;
        white.addArc(X(100.0f - 108.0f), Y(114.0f - 108.0f), S(216.0f), S(216.0f),
                     -0.977f, 0.346f, true);
        amber.addArc(X(100.0f - 108.0f), Y(114.0f - 108.0f), S(216.0f), S(216.0f),
                     0.346f, 0.820f, true);
        g.setColour(col(0xe6ffffff));
        g.strokePath(white, juce::PathStrokeType(S(1.8f)));
        g.setColour(col(0xffffb020));
        g.strokePath(amber, juce::PathStrokeType(S(1.8f)));
    }
    // Tick marks (exact SVG coordinates)
    auto tick = [&](float x1, float y1, float x2, float y2,
                    juce::Colour c, float wdt) {
        g.setColour(c);
        g.drawLine(X(x1), Y(y1), X(x2), Y(y2), S(wdt));
    };
    const juce::Colour tkw(0xffffffff), tka(0xffffb020);
    const float wm[5][4] = {{14.9f,47.5f,25.5f,55.8f},{47.6f,19.5f,54.2f,31.3f},
                            {63.4f,12.4f,68.0f,25.1f},{80.1f,7.8f,82.6f,21.1f},
                            {102.3f,6.0f,102.0f,19.5f}};
    for (auto& t : wm) tick(t[0],t[1],t[2],t[3],tkw,2.0f);
    const float am[2][4] = {{136.6f,12.4f,132.0f,25.1f},{169.4f,31.3f,160.7f,41.6f}};
    for (auto& t : am) tick(t[0],t[1],t[2],t[3],tka,2.0f);
    const float wm2[2][4] = {{114.5f,7.0f,113.2f,16.4f},{124.5f,8.8f,122.3f,18.1f}};
    for (auto& t : wm2) tick(t[0],t[1],t[2],t[3],col(0xccffffff),1.4f);
    const float ws[11][4] = {{19.7f,41.7f,24.2f,45.7f},{25.0f,36.3f,29.1f,40.6f},
                             {30.6f,31.3f,34.4f,35.9f},{36.5f,26.6f,40.0f,31.5f},
                             {42.8f,22.4f,45.9f,27.5f},{56.1f,15.3f,58.5f,20.8f},
                             {70.2f,10.2f,71.9f,16.0f},{85.0f,7.1f,85.8f,13.0f},
                             {92.5f,6.3f,92.9f,12.2f},{107.5f,6.3f,107.1f,12.2f},
                             {129.8f,10.2f,128.1f,16.0f}};
    for (auto& t : ws) tick(t[0],t[1],t[2],t[3],col(0xb3ffffff),1.1f);
    const float as_[5][4] = {{143.9f,15.3f,141.5f,20.8f},{150.7f,18.6f,147.9f,23.9f},
                             {157.2f,22.4f,154.1f,27.5f},{163.5f,26.6f,160.0f,31.5f},
                             {175.0f,36.3f,170.9f,40.6f}};
    for (auto& t : as_) tick(t[0],t[1],t[2],t[3],tka,1.1f);

    // Numerals: Roboto Condensed 700, 11.5px
    juce::Font numFont(BiteyFonts::robotoCondensed(S(11.5f)));
    g.setFont(numFont);
    struct Num { const char* t; float x, y; bool amber; };
    const Num nums[] = {{"-20",37.0f,68.2f,false},{"-10",61.2f,47.5f,false},
                        {"-7",72.9f,42.2f,false},{"-5",85.3f,38.9f,false},
                        {"-3",101.7f,37.5f,false},{"0",127.1f,42.2f,true},
                        {"+3",151.4f,56.2f,true}};
    for (auto& nu : nums) {
        g.setColour(nu.amber ? col(0xffffb020) : col(0xffffffff));
        // SVG text-anchor=middle with baseline at y: emulate with centred box
        g.drawText(nu.t, int(X(nu.x) - S(20.0f)), int(Y(nu.y) - S(11.5f)),
                   int(S(40.0f)), int(S(13.0f)), juce::Justification::centred);
    }

    // Green Bitey logo with glow (web .vu-logo drop-shadows)
    {
        juce::Image logo = juce::ImageCache::getFromMemory(
            BinaryData::biteylogogreen_png, BinaryData::biteylogogreen_pngSize);
        if (logo.isValid()) {
            // Subtle underlighting only — no distinct bright circle
            juce::ColourGradient lg(col(0x264dff7a), X(100.0f), Y(66.0f),
                                    col(0x004dff7a), X(100.0f), Y(92.0f), true);
            g.setGradientFill(lg);
            g.fillEllipse(X(72.0f), Y(48.0f), S(56.0f), S(40.0f));
            g.drawImage(logo, X(72.9f), Y(46.8f), S(54.3f), S(39.4f),
                        0, 0, logo.getWidth(), logo.getHeight());
        }
    }

    // Needle: white polygon, pivot at (100,88). Rest (-20) = -65.5deg,
    // full (+3) = +51.6deg from vertical (derived from SVG tick geometry).
    {
        const float lvl = juce::jlimit(0.0f, 1.0f, smoothed_);
        const float ang = (-65.5f + 117.1f * lvl) * kDeg2Rad;
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(ang, X(100.0f), Y(88.0f)));
        juce::Path needle;
        needle.startNewSubPath(X(99.12f), Y(94.0f));
        needle.lineTo(X(100.88f), Y(94.0f));
        needle.lineTo(X(100.34f), Y(10.0f));
        needle.lineTo(X(99.66f), Y(10.0f));
        needle.closeSubPath();
        g.setColour(col(0xffffffff));
        g.fillPath(needle);
        g.setColour(col(0x7300141e));
        g.strokePath(needle, juce::PathStrokeType(S(0.5f)));
        g.restoreState();
    }

    // Pivot cap: radial-gradient(circle at 38% 32%, #f4f4f4, #8a8a8a 55%, #2e2e2e)
    {
        const float px = X(100.0f), py = Y(88.0f), pr = S(6.2f);
        juce::ColourGradient cap(col(0xfff4f4f4), px - pr * 0.24f, py - pr * 0.36f,
                                 col(0xff2e2e2e), px + pr * 0.3f, py + pr * 0.35f, true);
        cap.addColour(0.55, col(0xff8a8a8a));
        g.setGradientFill(cap);
        g.fillEllipse(px - pr, py - pr, pr * 2.0f, pr * 2.0f);
        g.setColour(col(0x80000000));
        g.drawEllipse(px - pr, py - pr, pr * 2.0f, pr * 2.0f, S(0.6f));
        // Slot
        g.setColour(col(0xff1a1a1a));
        g.drawLine(X(97.9f), Y(86.6f), X(102.1f), Y(89.4f), S(1.1f));
        // Center dot
        g.setColour(col(0xff141414));
        g.fillEllipse(px - S(2.0f), py - S(2.0f), S(4.0f), S(4.0f));
    }

    // "VU" corner label: 9.5px, letter-spacing 2.5
    {
        juce::Font vuFont(BiteyFonts::robotoCondensed(S(9.5f)));
        vuFont.setExtraKerningFactor(0.26f);
        g.setFont(vuFont);
        g.setColour(col(0xe6ffffff));
        g.drawText("VU", int(X(150.0f)), int(Y(82.0f)), int(S(40.0f)), int(S(14.0f)),
                   juce::Justification::right);
    }

    // Diagonal glass reflection (117deg, subtle)
    {
        juce::Path refl;
        refl.startNewSubPath(fx, fy);
        refl.lineTo(fx + fw * 0.42f, fy);
        refl.lineTo(fx + fw * 0.16f, fy + fh);
        refl.lineTo(fx, fy + fh);
        refl.closeSubPath();
        juce::ColourGradient rg(col(0x36ffffff), fx, fy,
                                col(0x00ffffff), fx + fw * 0.3f, fy + fh * 0.6f, false);
        g.setGradientFill(rg);
        g.fillPath(refl);
    }
    g.restoreState(); // unclip face

    // Zero-adjust screw below the face (.zscr 0.68cqw)
    {
        const float sr = 3.6f;
        const float sx = bw * 0.5f, sy = bh - border - 2.0f - sr;
        juce::ColourGradient sg(col(0xfff0f0f0), sx - sr * 0.3f, sy - sr * 0.4f,
                                col(0xff2e2e2e), sx + sr * 0.3f, sy + sr * 0.3f, true);
        sg.addColour(0.52, col(0xff8a8a8a));
        g.setGradientFill(sg);
        g.fillEllipse(sx - sr, sy - sr, sr * 2.0f, sr * 2.0f);
        g.setColour(col(0xff171717));
        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(28.0f * kDeg2Rad, sx, sy));
        g.fillRoundedRectangle(sx - sr * 0.72f, sy - 0.7f, sr * 1.44f, 1.4f, 0.7f);
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
    kReverb_ = std::make_unique<BiteyKnob>(proc, p + "fx", BiteyKnob::Size::Standard, BiteyKnob::Scale::ZeroToTen);
    kHigh_ = std::make_unique<BiteyKnob>(proc, p + "high", BiteyKnob::Size::Standard, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, p + "low", BiteyKnob::Size::Standard, BiteyKnob::Scale::Eq);
    kLevel_ = std::make_unique<BiteyKnob>(proc, p + "level", BiteyKnob::Size::Large, BiteyKnob::Scale::ZeroToTen);
    kReverb_->setKnobLabel("REVERB");
    kHigh_->setKnobLabel("HIGH");
    kLow_->setKnobLabel("LOW");
    kLevel_->setKnobLabel("LEVEL");
    lowCut_ = std::make_unique<MetalToggle>(proc, p + "lowcut", "96 Hz", true,
                                            std::vector<juce::String>{"", ""},
                                            false /* icons left, body right */, 1 /* icons */);
    pad_ = std::make_unique<MetalToggle>(proc, p + "pad", "PAD", true,
                                        std::vector<juce::String>{"+10", "+4", "0", "-10"},
                                        true /* body left, labels right */);
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
    // Channel label: "CHANNEL 1" or "CHANNEL 2" — hardware font (Peavey PA-600)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f, true));
    g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    juce::String label = (number_ == "1") ? "CHANNEL 1" : "CHANNEL 2";
    g.drawText(label, 0, H - 30, W, 26, juce::Justification::centred);
}

void ChannelStrip::resized() {
    const int cx = 78; // center x for 157px wide strip (web grid)
    // Web rows: tick-tops at 12 / 110.5 / 209 / 307.5 (strip-relative)
    kReverb_->setCentrePosition(cx, 59);
    kHigh_->setCentrePosition(cx, 157);
    kLow_->setCentrePosition(cx, 256);
    kLevel_->setCentrePosition(cx, 363);
    // 96Hz + PAD toggles: one row, hex bodies sharing a baseline (y=448),
    // clear of the LEVEL label chip and the CHANNEL label
    lowCut_->setTopLeftPosition(16, 432);
    pad_->setTopLeftPosition(94, 420);
}

// ---------------------------------------------------------------------------
// MasterStrip
// ---------------------------------------------------------------------------

MasterStrip::MasterStrip(BiteyProcessor& proc)
    : PanelBox("", col(0xff1a1a1a)), proc_(proc) {
    kHigh_ = std::make_unique<BiteyKnob>(proc, "m_high", BiteyKnob::Size::Standard, BiteyKnob::Scale::Eq);
    kMid_  = std::make_unique<BiteyKnob>(proc, "m_mid", BiteyKnob::Size::Standard, BiteyKnob::Scale::Eq);
    kLow_  = std::make_unique<BiteyKnob>(proc, "m_low", BiteyKnob::Size::Standard, BiteyKnob::Scale::Eq);
    kHigh_->setKnobLabel("HIGH");
    kMid_->setKnobLabel("MID");
    kLow_->setKnobLabel("LOW");
    midFreq_ = std::make_unique<MetalToggle>(proc, "m_midfreq", "", false,
                                             std::vector<juce::String>{"0.7k", "1.0k", "1.4k"},
                                             false /* labels left */);
    kMain_ = std::make_unique<BiteyKnob>(proc, "m_level", BiteyKnob::Size::Large, BiteyKnob::Scale::ZeroToTen);
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
    // Knob labels drawn by knobs themselves (ClipBulb draws its own CLIP row)
    // Large MAIN label at bottom (web .bigword 1.9cqw = 20px)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f, true)); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("MAIN", 0, H - 30, W, 26, juce::Justification::centred);
}

void MasterStrip::resized() {
    const int cx = 78; // center x for 157px wide strip (web grid)
    kHigh_->setCentrePosition(cx, 59);
    kMid_->setCentrePosition(cx, 157);
    kLow_->setCentrePosition(cx, 256);
    midFreq_->setTopLeftPosition(104, 88); // up+left, fully on MAIN strip (Nathan 2026-10-07)
    kMain_->setCentrePosition(cx, 363);
    clipBulb_->setBounds(cx - 35, 439, 70, 26); // web .clip: centered row
}

// ---------------------------------------------------------------------------
// ReverbStrip
// ---------------------------------------------------------------------------

ReverbStrip::ReverbStrip(BiteyProcessor& proc)
    : PanelBox("", col(0xff1a1a1a)), proc_(proc) {
    kDrive_ = std::make_unique<BiteyKnob>(proc, "rev_drive", BiteyKnob::Size::Standard, BiteyKnob::Scale::ZeroToTen);
    kContour_ = std::make_unique<BiteyKnob>(proc, "rev_contour", BiteyKnob::Size::Standard, BiteyKnob::Scale::ZeroToTen);
    kTime_  = std::make_unique<BiteyKnob>(proc, "rev_time", BiteyKnob::Size::Standard, BiteyKnob::Scale::ZeroToTen);
    kReturn_   = std::make_unique<BiteyKnob>(proc, "rev_return", BiteyKnob::Size::Large, BiteyKnob::Scale::ZeroToTen);
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
    // Knob labels drawn by knobs themselves (ClipBulb draws its own CLIP row)
    // Large REVERB label at bottom (web .bigword 1.9cqw = 20px)
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f, true)); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("REVERB", 0, H - 30, W, 26, juce::Justification::centred);
}

void ReverbStrip::resized() {
    const int cx = 78; // center x for 157px wide strip (web grid)
    kDrive_->setCentrePosition(cx, 59);
    kContour_->setCentrePosition(cx, 157);
    kTime_->setCentrePosition(cx, 256);
    kReturn_->setCentrePosition(cx, 363);
    clipBulb_->setBounds(cx - 35, 439, 70, 26); // web .clip: centered row
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
    echo_ = std::make_unique<BiteyKnob>(proc, "tape_mix", BiteyKnob::Size::Small,
                                       BiteyKnob::Scale::None);
    echo_->setKnobLabel("ECHO");
    dryWet_ = std::make_unique<BiteyKnob>(proc, "m_mix", BiteyKnob::Size::Small,
                                         BiteyKnob::Scale::None);
    dryWet_->setKnobLabel("DRY/WET");
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

    // (No black strip behind tape cluster — website doesn't have it)

    // ECHO/DRY/WET captions are drawn by the BiteyKnob components themselves
    // (no duplicate drawing here)

    // (No REVERB/MAIN captions — the website doesn't have them)

    // Board tape: masking tape strip with "REVERB MIXER"
    // Website: 92% width, 2.75cqw (29px) height, torn edges via clip-path
    const float tapeW = W * 0.92f;
    const float tapeH = 28.9f; // 2.75cqw
    const float tapeX = (W - tapeW) / 2.0f;
    const float tapeY = 401.0f;

    g.saveState();
    // Slight rotation for realism (-1.6 degrees like website)
    g.addTransform(juce::AffineTransform::rotation(-0.028f, tapeX + tapeW/2, tapeY + tapeH/2));

    // Torn edges: jagged rips (not cuts), no bevel — like ripped masking tape.
    // Full piece (no horizontal tear). Path goes clockwise: top-left -> right
    // along top -> down right rip -> left along bottom -> up left rip -> close.
    juce::Path tapePath;
    // Top edge (slightly wavy), left to right
    tapePath.startNewSubPath(tapeX + 8.0f, tapeY + 2.0f);
    tapePath.lineTo(tapeX + tapeW * 0.97f, tapeY + 1.0f);
    // Right rip: jagged, top to bottom
    tapePath.lineTo(tapeX + tapeW - 2.0f, tapeY + tapeH * 0.2f);
    tapePath.lineTo(tapeX + tapeW - 7.0f, tapeY + tapeH * 0.4f);
    tapePath.lineTo(tapeX + tapeW - 1.0f, tapeY + tapeH * 0.6f);
    tapePath.lineTo(tapeX + tapeW - 6.0f, tapeY + tapeH * 0.8f);
    tapePath.lineTo(tapeX + tapeW - 3.0f, tapeY + tapeH - 1.0f);
    // Bottom edge (slightly wavy), right to left
    tapePath.lineTo(tapeX + 6.0f, tapeY + tapeH - 1.0f);
    // Left rip: jagged, bottom to top
    tapePath.lineTo(tapeX + 1.0f, tapeY + tapeH * 0.75f);
    tapePath.lineTo(tapeX + 7.0f, tapeY + tapeH * 0.55f);
    tapePath.lineTo(tapeX + 2.0f, tapeY + tapeH * 0.35f);
    tapePath.lineTo(tapeX + 3.0f, tapeY + tapeH * 0.15f);
    tapePath.closeSubPath();

    // Masking tape base (website: #ead9ae to #d8bf87)
    juce::ColourGradient tape(col(0xffead9ae), tapeX, tapeY,
                              col(0xffd8bf87), tapeX, tapeY + tapeH, false);
    g.setGradientFill(tape);
    g.fillPath(tapePath);

    // Tape text: bigger Sharpie scrawl (20px), #1c1a17
    juce::Font tapeFont(BiteyFonts::permanentMarker(20.0f));
    tapeFont.setExtraKerningFactor(0.07f);
    g.setFont(tapeFont);
    g.setColour(col(0xff2a2520));
    // Slight rotation for handwritten feel (on top of the tape's -1.6deg)
    g.addTransform(juce::AffineTransform::rotation(0.015f, tapeX + tapeW/2, tapeY + tapeH/2));
    g.drawText("REVERB MIXER", tapeX, tapeY, tapeW, tapeH,
               juce::Justification::centred);

    g.restoreState();

    // (DRY/WET label drawn by the BiteyKnob itself; no duplicate here)

    // Power section divider
    g.setColour(col(0x0dffffff)); // white/5
    g.fillRect(6, 416, W - 12, 1);

    // (ON caption removed — was hidden under the REVERB MIXER tape)

    // Bottom labels: POWER and PHASE, 20pt like other strips (website: 1.9cqw)
    const int ch = getHeight();
    juce::Font bf(BiteyFonts::robotoCondensed(20.0f, true)); g.setFont(bf);
    g.setColour(col(0xe6ffffff));
    g.drawText("POWER", -18, ch - 30, 130, 26, juce::Justification::centred);
    g.drawText("PHASE", 131, ch - 30, 143, 26, juce::Justification::centred);
}

void CenterPanel::resized() {
    // 260px wide center panel (web grid)
    // Top row: IPS (left), ECHO, DRY/WET, TAPE (right) — web .cknobs
    ips_->setTopLeftPosition(14, 12);
    tapeSize_->setTopLeftPosition(210, 16); // down+right, clear of DRY/WET ticks (Nathan 2026-10-07)
    echo_->setCentrePosition(86, 48);    // small ring knob, tick-top at 12
    dryWet_->setCentrePosition(176, 48); // clear of the TAPE toggle
    // VU meters: 92% of 260 = 239px
    vuReverb_->setBounds(10, 109, 239, 138);
    vuMain_->setBounds(10, 255, 239, 138);
    // Bottom row: POWER toggle, jewel, PHASE toggle (web .cbtm-top)
    power_->setTopLeftPosition(36, 444);
    jewel_->setBounds(106, 435, 48, 48);
    phase_->setTopLeftPosition(192, 444);
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
    // Web grid: pad .55cqw, cheeks 30fr, channels 145fr, center 240fr, gap 1.35%
    cheekL_->setBounds(6, 6, 32, 508);
    cheekR_->setBounds(1012, 6, 32, 508);
    chL_->setBounds(53, 6, 157, 508);
    chR_->setBounds(224, 6, 157, 508);
    center_->setBounds(395, 6, 260, 508);
    master_->setBounds(669, 6, 157, 508);
    reverb_->setBounds(840, 6, 157, 508);
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
        // Do NOT dim the channel strips — Nathan wants them always visible.
        // Only the VU meters dim (via center_->syncPower) and the jewel changes.
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
