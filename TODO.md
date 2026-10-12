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
- [ ] **Dual-Axis Saddle Polar Radiation**: Route $V$ (pumping) and $H$ (rocking) into distinct soundboard coupling channels.
- [x] **Dynamic Phantom Harmonics Quadratic Scaler**: Scale tension bloom strictly with $E_{\text{pot}} \propto \text{disp}^2$.
- [x] **Positional Damping Profile**: Fretted fingertip compliance vs. open bone nut ring-out.

---

## 2. Acoustic Soundboard & Body Modeling

- [x] **Zero-Latency Partitioned Convolution Engine** (`juce::dsp::Convolution`).
- [x] **Classical Nylon Studio Mic IR** (High-resolution stereo capture).
- [x] **Gibson Dreadnought Acoustic IR** (Woody dreadnought transfer function).
- [x] **32-Mode IRCAM Modalys Physical Resonator Bank** (Spruce/rosewood eigenmodes).
- [x] **Stereo Mid/Side Spatializer & Microphone Field** (Continuously variable Width knob).
- [x] **High-Frequency Wood Viscosity & Air Dispersion Roll-Off** (Gentle top-end acoustic dissipation above 10.5 kHz).
- [ ] **Soundboard Compliance Bloom / Soft Nonlinearity** (Wood cavity acoustic headroom compression under heavy strumming).

---

## 3. Electric Guitar Modeling Mode

- [ ] **Magnetic Pickup Physical Transducer (Comb Filtering & Magnetic Aperture)**:
  - String velocity integration ($v(x, t) = \partial u / \partial t$) evaluated at pickup coil positions:
    - **Neck Pickup** (~$x/L = 0.80\text{--}0.85$) -> Deep, round, warm fundamental.
    - **Middle Pickup** (~$x/L = 0.88$) -> Strat-style scoop / quack.
    - **Bridge Pickup** (~$x/L = 0.94\text{--}0.96$) -> Bright, biting, high-harmonic bite.
  - Pickup selector switch: Neck, Neck+Middle, Middle, Middle+Bridge, Bridge.
- [ ] **Magnetic String Damping & String Material Profile**:
  - Nickel/Steel string density, tension, and higher Young's modulus ($E \approx 200\text{ GPa}$ vs. nylon $5\text{ GPa}$).
  - Magnetic pull damping from pole-piece magnets.
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
- [ ] **Legato Hammer-On & Pull-Off (HOPO)**:
  - Triggering a new MIDI note on an already-ringing string without re-plucking: updates string length/fret position $L$ and introduces a small fingertip fret-collision transient.
- [ ] **Continuous Pitch Slide / Glissando**:
  - Smooth fretboard glissando across fret obstacles.
- [ ] **True Finger Vibrato (Pitch & Tension Modulation)**:
  - Lateral finger rocking vs. axial classical vibrato with tension modulation.
- [ ] **Flamenco Rasgueado / Multi-Finger Strumming**:
  - Rapid 4-finger fan strum articulation.
- [ ] **Artificial & Natural Harmonics**:
  - Nodal touch at $1/2$ (12th fret), $1/3$ (7th fret), $1/4$ (5th fret) damping fundamental and ringing pure harmonics.

