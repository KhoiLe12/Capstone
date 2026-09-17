# Hybrid Physical Modeling Synthesizer

An acoustic guitar physical modeling synthesizer implemented as a JUCE VST3 plugin.
No static samples — all audio is generated in real-time from first principles.

## Architecture

```
MIDI → SynthEngine (6 voices) → BodyResonance → Audio Out
              │
       Each voice:
         Exciter → KarplusStrong waveguide
```

### DSP Modules
| Module | File | Description |
|---|---|---|
| `BiquadFilter` | `src/dsp/BiquadFilter.*` | Direct Form I IIR, peaking resonator design |
| `Exciter` | `src/dsp/Exciter.*` | xorshift32 noise, pick-position comb, brightness LPF |
| `KarplusStrong` | `src/dsp/KarplusStrong.*` | Delay line + averaging LPF loop + allpass interpolation |
| `Voice` | `src/dsp/Voice.*` | Wraps Exciter + KarplusStrong per string |
| `BodyResonance` | `src/dsp/BodyResonance.*` | 8-mode series biquad chain (guitar body modes) |
| `SynthEngine` | `src/dsp/SynthEngine.*` | 6-voice orchestrator, voice stealing, audio mix |

## Building

### Prerequisites
- CMake ≥ 3.22
- Visual Studio 2022 with "Desktop development with C++" workload
- Git (JUCE is a submodule)

### Steps

```powershell
# Clone and initialise submodules (if fresh clone)
git submodule update --init --recursive

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Release
cmake --build build --config Release

# The VST3 is automatically copied to your system VST3 folder
```

## Parameters

| Knob | Range | Effect |
|---|---|---|
| Decay | 0–1 | String sustain (loop gain) |
| Brightness | 0–1 | Spectral content of exciter |
| Pick Pos | 0.05–0.5 | Plectrum contact point (fraction of string) |
| Body Mix | 0–1 | Guitar body resonance wet/dry |
| Master Gain | 0–1 | Output level |

