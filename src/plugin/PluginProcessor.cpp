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
    setCurrentProgram(0);
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
        0.78f));

    // Brightness: spectral content of exciter burst / fingernail polish
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "brightness", 1 },
        "Brightness",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.72f));

    // Pick Position: fractional position of plectrum/finger along string (0.22 = acoustic soundhole)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "pickPosition", 1 },
        "Pick Position",
        juce::NormalisableRange<float>(0.05f, 0.50f),
        0.22f));

    // Stiffness: string inharmonicity / dispersion (0 = pure harmonic, 1 = metallic steel)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "stiffness", 1 },
        "Stiffness",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.28f));

    // Body Size: modal scaling factor (0.6 = small parlor, 1.6 = jumbo)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bodySize", 1 },
        "Body Size",
        juce::NormalisableRange<float>(0.6f, 1.6f),
        1.15f));

    // Body Coupling: 0 = solid body electric (max sustain), 1 = full acoustic soundboard
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bodyMix", 1 },
        "Body Coupling",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.80f));

    // Engine Model: 0 = Digital Waveguide (DWG), 1 = Bilbao FDTD (DAFx24)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "engineModel", 1 },
        "Engine Model",
        juce::StringArray{ "Digital Waveguide (DWG)", "Bilbao FDTD (DAFx24)" },
        1));

    // String Material: 0 = Nylon Classical, 1 = Steel Acoustic
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "stringMaterial", 1 },
        "String Material",
        juce::StringArray{ "Nylon Classical", "Steel Acoustic" },
        1));

    // Body Model: 0 = Classical Nylon (IR), 1 = Gibson Acoustic (IR), 2 = Modal Bank (32-Mode)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "bodyModel", 1 },
        "Body Model",
        juce::StringArray{ "Classical Nylon (IR)", "Gibson Acoustic (IR)", "Modal Bank (32-Mode)" },
        1));

    // Stereo Width: acoustic soundboard & string stereo field (0 = mono, 0.7 = natural, 1.0 = wide studio)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "stereoWidth", 1 },
        "Stereo Width",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.75f));

    // Master Gain: output level
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "masterGain", 1 },
        "Master Gain",
        juce::NormalisableRange<float>(0.f, 1.f),
        0.80f));

    // Palm Mute: physical bridge palm damping (also momentary triggered via C1 / FL: C2 keyswitch)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "palmMute", 1 },
        "Palm Mute",
        false));

    // Full Mute: fret-hand mute / dead notes / choke (also momentary triggered via D1 / FL: D2 keyswitch)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "fullMute", 1 },
        "Full Mute",
        false));

    // Strum Pattern: 8th-note alternating strum (also momentary triggered via E1 / FL: E2 keyswitch)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "strumPattern", 1 },
        "Strum 8th",
        false));

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

    // Query DAW tempo for sample-accurate rhythmic sync
    if (auto* playHead = getPlayHead())
    {
        if (auto posOpt = playHead->getPosition())
        {
            if (posOpt->getBpm())
                synth.setBpm(static_cast<float>(*posOpt->getBpm()));
        }
    }

    // Sync parameters from APVTS to SynthEngine (thread-safe atomic load)
    synth.paramDecay      = apvts.getRawParameterValue("decay")->load();
    synth.paramBrightness = apvts.getRawParameterValue("brightness")->load();
    synth.paramPickPos    = apvts.getRawParameterValue("pickPosition")->load();
    synth.paramStiffness  = apvts.getRawParameterValue("stiffness")->load();
    synth.paramBodySize   = apvts.getRawParameterValue("bodySize")->load();
    synth.paramBodyMix    = apvts.getRawParameterValue("bodyMix")->load();
    synth.paramBodyType   = apvts.getRawParameterValue("bodyModel")->load();
    synth.paramEngineType = apvts.getRawParameterValue("engineModel")->load();
    synth.paramStringMaterial = apvts.getRawParameterValue("stringMaterial")->load();
    synth.paramStereoWidth = apvts.getRawParameterValue("stereoWidth")->load();
    synth.paramMasterGain = apvts.getRawParameterValue("masterGain")->load();
    synth.paramPalmMute   = apvts.getRawParameterValue("palmMute")->load() > 0.5f;
    synth.paramFullMute   = apvts.getRawParameterValue("fullMute")->load() > 0.5f;
    synth.paramStrum      = apvts.getRawParameterValue("strumPattern")->load() > 0.5f;

    // Process MIDI events
    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            if (msg.getFloatVelocity() > 0.0001f)
                synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity());
            else
                synth.noteOff(msg.getNoteNumber());
        }
        else if (msg.isNoteOff())
        {
            synth.noteOff(msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isResetAllControllers())
        {
            synth.reset();
        }
    }

    // Render audio
    if (buffer.getNumChannels() == 0) return;

    auto* outputL = buffer.getWritePointer(0);
    auto* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;
    synth.process(outputL, outputR, buffer.getNumSamples());
}

// ---------------------------------------------------------------------------
// Factory Presets & DAW Program Management
// ---------------------------------------------------------------------------

const std::vector<HybridSynthProcessor::FactoryPreset>& HybridSynthProcessor::getFactoryPresets() noexcept
{
    static const std::vector<FactoryPreset> presets = {
        // Name,                      decay, brt,   pick,  stiff, size,  mix,   eng, mat, body, width, gain,  palm,  full,  strum
        { "Dreadnought Acoustic",     0.78f, 0.72f, 0.22f, 0.28f, 1.15f, 0.80f, 1,   1,   1,    0.75f, 0.80f, false, false, false },
        { "Nylon Concert Classical",   0.75f, 0.48f, 0.28f, 0.04f, 1.00f, 0.75f, 1,   0,   0,    0.65f, 0.85f, false, false, false },
        { "Flamenco Rasgueado",       0.55f, 0.82f, 0.14f, 0.06f, 0.85f, 0.65f, 1,   0,   0,    0.80f, 0.85f, false, false, false },
        { "Fingerstyle Auditorium",   0.85f, 0.65f, 0.25f, 0.18f, 1.05f, 0.72f, 0,   1,   1,    0.70f, 0.80f, false, false, false },
        { "Vintage Parlor Blues",     0.62f, 0.75f, 0.18f, 0.35f, 0.75f, 0.60f, 1,   1,   2,    0.50f, 0.82f, false, false, false },
        { "12-String Shimmer",        0.90f, 0.85f, 0.20f, 0.30f, 1.45f, 0.85f, 0,   1,   2,    1.00f, 0.78f, false, false, false },
        { "Percussive Palm Chops",    0.40f, 0.70f, 0.20f, 0.20f, 1.00f, 0.85f, 1,   1,   1,    0.70f, 0.85f, true,  false, false },
        { "Warm Jazz Archtop",        0.70f, 0.32f, 0.35f, 0.08f, 1.25f, 0.55f, 0,   0,   2,    0.60f, 0.85f, false, false, false }
    };
    return presets;
}

int HybridSynthProcessor::getNumPrograms()
{
    return static_cast<int>(getFactoryPresets().size());
}

int HybridSynthProcessor::getCurrentProgram()
{
    return currentProgram;
}

void HybridSynthProcessor::setCurrentProgram(int index)
{
    const auto& presets = getFactoryPresets();
    if (index < 0 || index >= static_cast<int>(presets.size()))
        return;

    currentProgram = index;
    const auto& p = presets[static_cast<size_t>(index)];

    auto setParam = [this](const char* id, float val) {
        if (auto* param = apvts.getParameter(id))
            param->setValueNotifyingHost(param->convertTo0to1(val));
    };

    setParam("decay",          p.decay);
    setParam("brightness",     p.brightness);
    setParam("pickPosition",   p.pickPos);
    setParam("stiffness",      p.stiffness);
    setParam("bodySize",       p.bodySize);
    setParam("bodyMix",        p.bodyMix);
    setParam("engineModel",    static_cast<float>(p.engineModel));
    setParam("stringMaterial", static_cast<float>(p.stringMaterial));
    setParam("bodyModel",      static_cast<float>(p.bodyModel));
    setParam("stereoWidth",    p.stereoWidth);
    setParam("masterGain",     p.masterGain);
    setParam("palmMute",       p.palmMute ? 1.0f : 0.0f);
    setParam("fullMute",       p.fullMute ? 1.0f : 0.0f);
    setParam("strumPattern",   p.strumPattern ? 1.0f : 0.0f);
}

const juce::String HybridSynthProcessor::getProgramName(int index)
{
    const auto& presets = getFactoryPresets();
    if (index >= 0 && index < static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index)].name;
    return {};
}

void HybridSynthProcessor::changeProgramName(int, const juce::String&)
{
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
    state.setProperty("currentProgram", currentProgram, nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void HybridSynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        auto vt = juce::ValueTree::fromXml(*xmlState);
        if (vt.hasProperty("currentProgram"))
            currentProgram = static_cast<int>(vt.getProperty("currentProgram"));
        apvts.replaceState(vt);
    }
}

// ---------------------------------------------------------------------------
// Plugin factory function (required by JUCE)
// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HybridSynthProcessor();
}
