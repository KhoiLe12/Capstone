#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

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

    // Body Model (order is the saved index; append only): 0 = Classical (IR, measured), 1 = Legacy Body
    // (IR, unknown origin, formerly "Gibson Acoustic"), 2 = Modal Bank, 3 = Flamenco (IR, measured),
    // 4 = Custom IR (user file; sounds like Classical until one is loaded)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "bodyModel", 1 },
        "Body Model",
        juce::StringArray{ "Classical - Contreras 1971 (IR)", "Legacy Body (IR)", "Modal Bank (32-Mode)",
                           "Flamenco - Conde 1964 (IR)", "Custom IR" },
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

    // Legato: overlapping notes on one string play as hammer-ons / pull-offs (40 ms chord window)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "legato", 1 },
        "Legato (HO/PO)",
        true));

    // Strict fretboard: drop chord notes that have no physical string position (off = still play them)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "strictFretboard", 1 },
        "Strict Fretboard",
        false));

    // ── Electric Guitar Parameters (appended to maintain state backwards compatibility) ──

    // Instrument Mode: 0 = Acoustic, 1 = Electric
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "instrumentMode", 1 },
        "Instrument Mode",
        juce::StringArray{ "Acoustic", "Electric" },
        0));

    // Electric Guitar Model: 0 = 1962 Stratocaster (25.5"), 1 = 1959 Les Paul (24.75")
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "guitarModel", 1 },
        "Guitar Model",
        juce::StringArray{ "1962 Stratocaster (25.5\")", "1959 Les Paul (24.75\")" },
        0));

    // Pickup Position: 5-way Strat / 3-way LP selector
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "pickupPosition", 1 },
        "Pickup Position",
        juce::StringArray{ "Bridge", "Bridge + Middle", "Middle", "Neck + Middle", "Neck", "Bridge + Neck" },
        0));

    // Pickup Type: 0 = Single-Coil, 1 = Humbucker
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "pickupType", 1 },
        "Pickup Type",
        juce::StringArray{ "Single-Coil (Alnico V)", "Humbucker (PAF Alnico II)" },
        0));

    // Tone Knob: passive guitar tone control (0 = dark woman tone, 1 = full open chime)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "toneKnob", 1 },
        "Tone",
        juce::NormalisableRange<float>(0.f, 1.f),
        1.0f));

    // Cab / Air Filter: 0 = pure DI output, 1 = smooth speaker cabinet roll-off
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "cabAirFilter", 1 },
        "Cab/Air Roll-off",
        false));

    return { params.begin(), params.end() };
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void HybridSynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.init(static_cast<float>(sampleRate), samplesPerBlock);
    applyCustomIRToEngine(true); // init() re-prepares the convolution engines
    isPrepared.store(true);
}

void HybridSynthProcessor::releaseResources()
{
    isPrepared.store(false);
    synth.reset();
}

// ---------------------------------------------------------------------------
// Custom IR
// ---------------------------------------------------------------------------

juce::String HybridSynthProcessor::loadCustomIRFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return "Could not read \"" + file.getFileName() + "\" as audio (supported: WAV, AIFF, FLAC, Ogg).";
    if (reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0)
        return "\"" + file.getFileName() + "\" contains no audio.";

    const auto maxLen = static_cast<juce::int64>(BodyResonance::kMaxCustomIRSeconds * reader->sampleRate);
    const int len = static_cast<int>(std::min(reader->lengthInSamples, maxLen));
    const int channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2u));
    juce::AudioBuffer<float> ir(channels, len);
    reader->read(&ir, 0, len, 0, true, channels > 1);

    {
        const juce::ScopedLock sl(customIRLock);
        customIRBuffer = std::move(ir);
        customIRSampleRate = reader->sampleRate;
        customIRName = file.getFileName();
    }
    applyCustomIRToEngine(false);
    return {};
}

juce::String HybridSynthProcessor::getCustomIRFileWarning(const juce::File& file) const
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return {}; // loadCustomIRFile reports unreadable files as errors

    // Judge the part that would actually be used (the first kMaxCustomIRSeconds)
    const auto maxLen = static_cast<juce::int64>(BodyResonance::kMaxCustomIRSeconds * reader->sampleRate);
    const int len = static_cast<int>(std::min(reader->lengthInSamples, maxLen));
    const int channels = static_cast<int>(std::min<unsigned int>(reader->numChannels, 2u));
    juce::AudioBuffer<float> ir(channels, len);
    reader->read(&ir, 0, len, 0, true, channels > 1);
    return BodyResonance::describeIRProblems(ir, reader->sampleRate);
}

void HybridSynthProcessor::clearCustomIR()
{
    {
        const juce::ScopedLock sl(customIRLock);
        customIRBuffer.setSize(0, 0);
        customIRSampleRate = 0.0;
        customIRName.clear();
    }
    applyCustomIRToEngine(false);
}

bool HybridSynthProcessor::hasCustomIR() const
{
    const juce::ScopedLock sl(customIRLock);
    return customIRBuffer.getNumSamples() > 0;
}

juce::String HybridSynthProcessor::getCustomIRName() const
{
    const juce::ScopedLock sl(customIRLock);
    return customIRName;
}

void HybridSynthProcessor::applyCustomIRToEngine(bool prepareNow)
{
    // Never called on the audio thread. With prepareNow = false, JUCE's convolution loads the IR on
    // its background thread and swaps it in while audio keeps running.
    const juce::ScopedLock sl(customIRLock);
    synth.getBodyResonance().setCustomIR(customIRBuffer, customIRSampleRate, prepareNow);
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
    synth.paramLegatoEnabled = apvts.getRawParameterValue("legato")->load() > 0.5f;
    synth.paramStrictFretboard = apvts.getRawParameterValue("strictFretboard")->load() > 0.5f;

    // Electric guitar parameters
    synth.paramInstrumentMode = apvts.getRawParameterValue("instrumentMode")->load();
    synth.paramGuitarModel    = apvts.getRawParameterValue("guitarModel")->load();
    synth.paramPickupPosition = apvts.getRawParameterValue("pickupPosition")->load();
    synth.paramPickupType     = apvts.getRawParameterValue("pickupType")->load();
    synth.paramToneKnob       = apvts.getRawParameterValue("toneKnob")->load();
    synth.paramCabAirFilter   = apvts.getRawParameterValue("cabAirFilter")->load() > 0.5f;

    if (buffer.getNumChannels() == 0) return;

    auto* outputL = buffer.getWritePointer(0);
    auto* outputR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : outputL;
    const int numSamples = buffer.getNumSamples();

    // Sample-accurate MIDI: collect timestamped events and let SynthEngine apply each one at its
    // exact sample. If a block carries more events than the fixed queue holds, render up to the
    // overflowing event's position and continue with an empty queue (no allocation either way).
    int segmentStart = 0;
    int numEvents = 0;

    for (const auto meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        SynthEngine::Event e;
        if (msg.isNoteOn())
        {
            e.type     = SynthEngine::Event::Type::NoteOn;   // velocity 0 is treated as note-off by the engine
            e.note     = msg.getNoteNumber();
            e.velocity = msg.getFloatVelocity();
        }
        else if (msg.isNoteOff())
        {
            e.type = SynthEngine::Event::Type::NoteOff;
            e.note = msg.getNoteNumber();
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff() || msg.isResetAllControllers())
        {
            e.type = SynthEngine::Event::Type::AllNotesOff;
        }
        else
        {
            continue;
        }

        const int pos = std::clamp(meta.samplePosition, segmentStart, numSamples);
        if (numEvents == kMaxEventsPerSegment)
        {
            synth.process(outputL + segmentStart, outputR + segmentStart, pos - segmentStart,
                          eventQueue.data(), numEvents);
            segmentStart = pos;
            numEvents = 0;
        }
        e.sampleOffset = pos - segmentStart;
        eventQueue[static_cast<size_t>(numEvents++)] = e;
    }

    synth.process(outputL + segmentStart, outputR + segmentStart, numSamples - segmentStart,
                  eventQueue.data(), numEvents);
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
        { "Flamenco Rasgueado",       0.55f, 0.82f, 0.14f, 0.06f, 0.85f, 0.65f, 1,   0,   3,    0.80f, 0.85f, false, false, false },
        { "Fingerstyle Auditorium",   0.85f, 0.65f, 0.25f, 0.18f, 1.05f, 0.72f, 1,   1,   1,    0.70f, 0.80f, false, false, false },
        { "Vintage Parlor Blues",     0.62f, 0.75f, 0.18f, 0.35f, 0.75f, 0.60f, 1,   1,   2,    0.50f, 0.82f, false, false, false },
        { "12-String Shimmer",        0.90f, 0.85f, 0.20f, 0.30f, 1.45f, 0.85f, 1,   1,   2,    1.00f, 0.78f, false, false, false },
        { "Percussive Palm Chops",    0.40f, 0.70f, 0.20f, 0.20f, 1.00f, 0.85f, 1,   1,   1,    0.70f, 0.85f, true,  false, false },
        { "Warm Jazz Archtop",        0.70f, 0.32f, 0.35f, 0.08f, 1.25f, 0.55f, 1,   0,   2,    0.60f, 0.85f, false, false, false },

        // Electric Guitar DI Presets:
        // Name,                      decay, brt,   pick,  stiff, size,  mix,   eng, mat, body, width, gain,  palm,  full,  strum, inst, gtr, pickPos, pickType, tone, cabAir
        { "Strat Clean Funk DI",      0.82f, 0.78f, 0.22f, 0.15f, 1.00f, 0.00f, 1,   1,   1,    0.00f, 0.76f, false, false, false, 1,    0,   3,       0,        1.0f, false },
        { "Texas Blues Lead",         0.86f, 0.72f, 0.20f, 0.18f, 1.00f, 0.00f, 1,   1,   1,    0.00f, 0.76f, false, false, false, 1,    0,   4,       0,        0.88f, false },
        { "Les Paul Vintage Warmth",  0.90f, 0.60f, 0.24f, 0.12f, 1.00f, 0.00f, 1,   1,   1,    0.00f, 0.76f, false, false, false, 1,    1,   4,       1,        0.75f, false },
        { "Bridge Humbucker Bite",    0.88f, 0.82f, 0.18f, 0.20f, 1.00f, 0.00f, 1,   1,   1,    0.00f, 0.78f, false, false, false, 1,    1,   0,       1,        1.00f, false }
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
    setParam("instrumentMode", static_cast<float>(p.instrumentMode));
    setParam("guitarModel",    static_cast<float>(p.guitarModel));
    setParam("pickupPosition", static_cast<float>(p.pickupPosition));
    setParam("pickupType",     static_cast<float>(p.pickupType));
    setParam("toneKnob",       p.toneKnob);
    setParam("cabAirFilter",   p.cabAirFilter ? 1.0f : 0.0f);

    // Immediate direct sync to SynthEngine so DSP updates without waiting
    synth.paramDecay          = p.decay;
    synth.paramBrightness     = p.brightness;
    synth.paramPickPos        = p.pickPos;
    synth.paramStiffness      = p.stiffness;
    synth.paramBodySize       = p.bodySize;
    synth.paramBodyMix        = p.bodyMix;
    synth.paramEngineType     = static_cast<float>(p.engineModel);
    synth.paramStringMaterial = static_cast<float>(p.stringMaterial);
    synth.paramBodyType       = static_cast<float>(p.bodyModel);
    synth.paramStereoWidth    = p.stereoWidth;
    synth.paramMasterGain     = p.masterGain;
    synth.paramPalmMute       = p.palmMute;
    synth.paramFullMute       = p.fullMute;
    synth.paramStrum          = p.strumPattern;
    synth.paramInstrumentMode = static_cast<float>(p.instrumentMode);
    synth.paramGuitarModel    = static_cast<float>(p.guitarModel);
    synth.paramPickupPosition = static_cast<float>(p.pickupPosition);
    synth.paramPickupType     = static_cast<float>(p.pickupType);
    synth.paramToneKnob       = p.toneKnob;
    synth.paramCabAirFilter   = p.cabAirFilter;

    // Tell the host the program changed, so its preset display (and the AU's present-preset
    // property) follows selections made in the plugin's own preset dropdown.
    updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withProgramChanged(true));
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

    // Embed the custom IR (planar float32, base64) so the project is self-contained
    {
        const juce::ScopedLock sl(customIRLock);
        if (customIRBuffer.getNumSamples() > 0)
        {
            juce::MemoryBlock samples;
            for (int ch = 0; ch < customIRBuffer.getNumChannels(); ++ch)
                samples.append(customIRBuffer.getReadPointer(ch),
                               sizeof(float) * static_cast<size_t>(customIRBuffer.getNumSamples()));
            juce::ValueTree ir("CUSTOM_IR");
            ir.setProperty("name", customIRName, nullptr);
            ir.setProperty("sampleRate", customIRSampleRate, nullptr);
            ir.setProperty("channels", customIRBuffer.getNumChannels(), nullptr);
            ir.setProperty("samples", customIRBuffer.getNumSamples(), nullptr);
            ir.setProperty("data", samples.toBase64Encoding(), nullptr);
            state.appendChild(ir, nullptr);
        }
    }
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

        // Custom IR travels in the state; detach it so it is not kept inside the APVTS tree
        juce::AudioBuffer<float> restoredIR;
        double restoredRate = 0.0;
        juce::String restoredName;
        if (auto ir = vt.getChildWithName("CUSTOM_IR"); ir.isValid())
        {
            const int channels = static_cast<int>(ir.getProperty("channels", 0));
            const int samples  = static_cast<int>(ir.getProperty("samples", 0));
            juce::MemoryBlock data;
            if (channels >= 1 && channels <= 2 && samples > 0
                && data.fromBase64Encoding(ir.getProperty("data").toString())
                && data.getSize() == sizeof(float) * static_cast<size_t>(channels) * static_cast<size_t>(samples))
            {
                restoredIR.setSize(channels, samples);
                for (int ch = 0; ch < channels; ++ch)
                    restoredIR.copyFrom(ch, 0, static_cast<const float*>(data.getData()) + static_cast<size_t>(ch) * static_cast<size_t>(samples), samples);
                restoredRate = static_cast<double>(ir.getProperty("sampleRate", 48000.0));
                restoredName = ir.getProperty("name").toString();
            }
            vt.removeChild(ir, nullptr);
        }
        {
            const juce::ScopedLock sl(customIRLock);
            customIRBuffer = std::move(restoredIR);
            customIRSampleRate = restoredRate;
            customIRName = restoredName;
        }
        applyCustomIRToEngine(false);

        apvts.replaceState(vt);

        // Snap discrete parameters (bools, choices) to their exact restored value. A host may have
        // set an unquantised normalised value (e.g. 0.18 on a bool, which reads as "off");
        // replaceState() compares quantised values, sees "off" == "off" and skips the update, so the
        // raw 0.18 would survive the state load and be reported back to the host (pluginval
        // "Plugin state restoration"). Snapping makes a loaded state hold exactly what was saved.
        for (auto* p : apvts.processor.getParameters())
        {
            auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p);
            if (ranged == nullptr || ! (ranged->isDiscrete() || ranged->isBoolean()))
                continue;
            const float raw = ranged->getValue();
            const float snapped = ranged->convertTo0to1(ranged->convertFrom0to1(raw));
            if (! juce::exactlyEqual(raw, snapped))
                ranged->setValueNotifyingHost(snapped);
        }
    }
}

// ---------------------------------------------------------------------------
// Plugin factory function (required by JUCE)
// ---------------------------------------------------------------------------

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HybridSynthProcessor();
}
