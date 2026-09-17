#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

// ---------------------------------------------------------------------------
// Constructor — APVTS is constructed here with the parameter layout
// ---------------------------------------------------------------------------

HybridSynthProcessor::HybridSynthProcessor()
    : juce::AudioProcessor(
          BusesProperties()
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "HybridSynthState", createParameterLayout())
{
}

// ---------------------------------------------------------------------------
// Parameter layout
// ---------------------------------------------------------------------------

juce::AudioProcessorValueTreeState::ParameterLayout
HybridSynthProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Decay: controls KS loop gain (= string sustain)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "decay", 1 },
        "Decay",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.80f));

    // Brightness: spectral content of exciter burst
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "brightness", 1 },
        "Brightness",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.50f));

    // Pick Position: fractional position of plectrum along string
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "pickPosition", 1 },
        "Pick Position",
        juce::NormalisableRange<float>(0.05f, 0.50f),
        0.12f));

    // Body Mix: dry/wet blend of BodyResonance
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bodyMix", 1 },
        "Body Mix",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.70f));

    // Master Gain: output level
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "masterGain", 1 },
        "Master Gain",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.80f));

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void HybridSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.init(static_cast<float>(sampleRate), samplesPerBlock);
}

void HybridSynthProcessor::releaseResources()
{
    synth.reset();
}

// ---------------------------------------------------------------------------
// Audio + MIDI processing
// ---------------------------------------------------------------------------

void HybridSynthProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Sync parameters from APVTS to SynthEngine (thread-safe atomic load)
    synth.paramDecay      = apvts.getRawParameterValue("decay")->load();
    synth.paramBrightness = apvts.getRawParameterValue("brightness")->load();
    synth.paramPickPos    = apvts.getRawParameterValue("pickPosition")->load();
    synth.paramBodyMix    = apvts.getRawParameterValue("bodyMix")->load();
    synth.paramMasterGain = apvts.getRawParameterValue("masterGain")->load();

    // Process MIDI events
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            synth.noteOff(msg.getNoteNumber());
        // All Sounds Off / All Notes Off
        else if (msg.isAllNotesOff() || msg.isResetAllControllers())
            synth.reset();
    }

    // Render audio
    auto* outputL = buffer.getWritePointer(0);
    auto* outputR = buffer.getWritePointer(1);
    synth.process(outputL, outputR, buffer.getNumSamples());
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

juce::AudioProcessorEditor* HybridSynthProcessor::createEditor()
{
    return new HybridSynthEditor(*this);
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------

void HybridSynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HybridSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
}

// ---------------------------------------------------------------------------
// Plugin factory function (required by JUCE)
// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HybridSynthProcessor();
}

