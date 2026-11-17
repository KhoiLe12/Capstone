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
// FdtdString::reserveCapacity(), called from init(sampleRate) in prepareToPlay (v62.f):
// N <= fs / (2 f0) for the lowest string, so size the grid from the sample rate.
const size_t needed = std::max<size_t>(512, std::ceil(fs / (2.0f * kMinStringFreqHz)) + 8);
for (auto* v : { &u_next, &u_curr, &u_prev, /* ...all grid + legato scratch vectors... */ })
    v->reserve(needed);
// configureGrid() then only resize()s within that capacity (and caps N as a backstop).
```
Both body IR convolution engines are likewise loaded in `BodyResonance::init()`; switching body model only flips which engine is processed (see wiki 04).

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

Quick check (seconds): `./build/bench_synth --events-only` runs the event-stress RTCheck and the sample-accurate MIDI timing check.

### Validation Metrics & Requirements
1. **`[RTCheck Result]`**: Must report **`PASS (0)`** (zero heap allocations intercepted on simulated audio threads across all voice and block sweeps).
   - **Event stress (v62.f)**: the timing sweeps trigger their notes *before* the guarded region, so they only cover steady-state rendering. The event stress test runs every event path *inside* the guarded region: note-ons on every string (including Drop-D D2), legato pull-offs that grow the FDTD grid, hammer-ons, palm/choke/strum keyswitches, engine/material/body-model switches, and an All-Notes-Off `reset()`. It runs at 44.1, 48, 88.2, 96 and 192 kHz with 32- and 512-sample blocks. On v61.f code it reported ~2,500 heap operations per run (body switches, plus FDTD grids beyond 512 points at ≥ 88.2 kHz).
2. **`[Timing Result]`** (v64.f): must report **PASS**. For both engines, a note-on at sample $k \in \{1, 37, 200, 511\}$ of a 512-sample block must match a note-on at sample 0 of a block starting $k$ samples later (relative difference $< 10^{-4}$), with output before $k$ below −120 dB re. peak. That residual is FFT-convolution round-off, ~−150 dB.
3. **Single-Instance Latency Sweep**:
   - Tests 1, 4, and 6 active voices across block sizes 32, 64, 128, 256, and 512 samples at $48\text{ kHz}$.
   - Bilbao FDTD 6-voice peak load must remain $\le 30\%$ of the audio deadline.
3. **DAW Multi-Track Capacity Test**:
   - Simulates 1 to 64 concurrent 6-string acoustic guitar tracks (up to 384 concurrent vibrating strings) across multi-threaded worker pools.

---

## 3. Automated Plugin Conformance (`pluginval`)

**Setup**: pluginval is **not** in the repository (`tools/` is gitignored), so each machine needs its own copy. Download the release for your OS from <https://github.com/Tracktion/pluginval/releases> into `tools/pluginval/` (macOS: the whole `pluginval.app`; CMake also searches `pluginval.app/Contents/MacOS` since v66.f), or put it on `PATH`. If CMake reports `PLUGINVAL_EXE-NOTFOUND`, the `validate` target does not exist.

**History**: pluginval was last recorded passing at v47.f, most likely on the Windows machine whose `tools/pluginval/` was never synced. From v52.f to v65.f only `bench_synth` was run. v66.f set it up on macOS (pluginval v1.0.4) and fixed the state-restoration failure below.

**Seeds**: pluginval randomises parameter values with a new seed each run (printed as `Random seed: 0x...`). Re-run a failure with `--seed <value>` to reproduce it exactly.

Before finalizing any release, the plugin must pass Steinberg VST3 automated conformance testing via `pluginval`:
```bash
cmake --build build --config Release --target validate
```
- **Strictness Level**: Configured to **Level 8** in [`CMakeLists.txt`](../../CMakeLists.txt).
- Tests parameter thread safety, parameter fuzzing, automation ramps, sample rate switches ($44.1\text{k}, 48\text{k}, 96\text{k}$), and bus configurations.
- **Requirement**: Must return **`SUCCESS`** with 0 errors.
- **v66.f results (macOS, pluginval 1.0.4, strictness 8)**:
  - VST3: `SUCCESS` with 0 warnings, on seeds 0x7c8e781, 0x1234, 0xbeef and 0x51a7e, and via the `validate` target.
  - AU: `SUCCESS`, with one known warning ("Current program is -1"). JUCE's AU wrapper starts its present-preset property at −1 and only refreshes it on a program-change notification, which cannot reach it from the plugin constructor. Program changes themselves work.
- **Fixed in v66.f, state restoration**: pluginval failed "Plugin state restoration" for Palm Mute / Strum 8th / Full Mute, depending on the seed.
  - Cause: a host can set an unquantised value on a bool (e.g. 0.18, which reads as off). `apvts.replaceState()` compares quantised values, so restoring "off" over "off" skipped the update, and the raw 0.18 survived the load.
  - Fix: `setStateInformation` now snaps every discrete parameter to its exact value after `replaceState()`.

---

## 4. Cross-Platform GitHub Actions CI Matrix

The workflow [`.github/workflows/build_and_validate.yml`](../../.github/workflows/build_and_validate.yml) runs on every push and PR:
- **`ubuntu-22.04` (Linux x86_64)**: Clang/GCC with `-mavx2 -mfma`. Builds VST3, LV2, and Standalone. Runs `bench_synth`.
- **`macos-latest` (Apple Silicon)**: Apple Clang with ARM NEON. Builds VST3, AU, and Standalone. Runs `bench_synth`.
- **`windows-latest` (MSVC x64)**: MSVC `cl.exe` with `/arch:AVX2 /fp:fast`. Builds VST3 and Standalone. Runs `bench_synth.exe`.

---

## 5. GuitarSet Multi-Scale STFT Benchmark (v63.f method)

[`benchmarks/guitarset_eval.py`](../../benchmarks/guitarset_eval.py) compares both string engines against real acoustic guitar recordings from **GuitarSet** (ISMIR 2018: `audio_mono-mic` + JAMS note annotations, expected in `benchmarks/data/guitarset/`).

```bash
.venv/bin/python benchmarks/guitarset_eval.py           # scores -> benchmarks/guitarset_results.json
.venv/bin/python benchmarks/guitarset_eval.py --plots   # + spectrograms -> benchmarks/plots/
```

### Metrics
STFT sizes $N \in \{2048, 1024, 512, 256, 128\}$, hop $N/4$, averaged over sizes. Lower is better.
- **Spectral convergence**: $\mathcal{SC} = \| |S_\text{ref}| - |S_\text{syn}| \|_F \,/\, \| |S_\text{ref}| \|_F$ (linear magnitude).
- **Log-magnitude L1**: $\frac{1}{N}\| \log|S_\text{ref}| - \log|S_\text{syn}| \|_1$, with magnitudes floored at −80 dB re. the reference peak.
- **Combined** = SC + log-L1.

### Method
- **Re-synthesis from annotations**: every test renders the annotated notes of its window (onsets, durations, simultaneous chord notes) through `render_audio --eventsFile`. Settings: steel strings, Gibson IR, body mix 0.75. Velocity is not annotated, so all notes use 0.80.
- **Alignment and level**: synth output is aligned to the reference by envelope cross-correlation and RMS-normalised, so scores compare spectro-temporal shape rather than latency or loudness.
- **Isolated notes are chosen automatically** from all 360 takes. A note qualifies if it is within ±15 cents of a MIDI pitch (the synth has no fine-tune or pitch bend), held ≥ 1.0 s, with no other note starting from 0.3 s before to the end of the 1.5 s window, and none still sounding at the onset. Only pitches with clean notes in **two different takes** are tested (currently 12 pitches, MIDI 51–74).
- **Phrases**: the first 5–6 s of four takes (SS1 comp and solo, BN1 comp, Rock1 comp). These contain bends and off-grid pitches (mean 9–20 cents from the nearest MIDI note) that the synth cannot reproduce.
- **Reference points**:
  - *Real (other take)*: a second real recording of the same pitch, scored against the reference. This is roughly the best a perfect guitar model could expect, given player and take variation.
  - *Naive tone*: a decaying additive harmonic tone with the same events. An engine that does not clearly beat this is not being distinguished by the metric.

### Results
| Group | System | SC | log-L1 | Combined |
| :--- | :--- | ---: | ---: | ---: |
| Isolated (12) | Real (other take) | 0.635 | 0.628 | 1.263 |
| | **Bilbao FDTD** | 0.629 | **0.607** | **1.235** |
| | Digital waveguide | 0.614 | 0.667 | 1.281 |
| | Naive tone | 0.577 | 0.704 | 1.281 |
| Phrases (4) | **Bilbao FDTD** | 0.740 | **0.586** | **1.326** |
| | Digital waveguide | 0.735 | 0.772 | 1.508 |
| | Naive tone | 0.741 | 0.741 | 1.482 |

v65.f results. FDTD beats the waveguide on log-L1 in **15/16** tests and on the combined score in **12/16**. FDTD log-L1 is 9% lower on isolated notes and 24% lower on phrases. v63.f–v64.f had 16/16 and 15/16. The v65.f damping recalibration (wiki 02 §7) made FDTD sustain longer at the benchmark's Decay 0.75 setting: log-L1 improved slightly, while SC on isolated notes worsened.

### How to read these numbers
- **Log-L1 is the discriminating metric.** It ranks FDTD < waveguide < naive tone consistently.
- **Spectral convergence does not discriminate here.** The naive tone gets the best SC, and all systems sit within ±0.03. SC on linear magnitude is dominated by the few strongest low partials.
- **This is not a realism score.** A second real recording of the same pitch scores no better than the synths, because take-to-take differences (dynamics, pick position, string, room noise) dominate. Supported claim: *FDTD's spectral envelope and decay fit real recordings better than the waveguide's*. Not supported: *FDTD sounds as real as a recording* — that needs a listening test (e.g. MUSHRA/ABX).

### Related: decay calibration
[`benchmarks/decay_calibration.py`](../../benchmarks/decay_calibration.py) fits fundamental decay rate against pitch for GuitarSet and the synth. It is used to calibrate FDTD register damping (wiki 02 §7).

### History
The v61.f version of this benchmark reported "18–33.6% lower log-magnitude L1" for FDTD. That figure came from:
- two flawed isolated tests (a "Bb3 pluck" that was actually a G#3 bend; a chord muted after 0.5–0.9 s that the synth strummed over 18 ms and let ring);
- no alignment on the isolated tests;
- peak normalisation.

It also left out that on the combined score it reported, FDTD lost 4 of 5 tests. v63.f replaces that method entirely; do not cite the v61.f numbers.
