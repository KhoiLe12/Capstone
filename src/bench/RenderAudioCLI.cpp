#include "dsp/SynthEngine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>

// ===========================================================================
// render_audio — Headless C++ CLI Renderer for Benchmarking & STFT Analysis
//
// Usage:
//   render_audio --engine [0|1] --chord [notes] --vel [0..1] --bodyType [0|1|2]
//                --bodyMix [0..1] --sr [sampleRate] --dur [sec] --out [output.wav]
//
// Example:
//   render_audio --engine 1 --chord 40,47,52,55,59,64 --vel 0.85 --dur 4.0 --out syn_E.wav
// ===========================================================================

int main(int argc, char* argv[])
{
    float engineType = 1.0f; // 0 = Waveguide, 1 = Bilbao FDTD
    std::vector<int> midiNotes = { 40, 47, 52, 55, 59, 64 }; // E major chord default
    float velocity = 0.85f;
    float bodyType = 0.0f;   // 0 = Classical Nylon IR, 1 = Gibson Acoustic IR
    float stringMaterial = 0.0f; // 0 = Nylon Classical, 1 = Steel Acoustic
    float strumSpreadMs = 0.0f;  // Delay per string in ms (e.g. 8-12ms for acoustic strum)
    int strumMode = 1;           // 0 = Flat linear MIDI spacing, 1 = Physical acoustic strum
    bool isDownstroke = true;    // true = downstroke (bass to treble), false = upstroke
    float bodyMix = 0.70f;
    float sampleRate = 44100.0f;
    float durationSec = 4.0f;
    bool palmMute = false;
    bool fullMute = false;
    float noteSpacingSec = 0.0f; // Sequential note timing (for melodies / legato tests)
    bool forceRepluck = false;   // Disable legato detection to render picked re-strum comparison
    std::string outputPath = "synth_output.wav";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--engine" && i + 1 < argc)
        {
            engineType = std::stof(argv[++i]);
        }
        else if (arg == "--forceRepluck" || arg == "--noLegato")
        {
            forceRepluck = true;
        }
        else if (arg == "--seq" && i + 1 < argc)
        {
            noteSpacingSec = std::stof(argv[++i]);
        }
        else if (arg == "--palmMute" || arg == "--palm")
        {
            palmMute = true;
        }
        else if (arg == "--fullMute" || arg == "--dead")
        {
            fullMute = true;
        }
        else if (arg == "--chord" && i + 1 < argc)
        {
            midiNotes.clear();
            std::stringstream ss(argv[++i]);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                if (!item.empty())
                    midiNotes.push_back(std::stoi(item));
            }
        }
        else if (arg == "--vel" && i + 1 < argc)
        {
            velocity = std::stof(argv[++i]);
        }
        else if ((arg == "--material" || arg == "--stringMaterial") && i + 1 < argc)
        {
            stringMaterial = std::stof(argv[++i]);
        }
        else if (arg == "--strumMode" && i + 1 < argc)
        {
            strumMode = std::stoi(argv[++i]);
        }
        else if (arg == "--stroke" && i + 1 < argc)
        {
            std::string s = argv[++i];
            isDownstroke = (s == "down" || s == "0" || s == "downstroke");
        }
        else if ((arg == "--strum" || arg == "--strumSpreadMs") && i + 1 < argc)
        {
            strumSpreadMs = std::stof(argv[++i]);
        }
        else if (arg == "--bodyType" && i + 1 < argc)
        {
            bodyType = std::stof(argv[++i]);
        }
        else if (arg == "--bodyMix" && i + 1 < argc)
        {
            bodyMix = std::stof(argv[++i]);
        }
        else if (arg == "--sr" && i + 1 < argc)
        {
            sampleRate = std::stof(argv[++i]);
        }
        else if (arg == "--dur" && i + 1 < argc)
        {
            durationSec = std::stof(argv[++i]);
        }
        else if (arg == "--out" && i + 1 < argc)
        {
            outputPath = argv[++i];
        }
    }

    const int totalSamples = static_cast<int>(sampleRate * durationSec);
    constexpr int blockSize = 128;

    SynthEngine engine;
    engine.init(sampleRate, blockSize);
    engine.paramEngineType = engineType;
    engine.paramStringMaterial = stringMaterial;
    engine.paramBodyType = bodyType;
    engine.paramBodyMix = bodyMix;
    engine.paramPalmMute = palmMute;
    engine.paramFullMute = fullMute;
    engine.paramDecay = 0.75f;
    engine.paramBrightness = 0.65f;
    engine.paramMasterGain = 0.85f;
    engine.paramLegatoEnabled = !forceRepluck;

    struct ScheduledNote {
        int sampleOffset;
        int note;
        float vel;
        float brightness;
        float pickPos;
        bool triggered;
    };
    std::vector<ScheduledNote> scheduledNotes;
    scheduledNotes.reserve(midiNotes.size());

    const int numNotes = static_cast<int>(midiNotes.size());
    std::vector<int> sortedNotes = midiNotes;
    if (noteSpacingSec <= 0.0f)
    {
        if (isDownstroke)
            std::sort(sortedNotes.begin(), sortedNotes.end());
        else
            std::sort(sortedNotes.begin(), sortedNotes.end(), std::greater<int>());
    }

    const float baseBrt = 0.65f;
    const float basePos = 0.23f;

    for (int n = 0; n < numNotes; ++n)
    {
        int offset = 0;
        float vel = velocity;
        float brt = baseBrt;
        float pos = basePos;

        if (noteSpacingSec > 0.0f)
        {
            // Sequential melodic notes (e.g., Hammer-on / Pull-off phrase)
            offset = static_cast<int>(static_cast<float>(n) * noteSpacingSec * sampleRate);
        }
        else if (strumSpreadMs > 0.0f && numNotes > 1)
        {
            if (strumMode == 0)
            {
                // Mode 0: Flat linear MIDI spacing (what user did manually in piano roll)
                offset = static_cast<int>(static_cast<float>(n) * (strumSpreadMs / 1000.0f) * sampleRate);
                vel = velocity;
                brt = baseBrt;
                pos = basePos;
            }
            else
            {
                // Mode 1: Physical Acoustic Strum (organic wrist arc + plectrum flex + attack angle)
                const float totalStrumMs = static_cast<float>(numNotes - 1) * strumSpreadMs;
                const float u = static_cast<float>(n) / static_cast<float>(numNotes - 1); // 0.0 to 1.0

                if (isDownstroke)
                {
                    // Wrist inertia curve: initial contact drag on bass strings, whipping acceleration through treble strings
                    const float timingWarp = 0.60f * u + 0.40f * (u * u);
                    const float tMs = totalStrumMs * timingWarp;
                    offset = static_cast<int>((tMs / 1000.0f) * sampleRate);

                    // Plectrum deceleration taper: pick transfers kinetic momentum to wound strings
                    vel = std::clamp(velocity * (1.0f - 0.035f * static_cast<float>(n)), 0.05f, 1.0f);

                    // Plectrum flex & glancing angle:
                    // Bass string gets square hit; middle strings flex at 45-60 deg (softened); final string snaps off edge
                    if (n == 0)
                        brt = baseBrt;
                    else if (n < numNotes - 1)
                        brt = std::clamp(baseBrt * (0.86f + 0.05f * u), 0.05f, 1.0f);
                    else
                        brt = std::clamp(baseBrt * 1.15f, 0.05f, 1.0f);

                    // Hand pivoting arc: closer to soundhole on bass (0.24), angled closer to bridge on treble (0.17)
                    pos = std::clamp(basePos + 0.01f - 0.07f * u, 0.08f, 0.45f);
                }
                else
                {
                    // Upstroke: lighter grazing rake from treble to bass
                    const float timingWarp = 0.50f * u + 0.50f * std::sqrt(u);
                    const float tMs = totalStrumMs * 0.82f * timingWarp;
                    offset = static_cast<int>((tMs / 1000.0f) * sampleRate);

                    vel = std::clamp(velocity * 0.82f * (1.0f - 0.03f * static_cast<float>(n)), 0.05f, 1.0f);
                    brt = std::clamp(baseBrt * 1.05f, 0.05f, 1.0f);
                    pos = std::clamp(basePos - 0.05f + 0.06f * u, 0.08f, 0.45f);
                }
            }
        }

        scheduledNotes.push_back({ offset, sortedNotes[n], vel, brt, pos, false });
    }

    std::vector<float> audioL(totalSamples, 0.0f);
    std::vector<float> audioR(totalSamples, 0.0f);

    std::vector<float> blockL(blockSize, 0.0f);
    std::vector<float> blockR(blockSize, 0.0f);

    int samplesRendered = 0;
    while (samplesRendered < totalSamples)
    {
        // Trigger any notes scheduled up to the current sample
        for (auto& sn : scheduledNotes)
        {
            if (!sn.triggered && samplesRendered >= sn.sampleOffset)
            {
                engine.noteOn(sn.note, sn.vel, sn.brightness, sn.pickPos);
                sn.triggered = true;
            }
        }

        const int numToProcess = std::min(blockSize, totalSamples - samplesRendered);
        engine.process(blockL.data(), blockR.data(), numToProcess);

        for (int s = 0; s < numToProcess; ++s)
        {
            audioL[samplesRendered + s] = blockL[s];
            audioR[samplesRendered + s] = blockR[s];
        }
        samplesRendered += numToProcess;
    }

    // Write output WAV using JUCE
    juce::File outputFile(outputPath);
    outputFile.deleteFile();

    std::unique_ptr<juce::FileOutputStream> fileStream(outputFile.createOutputStream());
    if (!fileStream)
    {
        std::cerr << "Error: Could not open output file for writing: " << outputPath << std::endl;
        return 1;
    }

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        wavFormat.createWriterFor(fileStream.get(), sampleRate, 2, 16, {}, 0));

    if (!writer)
    {
        std::cerr << "Error: Could not create WAV writer." << std::endl;
        return 1;
    }

    fileStream.release(); // Writer took ownership

    const float* channelPointers[2] = { audioL.data(), audioR.data() };
    writer->writeFromFloatArrays(channelPointers, 2, totalSamples);

    std::cout << "Successfully rendered " << durationSec << "s audio ("
              << (engineType >= 0.5f ? "Bilbao FDTD" : "Waveguide")
              << ", " << midiNotes.size() << " notes) to: " << outputPath << std::endl;

    return 0;
}

