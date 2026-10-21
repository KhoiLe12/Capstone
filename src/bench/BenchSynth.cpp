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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

// ===========================================================================
// Real-Time Safety Verification (RTCheck)
// ===========================================================================
// Intercepts global memory allocations on simulated audio render threads.
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
// Lightweight Zero-Allocation Workstation Thread Pool
// Simulates a DAW audio engine thread pool (e.g. Reaper / Cubase worker threads)
// ===========================================================================

class BenchThreadPool
{
public:
    struct ITask
    {
        virtual void run(size_t threadIdx, size_t totalThreads) = 0;
        virtual ~ITask() = default;
    };

    explicit BenchThreadPool(size_t numThreads)
        : activeWorkers(numThreads), stopFlag(false)
    {
        workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i)
        {
            workers.emplace_back([this, i, numThreads]() {
                uint64_t myGen = 0;
                while (true)
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    cvStart.wait(lock, [this, myGen]() {
                        return stopFlag || (workGen > myGen);
                    });
                    if (stopFlag) return;

                    myGen = workGen;
                    ITask* task = currentTask;
                    lock.unlock();

                    g_realtimeThreadActive = true;
                    if (task) task->run(i, numThreads);
                    g_realtimeThreadActive = false;

                    std::unique_lock<std::mutex> lockDone(mutex);
                    if (++finishedCount == activeWorkers)
                    {
                        cvDone.notify_one();
                    }
                }
            });
        }
    }

    ~BenchThreadPool()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopFlag = true;
        }
        cvStart.notify_all();
        for (auto& w : workers)
        {
            if (w.joinable()) w.join();
        }
    }

    void runTask(ITask* task)
    {
        std::unique_lock<std::mutex> lock(mutex);
        currentTask = task;
        finishedCount = 0;
        workGen++;
        cvStart.notify_all();

        cvDone.wait(lock, [this]() { return finishedCount == activeWorkers; });
    }

    size_t getNumThreads() const { return activeWorkers; }

private:
    std::vector<std::thread> workers;
    std::mutex mutex;
    std::condition_variable cvStart;
    std::condition_variable cvDone;
    ITask* currentTask = nullptr;
    size_t activeWorkers = 0;
    size_t finishedCount = 0;
    uint64_t workGen = 0;
    bool stopFlag = false;
};

// ===========================================================================
// Part 1: Single-Instance Polyphony Benchmark
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

    for (int note : midiNotes)
    {
        engine.noteOn(note, 0.85f);
    }

    // 1. Warm-up loop
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

    const size_t allocs = g_realtimeAllocCount.load();
    const size_t frees  = g_realtimeFreeCount.load();

    std::sort(blockTimesUs.begin(), blockTimesUs.end());
    const double sumUs = std::accumulate(blockTimesUs.begin(), blockTimesUs.end(), 0.0);
    const double meanUs = sumUs / static_cast<double>(measureBlocks);
    const double peakUs = blockTimesUs.back();
    const size_t p99Idx = static_cast<size_t>(measureBlocks * 0.99);
    const double p99Us = blockTimesUs[std::min(p99Idx, blockTimesUs.size() - 1)];

    const double deadlineUs = (static_cast<double>(blockSize) / sampleRate) * 1e6;
    const double cpuLoadPct = (meanUs / deadlineUs) * 100.0;
    const double peakLoadPct = (peakUs / deadlineUs) * 100.0;

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
              << " HYBRID SYNTH SINGLE-INSTANCE LATENCY & RTCHECK (Sample Rate: " << static_cast<int>(sampleRate) << " Hz)\n"
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

// ===========================================================================
// Part 2: DAW Multi-Track Capacity & DAWBench-Style Stress Benchmark
// ===========================================================================

struct MultiTrackResult
{
    std::string engineName;
    int blockSize = 0;
    int numTracks = 0;
    int totalStrings = 0;
    double deadlineUs = 0.0;
    double meanUs = 0.0;
    double p99Us = 0.0;
    double cpuLoadPct = 0.0;
    bool isRealtime = true;
    double estMaxTracks = 0.0;
    bool rtSafe = true;
};

static MultiTrackResult runMultiTrackScenario(
    const std::string& engineName,
    float engineTypeParam,
    int numTracks,
    int blockSize,
    float sampleRate,
    BenchThreadPool* threadPool,
    int warmupBlocks = 32,
    int measureBlocks = 150)
{
    std::vector<std::unique_ptr<SynthEngine>> engines;
    engines.reserve(numTracks);
    std::vector<std::vector<float>> bufsL(numTracks, std::vector<float>(blockSize, 0.0f));
    std::vector<std::vector<float>> bufsR(numTracks, std::vector<float>(blockSize, 0.0f));
    std::vector<float> masterL(blockSize, 0.0f);
    std::vector<float> masterR(blockSize, 0.0f);

    const std::vector<int> chord6Voice = { 40, 45, 50, 55, 59, 64 };

    for (int t = 0; t < numTracks; ++t)
    {
        auto eng = std::make_unique<SynthEngine>();
        eng->init(sampleRate, blockSize);
        eng->paramEngineType = engineTypeParam;
        eng->paramDecay = 0.70f;
        eng->paramBrightness = 0.60f;
        eng->paramPickPos = 0.23f;
        eng->paramBodyMix = 0.70f;
        eng->paramMasterGain = 0.80f;
        eng->paramBodyType = 0.0f; // Classical Nylon IR

        for (int note : chord6Voice)
        {
            eng->noteOn(note, 0.85f);
        }
        engines.push_back(std::move(eng));
    }

    // Warm-up
    for (int b = 0; b < warmupBlocks; ++b)
    {
        for (int t = 0; t < numTracks; ++t)
        {
            engines[t]->process(bufsL[t].data(), bufsR[t].data(), blockSize);
        }
    }

    struct TrackTask : public BenchThreadPool::ITask
    {
        std::vector<std::unique_ptr<SynthEngine>>* engs = nullptr;
        std::vector<std::vector<float>>* bL = nullptr;
        std::vector<std::vector<float>>* bR = nullptr;
        int bs = 0;
        size_t nTracks = 0;

        void run(size_t threadIdx, size_t totalThreads) override
        {
            const size_t tracksPerThread = (nTracks + totalThreads - 1) / totalThreads;
            const size_t startTrack = threadIdx * tracksPerThread;
            const size_t endTrack = std::min(nTracks, startTrack + tracksPerThread);

            for (size_t t = startTrack; t < endTrack; ++t)
            {
                (*engs)[t]->process((*bL)[t].data(), (*bR)[t].data(), bs);
            }
        }
    } task;

    task.engs = &engines;
    task.bL = &bufsL;
    task.bR = &bufsR;
    task.bs = blockSize;
    task.nTracks = numTracks;

    // Active measurement
    g_realtimeAllocCount.store(0);
    g_realtimeAllocBytes.store(0);
    g_realtimeFreeCount.store(0);

    std::vector<double> blockTimesUs;
    blockTimesUs.reserve(measureBlocks);

    for (int b = 0; b < measureBlocks; ++b)
    {
        auto start = std::chrono::high_resolution_clock::now();

        if (threadPool != nullptr && threadPool->getNumThreads() > 1 && numTracks > 1)
        {
            // Worker threads have g_realtimeThreadActive = true inside task->run()
            threadPool->runTask(&task);
        }
        else
        {
            g_realtimeThreadActive = true;
            for (int t = 0; t < numTracks; ++t)
            {
                engines[t]->process(bufsL[t].data(), bufsR[t].data(), blockSize);
            }
            g_realtimeThreadActive = false;
        }

        // Master mix bus summation
        std::fill(masterL.begin(), masterL.end(), 0.0f);
        std::fill(masterR.begin(), masterR.end(), 0.0f);
        for (int t = 0; t < numTracks; ++t)
        {
            for (int s = 0; s < blockSize; ++s)
            {
                masterL[s] += bufsL[t][s];
                masterR[s] += bufsR[t][s];
            }
        }

        auto finish = std::chrono::high_resolution_clock::now();
        double elapsedUs = std::chrono::duration<double, std::micro>(finish - start).count();
        blockTimesUs.push_back(elapsedUs);
    }

    const size_t allocs = g_realtimeAllocCount.load();
    const size_t frees  = g_realtimeFreeCount.load();

    std::sort(blockTimesUs.begin(), blockTimesUs.end());
    const double sumUs = std::accumulate(blockTimesUs.begin(), blockTimesUs.end(), 0.0);
    const double meanUs = sumUs / static_cast<double>(measureBlocks);
    const size_t p99Idx = static_cast<size_t>(measureBlocks * 0.99);
    const double p99Us = blockTimesUs[std::min(p99Idx, blockTimesUs.size() - 1)];

    const double deadlineUs = (static_cast<double>(blockSize) / sampleRate) * 1e6;
    const double cpuLoadPct = (meanUs / deadlineUs) * 100.0;
    const double estMaxTracks = (meanUs > 0.0) ? (static_cast<double>(numTracks) * (deadlineUs / meanUs)) : 0.0;

    MultiTrackResult res;
    res.engineName = engineName;
    res.blockSize = blockSize;
    res.numTracks = numTracks;
    res.totalStrings = numTracks * 6;
    res.deadlineUs = deadlineUs;
    res.meanUs = meanUs;
    res.p99Us = p99Us;
    res.cpuLoadPct = cpuLoadPct;
    res.isRealtime = (cpuLoadPct <= 100.0);
    res.estMaxTracks = estMaxTracks;
    res.rtSafe = (allocs == 0 && frees == 0);

    return res;
}

static void printMultiTrackHeader(float sampleRate, size_t numThreads, const std::string& modeName)
{
    std::cout << "\n"
              << "=========================================================================================================================\n"
              << " DAW MULTI-TRACK STRESS & CAPACITY BENCHMARK (" << static_cast<int>(sampleRate) << " Hz, 6 Strings / Track)\n"
              << " Engine Mode: " << modeName << " (" << numThreads << " Threads) | Simulated DAW Track Pool\n"
              << "=========================================================================================================================\n"
              << std::left
              << std::setw(16) << "Engine"
              << std::setw(8)  << "Block"
              << std::setw(10) << "Tracks"
              << std::setw(11) << "Strings"
              << std::setw(12) << "Deadline"
              << std::setw(13) << "Render Time"
              << std::setw(12) << "P99 Time"
              << std::setw(13) << "CPU Load %"
              << std::setw(12) << "Status"
              << std::setw(14) << "Est Max Tracks"
              << "\n"
              << "-------------------------------------------------------------------------------------------------------------------------\n"
              << std::flush;
}

static void printMultiTrackRow(const MultiTrackResult& r)
{
    std::string statusStr = r.isRealtime ? "REALTIME" : "OVERRUN";
    std::string maxTrkStr = std::to_string(static_cast<int>(r.estMaxTracks)) + " tracks";

    std::cout << std::left
              << std::setw(16) << r.engineName
              << std::setw(8)  << r.blockSize
              << std::setw(10) << (std::to_string(r.numTracks) + " trk")
              << std::setw(11) << (std::to_string(r.totalStrings) + " str")
              << std::setw(12) << (std::to_string(static_cast<int>(r.deadlineUs)) + " us")
              << std::setw(13) << (std::to_string(static_cast<int>(r.meanUs)) + " us")
              << std::setw(12) << (std::to_string(static_cast<int>(r.p99Us)) + " us")
              << std::setw(13) << (std::to_string(r.cpuLoadPct).substr(0, 5) + " %")
              << std::setw(12) << statusStr
              << std::setw(14) << maxTrkStr
              << "\n"
              << std::flush;
}

// ===========================================================================
// Main Entry Point
// ===========================================================================

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "Starting Hybrid Synth DSP Benchmarks & Multi-Track Stress Suite...\n";

    const float sampleRates[] = { 48000.0f };
    const int blockSizes[]   = { 32, 64, 128, 256, 512 };

    const std::vector<int> chord1Voice = { 52 };
    const std::vector<int> chord4Voice = { 40, 47, 52, 55 };
    const std::vector<int> chord6Voice = { 40, 45, 50, 55, 59, 64 };

    bool allRtPassed = true;
    double maxFdtdLoad = 0.0;
    double maxWaveguideLoad = 0.0;

    // -----------------------------------------------------------------------
    // PART 1: Single-Instance Voice Scaling & RTCheck
    // -----------------------------------------------------------------------
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

    // -----------------------------------------------------------------------
    // PART 2: Workstation Multi-Track Capacity & DAWBench-Style Stress
    // -----------------------------------------------------------------------
    const unsigned int hwThreads = std::max(1u, std::thread::hardware_concurrency());
    BenchThreadPool pool(hwThreads);

    const int multiTrackCounts[] = { 1, 4, 8, 16, 24, 32, 48, 64 };
    const int targetBlockSize = 128; // Standard professional tracking buffer (2.66 ms)

    printMultiTrackHeader(48000.0f, pool.getNumThreads(), "Multi-Threaded Workstation");

    // A. Waveguide Multi-Track Capacity
    for (int numTrk : multiTrackCounts)
    {
        auto res = runMultiTrackScenario("Waveguide (KS)", 0.0f, numTrk, targetBlockSize, 48000.0f, &pool);
        printMultiTrackRow(res);
        if (!res.rtSafe) allRtPassed = false;
    }
    std::cout << "-------------------------------------------------------------------------------------------------------------------------\n";

    // B. Bilbao FDTD Multi-Track Capacity
    for (int numTrk : multiTrackCounts)
    {
        auto res = runMultiTrackScenario("Bilbao FDTD", 1.0f, numTrk, targetBlockSize, 48000.0f, &pool);
        printMultiTrackRow(res);
        if (!res.rtSafe) allRtPassed = false;
    }
    std::cout << "-------------------------------------------------------------------------------------------------------------------------\n";

    // -----------------------------------------------------------------------
    // PART 3: Single-Core Serial Capacity (Per-Core Limit)
    // -----------------------------------------------------------------------
    printMultiTrackHeader(48000.0f, 1, "Single-Core Serial (Worst-Case Bus Chain)");

    for (int numTrk : { 1, 2, 4, 6, 8 })
    {
        auto res = runMultiTrackScenario("Bilbao FDTD", 1.0f, numTrk, targetBlockSize, 48000.0f, nullptr);
        printMultiTrackRow(res);
        if (!res.rtSafe) allRtPassed = false;
    }
    std::cout << "-------------------------------------------------------------------------------------------------------------------------\n";

    std::cout << "\n=========================================================================================================\n"
              << " SUMMARY & REAL-TIME VERDICT:\n"
              << "=========================================================================================================\n";

    if (allRtPassed)
    {
        std::cout << " [RTCheck Result]  : ALL TESTS PASSED. Zero heap allocations detected on simulated audio threads!\n";
    }
    else
    {
        std::cout << " [RTCheck Result]  : FAILED! Detected illegal memory allocations or deallocations during processBlock!\n";
    }

    std::cout << " [Max CPU Load KS] : " << maxWaveguideLoad << " % of audio deadline (single voice/chord sweep)\n"
              << " [Max CPU Load FDTD]: " << maxFdtdLoad << " % of audio deadline (single voice/chord sweep)\n"
              << "=========================================================================================================\n\n";

    return allRtPassed ? 0 : 1;
}
