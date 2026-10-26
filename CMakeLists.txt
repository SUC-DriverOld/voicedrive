cmake_minimum_required(VERSION 3.22)

project(VoiceDrive VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(third_party/JUCE)

juce_add_plugin(VoiceDrive
    COMPANY_NAME "Crypton"
    BUNDLE_ID "com.crypton.voicedrive"
    PLUGIN_MANUFACTURER_CODE VdRw
    PLUGIN_CODE Vdrc
    FORMATS VST3
    PRODUCT_NAME "VoiceDrive"
    COPY_PLUGIN_AFTER_BUILD FALSE
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

target_sources(VoiceDrive PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/DSP/VoiceDriveDSP.cpp
)

target_include_directories(VoiceDrive PRIVATE src)

target_compile_definitions(VoiceDrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(VoiceDrive
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
