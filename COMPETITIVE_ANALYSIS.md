# 🎸 Hybrid Physical Modeling Guitar VST — Market Competitive Analysis & Feature Roadmap

---

## 1. Executive Summary & Market Positioning

This project is an **all-acoustic physical modeling synthesizer** implementing both a **Dual-Polarization Digital Waveguide (DWG)** and a state-of-the-art **Bilbao Finite-Difference Time-Domain (FDTD)** engine with real-time soundboard impulse response (IR) convolution and 32-mode modal resonance.

### Who Are Your Competitors?
The market divides into two distinct categories:

1. **Physical Modeling Guitars (Direct Algorithmic Competitors)**:
   * **Applied Acoustics Systems (AAS) Strum GS-2**: The commercial industry benchmark for physical modeling acoustic and electric guitar.
   * **Modartt Pianoteq (Guitars/Harps add-ons)**: Renowned for pristine mechanical string/soundboard simulation and physical sympathetic resonance.

2. **Flagship Sample & Scripting Engines (Benchmark for Articulation & Workflow)**:
   * **Ample Sound (Ample Guitar AGM Martin, AGNL Nylon, AGL Luthier)**: The industry leader for playable acoustic guitar realism, legato logic, and customizable strummer.
   * **Native Instruments Session Guitarist (Picked Acoustic, Strummed Acoustic 2, Electric Sunburst)**: Benchmark for tempo-synced rhythm loops and sound design.
   * **MusicLab RealGuitar 5**: Benchmark for keyboard-to-guitar voicing translation and multi-velocity multi-fret logic.

---

## 2. Comprehensive Feature Matrix: Current State vs. Market Leaders

| Feature Domain | Feature Detail | **Our VST (Current v29.f)** | **AAS Strum GS-2** | **Ample Guitar (AGM/AGNL)** | **NI Session Guitarist** |
| :--- | :--- | :---: | :---: | :---: | :---: |
| **Core DSP** | **Physical Modeling (Zero Samples)** | **YES** (DWG + FDTD) | **YES** | NO (Sampled) | NO (Sampled) |
| | Dual Engine Switch (Waveguide vs. FDTD PDE) | **YES** | NO | NO | NO |
| | Soundboard Impulse Response (IR) Convolution | **YES** (Nylon & Gibson) | NO (Modal only) | NO | YES |
| | Modal Resonator Bank (IRCAM Modalys 32-mode) | **YES** | YES | NO | NO |
| | Pure Real-Time Synthesis (Instant load, <20 MB) | **YES** | YES | NO (10+ GB) | NO (8+ GB) |
| **Articulations** | Open Plucks | **YES** | YES | YES | YES |
| | Palm Muting (Hold Keyswitch `C1`) | **YES** | YES | YES | YES |
| | Full Mute / Choke / Fret Slap (`D1`) | **YES** | YES | YES | YES |
| | **Hammer-Ons & Pull-Offs (Legato)** | ❌ **Missing** | YES | YES | YES |
| | **Legato Slides (Fret Portamento)** | ❌ **Missing** | YES | YES | YES |
| | **Natural Harmonics (Bell tones)** | ❌ **Missing** | YES | YES | YES |
| | **Pitch Bend (Whammy / Fret Bend)** | ❌ **Missing** | YES | YES | YES |
| | **Mod Wheel Vibrato (`MIDI CC1`)** | ❌ **Missing** | YES | YES | YES |
| **Strumming & Rhythm** | Alternating Down/Up Rake | **YES** (8th-note `E1`) | YES | YES | YES |
| | Inter-Chord Bleed Damping | **YES** (v29.f) | YES | YES | YES |
| | **Variable Strum Speed / Rake Knob** | ❌ **Missing** | YES | YES | YES |
| | **Manual Strum Keys (Bass, Treble, Full)** | ❌ **Missing** | YES | YES | NO |
| | **Automatic Chord Voicing Recognition** | ❌ **Missing** | YES | YES | YES |
| | **Multi-Pattern Step Sequencer / Presets** | ❌ **Missing** | YES | YES | YES |
| **Acoustic Nuance** | Pick Position Adjustment | **YES** (0.05 – 0.50) | YES | NO | NO |
| | String Stiffness / Dispersion Control | **YES** (0.0 – 1.0) | YES | NO | NO |
| | Soundboard Size / Body Coupling Morph | **YES** | YES | NO | NO |
| | Stereo Width Control (M/S Spatializer) | **YES** | YES | YES | YES |
| | **Fret / Finger Release Scrape Noises** | ❌ **Missing** | YES | YES | YES |
| **Guitarist Tools** | 6-String Physical Pitch & Choking Logic | **YES** | YES | YES | YES |
| | **Capo Selector (Frets 1 – 7)** | ❌ **Missing** | YES | YES | NO |
| | **Alternate Tunings (Drop D, DADGAD, etc.)** | ❌ **Missing** | YES | YES | NO |
| **UI & UX** | Real-Time Rotary Knobs + LED Badges | **YES** | YES | YES | YES |
| | **Animated 6-String Interactive Fretboard** | ❌ **Missing** | YES | YES | NO |
| | **Factory Preset Management (Browser)** | ❌ **Missing** | YES | YES | YES |

---

## 3. Detailed Breakdown of Missing Features

### Domain A: Expressive Performance Articulations (Highest Musical Impact)

1. **Hammer-On & Pull-Off (Legato Engine)**:
   * *What it does*: When playing monophonically and holding note A while striking note B on the same string within a tight time window (e.g. legato interval $\le$ 4 semitones), note B does not re-trigger the exciter fingernail pluck. Instead, the vibrating string is instantly re-tuned to the new frequency while preserving existing kinetic energy, accompanied by a subtle fret-strike transient.
   * *Why it's crucial*: Makes solo melodies and rapid arpeggios sound authentic instead of re-plucking every single note like a typewriter.

2. **Mod Wheel Vibrato (`MIDI CC1`)**:
   * *What it does*: Maps `CC1` to an asymmetric low-frequency pitch modulation (4.5 – 6.0 Hz) with subtle high-frequency loss on the inward roll, replicating the mechanical rock of a fretting finger behind the fret wire.
   * *Why it's crucial*: Essential for expressive singing acoustic lines.

3. **Pitch Bend**:
   * *What it does*: Standard $\pm 2$ semitone pitch wheel support with upward string-tension curve.

4. **Natural Harmonics (Keyswitch or High Velocity)**:
   * *What it does*: Dampens the fundamental and even harmonics while amplifying the nodal nodes (octave @ fret 12, octave+fifth @ fret 7, double octave @ fret 5), producing crystalline chime tones.

---

### Domain B: Strumming & Rhythm Engine (Usability Impact)

1. **Strum Rake Speed Knob**:
   * *What it does*: Continuous control over the pick rake duration (e.g. from an instant 3 ms flamenco snap to a loose, relaxed 35 ms ballad sweep).
   * *Why it's crucial*: Different song tempos and genres require drastically different strumming tightness.

2. **Ample-Guitar Style Manual Strum Keys (Trigger Bank)**:
   * *What it does*: Instead of auto-repeating on every 8th note, the left hand holds any chord shape, while dedicated low keys trigger specific right-hand pick actions:
     * `C1`: Downstroke (all 6 strings)
     * `D1`: Upstroke (top 3–4 strings)
     * `E1`: Bass pluck (lowest root note string with thumb)
     * `F1`: Muted percussive chunk / dead note slap
   * *Why it's crucial*: Gives keyboard players total live expressive rhythm flexibility without being locked to a static metronome rhythm.

3. **Automatic Chord Recognition & Fretboard Voicing**:
   * *What it does*: Detects keyboard chords (e.g., standard piano triad `C - E - G`) and maps them to an authentic 6-string open guitar voicing (`x - 3 - 2 - 0 - 1 - 0` spanning `C3, E3, G3, C4, E4`).

---

### Domain C: Organic Mechanical Noise & Atmosphere

1. **Fret Lift & Release Noise Engine**:
   * *What it does*: Plays faint, randomized mechanical noises on key release (finger lifting off wound nylon or bronze strings).
   * *Why it's crucial*: In blind tests, human listeners identify digital synthesizers by the *absence* of background mechanical friction.

---

### Domain D: Visual & GUI Elements

1. **Animated 6-String Fretboard Display**:
   * *What it does*: A visual fretboard across the top or middle of the UI showing real-time string vibration and active finger fret markers.
   * *Why it's crucial*: Looks ultra-professional and provides immediate visual feedback on voicing and choking.

2. **Preset Browser**:
   * *What it does*: Curated sound presets showcasing the engine's versatility:
     * *Spanish Flamenco (Bright, short decay, bridge pick)*
     * *Classical Nylon Concert (Warm, deep IR, neck pick)*
     * *Gibson Dreadnought Strummer (Stereo width, steel stiffness)*
     * *Fingerstyle Ballad (Loose rake, singing sustain)*
     * *Percussive Lo-Fi Mute (Heavy palm damping, wood thud)*

---

## 4. Strategic Capstone Roadmap: Recommended Phases

```
┌────────────────────────────────────────────────────────┐
│  Current State: v29.f (FDTD + IR Body + 8th Strum)    │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│  Phase 1: Performance Nuance (Quick Wins)              │
│  - Mod Wheel Vibrato (CC1)                             │
│  - Strum Rake Speed Knob                               │
│  - Hammer-On / Pull-Off Legato Engine                  │
│  - Pitch Bend (+/- 2 Semitones)                        │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│  Phase 2: Rhythm & Voicing Mastery                     │
│  - Manual Strum Trigger Keys (Bass, Treble, Full)      │
│  - Auto-Voicing Engine (Piano Triad -> 6-String Guitar)│
│  - Bass + Strum Alternation Mode                       │
└──────────────────────────┬─────────────────────────────┘
                           │
                           ▼
┌────────────────────────────────────────────────────────┐
│  Phase 3: Visual Polish & Capstone Defense             │
│  - Animated 6-String Fretboard UI Component            │
│  - Factory Preset System (Classical, Flamenco, Folk)   │
│  - Comprehensive Capstone Documentation & Demo Clips   │
└────────────────────────────────────────────────────────┘
```

---

## 5. Summary Evaluation for Your Capstone Defense

* **Your Core Innovation**: Unlike 95% of commercial guitar plugins that rely on gigabytes of pre-recorded sample libraries, your project computes real-time partial differential equations (FDTD) and digital waveguides from scratch. That is an enormous academic achievement for a capstone.
* **The Key to Outperforming Competitors**: Commercial sample libraries sound good because of recorded human imperfection (slides, hammer-ons, hand noise, dynamic strums). By layering **Hammer-On Legato**, **Strum Speed control**, and **Mod Wheel Vibrato** onto your physical model, you bridge the gap between academic simulation and commercially viable music production.
