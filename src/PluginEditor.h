#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

//==============================================================================
/*  Faithful replication of the original VOICEDRIVE editor (from reverse
    engineering - see docs/deep-dive-editor.md):

    - window 162 x 200, three vertically stacked panels
    - paint(): bg 0xFF555555, panels 50% white, labels 0xFFD3D3D3 @8px,
      panel titles 0xFF808080 @8px bold, 1px black borders
    - stock widgets: rotary sliders (16x16), combos (70x16), LookAndFeel_V2
      for the editor, LookAndFeel_V3 for the combos
*/
class VoiceDriveAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::Slider::Listener,
                                       private juce::ComboBox::Listener,
                                       private juce::Timer
{
public:
    explicit VoiceDriveAudioProcessorEditor (VoiceDriveAudioProcessor&);
    ~VoiceDriveAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int originalWidth  = 162;
    static constexpr int originalHeight = 200;
    static constexpr float uiScale      = 2.0f;   // original window is tiny; render at 2x

private:
    void sliderValueChanged (juce::Slider*) override;
    void comboBoxChanged (juce::ComboBox*) override;
    void timerCallback() override;
    void syncPresetCombo();   // reflect the "Preset No" parameter in the combo (0..8 -> item, else Custom)

    VoiceDriveAudioProcessor& processor;

    juce::LookAndFeel_V2 editorLookAndFeel;   // original: editor uses LookAndFeel_V2
    juce::LookAndFeel_V3 comboLookAndFeel;    // original: combos use LookAndFeel_V3

    // widgets (embedded by value, as in the original)
    juce::ComboBox waveformCombo;   // {84, 21, 70, 16}
    juce::Slider   speedKnob;       // {139, 37, 16, 16}
    juce::Slider   jitterKnob;      // {139, 76, 16, 16}
    juce::Slider   shimmerKnob;     // {139, 92, 16, 16}
    juce::Slider   harmonicsKnob;   // {139, 108, 16, 16}
    juce::Slider   formantKnob;     // {139, 124, 16, 16}
    juce::ComboBox presetCombo;     // {84, 163, 70, 16}
    juce::Slider   effectKnob;      // {139, 179, 16, 16}

    juce::Label    valueTip;        // floating value readout while a knob is hovered/dragged

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment,
        jitterAttachment, shimmerAttachment, harmonicsAttachment,
        formantAttachment, effectAttachment;

    bool suppressCustomMarking = false;
    int  lastPresetNoShown = -1;
    juce::uint32 lastComboSync = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceDriveAudioProcessorEditor)
};
