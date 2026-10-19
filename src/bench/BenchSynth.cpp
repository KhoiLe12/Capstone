#include "dsp/SynthEngine.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#include <string>

// ===========================================================================
// Real-Time Safety Verification (RTCheck)
// ===========================================================================
// Intercepts global memory allocations on the simulated audio render thread.
// Real-time audio threads MUST NEVER allocate or free heap memory.
// ===========================================================================

static thread_local bool g_realtimeThreadActive = false;
static std::atomic<size_t> g_realtimeAllocCount{0};
static std::atomic<size_t> g_realtimeAllocBytes{0};
static std::atomic<size_t> g_realtimeFreeCount{0};

void* operator new(std::size_t size)
{
    if (g_realtimeThreadActive)
    {
        g_realtimeAllocCount.fetch_add(1, std::memory_order_relaxed);
        g_realtimeAllocBytes.fetch_add(size, std::memory_order_relaxed);
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept
{
    if (g_realtimeThreadActive && p)
    {
        g_realtimeFreeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

void* operator new[](std::size_t size)
{
    if (g_realtimeThreadActive)
    {
        g_realtimeAllocCount.fetch_add(1, std::memory_order_relaxed);
        g_realtimeAllocBytes.fetch_add(size, std::memory_order_relaxed);
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete[](void* p) noexcept
{
    if (g_realtimeThreadActive && p)
    {
        g_realtimeFreeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

#if defined(__cpp_sized_deallocation) && __cpp_sized_deallocation
void operator delete(void* p, std::size_t) noexcept
{
    if (g_realtimeThreadActive && p)
    {
        g_realtimeFreeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept
{
    if (g_realtimeThreadActive && p)
    {
        g_realtimeFreeCount.fetch_add(1, std::memory_order_relaxed);
    }
    std::free(p);
}
#endif

// ===========================================================================
// Benchmark Runner
// ===========================================================================

struct BenchmarkResult
{
    std::string engineName;
    int numVoices = 0;
    int blockSize = 0;
    float sampleRate = 0.0f;
    double deadlineUs = 0.0;
    double meanUs = 0.0;
    double p99Us = 0.0;
    double peakUs = 0.0;
    double cpuLoadPct = 0.0;
    double peakLoadPct = 0.0;
    double throughputMSamplesPerSec = 0.0;
    size_t rtAllocations = 0;
    size_t rtFrees = 0;
    bool rtSafe = true;
};

static BenchmarkResult runScenario(
    const std::string& engineName,
    float engineTypeParam,
    const std::vector<int>& midiNotes,
    float sampleRate,
    int blockSize,
    int warmupBlocks = 64,
    int measureBlocks = 300)
{
    SynthEngine engine;
    engine.init(sampleRate, blockSize);
    engine.paramEngineType  = engineTypeParam;
    engine.paramDecay       = 0.70f;
    engine.paramBrightness  = 0.60f;
    engine.paramPickPos     = 0.23f;
    engine.paramBodyMix     = 0.70f;
    engine.paramMasterGain  = 0.80f;
    engine.paramBodyType    = 0.0f; // Classical Nylon IR

    std::vector<float> bufferL(blockSize, 0.0f);
    std::vector<float> bufferR(blockSize, 0.0f);

    // Trigger chords
    for (int note : midiNotes)
    {
        engine.noteOn(note, 0.85f);
    }

    // 1. Warm-up loop (Caches warm, branch predictors primed, steady state reached)
    for (int b = 0; b < warmupBlocks; ++b)
    {
        engine.process(bufferL.data(), bufferR.data(), blockSize);
    }

    // 2. Real-Time Safety & Timing Measurement
    g_realtimeAllocCount.store(0);
    g_realtimeAllocBytes.store(0);
    g_realtimeFreeCount.store(0);

    std::vector<double> blockTimesUs;
    blockTimesUs.reserve(measureBlocks);

    g_realtimeThreadActive = true;
    for (int b = 0; b < measureBlocks; ++b)
    {
        auto start = std::chrono::high_resolution_clock::now();
        engine.process(bufferL.data(), bufferR.data(), blockSize);
        auto finish = std::chrono::high_resolution_clock::now();

        double elapsedUs = std::chrono::duration<double, std::micro>(finish - start).count();
        blockTimesUs.push_back(elapsedUs);
    }
    g_realtimeThreadActive = false;

    // Collect RTCheck violations
    const size_t allocs = g_realtimeAllocCount.load();
    const size_t frees  = g_realtimeFreeCount.load();

    // Compute statistics
    std::sort(blockTimesUs.begin(), blockTimesUs.end());
    const double sumUs = std::accumulate(blockTimesUs.begin(), blockTimesUs.end(), 0.0);
    const double meanUs = sumUs / static_cast<double>(measureBlocks);
    const double peakUs = blockTimesUs.back();
    const size_t p99Idx = static_cast<size_t>(measureBlocks * 0.99);
    const double p99Us = blockTimesUs[std::min(p99Idx, blockTimesUs.size() - 1)];

    const double deadlineUs = (static_cast<double>(blockSize) / sampleRate) * 1e6;
    const double cpuLoadPct = (meanUs / deadlineUs) * 100.0;
    const double peakLoadPct = (peakUs / deadlineUs) * 100.0;

    // Total samples processed per second during rendering
    const double totalSeconds = (sumUs * 1e-6);
    const double totalSamples = static_cast<double>(measureBlocks * blockSize);
    const double throughputMsa = (totalSamples / totalSeconds) * 1e-6;

    BenchmarkResult res;
    res.engineName = engineName;
    res.numVoices = static_cast<int>(midiNotes.size());
    res.blockSize = blockSize;
    res.sampleRate = sampleRate;
    res.deadlineUs = deadlineUs;
    res.meanUs = meanUs;
    res.p99Us = p99Us;
    res.peakUs = peakUs;
    res.cpuLoadPct = cpuLoadPct;
    res.peakLoadPct = peakLoadPct;
    res.throughputMSamplesPerSec = throughputMsa;
    res.rtAllocations = allocs;
    res.rtFrees = frees;
    res.rtSafe = (allocs == 0 && frees == 0);

    return res;
}

static void printHeader(float sampleRate)
{
    std::cout << "\n"
              << "=========================================================================================================\n"
              << " HYBRID SYNTH REAL-TIME BENCHMARK & RTCHECK SUITE (Sample Rate: " << static_cast<int>(sampleRate) << " Hz)\n"
              << "=========================================================================================================\n"
              << std::left
              << std::setw(16) << "Engine"
              << std::setw(9)  << "Voices"
              << std::setw(8)  << "Block"
              << std::setw(12) << "Deadline"
              << std::setw(12) << "Mean Time"
              << std::setw(12) << "P99 Time"
              << std::setw(13) << "CPU Load %"
              << std::setw(13) << "Peak Load %"
              << std::setw(12) << "RTCheck"
              << "\n"
              << "---------------------------------------------------------------------------------------------------------\n";
}

static void printResultRow(const BenchmarkResult& r)
{
    std::string rtStatus = r.rtSafe ? "PASS (0)" : ("FAIL (" + std::to_string(r.rtAllocations) + ")");

    std::cout << std::left
              << std::setw(16) << r.engineName
              << std::setw(9)  << (std::to_string(r.numVoices) + " vce")
              << std::setw(8)  << r.blockSize
              << std::setw(12) << (std::to_string(static_cast<int>(r.deadlineUs)) + " us")
              << std::setw(12) << (std::to_string(static_cast<int>(r.meanUs)) + " us")
              << std::setw(12) << (std::to_string(static_cast<int>(r.p99Us)) + " us")
              << std::setw(13) << (std::to_string(r.cpuLoadPct).substr(0, 5) + " %")
              << std::setw(13) << (std::to_string(r.peakLoadPct).substr(0, 5) + " %")
              << std::setw(12) << rtStatus
              << "\n";
}

int main(int argc, char* argv[])
{
    std::cout << "Starting Hybrid Synth DSP Benchmarks & Real-Time Safety Tests...\n";

    const float sampleRates[] = { 48000.0f };
    const int blockSizes[]   = { 64, 128, 256, 512 };

    // Standard Guitar Chords:
    // 1 Voice: Solo note E3 (MIDI 52)
    // 4 Voices: E Minor 7 chord (MIDI 40 [E2], 47 [B2], 52 [E3], 55 [G3])
    // 6 Voices: Full 6-string Open Strum (MIDI 40 [E2], 45 [A2], 50 [D3], 55 [G3], 59 [B3], 64 [E4])
    const std::vector<int> chord1Voice = { 52 };
    const std::vector<int> chord4Voice = { 40, 47, 52, 55 };
    const std::vector<int> chord6Voice = { 40, 45, 50, 55, 59, 64 };

    bool allRtPassed = true;
    double maxFdtdLoad = 0.0;
    double maxWaveguideLoad = 0.0;

    for (float sr : sampleRates)
    {
        printHeader(sr);

        for (int bs : blockSizes)
        {
            // 1. Digital Waveguide (Karplus-Strong) Tests
            for (const auto& chord : { chord1Voice, chord4Voice, chord6Voice })
            {
                auto res = runScenario("Waveguide (KS)", 0.0f, chord, sr, bs);
                printResultRow(res);
                if (!res.rtSafe) allRtPassed = false;
                maxWaveguideLoad = std::max(maxWaveguideLoad, res.cpuLoadPct);
            }

            // 2. Bilbao FDTD Physical Model Tests
            for (const auto& chord : { chord1Voice, chord4Voice, chord6Voice })
            {
                auto res = runScenario("Bilbao FDTD", 1.0f, chord, sr, bs);
                printResultRow(res);
                if (!res.rtSafe) allRtPassed = false;
                maxFdtdLoad = std::max(maxFdtdLoad, res.cpuLoadPct);
            }
            std::cout << "---------------------------------------------------------------------------------------------------------\n";
        }
    }

    std::cout << "\n=========================================================================================================\n"
              << " SUMMARY & REAL-TIME VERDICT:\n"
              << "=========================================================================================================\n";

    if (allRtPassed)
    {
        std::cout << " [RTCheck Result]  : ALL TESTS PASSED. Zero heap allocations detected on the audio thread!\n";
    }
    else
    {
        std::cout << " [RTCheck Result]  : FAILED! Detected illegal memory allocations or deallocations during processBlock!\n";
    }

    std::cout << " [Max CPU Load KS] : " << maxWaveguideLoad << " % of audio deadline\n"
              << " [Max CPU Load FDTD]: " << maxFdtdLoad << " % of audio deadline\n"
              << "=========================================================================================================\n\n";

    return allRtPassed ? 0 : 1;
}

