#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
const juce::StringArray VoiceDriveAudioProcessor::presetNames {
    "Rough Voice", "Rough Voice+", "Rough Voice++",
    "Pop Growl 1", "Pop Growl 2", "Pop Growl 3",
    "Growl", "Grunt", "Guttural"
};

const juce::StringArray VoiceDriveAudioProcessor::presetGroupNames {
    "ROUGH VOICE", "POP GROWL", "EXTREME"
};

//==============================================================================
VoiceDriveAudioProcessor::VoiceDriveAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // collect raw parameter handles in original VST2 index order
    const char* ids[27] = {
        "effectLevel", "presetNo", "oscWaveform", "oscSpeed", "oscNoiseRatio",
        "jitter", "shimmer", "shimmerFreq1", "shimmerFreq2",
        "f0EstType", "f0EstRgeMin", "f0EstRgeMax",
        "bstLevel", "bstAlgo", "bstRgeTopMin", "bstRgeTopMax", "bstRgeBtmMin", "bstRgeBtmMax",
        "shiftLevel", "shiftRgeMin", "shiftRgeMax", "shiftStyle", "shiftRate", "shiftMixLevel",
        "addGain", "lagOrder", "phaseShift"
    };
    for (int i = 0; i < 27; ++i)
        params[(size_t) i] = dynamic_cast<juce::RangedAudioParameter*> (apvts.getParameter (ids[i]));

    loadPreset (0); // original ctor loads preset 0 (Rough Voice)
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VoiceDriveAudioProcessor::createParameterLayout()
{
    using P = juce::AudioProcessorValueTreeState;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; };
    juce::ignoreUnused (percent);

    auto floatParam = [&] (const juce::String& id, const juce::String& name,
                           float minValue, float maxValue, float interval, float def,
                           bool automatable = false,
                           std::function<juce::String (float)> toString = nullptr) {
        juce::AudioParameterFloatAttributes attrs;
        if (! automatable)
            attrs = attrs.withAutomatable (false);
        if (toString)
            attrs = attrs.withStringFromValueFunction ([toString] (float v, int) { return toString (v); });
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name,
            juce::NormalisableRange<float> { minValue, maxValue, interval }, def, attrs));
    };

    auto intParam = [&] (const juce::String& id, const juce::String& name,
                         int minValue, int maxValue, int def,
                         bool automatable = false) {
        auto attrs = juce::AudioParameterIntAttributes();
        if (! automatable)
            attrs = attrs.withAutomatable (false);
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { id, 1 }, name, minValue, maxValue, def, attrs));
    };

    // ---- order & semantics mirror the original VST2 plugin ------------------
    // #0 Effect Level (UI knob)
    floatParam ("effectLevel", "Effect Level", 0.0f, 1.0f, 0.01f, 1.0f, true,
                [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; });
    // #1 Preset No (UI combo)
    intParam ("presetNo", "Preset No", 0, 100, 0, true);   // default 0 = first preset (was 100 = custom, which hosts applied on insert -> blank combo)
    // #2 OSC Waveform (UI combo): 1=Sin 2=Square 3=Triangle 4=FullWave 5=Noise
    // range MUST match the combo's 5 items: ComboBoxAttachment maps the
    // normalized parameter onto item indices, so 1..100 collapsed to "Sin".
    intParam ("oscWaveform", "OSC Waveform", 1, 5, 2, true);
    // #3 OSC Speed (UI knob)
    floatParam ("oscSpeed", "OSC Speed", 0.0f, 1.0f, 0.01f, 0.5f, true,
                [] (float v) { return juce::String (v, 2); });
    // #4..#26 engine ("NonAuto") parameters -- except the four that carry UI knobs
    floatParam ("oscNoiseRatio", "OSC Noise Ratio", 0.0f, 1.0f, 0.01f, 0.3f);
    floatParam ("jitter", "Jitter", 0.0f, 1.0f, 0.001f, 0.025f, true);      // UI knob -> automatable
    floatParam ("shimmer", "Shimmer", 0.0f, 1.0f, 0.01f, 0.4f, true);       // UI knob -> automatable
    floatParam ("shimmerFreq1", "Shimmer Freq1", 0.0f, 22050.0f, 0.01f, 2000.0f);
    floatParam ("shimmerFreq2", "Shimmer Freq2", 0.0f, 22050.0f, 0.01f, 7000.0f);
    intParam   ("f0EstType", "F0 Est Type", 1, 100, 3);
    floatParam ("f0EstRgeMin", "F0 Est Rge Min", 0.0f, 1000.0f, 0.01f, 40.0f);
    floatParam ("f0EstRgeMax", "F0 Est Rge Max", 0.0f, 1000.0f, 0.01f, 1000.0f);
    floatParam ("bstLevel", "Bst Level", 0.0f, 1.0f, 0.01f, 0.4f, true);    // UI knob -> automatable
    intParam   ("bstAlgo", "Bst Algo", 1, 1000, 3);
    floatParam ("bstRgeTopMin", "Bst Rge Top Min", 0.0f, 22050.0f, 0.01f, 4000.0f);
    floatParam ("bstRgeTopMax", "Bst Rge Top Max", 0.0f, 22050.0f, 0.01f, 22050.0f);
    floatParam ("bstRgeBtmMin", "Bst Rge Btm Min", 0.0f, 22050.0f, 0.01f, 0.0f);
    floatParam ("bstRgeBtmMax", "Bst Rge Btm Max", 0.0f, 22050.0f, 0.01f, 22050.0f);
    floatParam ("shiftLevel", "Shift Level", 0.0f, 1.0f, 0.01f, 1.0f);
    floatParam ("shiftRgeMin", "Shift Rge Min", 0.0f, 22050.0f, 0.01f, 0.0f);   // ref preset default = 0 Hz
    floatParam ("shiftRgeMax", "Shift Rge Max", 0.0f, 22050.0f, 0.01f, 10000.0f);
    intParam   ("shiftStyle", "Shift Style", 1, 1000, 4);
    // NOTE: original stored raw negative values via its NonAuto setter, so the
    // rewrite widens the range to -1..1 to keep the presets faithful.
    floatParam ("shiftRate", "Shift Rate", -1.0f, 1.0f, 0.01f, 0.0f, true); // UI knob -> automatable
    floatParam ("shiftMixLevel", "Shift Mix Level", 0.0f, 1.0f, 0.01f, 1.0f);
    floatParam ("addGain", "Add Gain", 0.0f, 20.0f, 0.01f, 0.0f);
    floatParam ("lagOrder", "Lag Order", 0.0f, 1.1337868f, 0.00002f, 0.11337868f);
    floatParam ("phaseShift", "Phase Shift", 0.0f, 1.0f, 0.01f, 1.0f);

    return layout;
}

//==============================================================================
void VoiceDriveAudioProcessor::loadPreset (int presetIndex)
{
    if (presetIndex < 0 || presetIndex > 8)
        return;

    auto setF = [this] (int idx, float v) { params[(size_t) idx]->setValueNotifyingHost (params[(size_t) idx]->convertTo0to1 (v)); };
    auto setI = [this] (int idx, int v)   { params[(size_t) idx]->setValueNotifyingHost (params[(size_t) idx]->convertTo0to1 ((float) v)); };

    // ---- common base applied by every preset (from sub_18050B530) ----------
    setI (2, 2);          setF (3, 0.5f);       setF (5, 0.025f);    setF (6, 0.4f);
    setF (7, 2000.0f);    setF (8, 7000.0f);
    setF (12, 0.4f);      setI (13, 3);
    setF (14, 4000.0f);   setF (15, 22050.0f);  setF (16, 0.0f);     setF (17, 22050.0f);
    setF (18, 1.0f);      setF (19, 0.0f);      setF (20, 10000.0f); setI (21, 4);
    setF (23, 1.0f);
    setF (24, 0.0f);      setF (25, 0.11337868f); setF (26, 1.0f);

    // ---- per-preset deltas --------------------------------------------------
    struct Delta { int waveform; float speed, jitter, shimmer, bstLevel, shiftRate, noiseRatio; };
    static const Delta deltas[9] = {
        /* Rough Voice   */ { 2, 0.50f, 0.025f, 0.4f, 0.4f,  0.0f, 0.3f },
        /* Rough Voice+  */ { 2, 0.50f, 0.025f, 0.4f, 0.8f,  0.0f, 0.3f },
        /* Rough Voice++ */ { 2, 0.50f, 0.050f, 0.6f, 1.0f,  0.0f, 0.3f },
        /* Pop Growl 1   */ { 4, 0.22f, 0.025f, 0.4f, 0.4f, -0.2f, 0.3f },
        /* Pop Growl 2   */ { 1, 0.22f, 0.050f, 0.5f, 0.8f, -0.2f, 0.3f },
        /* Pop Growl 3   */ { 4, 0.15f, 0.050f, 0.4f, 0.8f, -0.2f, 0.3f },
        /* Growl         */ { 5, 0.50f, 0.500f, 0.0f, 1.0f,  0.0f, 0.0f },
        /* Grunt         */ { 5, 0.50f, 0.500f, 0.0f, 1.0f, -0.3f, 0.0f },
        /* Guttural      */ { 5, 0.50f, 0.500f, 0.0f, 1.0f, -1.0f, 0.0f },
    };
    const auto& d = deltas[presetIndex];
    setI (2, d.waveform);
    setF (3, d.speed);
    setF (4, d.noiseRatio);
    setF (5, d.jitter);
    setF (6, d.shimmer);
    setF (12, d.bstLevel);
    setF (22, d.shiftRate);

    setI (1, presetIndex);
}

void VoiceDriveAudioProcessor::markAsCustom()
{
    params[1]->setValueNotifyingHost (params[1]->convertTo0to1 (100.0f));
}

//==============================================================================
void VoiceDriveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    dsp.prepare (sampleRate, (juce::uint32) samplesPerBlock,
                 (juce::uint32) getTotalNumOutputChannels());
    setLatencySamples (dsp.getLatencySamples());
}

void VoiceDriveAudioProcessor::releaseResources()
{
    dsp.reset();
}

bool VoiceDriveAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    auto in = layouts.getMainInputChannelSet();
    auto out = layouts.getMainOutputChannelSet();
    if (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::mono())
        return true;
    if (in == juce::AudioChannelSet::stereo() && out == juce::AudioChannelSet::stereo())
        return true;
    return false;
}

void VoiceDriveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto readF = [this] (int i) { return dynamic_cast<juce::AudioParameterFloat*> (params[(size_t) i])->get(); };
    auto readI = [this] (int i) { return dynamic_cast<juce::AudioParameterInt*>   (params[(size_t) i])->get(); };

    voicedrive::Params p {};
    p.effectLevel   = readF (0);
    p.oscWaveform   = readI (2);
    p.oscSpeed      = readF (3);
    p.oscNoiseRatio = readF (4);
    p.jitter        = readF (5);
    p.shimmer       = readF (6);
    p.shimmerFreq1  = readF (7);
    p.shimmerFreq2  = readF (8);
    p.f0EstType     = readI (9);
    p.f0EstRgeMin   = readF (10);
    p.f0EstRgeMax   = readF (11);
    p.bstLevel      = readF (12);
    p.bstAlgo       = readI (13);
    p.bstRgeTopMin  = readF (14);
    p.bstRgeTopMax  = readF (15);
    p.bstRgeBtmMin  = readF (16);
    p.bstRgeBtmMax  = readF (17);
    p.shiftLevel    = readF (18);
    p.shiftRgeMin   = readF (19);
    p.shiftRgeMax   = readF (20);
    p.shiftStyle    = readI (21);
    p.shiftRate     = readF (22);
    p.shiftMixLevel = readF (23);
    p.addGain       = readF (24);
    p.lagOrder      = readF (25);
    p.phaseShift    = readF (26);

    dsp.process (buffer, p);
}

//==============================================================================
juce::AudioProcessorEditor* VoiceDriveAudioProcessor::createEditor() { return new VoiceDriveAudioProcessorEditor (*this); }

void VoiceDriveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VoiceDriveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VoiceDriveAudioProcessor(); }
