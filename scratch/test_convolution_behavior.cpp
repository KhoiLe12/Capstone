#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include "../src/dsp/EmbeddedIRs.h"

int main()
{
    juce::MessageManager::getInstance();

    std::cout << "Testing Scenario A: prepare() THEN loadImpulseResponse(data)..." << std::endl;
    {
        juce::dsp::Convolution conv { juce::dsp::Convolution::Latency { 0 } };
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        conv.prepare(spec);
        conv.loadImpulseResponse(kClassicalNylonWav_data, kClassicalNylonWav_size,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no, 0,
                                 juce::dsp::Convolution::Normalise::no);

        // Immediate offline processing (simulating FL Studio offline export)
        std::vector<float> inL(512, 1.0f), inR(512, 1.0f);
        float* ch[2] = { inL.data(), inR.data() };
        juce::dsp::AudioBlock<float> block(ch, 2, 512);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        float maxOut = 0.0f;
        for (float s : inL) maxOut = std::max(maxOut, std::abs(s));
        std::cout << "Immediate process maxOut: " << maxOut << " (Current IR size: " << conv.getCurrentIRSize() << ")" << std::endl;
    }

    std::cout << "\nTesting Scenario B: loadImpulseResponse(data) THEN prepare()..." << std::endl;
    {
        juce::dsp::Convolution conv { juce::dsp::Convolution::Latency { 0 } };
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        conv.loadImpulseResponse(kClassicalNylonWav_data, kClassicalNylonWav_size,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no, 0,
                                 juce::dsp::Convolution::Normalise::no);
        conv.prepare(spec);

        std::vector<float> inL(512, 1.0f), inR(512, 1.0f);
        float* ch[2] = { inL.data(), inR.data() };
        juce::dsp::AudioBlock<float> block(ch, 2, 512);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        float maxOut = 0.0f;
        for (float s : inL) maxOut = std::max(maxOut, std::abs(s));
        std::cout << "Immediate process maxOut: " << maxOut << " (Current IR size: " << conv.getCurrentIRSize() << ")" << std::endl;
    }

    std::cout << "\nTesting Scenario C: FL Studio variable block size (e.g. prepared for 512, processed with 1024)..." << std::endl;
    {
        juce::dsp::Convolution conv { juce::dsp::Convolution::Latency { 0 } };
        juce::dsp::ProcessSpec spec { 48000.0, 512, 2 };
        conv.loadImpulseResponse(kClassicalNylonWav_data, kClassicalNylonWav_size,
                                 juce::dsp::Convolution::Stereo::yes,
                                 juce::dsp::Convolution::Trim::no, 0,
                                 juce::dsp::Convolution::Normalise::no);
        conv.prepare(spec);

        std::vector<float> inL(1024, 1.0f), inR(1024, 1.0f);
        float* ch[2] = { inL.data(), inR.data() };
        juce::dsp::AudioBlock<float> block(ch, 2, 1024);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        conv.process(ctx);

        float maxOut = 0.0f;
        for (float s : inL) maxOut = std::max(maxOut, std::abs(s));
        std::cout << "1024 process maxOut: " << maxOut << " (Current IR size: " << conv.getCurrentIRSize() << ")" << std::endl;
    }

    return 0;
}
