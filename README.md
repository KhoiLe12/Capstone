# Hybrid Physical Modeling Synthesizer

An advanced acoustic & electric guitar physical modeling synthesizer implemented as a **VST3, Audio Unit (AU), and Standalone** application using the **JUCE** framework (C++17) and CMake.

All audio is synthesized in real time from mechanical and acoustic first principles—**no static samples or wavetables**.

---

## 🎵 Physical Architecture

The synthesizer combines a **Physical 6-String Fretboard Matrix**, a **Mechanical Strumming Engine**, an energy-conserving **Multi-Port Waveguide Network**, **Virtual Magnetic Pickups**, and an **IRCAM Modalys-style 32-mode parallel soundboard**:

```
MIDI In ──► Strummer Engine (Time-staggered pick sweep & down/upstroke dynamics)
                  │
                  ▼
            Fretboard Matrix (6 strings: EADGBE x 22 frets, ergonomic hand solver)
                  │  Trigger per physical string
                  ▼
        ┌───────────────────────────────────────────────────────┐
        │  Each Physical String (0..5):                         │
        │  1. Plectrum Exciter (Velocity-dependent 1-3ms pulse) │
        │  2. Waveguide with Allpass Pitch Tuning               │
        │  3. Stiffness Dispersion Allpass (Inharmonicity)      │
        │  4. Dynamic Tension Modulation (Pluck Pitch Settle)   │
        └─────────┬─────────────────────────────────────────────┘
                  │  Downward string forces
                  ▼
        ┌───────────────────────────────────────────────────────┐
        │  Multi-Port Bridge Scattering Junction                │
        │  - Mutual physical string coupling at the bridge      │
        │  - True Physical Sympathetic Resonance                │
        └─────────┬─────────────────────────────────────────────┘
                  │
        ┌─────────┴─────────────────────────────────────────────┐
        ▼                                                       ▼
┌──────────────────────────────┐        ┌──────────────────────────────┐
│  Virtual Magnetic Pickups    │        │  IRCAM Modalys 32-Mode Body  │
│  - Spatial comb filter       │        │  - Helmholtz A0 (105 Hz)     │
│    (Bridge vs. Neck position)│        │  - Plate dipoles & formants  │
│  - Passive RLC Tone Circuit  │        │  - Continuous Acoustic Morph │
└──────────────┬───────────────┘        └──────────────┬───────────────┘
               │                                       │
               └───────────────────┬───────────────────┘
                                   ▼
                    Master Gain & Stereo Audio Out
```

---

## 🧩 DSP Modules

| Module | File | Physical Description |
|---|---|---|
| `Fretboard` | `src/dsp/Fretboard.*` | Models the physical 6-string neck ($E_2, A_2, D_3, G_3, B_3, E_4$) $\times$ 22 frets. Enforces physical 1-note-per-string monophony and ergonomic chord fingering. |
| `Strummer` | `src/dsp/Strummer.*` | Staggers chord plucks across physical strings (10–35 ms sweep) with alternating downstroke/upstroke dynamics. |
| `PickupModel` | `src/dsp/PickupModel.*` | Models virtual electric guitar magnetic pickups (spatial comb $H(\omega) = \sin(\omega d / c)$ from Bridge to Neck) and passive 1-pole RLC guitar tone circuit. |
| `KarplusStrong` | `src/dsp/KarplusStrong.*` | 1D digital waveguide with sub-sample allpass tuning, 1st-order stiffness dispersion filter ($f_n \approx n f_0 \sqrt{1 + B n^2}$), dynamic tension modulation, and bridge injection. |
| `Exciter` | `src/dsp/Exciter.*` | Velocity-dependent finite contact pulse (1–3 ms), winding micro-friction texture, pick-position comb filter, and brightness LPF. |
| `Voice` | `src/dsp/Voice.*` | Encapsulates a single string (Exciter + Waveguide) with zero-allocation audio-thread scratch buffers. |
| `BodyResonance` | `src/dsp/BodyResonance.*` | 32-mode parallel acoustic soundboard model based on empirical luthier laser-vibrometry measurements. Supports modal scaling (`bodySize`) and electric/acoustic coupling (`bodyMix`). |
| `SynthEngine` | `src/dsp/SynthEngine.*` | Orchestrates fretboard note allocation, strumming delays, multi-port bridge scattering (sympathetic resonance), pickup tone, and modal soundboard. |
| `BiquadFilter` | `src/dsp/BiquadFilter.*` | Direct Form I second-order IIR; provides peaking EQ and bandpass modal resonator design. |

---

## 🎛️ Parameters

The user interface is organized into a clean 2-row layout (640 × 290 px):

### Row 1: String & Strum
| Knob | Range | Default | Physical Effect |
|---|---|---|---|
| **Decay** | 0.0 – 1.0 | 0.80 | String sustain (waveguide loop gain). |
| **Brightness** | 0.0 – 1.0 | 0.50 | Spectral hardness of the plectrum contact pulse (1.5 kHz – 20 kHz). |
| **Pick Pos** | 0.05 – 0.50 | 0.12 | Plectrum contact point along the string fraction (bridge vs. 12th fret). |
| **Stiffness** | 0.0 – 1.0 | 0.25 | String inharmonicity / dispersion (0 = pure nylon, 1 = metallic steel twang). |
| **Strum** | 0.0 – 1.0 | 0.20 | Pick sweep timing (0 = instant keyboard chord, 1 = slow acoustic rake). |

### Row 2: Electronics & Body
| Knob | Range | Default | Physical Effect |
|---|---|---|---|
| **Pickup Pos** | 0.0 – 1.0 | 0.35 | Virtual pickup placement: 0.0 = biting Bridge pickup; 1.0 = warm, hollow Neck pickup. |
| **Tone** | 0.0 – 1.0 | 0.85 | Passive guitar tone capacitor (700 Hz rolled off $\leftrightarrow$ 18 kHz wide open). |
| **Body Size** | 0.6 – 1.6 | 1.00 | Soundboard modal frequency scale (parlor $\leftrightarrow$ dreadnought $\leftrightarrow$ jumbo). |
| **Body Coupl** | 0.0 – 1.0 | 0.70 | Body coupling: 0.0 = solid-body electric (max sustain); 1.0 = acoustic soundboard bloom. |
| **Gain** | 0.0 – 1.0 | 0.80 | Master output level. |

---

## 🛠️ Building

### Prerequisites
- **CMake** $\ge$ 3.22
- **C++17 Compiler**: Apple Clang (macOS) or Visual Studio 2022 (Windows)
- **Ninja** (recommended for fast builds)
- **Git** (JUCE is tracked as a submodule)

### 1. Initialize Submodules
```bash
git submodule update --init --recursive
```

### 2. Configure & Build

#### On macOS (Apple Silicon / Intel):
```bash
# Configure with Ninja in Release mode
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build all targets (Standalone, VST3, AU)
cmake --build build --config Release
```

#### On Windows:
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

---

## 📦 Output Artifacts & Installation

Built artifacts are located in `build/HybridSynth_artefacts/Release/`:
- **Standalone App**: `Standalone/Hybrid Synth.app`
  - Launch directly with: `open "build/HybridSynth_artefacts/Release/Standalone/Hybrid Synth.app"`
- **VST3 Plugin**: `VST3/Hybrid Synth.vst3`
- **Audio Unit (AU)**: `AU/Hybrid Synth.component`

### Installing to System Folders (macOS)
To make the plugins available across all DAWs (FL Studio, Ableton Live, Logic Pro, Reaper):
```bash
sudo cp -R "build/HybridSynth_artefacts/Release/VST3/Hybrid Synth.vst3" /Library/Audio/Plug-Ins/VST3/
sudo cp -R "build/HybridSynth_artefacts/Release/AU/Hybrid Synth.component" /Library/Audio/Plug-Ins/Components/
```
In your DAW, open the Plugin Manager and click **Find installed plugins / Rescan**.
