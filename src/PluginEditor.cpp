#include "PluginEditor.h"

namespace
{
// ---- original colours (docs/deep-dive-editor.md) ---------------------------
constexpr juce::uint32 colBackground    = 0xFF323232;  // flat dark gray background
constexpr juce::uint32 colPanelBorder   = 0xFF787878;  // plain 1px box outlines
constexpr juce::uint32 colRowLabel      = 0xFFD3D3D3;  // Font 8.0 regular
constexpr juce::uint32 colPanelTitle    = 0xFF9E9E9E;  // Font 8.0 bold
constexpr juce::uint32 colComboBg       = 0xFF323232;  // combo boxes match the background
constexpr juce::uint32 colComboHover    = 0xFF464646;  // popup highlight

// ---- original layout rects ------------------------------------------------
constexpr int vibX = 3,  vibY = 3,   vibW = 156, vibH = 52;
constexpr int drvX = 3,  drvY = 58,  drvW = 156, drvH = 84;
constexpr int mstX = 3,  mstY = 145, mstW = 156, mstH = 52;

void styleCombo (juce::ComboBox& box, juce::LookAndFeel& lf)
{
    box.setLookAndFeel (&lf);
    // dark combo boxes with white text (matches the flat dark theme)
    box.setColour (juce::ComboBox::backgroundColourId,       juce::Colour (colComboBg));
    box.setColour (juce::ComboBox::outlineColourId,          juce::Colour (colPanelBorder));
    box.setColour (juce::ComboBox::textColourId,             juce::Colours::white);
    box.setColour (juce::ComboBox::arrowColourId,            juce::Colour (colRowLabel));
    box.setColour (juce::TextEditor::backgroundColourId,     juce::Colour (colComboBg));
    box.setColour (juce::TextEditor::outlineColourId,        juce::Colour (colPanelBorder));
    box.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colour (colPanelTitle));
    box.setColour (juce::TextEditor::textColourId,           juce::Colours::white);
    box.setColour (juce::PopupMenu::backgroundColourId,      juce::Colour (colComboBg));
    box.setColour (juce::PopupMenu::textColourId,            juce::Colours::white);
    box.setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (colComboHover));
    box.setColour (juce::PopupMenu::highlightedTextColourId,      juce::Colours::white);
    box.setJustificationType (juce::Justification::centred);
    box.setEditableText (false);
}

void styleKnob (juce::Slider& s, double doubleClickValue)
{
    s.setSliderStyle (juce::Slider::Rotary);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 100, 20);
    s.setRange (0.0, 1.0);
    s.setDoubleClickReturnValue (true, doubleClickValue);
    // white knob accent (was LookAndFeel_V2 default blue)
    s.setColour (juce::Slider::thumbColourId,              juce::Colours::white);
    s.setColour (juce::Slider::rotarySliderFillColourId,   juce::Colours::white);
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF5A5A5A));
}
} // namespace

//==============================================================================
VoiceDriveAudioProcessorEditor::VoiceDriveAudioProcessorEditor (VoiceDriveAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    // original editor sets LookAndFeel_V2 globally for the editor
    setLookAndFeel (&editorLookAndFeel);

    // SliderAttachment constructors send sendNotificationSync initial updates
    // (juce_ParameterAttachments.cpp:171-172) which would fire sliderValueChanged
    // -> markAsCustom on every editor construction. Suppress until setup is done.
    suppressCustomMarking = true;

    styleCombo (waveformCombo, comboLookAndFeel);
    waveformCombo.addItem ("Sin", 1);
    waveformCombo.addItem ("Square", 2);
    waveformCombo.addItem ("Triangle", 3);
    waveformCombo.addItem ("FullWave", 4);
    waveformCombo.addItem ("Noise", 5);
    addAndMakeVisible (waveformCombo);

    styleKnob (speedKnob, 0.5);      // original double-click returns
    styleKnob (jitterKnob, 0.025);
    styleKnob (shimmerKnob, 0.4);
    styleKnob (harmonicsKnob, 0.4);
    styleKnob (formantKnob, 0.0);
    styleKnob (effectKnob, 0.5);
    for (auto* s : { &speedKnob, &jitterKnob, &shimmerKnob, &harmonicsKnob, &formantKnob, &effectKnob })
    {
        s->setLookAndFeel (&editorLookAndFeel);
        addAndMakeVisible (*s);
        s->addListener (this);
    }

    styleCombo (presetCombo, comboLookAndFeel);
    presetCombo.addSectionHeading ("ROUGH VOICE");
    presetCombo.addItem ("Rough Voice",   1);
    presetCombo.addItem ("Rough Voice+",  2);
    presetCombo.addItem ("Rough Voice++", 3);
    presetCombo.addSeparator();
    presetCombo.addSectionHeading ("POP GROWL");
    presetCombo.addItem ("Pop Growl 1", 4);
    presetCombo.addItem ("Pop Growl 2", 5);
    presetCombo.addItem ("Pop Growl 3", 6);
    presetCombo.addSeparator();
    presetCombo.addSectionHeading ("EXTREME");
    presetCombo.addItem ("Growl",    7);
    presetCombo.addItem ("Grunt",    8);
    presetCombo.addItem ("Guttural", 9);
    presetCombo.addSeparator();
    presetCombo.addItem ("Custom",   10);   // shown when Preset No != 0..8 (real item: setText on a non-editable combo displays nothing)
    presetCombo.addListener (this);
    addAndMakeVisible (presetCombo);

    using P = juce::AudioProcessorValueTreeState;
    waveformAttachment  = std::make_unique<P::ComboBoxAttachment> (processor.apvts, "oscWaveform", waveformCombo);
    speedAttachment     = std::make_unique<P::SliderAttachment> (processor.apvts, "oscSpeed", speedKnob);
    jitterAttachment    = std::make_unique<P::SliderAttachment> (processor.apvts, "jitter", jitterKnob);
    shimmerAttachment   = std::make_unique<P::SliderAttachment> (processor.apvts, "shimmer", shimmerKnob);
    harmonicsAttachment = std::make_unique<P::SliderAttachment> (processor.apvts, "bstLevel", harmonicsKnob);
    formantAttachment   = std::make_unique<P::SliderAttachment> (processor.apvts, "shiftLevel", formantKnob);
    effectAttachment    = std::make_unique<P::SliderAttachment> (processor.apvts, "effectLevel", effectKnob);

    // widget names = row labels, painted by the editor itself (as in the original)
    waveformCombo.setName ("Waveform");
    speedKnob.setName ("Speed");
    jitterKnob.setName ("Jitter");
    shimmerKnob.setName ("Shimmer");
    harmonicsKnob.setName ("NI-Harmonics");
    formantKnob.setName ("Formant Shift");
    presetCombo.setName ("Presets");
    effectKnob.setName ("Effect Level");

    // select the current preset immediately (default: first item checked)
    syncPresetCombo();

    setSize (juce::roundToInt ((float) originalWidth * uiScale),
             juce::roundToInt ((float) originalHeight * uiScale));   // original: 162x200, scaled up
    startTimerHz (33); // original: 30 ms timer

    // re-arm custom marking on the next message-loop pass, after all attachment
    // initial updates have settled (they are synchronous, this is belt & braces)
    juce::MessageManager::callAsync (
        [safe = juce::Component::SafePointer<VoiceDriveAudioProcessorEditor> (this)]
        { if (safe != nullptr) safe->suppressCustomMarking = false; });
}

VoiceDriveAudioProcessorEditor::~VoiceDriveAudioProcessorEditor()
{
    stopTimer();
    presetCombo.setLookAndFeel (nullptr);
    waveformCombo.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

//==============================================================================
void VoiceDriveAudioProcessorEditor::sliderValueChanged (juce::Slider*)
{
    if (! suppressCustomMarking)
        processor.markAsCustom(); // Preset No -> 100 (custom), as in the original
}

void VoiceDriveAudioProcessorEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box == &presetCombo)
    {
        auto id = presetCombo.getSelectedId();
        if (id == 10)
            return;                     // "Custom" is a display state, not a preset
        if (id >= 1 && id <= 9)
        {
            suppressCustomMarking = true;
            processor.loadPreset (id - 1); // original: comboBoxChanged -> setPresetIndex(id-1)
            suppressCustomMarking = false;
            lastPresetNoShown = id - 1;
        }
    }
}

void VoiceDriveAudioProcessorEditor::syncPresetCombo()
{
    if (auto* ip = dynamic_cast<juce::AudioParameterInt*> (processor.params[1]))
    {
        int presetNo = ip->get();
        if (presetNo < 0 || presetNo > 8)
            presetNo = 100;                       // any custom value maps to the Custom item
        if (presetNo == lastPresetNoShown)
            return;
        lastPresetNoShown = presetNo;
        presetCombo.setSelectedId (presetNo <= 8 ? presetNo + 1 : 10, juce::dontSendNotification);
        repaint();
    }
}

void VoiceDriveAudioProcessorEditor::timerCallback()
{
    // sync preset combo with the "Preset No" parameter (>200 ms cadence in the original)
    auto now = juce::Time::getMillisecondCounter();
    if (now - lastComboSync < 200)
        return;
    lastComboSync = now;
    syncPresetCombo();
}

//==============================================================================
void VoiceDriveAudioProcessorEditor::paint (juce::Graphics& g)
{
    const float s = uiScale;
    g.fillAll (juce::Colour (colBackground));

    // panels: plain outlined boxes (no fill) + gray bold titles + light row labels
    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        auto rs = r.toFloat() * s;
        g.setColour (juce::Colour (colPanelBorder));
        g.drawRect (rs, s);
        g.setColour (juce::Colour (colPanelTitle));
        g.setFont (juce::FontOptions { 8.0f * s, juce::Font::bold });
        g.drawText (title, juce::Rectangle<int> { 0, (int) ((r.getY() + 1) * s),
                                                  (int) (originalWidth * s), (int) (13 * s) },
                    juce::Justification::centredTop);
    };

    drawPanel ({ vibX, vibY, vibW, vibH }, "VIBRATION");
    drawPanel ({ drvX, drvY, drvW, drvH }, "DRIVE ELEMENTS");
    drawPanel ({ mstX, mstY, mstW, mstH }, "MASTER");

    // row labels painted by the editor (component names), centred-left at x = 8
    g.setColour (juce::Colour (colRowLabel));
    g.setFont (juce::FontOptions { 8.0f * s });
    auto drawLabel = [&] (juce::Component& c)
    {
        auto b = c.getBounds();
        g.drawText (c.getName(), juce::Rectangle<int> { (int) (8 * s), (int) (b.getY() + s * 1.5f),
                                                        getWidth() - (int) (8 * s), (int) (13 * s) },
                    juce::Justification::centredLeft);
    };
    drawLabel (waveformCombo);
    drawLabel (speedKnob);
    drawLabel (jitterKnob);
    drawLabel (shimmerKnob);
    drawLabel (harmonicsKnob);
    drawLabel (formantKnob);
    drawLabel (presetCombo);
    drawLabel (effectKnob);
}

void VoiceDriveAudioProcessorEditor::resized()
{
    const auto scaleRect = [] (int x, int y, int w, int h)
    {
        return juce::Rectangle<int> (juce::roundToInt ((float) x * uiScale),
                                     juce::roundToInt ((float) y * uiScale),
                                     juce::roundToInt ((float) w * uiScale),
                                     juce::roundToInt ((float) h * uiScale));
    };
    waveformCombo.setBounds (scaleRect (84, 21, 70, 16));
    speedKnob.setBounds (scaleRect (139, 37, 16, 16));
    jitterKnob.setBounds (scaleRect (139, 76, 16, 16));
    shimmerKnob.setBounds (scaleRect (139, 92, 16, 16));
    harmonicsKnob.setBounds (scaleRect (139, 108, 16, 16));
    formantKnob.setBounds (scaleRect (139, 124, 16, 16));
    presetCombo.setBounds (scaleRect (84, 163, 70, 16));
    effectKnob.setBounds (scaleRect (139, 179, 16, 16));
}
