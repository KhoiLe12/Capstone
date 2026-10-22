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
    float bodyMix = 0.70f;
    float sampleRate = 44100.0f;
    float durationSec = 4.0f;
    std::string outputPath = "synth_output.wav";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--engine" && i + 1 < argc)
        {
            engineType = std::stof(argv[++i]);
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
    engine.paramDecay = 0.75f;
    engine.paramBrightness = 0.65f;
    engine.paramMasterGain = 0.85f;

    struct ScheduledNote {
        int sampleOffset;
        int note;
        float vel;
        bool triggered;
    };
    std::vector<ScheduledNote> scheduledNotes;
    scheduledNotes.reserve(midiNotes.size());
    for (size_t n = 0; n < midiNotes.size(); ++n)
    {
        const int offset = (strumSpreadMs > 0.0f)
            ? static_cast<int>(static_cast<float>(n) * (strumSpreadMs / 1000.0f) * sampleRate)
            : 0;
        scheduledNotes.push_back({ offset, midiNotes[n], velocity, false });
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
                engine.noteOn(sn.note, sn.vel);
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

