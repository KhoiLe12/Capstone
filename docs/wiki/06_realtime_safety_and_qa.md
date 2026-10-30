# 06 — Real-Time Safety, Quality Gates & Benchmarks

## 1. Audio Thread Safety Invariants

Audio plugins operate under strict real-time constraints. Any blocking call, heap allocation, or numerical exception on the audio processing thread causes audio dropouts, glitches, and host DAW crashes.

### The Zero-Allocation Invariant (`RTCheck`)
The following calls are **strictly prohibited** inside `processBlock()`, `tick()`, `tickPolar()`, `noteOn()`, `noteOff()`, or any method called from the audio thread:
- `malloc()`, `calloc()`, `realloc()`, `free()`
- `new`, `new[]`, `delete`, `delete[]`
- Resizing container methods: `std::vector::push_back`, `resize`, `reserve`, `insert`
- Dynamic string operations: `juce::String` formatting, string concatenation (`operator+`)
- Locking primitives: `std::mutex::lock`, memory barriers, condition variables

### Pre-Allocation Pattern
All state vectors, grid points, reflection buffers, scratch memory, and FFT convolution structures must be pre-allocated during `init()`, `prepareToPlay()`, or constructors:
```cpp
// Grid size capacity reserved in init():
constexpr size_t kMaxGridSize = 512;
u_next.reserve(kMaxGridSize);
u_curr.reserve(kMaxGridSize);
u_prev.reserve(kMaxGridSize);
// Pre-allocated FDTD legato scratch buffers (v58.f):
saved_curr.reserve(kMaxGridSize);
saved_prev.reserve(kMaxGridSize);
```

### Denormal & NaN Protection
Denormalized floating-point numbers collapse CPU throughput by switching the CPU into microcode emulation.
- Denormals are globally disabled via `juce::FloatVectorOperations::disableDenormalisedNumberSupport()`.
- State variables are bounded by physical damping clamps and stability checks:
```cpp
if (std::isnan(out) || std::isinf(out) || std::abs(out) > 50.0f)
{
    fdtdString.reset();
    active = false;
    return 0.0f;
}
```

---

## 2. Real-Time Safety Benchmark Harness (`bench_synth`)

The repository includes a native C++ benchmark harness [`src/bench/BenchSynth.cpp`](../../src/bench/BenchSynth.cpp).

### Running the Benchmark
```bash
# macOS / Linux:
./build/bench_synth

# Windows:
build/bench_synth.exe
```

### Validation Metrics & Requirements
1. **`[RTCheck Result]`**: Must report **`PASS (0)`** (zero heap allocations intercepted on simulated audio threads across all voice and block sweeps).
2. **Single-Instance Latency Sweep**:
   - Tests 1, 4, and 6 active voices across block sizes 32, 64, 128, 256, and 512 samples at $48\text{ kHz}$.
   - Bilbao FDTD 6-voice peak load must remain $\le 30\%$ of the audio deadline.
3. **DAW Multi-Track Capacity Test**:
   - Simulates 1 to 64 concurrent 6-string acoustic guitar tracks (up to 384 concurrent vibrating strings) across multi-threaded worker pools.

---

## 3. Automated VST3 Conformance (`pluginval`)

Before finalizing any release, the plugin must pass Steinberg VST3 automated conformance testing via `pluginval`:
```bash
cmake --build build --config Release --target validate
```
- **Strictness Level**: Configured to **Level 8** in [`CMakeLists.txt`](../../CMakeLists.txt).
- Tests parameter thread safety, parameter fuzzing, automation ramps, sample rate switches ($44.1\text{k}, 48\text{k}, 96\text{k}$), and bus configurations.
- **Requirement**: Must return **`SUCCESS`** with 0 errors.

---

## 4. Cross-Platform GitHub Actions CI Matrix

The workflow [`.github/workflows/build_and_validate.yml`](../../.github/workflows/build_and_validate.yml) runs on every push and PR:
- **`ubuntu-22.04` (Linux x86_64)**: Clang/GCC with `-mavx2 -mfma`. Builds VST3, LV2, and Standalone. Runs `bench_synth`.
- **`macos-latest` (Apple Silicon)**: Apple Clang with ARM NEON. Builds VST3, AU, and Standalone. Runs `bench_synth`.
- **`windows-latest` (MSVC x64)**: MSVC `cl.exe` with `/arch:AVX2 /fp:fast`. Builds VST3 and Standalone. Runs `bench_synth.exe`.

