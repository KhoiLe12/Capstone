# Hybrid Physical Modeling Synthesizer

An advanced acoustic & electric guitar physical modeling synthesizer implemented as a **VST3, Audio Unit (AU), and Standalone** application using the **JUCE** framework (C++17) and CMake.

All audio is synthesized in real time from mechanical and acoustic first principles—**no static samples or wavetables**.

---

## 🎵 Physical Architecture

The synthesizer combines an energy-conserving **Multi-Port Waveguide Network** with an **IRCAM Modalys-style 32-mode parallel soundboard**:

```
MIDI In ──► SynthEngine (6 Physical String Voices)
                  │
        ┌─────────┴─────────────────────────────────────────────┐
        │  Each Voice:                                          │
        │  1. Plectrum Exciter (Velocity-dependent 1-3ms pulse) │
        │  2. Waveguide with Allpass Pitch Tuning               │
        │  3. Stiffness Dispersion Allpass (Inharmonicity)      │
        │  4. Dynamic Tension Modulation (Pluck Pitch Settle)   │
        └─────────┬─────────────────────────────────────────────┘
                  │  Downward string forces
                  ▼
        ┌───────────────────────────────────────────────────────┐
        │  Multi-Port Bridge Scattering Junction                │
        │  - Computes mutual string forces at the bridge saddle │
        │  - Scatters energy back into open strings             │
        │    (True Physical Sympathetic Resonance)              │
        └─────────┬─────────────────────────────────────────────┘
                  │  Bridge velocity
                  ▼
        ┌───────────────────────────────────────────────────────┐
        │  IRCAM Modalys 32-Mode Parallel Soundboard            │
        │  - Helmholtz air mode (A0 @ 105 Hz)                   │
        │  - Top-plate dipoles (T(1,1) @ 185 Hz & 225 Hz)       │
        │  - Longitudinal wood modes & presence formants        │
        │  - Continuous Solid-Body Electric ◄► Acoustic Morph   │
        └─────────┬─────────────────────────────────────────────┘
                  ▼
       Master Gain & Stereo Audio Out
```

---

## 🧩 DSP Modules

| Module | File | Physical Description |
|---|---|---|
| `BiquadFilter` | `src/dsp/BiquadFilter.*` | Direct Form I second-order IIR; provides peaking EQ and bandpass modal resonator design. |
| `Exciter` | `src/dsp/Exciter.*` | Velocity-dependent finite contact pulse (1–3 ms), winding micro-friction texture, pick-position comb filter ($H(z) = 1 - z^{-M}$), and brightness LPF. |
| `KarplusStrong` | `src/dsp/KarplusStrong.*` | 1D digital waveguide with sub-sample allpass tuning, 1st-order stiffness dispersion filter ($f_n \approx n f_0 \sqrt{1 + B n^2}$), dynamic tension modulation, and bridge injection. |
| `Voice` | `src/dsp/Voice.*` | Encapsulates a single string (Exciter + Waveguide) with zero-allocation audio-thread scratch buffers. |
| `BodyResonance` | `src/dsp/BodyResonance.*` | 32-mode parallel acoustic soundboard model based on empirical luthier laser-vibrometry measurements. Supports modal scaling (`bodySize`) and electric/acoustic coupling (`bodyMix`). |
| `SynthEngine` | `src/dsp/SynthEngine.*` | 6-voice polyphonic orchestrator with an energy-conserving Multi-Port Bridge Scattering Junction (sympathetic resonance) and leaky RMS energy voice-stealing. |

---

## 🎛️ Parameters

| Knob | Range | Default | Physical Effect |
|---|---|---|---|
| **Decay** | 0.0 – 1.0 | 0.80 | String sustain (waveguide loop gain). |
| **Brightness** | 0.0 – 1.0 | 0.50 | Spectral hardness of the plectrum contact pulse (1.5 kHz – 20 kHz). |
| **Pick Pos** | 0.05 – 0.50 | 0.12 | Plectrum contact point along the string fraction (bridge vs. 12th fret). |
| **Stiffness** | 0.0 – 1.0 | 0.25 | String inharmonicity / dispersion (0 = pure nylon, 1 = metallic steel twang). |
| **Body Size** | 0.6 – 1.6 | 1.00 | Soundboard modal frequency scale (parlor $\leftrightarrow$ dreadnought $\leftrightarrow$ jumbo). |
| **Body Coupl** | 0.0 – 1.0 | 0.70 | Body coupling: 0.0 = solid-body electric (maximum sustain); 1.0 = acoustic soundboard bloom. |
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
