# Antigravity Pair-Programming Guidelines & Synchronization (AGENTS.md)

This document serves as the persistent project configuration, invariant rules, and living roadmap for all Antigravity and AI coding agents working on the **Hybrid Physical Modeling Synthesizer** across different machines and platforms (Windows, macOS, Linux).

---

## 1. Mandatory Agent Workflows

Every agent session MUST adhere to the following protocols before finalizing any task:

### A. Version Incrementing Protocol
Whenever a bug is fixed, an optimization is applied, or a new feature is implemented:
1. Bump the version in **[`CMakeLists.txt`](CMakeLists.txt)**:
   ```cmake
   project(HybridSynth VERSION <Major>.<Minor>.<Patch>)
   ```
2. Bump the version string in **[`src/Version.h`](src/Version.h)**:
   ```cpp
   #define HYBRID_SYNTH_VERSION_STRING "Version <Major><suffix>"
   ```
3. Use Conventional Commits for git messages:
   - `fix(dsp): ... (vXX)`
   - `feat(dsp): ... (vXX)`
   - `perf(fdtd): ... (vXX)`

---

### B. Validation & Quality Gates (pluginval & RTCheck)
Before declaring any task complete or committing code:
1. **Automated VST3 Conformance (`pluginval`)**:
   Run the CMake validation target to verify all parameter accessors, audio processing rates (44.1k, 48k, 96k), automation, and bus configurations:
   ```powershell
   cmake --build build --config Release --target validate
   ```
   *Requirement*: Must return **`SUCCESS`** with 0 errors.

2. **Real-Time Safety & Latency Sweeps (`bench_synth`)**:
   Run the native benchmark suite:
   ```powershell
   # Windows:
   build/bench_synth.exe
   # macOS / Linux:
   ./build/bench_synth
   ```
   *Requirements*:
   - `[RTCheck Result]`: Must report **`PASS (0)`** (zero heap allocations on the audio thread).
   - Latency / Deadline: Multi-voice polyphony (1, 4, 6 voices) across buffer sizes 64 to 512 must remain well within real-time deadlines.

---

### C. Git & Remote Push Invariant: Never Push Autonomously
- **NEVER run `git push` without explicit user confirmation**:
  Even if builds, `pluginval`, and benchmarks pass with 100% success, NEVER push changes to GitHub autonomously.
- **Workflow**:
  1. Build the target and deploy the binary locally for testing.
  2. Run `pluginval` and `bench_synth` to verify technical safety.
  3. Notify the user to test and listen in their DAW / host.
  4. Only commit or push after the user explicitly tests and confirms with "push to github" or gives direct authorization.

---

### D. Build & Environment Guidelines
- **Windows Toolchain**:
  - Always use **MSVC `cl.exe`** initialized through Developer Command Prompt (`vcvars64.bat`) with Ninja:
    ```cmd
    cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl'
    ```
  - Do NOT let CMake auto-detect or switch to MSYS2/MinGW GCC (`g++.exe`) on Windows, as mixing runtimes causes ABI breakages.
  - Release compilation uses `/arch:AVX2 /fp:fast` on MSVC.
- **macOS / Linux Toolchain**:
  - Standard CMake with Clang or GCC (`-O3 -flto -mavx2` on x86_64, or `-O3 -flto` on Apple Silicon NEON).
- **No Helper Scripts**:
  - Do NOT commit `.bat`, `.cmd`, or machine-specific shell scripts into the repository root. All builds, tests, and validations must remain natively driven through CMake.

---

### E. Audio Thread Safety Invariants
- **Zero Allocations**: Never call `malloc()`, `free()`, `new`, `delete`, or resizing methods (`std::vector::push_back`, `resize`, `reserve`, `juce::String` concatenation) inside `processBlock()`, `tick()`, or any audio thread callback.
- **Pre-allocation**: All buffers, tables, and state vectors must be pre-allocated in `prepareToPlay()` or constructors.
- **Buffer Swapping**: Rotate audio buffers using `std::vector::swap()` or pointer rotation ($O(1)$) instead of copying elements.
- **Denormals & NaNs**: Denormalized numbers must be disabled via `juce::FloatVectorOperations::disableDenormalisedNumberSupport()`. Protect physical loops with damping clamps and stability checks.

---

## 2. DSP Architecture Overview

- **Engine 0: Extended Karplus-Strong Waveguide**
  - Sub-sample allpass pitch tuning.
  - First-order stiffness dispersion filter (inharmonicity: $f_n \approx n f_0 \sqrt{1 + B n^2}$).
  - Dynamic pluck tension modulation (pitch settle).
- **Engine 1: Stefan Bilbao Elastodynamic Non-Linear FDTD**
  - Dual-polarization transverse string modeling (Vertical $u$ and Horizontal $u_H$).
  - Geometric Kirchhoff-Carrier tension variation (geometrical nonlinearity).
  - Exact conservative non-linear contact solver for fretboard collision and finger barrier.
  - Adaptive solver: Fast-path Rank-1 Sherman-Morrison solver for non-contact free vibration; seamless switch to 4x4 Woodbury solver upon collision.
- **Multi-Port Bridge Scattering**:
  - Energy-conserving scattering junction coupling all 6 string forces into mutual sympathetic resonance.
- **Soundboard & Body Models**:
  - 32-mode IRCAM Modalys-style parallel resonator bank with modal scaling and wood loss damping.
  - Partitioned dual-polarization IR convolution body resonator.

---

## 3. Living Project Roadmap & TODO List

### Completed Milestones
- [x] **v36.f**: Fret collision sizzle on hard plucks, wound bass wire rasp, and FDTD humanization micro-variation.
- [x] **v37.f**: Cross-bridge sympathetic string resonance.
- [x] **v38.f**: Dynamic phantom scaling, fret damping profile, and wood air loss.
- [x] **v39.f**: Dual-axis saddle polar radiation and soundboard compliance bloom.
- [x] **v39.h**: CFL stability violation fix on note-off and NaN/Inf safety guards.
- [x] **v40.f**: 
  - Integrated `pluginval` automated testing target in CMake (`target validate`).
  - Implemented `bench_synth` native benchmark harness with `RTCheck` (0 heap allocations).
  - Optimized Bilbao FDTD string: adaptive Sherman-Morrison fast path, $O(1)$ buffer swapping, branch-free stencils, and AVX2 (50% CPU reduction for 6 voices).
- [x] **v41.f**:
  - Implemented physical 6-string bridge saddle panning (symmetric constant-power layout).
  - Added acoustic soundboard cross-plate wave transit delay (~0.25 ms Haas delay).
  - Enabled active Mid/Side spatializer scaling from mono (0.0) to natural stereo (0.7) to wide studio (1.0).

---

### In Progress / Next Priorities
- [ ] **GitHub Actions Cross-Platform CI**:
  - Add `.github/workflows/build_and_validate.yml` to automatically build Standalone/VST3 on Windows, macOS, and Linux runners and run `pluginval` and `bench_synth` on every push/PR.
- [ ] **Preset Management System**:
  - Implement a JSON-based factory preset loader (e.g. "Dreadnought Acoustic", "Nylon Classical", "Strat Neck Single-Coil", "Flamenco Hard Attack", "12-String Shimmer").
- [ ] **Micro-Tuning & Alternate Tuning Support**:
  - Add support for Scala (`.scl`) files and standard alternate guitar tunings (Drop D, DADGAD, Open G, Open D).
- [ ] **MPE & Polyphonic Aftertouch Expression**:
  - Implement MIDI Polyphonic Expression (MPE) for per-finger vibrato (pitch-bend), fret pressure (soundboard coupling), and slide dynamics.
- [ ] **GUI Refinement & FDTD String Mesh Visualizer**:
  - Modernize the JUCE GUI with real-time waveform / string deflection animation and visual fretboard display.

---

### Known Watch-Items & Edge Cases
- When running `pluginval` on Windows, ensure the VST3 bundle path inside `build/HybridSynth_artefacts/Release/VST3/Hybrid Synth.vst3` is freshly linked.
- When adding parameters to `PluginProcessor.cpp`, verify that `pluginval` parameter accessors, discrete step counts, and default values pass strictness levels 5–8 without warnings.

