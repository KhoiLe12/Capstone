cmake_minimum_required(VERSION 3.22)
project(HybridSynth VERSION 0.1.0)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is added as a git submodule at ./JUCE
add_subdirectory(JUCE)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(HybridSynth
    COMPANY_NAME                "Capstone"
    PLUGIN_MANUFACTURER_CODE    Caps
    PLUGIN_CODE                 Hybs
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "Hybrid Synth"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
)

target_sources(HybridSynth
    PRIVATE
        src/dsp/BiquadFilter.cpp
        src/dsp/Exciter.cpp
        src/dsp/KarplusStrong.cpp
        src/dsp/Voice.cpp
        src/dsp/BodyResonance.cpp
        src/dsp/SynthEngine.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
)

# Allow #include "dsp/..." and #include "plugin/..." from any source file
target_include_directories(HybridSynth
    PRIVATE
        src/
)

target_compile_definitions(HybridSynth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(HybridSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

