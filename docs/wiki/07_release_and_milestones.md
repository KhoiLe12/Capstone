# 07 — Release History & Living Roadmap

This document records all version increments, DSP milestones, acoustic breakthrough calibrations, and future architectural priorities.

---

## 1. Completed Milestones

### v55.f
- **Full-Spectrum Dead-Note Tactile Pick Presence**: Restored full $1.0\times$ plectrum click amplitude with dual flexural eigenmodes ($f_1 \approx 4200\text{ Hz}, f_2 \approx 7200\text{ Hz}$ on steel, $2800\text{ Hz} / 4800\text{ Hz}$ on nylon), high-frequency edge air bite ($8\text{--}16\text{ kHz}$), and pre-release wound wire rasp on dead notes, delivering crisp acoustic tactile attack presence over the low-end body chug.
- **Keyswitch Choke Slap Click**: Enabled direct airborne tactile click in `Voice::choke()` when triggered via keyswitch D1 (`KEYSWITCH_CHOKE`), ensuring hand chokes have tactile snap + body thud.
- **Tightened Palm Mute Fundamental Sustain**: Increased $\text{effSigma0}$ from $\sim 5\text{ s}^{-1}$ to $9.5\text{--}11.0\text{ s}^{-1} + 8.5 \times (1 - \text{decay})$, reducing fundamental sustain time from loose $\sim 200\text{ ms}$ to a tight, punchy acoustic chug ($\sim 75\text{--}110\text{ ms}$) dynamically controllable via the `Decay` knob.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 27.31%).

### v54.f
- **Zero-Pitch Acoustic Dead-Note Mechanics ("X" Notes)**: Eliminated all harmonic standing-wave oscillation ($f_0$) on full mute / dead notes. Plucking an overdamped string now generates a non-oscillating, mass-dependent mechanical bridge dipole impulse ($\tau_\text{pulse} \approx 0.75\text{--}1.35\text{ ms}$, zero DC offset) directly pumping the `BodyResonance` acoustic soundboard cavity ($A_0 \approx 105\text{ Hz}$ air mode, $T_1 \approx 200\text{ Hz}$ plate mode) with authentic, woody percussive chug and zero pitch tone.
- **Tactile Dead-String Plectrum Radiation**: Preserved tactile plectrum slap on muted string wire with roundwound wire scrape texture in `tickPickClick()`, matching real-world acoustic dead-note chops.
- **Mechanical Choke Slap (No Synthetic Sines)**: Replaced artificial `std::sin()` oscillators with a physical mechanical dipole pulse on hand chokes, exciting the acoustic body IR naturally.
- **Preserved Authentic Palm Mute**: Maintained v53.f selective boundary damping for palm muting (`effSigma0 = 4.8–5.6 s⁻¹`), allowing the low fundamental (80–250 Hz) to sustain with warm momentum for ~180 ms.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 27.16%).

### v53.f
- **Acoustic Selective Viscoelastic Palm Muting**: Replaced uniform high damping with selective frequency boundary damping (`effSigma0 = 4.8–5.6 s⁻¹`, `effSigma1 * 4.0`), allowing low fundamentals (80–250 Hz) to sustain with authentic mass momentum (~160–220 ms) while flesh contact dissipates high partials and cushions plectrum snap (`muteClickScale = 0.38`), eliminating the "synthy" tone.
- **Authentic Percussive Dead-Note Chug ("X" Note)**: Eliminated artificial $1.8\text{ ms}$ string choke and $2200\text{ Hz}$ sine beep on full mute note-on. Retained natural physical string displacement to pump the acoustic guitar body resonance cavity ($105\text{--}130\text{ Hz}$) with rapid flesh decay ($\sim 45\text{--}65\text{ ms}$), delivering a low, woody, percussive acoustic chug.
- **Calibrated Waveguide Engine (Engine 0) Mutes**: Rescaled Karplus-Strong dead-note parameters (`deadTauV = 35–55 ms`, `maxDeadGainV = 0.35–0.55`, `saddleCutoff = 220–480 Hz`), achieving consistent deep chugs across both DSP engines.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread across all voice and block sweeps, max load 27.27%).

### v52.f
- **Native Linux & Cross-Platform Plugin Targets**: Platform-aware plugin format configuration in `CMakeLists.txt` automatically building native Linux **VST3**, **LV2**, and **Standalone** on Linux, **VST3**, **AU**, and **Standalone** on macOS, and **VST3** and **Standalone** on Windows.
- **Cross-Platform Optimization Flags**: Added `-mavx2 -mfma` for GCC and Clang on x86_64 Linux to match MSVC `/arch:AVX2` performance across all build targets.
- **Automated Cross-Platform GitHub Actions CI Matrix**: Added `.github/workflows/build_and_validate.yml` running automated parallel matrix builds on `ubuntu-22.04` (Linux x86_64), `macos-latest` (Apple Silicon), and `windows-latest` (MSVC x64) with native `bench_synth` real-time safety validation (`RTCheck: PASS (0)`) and artifact archiving on every push and PR.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, 6-voice FDTD max load 26.7%).

### v51.f
- **Plectrum Attack Presence & Impulse Response Calibration**: Calibrated the flexural modal resonators for transient impulse excitation ($2.4\times$ Mode 1, $2.0\times$ Mode 2), delivering full audible plate resonance without impulse attenuation.
- **Full-Strength Dipole Snap**: Increased the direct acoustic pressure snap contribution to $0.85\times$ shock, restoring the immediate attack punch.
- **Airborne Edge Sizzle (8–16 kHz)**: Integrated high-frequency edge cavitation and slip friction layer ($f_c \approx 8\text{ kHz}$ 1-pole HP) providing crisp acoustic "bite" and top-end air.
- **Quality Gates Passed**: `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, max load 27.1%).

### v50.f
- **Physical Elastodynamic Plectrum & Fingernail Model**: Replaced filtered white noise with an acoustic dipole shock wave (Ricker pressure doublet) coupled into dual material flexural resonators (Celluloid flatpick modes $4200/7200\text{ Hz}$ on steel; Keratin nail modes $2800/4800\text{ Hz}$ on nylon).
- **Pluck Position Sensitivity (`Pick Pos`)**: Wired `clampedPickPos` directly into release mechanics (bridge plucking sharpens dipole snap; neck plucking cushions release).
- **Wound-Wire Pre-Release Micro-Texture**: Accelerating micro-slip chirp ($1800\text{--}3400\text{ Hz}$) preceding snap on wound strings.
- **Quality Gates Passed**: `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, max load 26.8%).

### v49.f
- **Dual-Path Airborne Plectrum Radiation**: Direct plectrum snap radiation bypassing soundboard lowpass filter.
- **Cascaded 3-Stage High-Pass Filter**: Sample-rate adaptive 3-pole filter ($3.5\text{k}, 5.2\text{k}, 7.5\text{k}$) delivering authentic 18 dB/oct click profile.

### v48.f
- **Calibrated Frequency-Dependent Damping ($\sigma_1$)**: Air viscosity and internal viscoelastic shear damping in elastodynamic PDE ($\sigma_1 = 3.5 \times 10^{-4}\text{ s}^{-1}$ on bass).
- **Acoustic Waterfall Roll-Off Contour**: Transformed flat uniform FDTD decay into natural diagonal acoustic decay curve ($5\text{--}10\text{ kHz}$ decaying in $\sim 0.3\text{ s}$, bass fundamentals sustaining warmly for $3\text{--}5+\text{ s}$).

### v47.f
- **Helmholtz Plectrum Release Velocity Kick**: Modeled finite plectrum slip velocity launching sharp Helmholtz corner into FDTD PDE.
- **Calibrated Tactile Plectrum Scrape**: Rescaled mechanical scrape generator with roundwound bronze wire corrugation ridges (~2600 Hz).
- **Automated Validation Strictness 8 Conformance**: Elevated `pluginval` in `CMakeLists.txt` to strictness level 8 passing with 0 errors.

### v46.f
- **Physical String Material & Density Profiles**: Implemented authentic material densities and elastic moduli for Nylon Classical and Steel Acoustic.
- **Acoustic Steel-String Physics**: Dynamically scaled physical tension ($T_0 \approx 100\text{--}150\text{ N}$ per string), longitudinal P-wave snap velocity ($c_L \approx 5000\text{ m/s}$).
- **Expanded Grid Pre-Allocation**: Extended FDTD vector reservation capacity to $kMaxGridSize = 512$ up to 96 kHz.

### v45.f
- **Eliminated Chord Intermodulation Distortion & Static $\tanh$**: Delivered pure linear bridge force into the body IR, eliminating sample-rate step discontinuities.
- **Nylon IR Headroom Calibration & Peak Alignment**: Calibrated Classical Nylon IR wet gain ($0.62\times$ vs Gibson $0.82\times$), matching prominent 210 Hz body resonance.
- **Decaying String Re-Trigger Protection**: Enhanced voice allocator to route chord notes to truly idle strings.

### v44.f
- **Eliminated Chord Intermodulation Distortion**: Dynamically scaled soundboard compliance saturation headroom with active voice count.
- **Smooth Slew-Filtered Polyphonic Headroom**: 15 ms one-pole smoother on `polyScale`.
- **Extended Fret Reach Polyphonic String Assignment**: Expanded reachable fret span from 5/7 frets to 19 frets.

### v43.f
- **DAW Multi-Track Stress & Capacity Benchmark**: Evaluated full 6-string acoustic strumming sessions from 1 to 64 simultaneous tracks (up to 384 concurrent vibrating strings) on 12 worker threads.

### v42.f
- **Symmetrical Mirrored Soundboard Reflection Network**: Replaced string panning with constant-width reflection network ($d_1 = 5.2\text{ ms}, d_2 = 9.6\text{ ms}$). All fundamentals centered with $0.00\text{ dB}$ L/R balance.

### v40.f & v41.f
- **Optimization of Bilbao FDTD String**: Adaptive Sherman-Morrison fast path, $O(1)$ buffer swapping, branch-free stencils, and AVX2 SIMD.
- **Integrated `pluginval` & `bench_synth`**: 0 errors on VST3 validation; `RTCheck: PASS (0)` on all audio threads.

### v36.f – v39.h
- Fret collision sizzle, wound bass rasp, FDTD humanization micro-variation, cross-bridge sympathetic resonance, dynamic phantom scaling, dual-axis saddle polar radiation, soundboard compliance bloom, and CFL stability guards.

---

## 2. In Progress & Living Roadmap

- [ ] **Preset Management System**:
  - Implement a JSON-based factory preset loader (e.g. "Dreadnought Acoustic", "Nylon Classical", "Strat Neck Single-Coil", "Flamenco Hard Attack", "12-String Shimmer").
- [ ] **Micro-Tuning & Alternate Tuning Support**:
  - Add support for Scala (`.scl`) files and standard alternate guitar tunings (Drop D, DADGAD, Open G, Open D).
- [ ] **MPE & Polyphonic Aftertouch Expression**:
  - Implement MIDI Polyphonic Expression (MPE) for per-finger vibrato (pitch-bend), fret pressure (soundboard coupling), and slide dynamics.
- [ ] **GUI Refinement & FDTD String Mesh Visualizer**:
  - Modernize the JUCE GUI with real-time waveform / string deflection animation and visual fretboard display.

