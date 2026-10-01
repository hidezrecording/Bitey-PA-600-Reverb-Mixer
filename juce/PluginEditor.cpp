#include "PluginEditor.h"

BiteyEditor::Section& BiteyEditor::addSection(const juce::String& title) {
    auto s = std::make_unique<Section>();
    s->group.setText(title);
    addAndMakeVisible(s->group);
    sections_.push_back(std::move(s));
    return *sections_.back();
}

BiteyEditor::Knob* BiteyEditor::addKnob(Section& s, const juce::String& paramID,
                                        const juce::String& title) {
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 52, 16);
    k->label.setText(title, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centred);
    k->label.attachToComponent(&k->slider, false);
    k->att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc_.apvts, paramID, k->slider);
    addAndMakeVisible(k->slider);
    Knob* ptr = k.get();
    knobs_.push_back(std::move(k));
    s.knobs.push_back(ptr);
    return ptr;
}

BiteyEditor::Choice* BiteyEditor::addChoice(Section& s, const juce::String& paramID,
                                           const juce::String& title) {
    auto c = std::make_unique<Choice>();
    c->label.setText(title, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.attachToComponent(&c->box, false);
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(
            proc_.apvts.getParameter(paramID)))
        c->box.addItemList(p->getAllValueStrings(), 1);
    c->att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc_.apvts, paramID, c->box);
    addAndMakeVisible(c->box);
    Choice* ptr = c.get();
    choices_.push_back(std::move(c));
    s.choices.push_back(ptr);
    return ptr;
}

BiteyEditor::Switch* BiteyEditor::addSwitch(Section& s, const juce::String& paramID,
                                            const juce::String& title) {
    auto w = std::make_unique<Switch>();
    w->button.setButtonText(title);
    w->att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc_.apvts, paramID, w->button);
    addAndMakeVisible(w->button);
    Switch* ptr = w.get();
    switches_.push_back(std::move(w));
    s.switches.push_back(ptr);
    return ptr;
}

BiteyEditor::BiteyEditor(BiteyProcessor& p)
    : juce::AudioProcessorEditor(p), proc_(p) {

    for (int c = 1; c <= 2; ++c) {
        Section& s = addSection("Channel " + juce::String(c));
        const juce::String pfx = "ch" + juce::String(c) + "_";
        addKnob(s, pfx + "level", "Level");
        addChoice(s, pfx + "pad", "Pad");
        addSwitch(s, pfx + "lowcut", "96Hz Cut");
        addKnob(s, pfx + "low", "Low");
        addKnob(s, pfx + "high", "High");
        addKnob(s, pfx + "fx", "FX Send");
    }

    Section& tape = addSection("Tape");
    addChoice(tape, "tape_speed", "Speed");
    addChoice(tape, "tape_size", "Size");
    addKnob(tape, "tape_mix", "Mix");

    Section& rev = addSection("Reverb");
    addKnob(rev, "rev_drive", "Drive");
    addKnob(rev, "rev_contour", "Contour");
    addKnob(rev, "rev_time", "Time");
    addKnob(rev, "rev_return", "Return");

    Section& mst = addSection("Master");
    addKnob(mst, "m_low", "Low");
    addKnob(mst, "m_mid", "Mid");
    addChoice(mst, "m_midfreq", "Mid Freq");
    addKnob(mst, "m_high", "High");
    addKnob(mst, "m_level", "Main");
    addKnob(mst, "m_mix", "Mix");
    addSwitch(mst, "m_phase", "Phase Inv");

    presetVoid_.setButtonText("Be the Void");
    presetSwamp_.setButtonText("Psychedelic Swamp");
    presetBRoom_.setButtonText("B-Room");
    addAndMakeVisible(presetVoid_);
    addAndMakeVisible(presetSwamp_);
    addAndMakeVisible(presetBRoom_);
    presetVoid_.onClick = [this] { proc_.applyParamsToHost(BiteyProcessor::presetBeTheVoid()); };
    presetSwamp_.onClick = [this] { proc_.applyParamsToHost(BiteyProcessor::presetPsychedelicSwamp()); };
    presetBRoom_.onClick = [this] { proc_.applyParamsToHost(BiteyProcessor::presetBRoom()); };

    power_.button.setButtonText("Power");
    power_.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc_.apvts, "power", power_.button);
    addAndMakeVisible(power_.button);

    setSize(1200, 420);
}

void BiteyEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff181818));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("BITEY PA-600", 12, 6, 200, 20, juce::Justification::left);
}

static void layoutRow(juce::Component& group, int x, int y, int w, int h,
                      const std::vector<juce::Component*>& items) {
    group.setBounds(x, y, w, h);
    if (items.empty())
        return;
    const int n = (int)items.size();
    const int iw = w / n;
    for (int i = 0; i < n; ++i)
        items[i]->setBounds(x + i * iw + 4, y + 26, iw - 8, h - 34);
}

void BiteyEditor::resized() {
    const int top = 34;
    int x = 8;
    auto place = [&](Section& s, int w, int h) {
        std::vector<juce::Component*> items;
        for (auto* k : s.knobs) items.push_back(&k->slider);
        for (auto* c : s.choices) items.push_back(&c->box);
        for (auto* sw : s.switches) items.push_back(&sw->button);
        layoutRow(s.group, x, top, w, h, items);
        x += w + 8;
    };

    // Sections were added in order: ch1, ch2, tape, reverb, master.
    place(*sections_[0], 280, 300);
    place(*sections_[1], 280, 300);
    place(*sections_[2], 140, 300);
    place(*sections_[3], 180, 300);
    place(*sections_[4], 280, 300);

    presetVoid_.setBounds(8, 344, 150, 24);
    presetSwamp_.setBounds(164, 344, 150, 24);
    presetBRoom_.setBounds(320, 344, 150, 24);
    power_.button.setBounds(1020, 344, 170, 24);
}
