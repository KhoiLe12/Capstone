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
- [x] **v42.f**:
  - Replaced string/fretboard panning with constant-width Symmetrical Mirrored Soundboard Reflection Network ($d_1 = 5.2\text{ ms}, d_2 = 9.6\text{ ms}$).
  - All notes and fundamentals centered across the stereo field with bit-exact identical attack transient ($L=1.0, R=1.0$) and $0.00\text{ dB}$ L/R balance.
  - Natural acoustic 3D soundboard aura with clean mono folding and zero comb-filtering.
- [x] **v43.f**:
  - Implemented DAW Multi-Track Stress & Capacity Benchmark in `bench_synth` simulating multi-threaded DAW track pools (12 worker threads).
  - Evaluated full 6-string acoustic strumming sessions from 1 to 64 simultaneous tracks (up to 384 concurrent vibrating strings).
  - Integrated thread-safe real-time memory interception (`RTCheck: PASS (0)`) across concurrent audio worker threads.
  - Conformed to strict VST3 quality gates via `pluginval` (0 errors).
- [x] **v44.f**:
  - **Eliminated Chord Intermodulation Distortion**: Dynamically scaled soundboard compliance saturation headroom with active voice count, preventing polyphonic chord attacks from collapsing into inharmonic IMD fizz.
  - **Smooth Slew-Filtered Polyphonic Headroom**: Integrated 15 ms one-pole smoother on `polyScale` to eliminate zipper amplitude stepping across strummed chords.
  - **Frequency-Adaptive Acoustic Release & Micro-Jitter**: Replaced synchronized 35 ms noise gate with pitch-adaptive flesh damping ($\tau \approx 35\text{ ms}$ treble to $95\text{ ms}$ bass) and organic finger micro-jitter.
  - **Extended Fret Reach Polyphonic String Assignment**: Expanded reachable fret span from 5/7 frets to 19 frets, preventing high-register chord voicings from prematurely choking existing notes.
  - Passed all quality gates: `RTCheck: PASS (0)` and `pluginval: SUCCESS` (0 errors).
- [x] **v45.f**:
  - **Eliminated Chord Crackling & Instantaneous Knee Discontinuity**: Removed static pre-convolution $\tanh$ waveshaping on bridge force, delivering pure linear force into the body IR and eliminating sample-rate step discontinuities.
  - **Nylon IR Headroom Calibration & Peak Alignment**: Calibrated Classical Nylon IR wet gain ($0.62\times$ vs Gibson $0.82\times$), matching the prominent 4.0x 210 Hz body resonance and preventing output soft-limiter ceiling clamping.
  - **Decaying String Re-Trigger Protection**: Enhanced `assignStringForNote` to route incoming chord notes to truly idle strings even if the previous note is decaying in release, eliminating violent buffer clobbering pops.
  - **Deep Sympathetic Resonance for Sparse Voicings**: Increased cross-bridge sympathetic coupling ($0.05 \to 0.12$) so 3-note chords bloom with warm, resonant open-string harmonic aura.
  - Passed all quality gates: `RTCheck: PASS (0)` (0 heap allocations) and `pluginval: SUCCESS` (0 errors).
- [x] **v46.f**:
  - **Physical String Material & Density Profiles**: Implemented authentic material densities and elastic moduli for **Nylon Classical** ($\rho = 1140\text{ kg/m}^3, E \approx 1.2\text{ GPa}$) and **Steel Acoustic** ($\rho \approx 6200\text{--}7850\text{ kg/m}^3, E \approx 200\text{ GPa}$, phosphor bronze wound & plain steel gauges).
  - **Acoustic Steel-String Physics**: Dynamically scaled physical tension ($T_0 \approx 100\text{--}150\text{ N}$ per string, total tension $\approx 750\text{ N}$), extended high-frequency sustain, and increased longitudinal P-wave snap velocity ($c_L \approx 5000\text{ m/s}$).
  - **Expanded Grid Pre-Allocation**: Extended FDTD vector reservation capacity ($kMaxGridSize = 512$) ensuring zero dynamic allocations up to 96 kHz for dense steel string meshes.
  - **Parameter & GUI Integration**: Added `stringMaterial` parameter in APVTS and GUI ComboBox (`Nylon Classical` vs `Steel Acoustic`) and added `--material` and `--strum` CLI options in `render_audio`.
  - **Objective Benchmark Breakthrough**: Multi-Scale STFT spectral distance dropped across all chords, with Chord C achieving a **`GOOD`** rating ($2.40$ combined loss, $1.69$ log-STFT distance).
  - Quality gates passed: `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread).
- [x] **v47.f**:
  - **Helmholtz Plectrum Release Velocity Kick**: Modeled finite plectrum slip velocity ($v_\text{slip} = c \cdot \partial u / \partial x$) launching sharp traveling acoustic Helmholtz corner into the FDTD PDE rather than releasing from static rest.
  - **Calibrated Tactile Plectrum Scrape & High-Frequency Rasp**: Rescaled mechanical scrape generator from negligible meter units to dimensionless slope units sitting at authentic $-18\text{ dB to }-24\text{ dB}$ acoustic ratio, with roundwound bronze wire corrugation ridges (~2600 Hz) and high-frequency bypass around the 12 dB saddle filter.
  - **Audible Longitudinal P-Wave Attack Snap**: Re-calibrated axial plectrum release impulse coupling directly into soundboard plate pumping.
  - **Automated Validation Strictness 8 Conformance**: Elevated `pluginval` in `CMakeLists.txt` to strictness level 8 (including parameter thread safety and parameter fuzzing across all 14 automatable parameters) passing with 0 errors.
  - Quality gates passed: `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread) and `pluginval: SUCCESS` (strictness 8, 0 errors).
- [x] **v48.f**:
  - **Calibrated Frequency-Dependent Damping ($\sigma_1$)**: Calibrated physical air viscosity and internal viscoelastic shear damping in the elastodynamic PDE ($\sigma_1 = 3.5 \times 10^{-4}\text{ s}^{-1}$ on bass scaling with treble register and user decay modulation).
  - **Acoustic Waterfall Roll-Off Contour**: Transformed flat uniform FDTD decay into a natural acoustic diagonal decay curve ($5\text{ kHz}-10\text{ kHz}$ decaying in $\sim 0.25\text{--}0.4\text{ s}$ while $80\text{--}250\text{ Hz}$ fundamentals sustain warmly for $3\text{--}5+\text{ s}$).
  - **Quality Gates Passed**: `bench_synth: RTCheck PASS (0)` (0 heap allocations) and `pluginval: SUCCESS` (strictness 8, 0 errors).
  - **DAW Deployment**: Deployed freshly compiled release binary to `C:\Program Files\Common Files\VST3\Hybrid Synth.vst3`.

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

