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

### F. Living LLM Wiki Maintenance Protocol
This repository maintains a modular, on-demand **LLM Wiki** in [`docs/wiki/`](docs/wiki/README.md) to preserve technical clarity and prevent `AGENTS.md` context bloat:
1. **Targeted Reading**: When modifying specific DSP subsystems, read only the relevant wiki chapter rather than scanning the whole history.
2. **Synchronized Updates**: Whenever DSP algorithms, parameters, articulations, or architecture changes are made, update the corresponding chapter in [`docs/wiki/`](docs/wiki/README.md).
3. **Milestone Logging**: Log all detailed version changelogs in [`docs/wiki/07_release_and_milestones.md`](docs/wiki/07_release_and_milestones.md). Keep `AGENTS.md` lean and focused on operational invariants.

---

## 2. DSP Architecture & Living Wiki Reference

For comprehensive mathematical derivations, code architectures, and implementation details, refer to the documentation wiki:

| Chapter | Topic | Document | Key Contents |
| :--- | :--- | :--- | :--- |
| **01** | **Architecture Overview** | [`01_architecture_overview.md`](docs/wiki/01_architecture_overview.md) | Signal flow pipeline, 6-string polyphony, soundboard spatializer |
| **02** | **Bilbao FDTD Engine** | [`02_dsp_bilbao_fdtd.md`](docs/wiki/02_dsp_bilbao_fdtd.md) | Elastodynamic PDE, CFL stability bounds, Sherman-Morrison/Woodbury |
| **03** | **Waveguide Engine** | [`03_dsp_waveguide.md`](docs/wiki/03_dsp_waveguide.md) | Extended Karplus-Strong, fractional tuning, stiffness dispersion |
| **04** | **Body & Bridge Acoustics** | [`04_body_and_bridge_acoustics.md`](docs/wiki/04_body_and_bridge_acoustics.md) | Multi-port bridge coupling, partitioned IR convolution, Modalys bank |
| **05** | **Articulations & Keyswitches** | [`05_articulations_and_keyswitches.md`](docs/wiki/05_articulations_and_keyswitches.md) | Open plucks, palm mute, dead-note dipoles, choke slap, strumming |
| **06** | **Real-Time Safety & QA** | [`06_realtime_safety_and_qa.md`](docs/wiki/06_realtime_safety_and_qa.md) | Zero-alloc invariants, `bench_synth` RTCheck, `pluginval`, CI matrix |
| **07** | **Releases & Milestones** | [`07_release_and_milestones.md`](docs/wiki/07_release_and_milestones.md) | Full changelog (v36.f–v55.f) and roadmap milestones |

---

## 3. Living Project Roadmap & Active Priorities

> [!NOTE]
> Detailed historical milestone notes from **v36.f** to **v55.f** are archived in [`docs/wiki/07_release_and_milestones.md`](docs/wiki/07_release_and_milestones.md).

### Current Version Status: **v58.f**
- **DAW Legato Cross-Boundary Fretting Reach**: Resolved string-allocation boundary bug where overlapping whole-step / half-step notes (such as $C_4 \to D_4$, $F \to G$, $A \to B$) on the DAW piano roll were mistakenly routed to different strings instead of hammering on the actively held vibrating string.
- **Simultaneous Chord Protection via Voice Onset Age**: Added minimum voice onset threshold (`samplesSinceNoteOn >= 160`, ~4ms) preventing simultaneous chord voicings or near-simultaneous MIDI attacks from collapsing or misfiring legato transitions.
- **Zero-Allocation FDTD Legato Scratch**: Pre-reserved scratch buffers (`saved_curr`, `saved_prev`) in `FdtdString` eliminating heap allocations during runtime legato retuning.
- **Real-Time Zero-Allocation Safety**: `bench_synth: RTCheck PASS (0)` on all block sizes (32..512) and multi-track sweeps.

### In Progress / Next Priorities
- [x] **GitHub Actions Cross-Platform CI**:
  - Added `.github/workflows/build_and_validate.yml` to automatically build Standalone/VST3/LV2 on Windows, macOS, and Linux runners and run `bench_synth` on every push/PR.
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

