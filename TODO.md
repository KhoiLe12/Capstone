# Hybrid Synth Roadmap & Feature Checklist

This document tracks all planned, ongoing, and completed features across physical modeling, soundboard acoustics, electric guitar emulation, and expressive articulations.

---

## 1. Physical String Engine (Bilbao FDTD & Digital Waveguide)

- [x] **Core Bilbao FDTD Solver** (DAFx24 SAV Energy-Conserving Formulations).
- [x] **Transverse-Horizontal Dual Polarization & Beat Shimmer** ($V$ and $H$ string axes).
- [x] **Nonlinear Fretboard & Frets Collision** (Low-rank Woodbury $4\times 4$ solver, 0 Newton-Raphson loops).
- [x] **Fret Action Clearance & Hard-Pluck Sizzle** (Calibrated fret graze on velocity $\ge 0.85$).
- [x] **Wound Bass vs. Plain Treble String Texture** (2400 Hz winding rasp vs. glassy nylon slip).
- [x] **Axial P-Wave Attack Snap** ($c_L = \sqrt{E/\rho}$ longitudinal transient).
- [x] **Dynamic Phantom Harmonics Bloom** ($2f_0$ dynamic tension modulation).
- [x] **Cross-Bridge Sympathetic String Resonance** (6-string open harmonic resonator bank with fretting damping).
- [x] **Dual-Polarization Saddle Forces**: $F_V$ (pumping) $+ 0.35\,F_H$ (rocking) summed into the bridge drive. They are not separate body channels, and $F_H$ was silently dropped until v62.f.
- [x] **Dynamic Phantom Harmonics Quadratic Scaler**: Scale tension bloom strictly with $E_{\text{pot}} \propto \text{disp}^2$.
- [x] **Positional Damping Profile**: Fretted fingertip compliance vs. open bone nut ring-out.

---

## 2. Acoustic Soundboard & Body Modeling

- [x] **Zero-Latency Partitioned Convolution Engine** (`juce::dsp::Convolution`).
- [x] **Classical Nylon Studio Mic IR** (High-resolution stereo capture).
- [x] **Gibson Dreadnought Acoustic IR** (Woody dreadnought transfer function).
- [x] **32-Mode Modalys-style Modal Resonator Bank** (hand-authored mode table from published guitar body mode ranges).
- [x] **Stereo Mid/Side Spatializer & Microphone Field** (Continuously variable Width knob).
- [x] **High-Frequency Wood Viscosity & Air Dispersion Roll-Off** (Gentle top-end acoustic dissipation above 10.5 kHz).
- [x] **Soundboard Compliance Bloom / Soft Nonlinearity** (Wood cavity acoustic headroom compression under heavy strumming).

---

## 3. Electric Guitar Modeling Mode

- [ ] **Magnetic Pickup Physical Transducer (Comb Filtering & Magnetic Aperture)**:
  - String velocity integration ($v(x, t) = \partial u / \partial t$) evaluated at pickup coil positions:
    - **Neck Pickup** (~$x/L = 0.80\text{--}0.85$) -> Deep, round, warm fundamental.
    - **Middle Pickup** (~$x/L = 0.88$) -> Strat-style scoop / quack.
    - **Bridge Pickup** (~$x/L = 0.94\text{--}0.96$) -> Bright, biting, high-harmonic bite.
  - Pickup selector switch: Neck, Neck+Middle, Middle, Middle+Bridge, Bridge.
- [x] **Physical String Material & Density Profiles (v46.f)**:
  - **Nylon Classical**: $\rho = 1140\text{ kg/m}^3$, $E \approx 1.2\text{ GPa}$, polymer viscoelastic damping, nylon radii.
  - **Steel Acoustic**: $\rho \approx 6200\text{--}7850\text{ kg/m}^3$ (hex steel core with bronze wrap & plain steel wire), $E \approx 200\text{ GPa}$, high-tension ($T_0 \approx 100\text{--}150\text{ N}$ per string), metallic longitudinal P-wave snap ($c_L \approx 5000\text{ m/s}$), and longer sustain.
  - Exposed `stringMaterial` parameter in VST3/AU/Standalone APVTS and GUI ComboBox.
- [ ] **Electric Guitar Tone & Volume Circuit**:
  - Standard $250\text{k}\Omega$ / $500\text{k}\Omega$ potentiometer with $0.022\text{--}0.047\,\mu\text{F}$ tone capacitor model.
- [ ] **Guitar Amp / Cabinet Simulation Stage**:
  - Preamplifier triode tube warmth/saturation (12AX7 waveshaper).
  - Classic 1x12 / 2x12 / 4x12 Guitar Cabinet Impulse Responses (Celestion / Jensen style).

---

## 4. Performance & Articulations

- [x] **Palm Muting** (C1 hold keyswitch & DAW parameter).
- [x] **Full Muting & Fret Hand Slap Choke** (D1 hold/trigger keyswitch & DAW parameter).
- [x] **Alternating 8th-Note Acoustic Pick Rake & Strumming** (E1 hold keyswitch with down/up stroke physics).
- [x] **Physical 6-String Guitar Polyphony & Monophonic String Choking** (Prevents multi-fret collisions on single string).
- [x] **Legato Hammer-On & Pull-Off (HOPO)** (v56.f–v64.f): overlapping notes on one string retune the vibrating string without a re-pluck. 40 ms chord window, `legato` on/off parameter.
- [ ] **Continuous Pitch Slide / Glissando**:
  - Smooth fretboard glissando across fret obstacles.
- [ ] **True Finger Vibrato (Pitch & Tension Modulation)**:
  - Lateral finger rocking vs. axial classical vibrato with tension modulation.
- [ ] **Flamenco Rasgueado / Multi-Finger Strumming**:
  - Rapid 4-finger fan strum articulation.
- [ ] **Artificial & Natural Harmonics**:
  - Nodal touch at $1/2$ (12th fret), $1/3$ (7th fret), $1/4$ (5th fret) damping fundamental and ringing pure harmonics.

---

## 5. Known Issues & Calibration (measured v64.f)

- [x] **Waveguide tuning was sharp** (fixed v67.f): caused by the +0.18 Hz detune on the long-sustaining horizontal plane, not the filters. The detune now goes on the vertical plane; sustain is within ±0.05 cents (wiki 03).
- [x] **FDTD upper-register sustain too short** (fixed v65.f): calibrated $\sigma_0$ against GuitarSet decay rates, and replaced the ring-out detector that cut high notes while loud (wiki 02 §7).
- [x] **IR notch at ~981 Hz / unknown-origin IRs** (v68.f): Classical and Flamenco are now measured IRs (Mores, CC BY 4.0). The notch remains only in *Legacy Body*, which is kept for older steel presets (wiki 04).
- [x] **Custom IR loading** (v68.f): user IRs, embedded in the project state.
- [ ] **Licensed steel-string body IR**: none found with a redistribution license; steel uses Legacy, Modal Bank or Custom IR.
- [x] **High chords dropping notes** (fixed v65.f): joint chord string assignment, 22-fret neck, Strict Fretboard toggle (wiki 01).
- [ ] **`pluginval` in CI**: the CMake `validate` target exists, but CI does not download or run pluginval. It runs locally on macOS since v66.f (wiki 06 §3).
- [ ] **Unit tests**: only `bench_synth` checks (allocation, timing, chord-vs-legato); no pitch/decay regression tests yet.

