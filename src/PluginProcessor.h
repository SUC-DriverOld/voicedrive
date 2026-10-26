#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/VoiceDriveDSP.h"

//==============================================================================
/*  VoiceDrive — VST3 rewrite of the legacy VOICEDRIVE VST2 plugin.

    Reverse-engineered parameter layout (27 params, same order/semantics as the
    original). Primary UI params are automatable; engine params mirror the
    original's hidden "NonAuto" parameters.
*/
class VoiceDriveAudioProcessor : public juce::AudioProcessor
{
public:
    //==========================================================================
    VoiceDriveAudioProcessor();
    ~VoiceDriveAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==========================================================================
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Presets (mirrors the original's 9 factory presets; index 9 == custom)
    static const juce::StringArray presetNames;      // 9 entries
    static const juce::StringArray presetGroupNames; // 3 group headers
    void loadPreset (int presetIndex);               // sets params, presetNo = index
    void markAsCustom();                             // presetNo = 100 (custom)

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMS", createParameterLayout() };

    // raw parameter handles (indices follow the original VST2 order)
    std::array<juce::RangedAudioParameter*, 27> params {};

    voicedrive::VoiceDriveDSP dsp;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoiceDriveAudioProcessor)
};
