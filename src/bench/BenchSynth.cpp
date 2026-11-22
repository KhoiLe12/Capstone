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

// ---------------------------------------------------------------------------
// Event Stress RTCheck: every MIDI/parameter event path runs INSIDE the
// guarded region (note-ons on every string incl. Drop-D, legato hammer-ons and
// pull-offs that grow the FDTD grid, keyswitches, strum, engine / material /
// body model switches). Returns the number of audio-thread heap operations.
// ---------------------------------------------------------------------------
static size_t runEventStressScenario(float sampleRate, int blockSize)
{
    SynthEngine engine;
    engine.init(sampleRate, blockSize);
    engine.paramEngineType = 1.0f;
    engine.paramBodyType   = 0.0f;

    // Custom IR (stereo, 0.5 s decaying noise at 44.1 kHz) loaded before the guarded region, as the
    // plugin does on the message thread; body type 4 then exercises the custom convolution engine.
    {
        juce::AudioBuffer<float> ir(2, 22050);
        uint32_t rng = 0x1234567u;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < ir.getNumSamples(); ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const float noise = static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f;
                ir.setSample(ch, i, noise * std::exp(-static_cast<float>(i) / 4000.0f));
            }
        engine.getBodyResonance().setCustomIR(ir, 44100.0, true);
    }

    std::vector<float> bufferL(static_cast<size_t>(blockSize), 0.0f);
    std::vector<float> bufferR(static_cast<size_t>(blockSize), 0.0f);

    // Blocks per ~10 ms so the legato onset-age guard (160 samples) is satisfied at any block size
    const int step = std::max(1, static_cast<int>(0.010f * sampleRate) / blockSize + 1);

    g_realtimeAllocCount.store(0);
    g_realtimeAllocBytes.store(0);
    g_realtimeFreeCount.store(0);

    g_realtimeThreadActive = true;
    int block = 0;
    auto run = [&](int numBlocks) {
        for (int b = 0; b < numBlocks; ++b, ++block)
            engine.process(bufferL.data(), bufferR.data(), blockSize);
    };

    for (int engineType = 1; engineType >= 0; --engineType)
    {
        engine.paramEngineType = static_cast<float>(engineType);
        for (int material = 0; material <= 1; ++material)
        {
            engine.paramStringMaterial = static_cast<float>(material);
            for (int bodyType = 0; bodyType <= 4; ++bodyType)
            {
                engine.paramBodyType = static_cast<float>(bodyType);

                // Lowest notes on every string (largest FDTD grids), incl. Drop-D D2
                for (int note : { 38, 45, 50, 55, 59, 64, 86 })
                {
                    engine.noteOn(note, 0.9f);
                    run(1);
                }
                run(step);
                for (int note : { 38, 45, 50, 55, 59, 64, 86 })
                    engine.noteOff(note);
                run(step * 4);

                // Legato: start fretted (small grid), pull off to open / Drop-D (grid grows), hammer back
                engine.noteOn(43, 0.8f);  run(step * 5);   // > 40 ms legato chord window
                engine.noteOn(40, 0.8f);  run(step * 5);   // pull-off G2 -> E2
                engine.noteOn(38, 0.8f);  run(step * 5);   // pull-off E2 -> D2
                engine.noteOn(45, 0.8f);  run(step * 5);   // hammer-on D2 -> A2
                engine.noteOff(45);       run(step * 4);

                // Keyswitches: palm mute, choke / dead notes, auto-strum
                engine.noteOn(SynthEngine::KEYSWITCH_PALM_MUTE, 0.8f);
                engine.noteOn(40, 0.8f);  run(step);
                engine.noteOff(SynthEngine::KEYSWITCH_PALM_MUTE); run(step);
                engine.noteOn(SynthEngine::KEYSWITCH_FULL_MUTE, 0.8f);
                engine.noteOn(52, 0.8f);  run(step);
                engine.noteOff(SynthEngine::KEYSWITCH_FULL_MUTE);
                engine.noteOff(40); engine.noteOff(52);
                for (int note : { 40, 47, 52, 55, 59, 64 }) engine.noteOn(note, 0.8f);
                engine.noteOn(SynthEngine::KEYSWITCH_STRUM_8TH, 0.8f); run(step * 8);
                engine.noteOff(SynthEngine::KEYSWITCH_STRUM_8TH);
                for (int note : { 40, 47, 52, 55, 59, 64 }) engine.noteOff(note);
                run(step * 2);

                // Same-tick chords through the event path (joint string assignment), strict and free
                for (bool strict : { false, true })
                {
                    engine.paramStrictFretboard = strict;
                    SynthEngine::Event chordEv[6];
                    const int chordNotes[6] = { 72, 76, 79, 83, 86, 64 };
                    for (int c = 0; c < 6; ++c)
                    {
                        chordEv[c].sampleOffset = blockSize / 2;
                        chordEv[c].type = SynthEngine::Event::Type::NoteOn;
                        chordEv[c].note = chordNotes[c];
                        chordEv[c].velocity = 0.8f;
                    }
                    engine.process(bufferL.data(), bufferR.data(), blockSize, chordEv, 6);
                    run(step * 2);
                    for (int note : chordNotes) engine.noteOff(note);
                    run(step);
                }
                engine.paramStrictFretboard = false;

                // Host "All Notes Off" path (PluginProcessor calls SynthEngine::reset on the audio thread)
                engine.noteOn(40, 0.8f); run(step);
                engine.reset();          run(step);
            }
        }
    }
    g_realtimeThreadActive = false;

    return g_realtimeAllocCount.load() + g_realtimeFreeCount.load();
}

// ---------------------------------------------------------------------------
// Sample-accurate MIDI timing check: a note-on at offset k inside a 512-sample block must
// produce the same audio as a note-on at offset 0 of a block that starts k samples later.
// Returns the worst-case normalised difference; also fails if anything sounds before k.
// ---------------------------------------------------------------------------
static bool runTimingAccuracyCheck(float sampleRate)
{
    constexpr int block = 512;
    constexpr int total = block * 6;
    bool ok = true;

    for (float engineType : { 0.0f, 1.0f })
    {
        for (int k : { 1, 37, 200, 511 })
        {
            auto render = [&](bool splitAtK) {
                SynthEngine eng;
                eng.init(sampleRate, block);
                eng.paramEngineType = engineType;
                std::vector<float> L(total, 0.0f), R(total, 0.0f);
                SynthEngine::Event on;
                on.type = SynthEngine::Event::Type::NoteOn;
                on.note = 52;
                on.velocity = 0.8f;

                int pos = 0;
                if (splitAtK)
                {
                    eng.process(L.data(), R.data(), k);          // silence up to k
                    pos = k;
                    on.sampleOffset = 0;
                }
                else
                {
                    on.sampleOffset = k;
                }
                eng.process(L.data() + pos, R.data() + pos, block, &on, 1);
                pos += block;
                while (pos < total)
                {
                    const int n = std::min(block, total - pos);
                    eng.process(L.data() + pos, R.data() + pos, n);
                    pos += n;
                }
                return L;
            };

            const auto a = render(false);
            const auto b = render(true);
            float peak = 0.0f, maxDiff = 0.0f, preOnset = 0.0f;
            for (int i = 0; i < total; ++i)
            {
                peak = std::max(peak, std::abs(b[static_cast<size_t>(i)]));
                maxDiff = std::max(maxDiff, std::abs(a[static_cast<size_t>(i)] - b[static_cast<size_t>(i)]));
                if (i < k) preOnset = std::max(preOnset, std::abs(a[static_cast<size_t>(i)]));
            }
            const float relDiff = maxDiff / std::max(peak, 1e-9f);
            // Pre-onset output must be silent to within FFT-convolution round-off (< -120 dB re. peak)
            const bool pass = (relDiff < 1e-4f) && (preOnset < 1e-6f * peak) && (peak > 0.0f);
            std::cout << "  " << (engineType > 0.5f ? "Bilbao FDTD   " : "Waveguide (KS)") << "  note-on at sample "
                      << std::setw(3) << k << " of 512 : rel. diff " << std::scientific << std::setprecision(2) << relDiff
                      << "  pre-onset " << std::scientific << preOnset / std::max(peak, 1e-9f) << std::defaultfloat << std::setprecision(6) << (pass ? "  PASS" : "  FAIL") << "\n";
            ok = ok && pass;
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Chord vs legato regression: a rolled chord (notes 8 ms apart, within 3 semitones) must keep
// every note; a hammer-on 100 ms after the held note must retune the held string (one voice).
// ---------------------------------------------------------------------------
static bool runChordVsLegatoCheck(float sampleRate)
{
    auto voicesAfter = [&](const std::vector<std::pair<float, int>>& notes, bool legatoOn, bool strict = false) {
        SynthEngine eng;
        eng.init(sampleRate, 512);
        eng.paramEngineType = 1.0f;
        eng.paramLegatoEnabled = legatoOn;
        eng.paramStrictFretboard = strict;
        std::vector<SynthEngine::Event> evs;
        for (const auto& [t, note] : notes)
        {
            SynthEngine::Event e;
            e.sampleOffset = static_cast<int>(t * sampleRate);
            e.type = SynthEngine::Event::Type::NoteOn;
            e.note = note;
            e.velocity = 0.8f;
            evs.push_back(e);
        }
        const int total = static_cast<int>(0.25f * sampleRate);
        std::vector<float> L(static_cast<size_t>(total)), R(static_cast<size_t>(total));
        eng.process(L.data(), R.data(), total, evs.data(), static_cast<int>(evs.size()));
        return eng.getNumActiveVoices();
    };

    const int rolled  = voicesAfter({ { 0.000f, 52 }, { 0.008f, 55 }, { 0.016f, 48 } }, true);
    const int dyad    = voicesAfter({ { 0.000f, 52 }, { 0.045f, 55 } }, true);
    const int treble  = voicesAfter({ { 0.000f, 64 }, { 0.045f, 67 }, { 0.090f, 71 } }, true);
    const int hammer  = voicesAfter({ { 0.000f, 52 }, { 0.100f, 54 } }, true);
    const int noLeg   = voicesAfter({ { 0.000f, 52 }, { 0.100f, 54 } }, false);
    // Simultaneous piano-roll chords: joint string assignment
    const std::vector<std::pair<float, int>> openC   = { { 0.f, 48 }, { 0.f, 52 }, { 0.f, 55 }, { 0.f, 60 }, { 0.f, 64 } };
    const std::vector<std::pair<float, int>> highMaj9 = { { 0.f, 72 }, { 0.f, 76 }, { 0.f, 79 }, { 0.f, 83 }, { 0.f, 86 } };
    const int openCv   = voicesAfter(openC, true);
    const int highFree = voicesAfter(highMaj9, true, false);
    const int highStr  = voicesAfter(highMaj9, true, true);
    std::cout << "  Open C chord (5 notes, same tick)  : " << openCv   << " voices (expect 5)\n"
              << "  C5-E5-G5-B5-D6, strict fretboard off: " << highFree << " voices (expect 5)\n"
              << "  C5-E5-G5-B5-D6, strict fretboard on : " << highStr  << " voices (expect 4: B5 and D6 both need the high E)\n";

    const bool ok = (rolled == 3) && (dyad == 2) && (treble == 3) && (hammer == 1) && (noLeg == 2) && (openCv == 5) && (highFree == 5) && (highStr == 4);
    std::cout << "  Rolled chord E3-G3-C3 (8 ms apart)  : " << rolled << " voices (expect 3)\n"
              << "  Dyad E3+G3 (45 ms apart, minor 3rd) : " << dyad   << " voices (expect 2)\n"
              << "  Treble triad E4+G4+B4 (45 ms spread): " << treble << " voices (expect 3)\n"
              << "  Hammer-on E3->F#3 after 100 ms     : " << hammer << " voice  (expect 1)\n"
              << "  Same with legato disabled          : " << noLeg  << " voices (expect 2)\n";

    // Consecutive chord transition: C Major -> C# Major (must not drop F notes or distort fingering)
    SynthEngine cToCsEngine;
    cToCsEngine.init(sampleRate, 512);
    cToCsEngine.paramEngineType = 1.0f; // FDTD
    std::vector<float> bufL(512, 0.0f), bufR(512, 0.0f);
    // 1. Play C chord { 48, 52, 55, 60, 64 }
    std::vector<SynthEngine::Event> cEvs;
    for (int n : { 48, 52, 55, 60, 64 })
        cEvs.push_back({ 0, SynthEngine::Event::Type::NoteOn, n, 0.8f, -1.f, -1.f });
    cToCsEngine.process(bufL.data(), bufR.data(), 512, cEvs.data(), (int)cEvs.size());
    // 2. Play 0.5s of audio
    for (int b = 0; b < static_cast<int>(0.5f * sampleRate / 512); ++b)
        cToCsEngine.process(bufL.data(), bufR.data(), 512, nullptr, 0);
    // 3. Transition to C# Major chord { 49, 53, 56, 61, 65 } with NoteOffs and NoteOns
    std::vector<SynthEngine::Event> csEvs;
    for (int n : { 48, 52, 55, 60, 64 })
        csEvs.push_back({ 0, SynthEngine::Event::Type::NoteOff, n, 0.0f, -1.f, -1.f });
    for (int n : { 49, 53, 56, 61, 65 })
        csEvs.push_back({ 0, SynthEngine::Event::Type::NoteOn, n, 0.8f, -1.f, -1.f });
    cToCsEngine.process(bufL.data(), bufR.data(), 512, csEvs.data(), (int)csEvs.size());
    for (int b = 0; b < static_cast<int>(0.3f * sampleRate / 512); ++b)
        cToCsEngine.process(bufL.data(), bufR.data(), 512, nullptr, 0);

    const int csVoices = cToCsEngine.getNumActiveVoices();
    bool hasF4 = false, hasF5 = false;
    for (int s = 0; s < 6; ++s)
    {
        if (cToCsEngine.getVoice(s).isActive())
        {
            if (cToCsEngine.getVoice(s).getMidiNote() == 53 && cToCsEngine.getVoice(s).getEnergy() > 0.5f)
                hasF4 = true;
            if (cToCsEngine.getVoice(s).getMidiNote() == 65 && cToCsEngine.getVoice(s).getEnergy() > 0.5f)
                hasF5 = true;
        }
    }
    const bool chordTransitionOk = (csVoices == 5 && hasF4 && hasF5);
    std::cout << "  C -> C# chord transition (5 notes) : " << csVoices << " voices (F4="
              << (hasF4 ? "yes" : "no") << ", F5=" << (hasF5 ? "yes" : "no") << ")  "
              << (chordTransitionOk ? "PASS" : "FAIL") << "\n";

    return ok && chordTransitionOk;
}

// ---------------------------------------------------------------------------
// Custom IR safety: a "recording" loaded as an IR (2 s of sustained harmonics of the played note,
// the worst case for resonance) must not overload the output at full velocity (the soft limiter,
// threshold 0.85, may act on < 1% of samples) and must be flagged by the plausibility check; a
// realistic decaying IR must pass the check. Before the v68.f peak cap, a real phrase recording
// loaded as an IR engaged the limiter on 52% of samples.
// ---------------------------------------------------------------------------
static bool runCustomIRSafetyCheck(float sampleRate)
{
    const int note = 52;
    const float f0 = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
    struct Stats { double rmsDb; double limiterPct; };
    auto render = [&](const juce::AudioBuffer<float>* ir, double irRate) {
        SynthEngine eng;
        eng.init(sampleRate, 512);
        eng.paramEngineType = 1.0f;
        eng.paramBodyType = 0.0f;
        if (ir != nullptr)
        {
            eng.getBodyResonance().setCustomIR(*ir, irRate, true);
            eng.paramBodyType = 4.0f;
        }
        const int total = static_cast<int>(2.0f * sampleRate);
        std::vector<float> L(static_cast<size_t>(total)), R(static_cast<size_t>(total));
        SynthEngine::Event on;
        on.type = SynthEngine::Event::Type::NoteOn;
        on.note = note;
        on.velocity = 1.0f;
        eng.process(L.data(), R.data(), total, &on, 1);
        double sum = 0.0;
        int limited = 0;
        for (float v : L)
        {
            sum += static_cast<double>(v) * v;
            if (std::abs(v) > 0.85f) ++limited;
        }
        return Stats { 10.0 * std::log10(sum / total + 1e-20), 100.0 * limited / total };
    };

    juce::AudioBuffer<float> tonal(1, 96000);   // 2 s at 48 kHz of sustained harmonics
    for (int i = 0; i < tonal.getNumSamples(); ++i)
    {
        float v = 0.0f;
        for (int k = 1; k <= 8; ++k)
            v += std::sin(2.0f * 3.14159265f * f0 * static_cast<float>(k) * static_cast<float>(i) / 48000.0f) / static_cast<float>(k);
        tonal.setSample(0, i, 0.3f * v);
    }
    juce::AudioBuffer<float> decaying(1, 14400); // 300 ms decaying noise, IR-like
    uint32_t rng = 0xabcdefu;
    for (int i = 0; i < decaying.getNumSamples(); ++i)
    {
        rng = rng * 1664525u + 1013904223u;
        decaying.setSample(0, i, (static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f) * std::exp(-static_cast<float>(i) / 1200.0f));
    }

    const Stats ref = render(nullptr, 0.0);
    const Stats bad = render(&tonal, 48000.0);
    const bool flagged = BodyResonance::describeIRProblems(tonal, 48000.0).isNotEmpty();
    const bool realOk  = BodyResonance::describeIRProblems(decaying, 48000.0).isEmpty();
    const bool noOverload = bad.limiterPct < 1.0;
    std::cout << std::fixed << std::setprecision(1)
              << "  Classical body, E3 at full velocity  : RMS " << ref.rmsDb << " dBFS, limiter on " << ref.limiterPct << "% of samples\n"
              << "  Tonal 'recording' loaded as an IR    : RMS " << bad.rmsDb << " dBFS, limiter on " << bad.limiterPct
              << "% of samples (limit 1%)  " << (noOverload ? "PASS" : "FAIL") << "\n"
              << "  Plausibility check flags it          : " << (flagged ? "yes  PASS" : "no  FAIL") << "\n"
              << "  Realistic decaying IR passes check   : " << (realOk ? "yes  PASS" : "no  FAIL") << "\n"
              << std::defaultfloat << std::setprecision(6);
    return noOverload && flagged && realOk;
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
    const bool eventsOnly = (argc > 1 && std::string(argv[1]) == "--events-only");

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
        if (eventsOnly) break;
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
    // PART 1b: Event Stress RTCheck across host sample rates
    // -----------------------------------------------------------------------
    std::cout << "\n EVENT STRESS RTCHECK (note-ons, legato, keyswitches, strum, engine/material/body switches)\n"
              << "---------------------------------------------------------------------------------------------------------\n";
    for (float sr : { 44100.0f, 48000.0f, 88200.0f, 96000.0f, 192000.0f })
    {
        for (int bs : { 32, 512 })
        {
            const size_t heapOps = runEventStressScenario(sr, bs);
            std::cout << "  " << std::setw(6) << static_cast<int>(sr) << " Hz  block " << std::setw(4) << bs
                      << "  : " << (heapOps == 0 ? "PASS (0)" : "FAIL (" + std::to_string(heapOps) + " heap ops)") << "\n";
            if (heapOps != 0) allRtPassed = false;
        }
    }
    std::cout << "---------------------------------------------------------------------------------------------------------\n";

    std::cout << "\n SAMPLE-ACCURATE MIDI TIMING (48 kHz)\n"
              << "---------------------------------------------------------------------------------------------------------\n";
    bool timingOk = runTimingAccuracyCheck(48000.0f);
    std::cout << "---------------------------------------------------------------------------------------------------------\n"
              << " CHORD vs LEGATO DETECTION (48 kHz)\n";
    timingOk = runChordVsLegatoCheck(48000.0f) && timingOk;
    std::cout << "---------------------------------------------------------------------------------------------------------\n"
              << " CUSTOM IR SAFETY (48 kHz)\n";
    timingOk = runCustomIRSafetyCheck(48000.0f) && timingOk;
    std::cout << "---------------------------------------------------------------------------------------------------------\n";

    if (eventsOnly)
    {
        std::cout << " [Timing Result]   : " << (timingOk ? "PASS" : "FAILED") << "\n";
        std::cout << " [RTCheck Result]  : " << (allRtPassed ? "PASS (0)" : "FAILED") << " (event stress only)\n";
        return (allRtPassed && timingOk) ? 0 : 1;
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

    std::cout << " [Timing Result]   : " << (timingOk ? "PASS: MIDI events applied at their exact sample" : "FAILED") << "\n";
    return (allRtPassed && timingOk) ? 0 : 1;
}
