#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Functional first GUI for the PA-600: every parameter exposed, sections
// grouped like the hardware. The visual design port (custom knobs, artwork,
// VU meters from the browser prototype) is a separate step — this editor
// exists so the plugin is fully usable and testable now.
class BiteyEditor : public juce::AudioProcessorEditor {
public:
    explicit BiteyEditor(BiteyProcessor&);
    ~BiteyEditor() override = default;
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    BiteyProcessor& proc_;

    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    struct Choice {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };
    struct Switch {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };
    struct Section {
        juce::GroupComponent group;
        std::vector<Knob*> knobs;
        std::vector<Choice*> choices;
        std::vector<Switch*> switches;
    };

    std::vector<std::unique_ptr<Section>> sections_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<Choice>> choices_;
    std::vector<std::unique_ptr<Switch>> switches_;

    juce::TextButton presetVoid_, presetSwamp_, presetBRoom_;
    Switch power_;

    Knob* addKnob(Section& s, const juce::String& paramID, const juce::String& title);
    Choice* addChoice(Section& s, const juce::String& paramID, const juce::String& title);
    Switch* addSwitch(Section& s, const juce::String& paramID, const juce::String& title);
    Section& addSection(const juce::String& title);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BiteyEditor)
};
