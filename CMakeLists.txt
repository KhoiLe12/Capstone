cmake_minimum_required(VERSION 3.22)
project(HybridSynth VERSION 51.0.0)

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build" FORCE)

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
    COPY_PLUGIN_AFTER_BUILD     FALSE
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

if (MSVC)
    target_compile_options(HybridSynth PRIVATE /arch:AVX2 /fp:fast)
endif()


# ---------------------------------------------------------------------------
# Benchmark & Real-Time Safety Target (bench_synth)
# ---------------------------------------------------------------------------
add_executable(bench_synth
    src/bench/BenchSynth.cpp
    src/dsp/BiquadFilter.cpp
    src/dsp/Exciter.cpp
    src/dsp/KarplusStrong.cpp
    src/dsp/Voice.cpp
    src/dsp/BodyResonance.cpp
    src/dsp/SynthEngine.cpp
)

target_include_directories(bench_synth
    PRIVATE
        src/
)

target_compile_definitions(bench_synth
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(bench_synth
    PRIVATE
        juce::juce_audio_basics
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (MSVC)
    target_compile_options(bench_synth PRIVATE /arch:AVX2 /fp:fast)
endif()


# ---------------------------------------------------------------------------
# CLI Headless Audio Renderer (render_audio)
# Used for Multi-Scale STFT spectral distance evaluation against acoustic datasets
# ---------------------------------------------------------------------------
add_executable(render_audio
    src/bench/RenderAudioCLI.cpp
    src/dsp/BiquadFilter.cpp
    src/dsp/Exciter.cpp
    src/dsp/KarplusStrong.cpp
    src/dsp/Voice.cpp
    src/dsp/BodyResonance.cpp
    src/dsp/SynthEngine.cpp
)

target_include_directories(render_audio
    PRIVATE
        src/
)

target_compile_definitions(render_audio
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

target_link_libraries(render_audio
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if (MSVC)
    target_compile_options(render_audio PRIVATE /arch:AVX2 /fp:fast)
endif()


# ---------------------------------------------------------------------------
# Automated Plugin Validation Target (pluginval)
# ---------------------------------------------------------------------------
find_program(PLUGINVAL_EXE NAMES pluginval pluginval.exe
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}/tools/pluginval"
)

if (PLUGINVAL_EXE)
    set(PLUGIN_VALIDATE_PATH "${CMAKE_BINARY_DIR}/HybridSynth_artefacts/${CMAKE_BUILD_TYPE}/VST3/Hybrid Synth.vst3")
    add_custom_target(validate
        COMMAND "${PLUGINVAL_EXE}"
                --validate "${PLUGIN_VALIDATE_PATH}"
                --strictness-level 8
                --skip-gui-tests
                --verbose
        DEPENDS HybridSynth_VST3
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Running pluginval automated validation on Hybrid Synth VST3..."
    )
endif()




