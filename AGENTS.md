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
1. **Automated VST3 Conformance (`pluginval`)** (not committed; each machine needs it in `tools/pluginval/` or on `PATH`, see wiki 06 §3):
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

### G. First-Principles Physical Modeling Invariant (Zero Artificial DSP Tricks)
Every articulation, excitation, and damping mechanism in the synthesizer **MUST be physically modeled** within the core simulation framework (the FDTD elastodynamic PDE solver and the bridge/soundboard body acoustics), rather than patched with artificial post-filters, synthetic waveform generators, or standalone DSP shortcuts.
1. **Physical Modeling Imperative**:
   Articulations (including palm muting, dead notes, harmonics, hammer-ons/pull-offs, string bending, pick contact, and body knocks) must emerge naturally from physical boundary conditions, localized spatial loss distributions $\sigma(x)$, elastoplastic contact laws, or direct soundboard mechanical force injection.
2. **Impossibility & Sub-Optimality Disclosure**:
   If an articulation is mathematically impossible or demonstrably sub-optimal to solve in real time (e.g. non-convergent implicit nonlinear contact loops exceeding real-time audio deadlines at 96/192 kHz), the agent MUST explicitly explain why to the user and document the exact physical trade-off before adopting any simplification.
3. **Mandatory CPU Weight Increase Reporting**:
   For every added physical feature or mathematical modeling change, the agent MUST run `./build/bench_synth` and report the exact CPU weight increase across single-voice latency and multi-track capacity sweeps.

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

### Current Version Status: **v77.f**
- **v77.f**:
  - **Electric DI Gain Calibration & Headroom Optimization**:
    - Calibrated electric transverse string velocity induction scaling from raw $0.85\times$ down to $0.22\times$ (`diSample = totalPickupVoltage * smoothedPolyScale * 0.22f`).
    - Standardized peak DI voltage levels to standard Hi-Z instrument inputs ($\approx -12\text{ to }-6\text{ dBFS}$), preventing the $+8.3\text{ dB}$ resonant peak of the passive RLC tone biquad ($Q = 2.6$) from hitting the master soft-limiter threshold ($0.85$), eliminating all unwanted clipping fuzz and distortion.
    - Adjusted electric preset master gains to `0.76--0.78f` for ample dynamic headroom on heavy chord strums and aggressive bends.
  - **First-Boot Preset Selection Reliability**:
    - Created `PresetComboBox` with an overridden `showPopup()` callback that clears the internal selection index before menu presentation, guaranteeing that clicking any preset item in the dropdown (even the initial default program on first boot) triggers selection change on the very first click without requiring double-clicking.
    - Removed redundant `if (globalIdx != currentProgram)` guard in `presetBox.onChange`, allowing instant preset reload on direct user selection.

- **v76.f**:
  - **Electric Guitar Mode (Electromagnetic Pickup Induction, Passive RLC Tone Circuitry, Solid-Body Damping, and Pure DI Output)**:
    - **Physical Transverse Velocity Induction (`FdtdString::getPickupVelocity`)**:
      - Electromagnetic pickups physically detect string transverse velocity $v(x_p, t) = \frac{\partial u}{\partial t}(x_p, t)$ rather than bridge saddle force.
      - Extracted at exact continuous physical coordinates from the FDTD displacement grid ($v(x_p) = (u_{\text{curr}}(x_p) - u_{\text{prev}}(x_p)) / \Delta t$) via quadratic B-spline spatial interpolation. Vertical string motion drives the pickup flux, with subtle lateral field fringe coupling ($0.10 \times v_H$).
    - **Factory Blueprint Geometries**:
      - *1962 Fender Stratocaster (25.5" / 647.7 mm scale)*: Slanted bridge pickup (47.6 mm bass E down to 41.3 mm high E), straight middle pickup (98.4 mm), and straight neck pickup (146.0 mm). 5-way selector with authentic parallel coil "quack" out-of-phase comb notches.
      - *1959 Gibson Les Paul (24.75" / 628.65 mm scale)*: Bridge PAF humbucker (42.0 mm) and neck PAF humbucker (155.0 mm) with dual pole pieces separated by 18.5 mm. Dual-pole summing naturally synthesizes the famous ~3.6 kHz humbucker phase-cancellation notch without artificial filters.
    - **Passive Guitar RLC Resonant Tone Circuit**:
      - 2nd-order resonant lowpass biquad modeling coil inductance $L$, coil resistance $R$, guitar cable capacitance $C_{\text{cable}} \approx 500\text{ pF}$, and tone pot sweep.
      - Stratocaster: $f_{\text{res}}$ sweeps from 4.2 kHz ($Q=2.6$) down to 450 Hz ($Q=1.1$).
      - Les Paul: $f_{\text{res}}$ sweeps from 2.8 kHz ($Q=1.95$) down to 400 Hz ($Q=0.95$).
    - **Solid-Body Damping & Nickel-Plated Steel Strings**:
      - Rigid metal bridge on solid hardwood slab absorbs far less vibrational energy than a thin acoustic soundboard, reducing $\sigma_0$ to $0.16\text{--}0.20\text{ s}^{-1}$ ($T_{60} \approx 8\text{--}15\text{ s}$ of singing electric sustain).
      - Electric D'Addario EXL110 regular light (10-46) metallurgy: wound hex-core on E2, A2, D3; plain steel wire on G3, B3, E4 (`woundFactor = 0` on G string).
    - **Two-Page User Interface (Acoustic Page vs. Electric Page)**:
      - Clean top-level page switcher toggling between dedicated Acoustic Guitar and Electric Guitar DI interfaces.
      - Acoustic page displays String Material, Body Model IRs, IR loader, Body Size, and Body Coupling.
      - Electric page displays Guitar Model (Strat/LP), Pickup Selector (5-way/3-way), Pickup Type (Single-Coil/Humbucker), Tone Knob (RLC), and optional Cab/Air roll-off filter.
    - **Pristine DI Signal Path**:
      - Electric mode cleanly bypasses acoustic body IR convolution and airborne pick snap, routing the pure electromagnetic induction voltage straight into DAW amp modelers (Neural DSP, Guitar Rig, Amplitube, etc.).
    - **Verification & QA**:
      - Added 4 curated electric factory presets (`Strat Clean Funk DI`, `Texas Blues Lead`, `Les Paul Vintage Warmth`, `Bridge Humbucker Bite`).
      - `bench_synth`: `RTCheck ALL PASS (0)` across all block sizes (32..512) and sample rates (44.1..192 kHz), zero heap allocations, **+0.0% CPU overhead**.
      - `pluginval`: Strictness level 8 **`SUCCESS`** with 0 errors across all 22 parameters and 12 factory programs.

- **v75.f**:
  - **Flamenco Golpe (Soundboard Thumb Knock) Engine & Acoustic Chord Headroom Isolation**:
    - **Physical Viscoelastic Flesh-Wood Contact Law**: Implemented physical thumb-heel impact mechanics on the spruce top plate (Literature Review §6). Compressive contact force pulse $F_g(t) = 4.8 v^{1.2} \times 2.67 \sin^2(\pi \tau) e^{-2.2\tau}$ over duration $T_c \approx 2.4\text{--}3.4\text{ ms}$ (viscoelastic thumb flesh compliance). Smooth onset and offset guarantees zero DC bias or boundary clicks.
    - **Acoustic Headroom Isolation During Chords**: Root-caused why the golpe was masked/inaudible during chord playing. Previously, `golpeBodyForce` was summed into `totalForceV` *before* multiplying by `smoothedPolyScale`. When playing a 5- or 6-note chord, `smoothedPolyScale` ($\approx 0.45\text{--}0.51$) cut the golpe soundboard drive by half ($-6.5\text{ dB}$) right as the chord reached maximum level. Decoupled soundboard mechanical drive: $\text{monoBridge} = (\text{stringBridgeForce} \times \text{smoothedPolyScale}) + F_\text{body}$. The golpe excites the top plate with full energy regardless of how many strings are fretted.
    - **Dual-Path Acoustic Radiation & Wood Presence**:
      - *Soundboard Body Drive*: Direct normal force injected into the soundboard bus and bridge, naturally driving the Helmholtz air cavity ($A_0 \approx 100\text{--}105\text{ Hz}$) and fundamental wood plate dipole ($T(1, 1) \approx 195\text{--}205\text{ Hz}$) through the measured body IR without saddle lowpass roll-off.
      - *Tactile Wood & Tap Plate Contact Presence*: Combined deep flexural plate wave ($180\text{ Hz}$) with high-frequency spruce contact crackle ($1400\text{ Hz}$, $e^{-8\tau}$), providing the crisp acoustic snap of thumb flesh/nail meeting the soundboard that cuts cleanly through ringing chords.
      - *Sympathetic String Coupling*: Shockwave travels across the soundboard to the saddle, exciting undamped open strings via `SympatheticResonance`.
    - **Dedicated Non-Conflicting MIDI Mapping**:
      - Assigned to **MIDI Note 29 (F1 / FL Studio: F2)**, located immediately adjacent to E1 Strum (28), D1 Choke (26), and C1 Palm Mute (24), safely outside the guitar playing range (38..86).
      - Zero voice-stealing: golpe operates as a direct mechanical soundboard excitation, allowing full 6-string polyphonic chord strums and lead lines to sound simultaneously.
    - **Verification & QA**: Automated simultaneous 5-voice chord + golpe benchmark verification in `bench_synth` confirms diff peak $> 0.40$ ($+0.10\text{ dBFS}$) during full chord playback. `RTCheck PASS (0)`, `pluginval` Level 8 `SUCCESS`, **+0.0% CPU overhead**.

**v73.f**:
  - **Acoustic Chord Polyphony & Physical Fretboard Voicing Invariant**:
    - **Physical Chord Voicing Priority**: In `ChordSearch::evaluate()`, reordered optimization criteria to prioritize physical human hand ergonomics over avoiding ringing strings: `score = { physical, placed, -span, -fretSum, -ringingUsed }`. Previously, `-ringingUsed` preceded hand span, causing chord transitions (such as C Major $\to$ C# Major) to contort notes across 11 frets to avoid released strings, banishing the top F5 note from the high E string and burying F4 at fret 8 on the wound A string.
    - **Clean Chord Transition Invariant (`avoidRinging = false`)**: In `noteOnChord`, disabled `avoidRinging`. When a chord is struck, its physical voicing is determined strictly by optimal fretboard ergonomics rather than stale release tails from preceding chords.
    - **Natural String Reuse for Released Notes**: In `assignStringForNote()`, permitted immediate reuse of natural string $s$ when the previous note on $s$ is no longer physically held down (`heldNotes[oldNote] <= 0.001f`), naturally damping the release tail and preserving melodic first-position register.
    - **Full Conformance & Regression Testing**: Added automated C $\to$ C# transition verification in `bench_synth` ensuring both F4 and F5 ring out with healthy physical string energy. `RTCheck PASS (0)`, `pluginval` Level 8 `SUCCESS`, **+0.0% CPU overhead**.

**v72.f**:
  - **Acoustic Chord Polyphony & Physical Legato Calibration**:
    - **Single-String Legato Invariant**: Fixed over-aggressive legato detection that dropped notes in rolled chords, dyads, and treble harmonies. A hammer-on / pull-off is strictly physical and constrained to the *same* physical string (`naturalStringForNote(midiNote) == heldStr`). Notes on distinct strings ring freely as polyphonic voices rather than retuning held notes.
    - **Human Chord Window Calibration**: Widened `kLegatoChordWindowSec` from 40 ms to 75 ms, accommodating natural human finger asynchrony in keyboard and DAW chord performances without falsely converting rolled chord voicings into hammer-ons.
    - **Plectrum Contact Silence & Stealing Guard**: Guarded `fdtdIsSilent()` against false early termination during dynamic plectrum contact and the initial 75 ms onset transient (`samplesSinceNoteOn < 75 ms`), preventing newly triggered chord notes from being silenced or stolen.
    - **Full Conformance**: `bench_synth` `RTCheck PASS (0)` across all buffer sizes (32..512) and sample rates (44.1..192 kHz), `pluginval` Level 8 `SUCCESS`, **+0.0% CPU overhead** (`12.03%` 1-voice block 32, `28.38%` 6-voice block 32).

**v71.f**:
- **v71.f**:
  - **First-Principles Continuous Nonlinear Plectrum Contact Model**: Transitioned excitation from artificial static initial displacement release and synthetic Ricker dipole/bandpass oscillators into a dynamic elastoplastic plectrum-string contact law (Stefan Bilbao contact mechanics & Literature Review §6).
  - Plectrum tip dynamically traverses coordinate $x_p$ with stroke velocity $v_p$, compliantly pushing the string:
    $$f_p(t) = K_p [y_p(t) + \Delta y_\text{ridge}(t) - u_p(t)]_+ + R_p \frac{d}{dt}[y_p(t) - u_p(t)]_+$$
  - Solved analytically and non-iteratively in $O(1)$ scalar math during contact ($\sim 1.0\text{--}2.5\text{ ms}$). When restoring string tension exceeds the plectrum slip threshold ($F_\text{slip}$), the string unzips and releases a sharp Helmholtz traveling wave packet into the bridge and soundboard.
  - On wound strings, wire winding coils ($\Delta y_\text{ridge}$) dynamically modulate the physical contact force in real time, synthesizing authentic pre-release rasp naturally without external noise or bandpass filters.
  - Zero heap allocations (`bench_synth`: `RTCheck PASS (0)`), **+0.0% CPU overhead** (`12.01%` 1-vce block 32, `26.48%` 6-vce block 32), `pluginval` Level 8 `SUCCESS`.

**v70.f:**
- **v70.f**:
  - **Palm Muting Decay Calibration**: Widened palm contact patch to $L_\text{palm} = \min(0.065\text{ m}, 0.22L)$ ($6.5\text{ cm}$, representing true anatomical human palm heel coverage) and increased damping rate to $\sigma_\text{flesh} = 480.0 + 360.0 \times (1.0 - \text{decay})$, producing a tight, punchy acoustic palm mute ($\sim 45\text{--}70\text{ ms}$).
  - **Acoustic Dead Note Tone Calibration**: Extended fretting hand damping patch across $x \in [0.02L, 0.68L]$ (eliminating the undamped segment between nut and fret 2 that caused high-pitched ringing). Scaled airborne plectrum snap down to $0.25\times$ and reduced flexural click brightness to $0.35\times$, transforming the click from a harsh plastic snap into a deep, woody acoustic fretboard clack.
  - Zero heap allocations (`bench_synth`: `RTCheck PASS (0)`), +0.0% CPU overhead, `pluginval` Level 8 `SUCCESS`.

**v69.f:**
- **v69.f**:
  - **First-Principles Spatial Damping for Palm Mute & Dead Notes**: Eliminated all artificial heuristic shortcuts (synthetic Ricker dipole equations, global damping cuts, `effBrightness *= 0.50f`, and string solver bypass).
  - Palm muting is modeled physically via a localized spatial loss profile $\sigma_\text{palm}(x)$ on grid nodes within the bridge termination patch ($x \ge L - \Delta x_\text{palm}$), preserving fundamental mass momentum while absorbing high partial reflections.
  - Dead notes ("X" notes) run the full FDTD solver through a broad fretting-hand spatial damping patch $\sigma_\text{hand}(x)$ across the neck, delivering a genuine mechanical force pulse into the soundboard body IR without establishing periodic $f_0$.
  - Per-node damping coefficients $c_0[i]$ and $c_4[i]$ are pre-allocated and updated without allocations. Single-voice and multi-track benchmark measurements verify **+0.0% CPU overhead** and `RTCheck: PASS (0)`.

**v68.f:**
- **v68.f**:
  - Measured Classical (Contreras 1971) and Flamenco (Conde 1964) body IRs from the Mores archive (CC BY 4.0, attribution in `resources/ir/ATTRIBUTION.md`).
  - Old IR kept as "Legacy Body".
  - New Custom IR loading, embedded in the project state. See wiki 04.
  - **Body model indices are saved state: append only.**

**v67.f:**
- **v67.f**: waveguide tuning fixed. The +0.18 Hz polarization detune moved from the sustaining horizontal plane to the vertical plane; sustain is within ±0.05 cents (wiki 03).

**v66.f:**
- **v66.f**:
  - pluginval set up on macOS (`tools/pluginval/pluginval.app`, gitignored; CMake finds it).
  - Fixed state restoration of discrete parameters (pluginval failure) and host program-change notification.
  - VST3 strictness 8 SUCCESS; AU SUCCESS with one known JUCE warning (wiki 06 §3).

**v65.f:**
- **v65.f**:
  - FDTD damping calibrated against GuitarSet decay rates (frequency-independent $\sigma_0$; `benchmarks/decay_calibration.py`).
  - Ring-out detection now uses total string energy relative to the note's peak, so high notes are no longer cut off early. See wiki 02 §7.
  - Joint chord string assignment (22 frets) plus a `strictFretboard` parameter: high piano-roll chords no longer drop notes. See wiki 01 §2.

**v64.f:**
- **v64.f**:
  - Sample-accurate MIDI: events are applied at their exact sample via `SynthEngine::process(..., events, numEvents)`. Before, everything was applied at block start.
  - All Notes Off now stops strings without hard-resetting the body.
  - Legato needs a 40 ms chord window (was ~3.5 ms; live rolled chords lost notes) and is now a `legato` parameter with a GUI toggle.
  - `bench_synth` timing check added. See wiki 01 and wiki 07.

**v63.f:**
- **v63.f**:
  - GuitarSet benchmark rebuilt: automatic clean-note selection, annotation-driven re-synthesis, alignment + RMS normalisation, and real-take / naive-tone reference points.
  - FDTD beats the waveguide on log-L1 in 16/16 tests. The metric does not measure realism (a second real take scores no better), so do not claim realism from it. The v61.f "18–33.6%" figure is withdrawn.
  - Fixed the sympathetic-resonance ghost tone on note release.
  - See wiki 06 §5 and wiki 07.

**v62.f:**
- **v62.f DSP correctness & real-time safety pass**:
  - FDTD H-polarization now reaches the output.
  - Body model switching and high-sample-rate FDTD grids no longer allocate on the audio thread.
  - FDTD legato rebuilds all grid-derived state.
  - New `bench_synth --events-only` event stress RTCheck. Details in [`07_release_and_milestones.md`](docs/wiki/07_release_and_milestones.md).

**v61.f:**
- **Official GuitarSet Dataset STFT Benchmark & Comparative Evaluation**: Integrated official GuitarSet dataset (ISMIR 2018, NYU MARL / Queen Mary University of London) directly into automated synthesis verification pipeline ([`benchmarks/guitarset_eval.py`](benchmarks/guitarset_eval.py)).
- **Dual-Paradigm Acoustic Evaluation**:
  - *Isolated Acoustic Pluck & Cadence Extraction*: Clean ringing plucks (C3, Bb3) and cadence chords sliced from real condenser mic recordings (`audio_mono-mic`) using JAMS millisecond annotations.
  - *Continuous Performance Resynthesis*: Full 5–6s multi-string fingerstyle comping (`00_SS1-100-C#_comp`) and solo lead melodies (`00_SS1-100-C#_solo`) transcribed from JAMS note annotations.
- **Headless Renderer Event Scheduling (`--eventsFile`)**: Enhanced [`src/bench/RenderAudioCLI.cpp`](src/bench/RenderAudioCLI.cpp) with timestamped noteOn/noteOff CSV event scheduling, enabling continuous phrase resynthesis.
- ~~Multi-Scale STFT "18% to 33.6% lower" result~~: withdrawn in v63.f (flawed test cases and normalisation). Current results are in wiki 06 §5.
- **Factory Preset Management & First-Launch DAW Synchronization**: 8 curated physical modeling presets integrated natively into host DAW program dropdowns and plugin header GUI.
- **Real-Time Zero-Allocation Safety**: `bench_synth: RTCheck PASS (0)` on all block sizes (32..512) and multi-track sweeps.

### In Progress / Next Priorities
- [x] **Official GuitarSet Dataset Benchmark Pipeline**:
  - Downloaded official annotations and acoustic mono mic audio; built automated Multi-Scale STFT evaluation pipeline with 4-panel spectrogram comparisons.
- [x] **GitHub Actions Cross-Platform CI**:
  - Added `.github/workflows/build_and_validate.yml` to automatically build Standalone/VST3/LV2 on Windows, macOS, and Linux runners and run `bench_synth` on every push/PR.
- [x] **Preset Management System**:
  - Implemented 8 distinct physical modeling factory presets accessible via DAW program selector and plugin header dropdown.
- [ ] **Physical String Bending & MIDI Pitch Wheel Expression**:
  - Implement real-time physical string elongation & dynamic tension modulation $T(t) = T_0 + \Delta T(t)$ across vibrating strings in response to MIDI Pitch Wheel (configurable $\pm 2$ to $\pm 12$ semitones) and DAW pitch slides/vibrato.
- [ ] **Harmonics Engine (Natural & Pinch Harmonics)**:
  - Natural nodal touch damping (12th fret $1/2$ octave, 7th fret $1/3$ octave+5th, 5th fret $1/4$ 2 octaves) damping non-nodal modes into crystalline chimes.
  - Pinch / artificial harmonics with thumb-flesh damping for high-register harmonic squeals, triggered via keyswitch or articulation control.
- [x] **Electric Guitar Mode (Electromagnetic Pickups & Solid Body)**:
  - Electromagnetic pickup induction model sensing string transverse velocity ($\frac{\partial u}{\partial t}$) at physical positions $x_\text{pickup}$ (Neck, Middle, Bridge).
  - Single-coil vs. Humbucker aperture comb filtering, tone pot load, and solid body low-damping sustained vibration.
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

