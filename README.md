# Hybrid Physical Modeling Synthesizer

A physically modeled acoustic guitar synthesizer built with **JUCE** (C++17) and CMake. Ships as **VST3**, **AU** (macOS), **LV2** (Linux), and **Standalone**.

All sound is computed in real time from a simulation of the strings, bridge, and guitar body — **no samples or wavetables**.

> **Current version:** v67.f · Detailed engineering docs live in the [LLM & Developer Wiki](docs/wiki/README.md).

---

## Signal Flow

```
MIDI In ──► SynthEngine (sample-accurate) ── keyswitches (palm mute / choke / strum) ── legato ── 6-string allocator
                 │
                 ▼
   6 String Voices (E2 A2 D3 G3 B3 E4), each:
     Plectrum / fingernail exciter
       └─► String engine (selectable):
             0. Digital Waveguide — extended Karplus-Strong
             1. Bilbao FDTD       — stiff-string PDE with fret/finger collisions
                 │
                 │  bridge forces (V + 0.35·H)      airborne pick click
                 ▼                                         │
   Bridge sum + sympathetic open-string resonators         │
                 ▼                                         │
   Polyphonic headroom scaling (15 ms slew)                │
                 ▼                                         │
   Mirrored soundboard reflections (stereo width)          │
                 ▼                                         │
   Body: Classical Nylon IR │ Gibson Acoustic IR │ 32-mode modal bank
                 ▼                                         │
                 └──────────────── sum ◄───────────────────┘
                                    ▼
                DC blocker ► master gain ► soft limiter ► Stereo Out
```

See [01 — Architecture Overview](docs/wiki/01_architecture_overview.md) for the full pipeline.

---

## String Engines

| Engine | Model | Character | Cost (6 voices, 48 kHz) |
|---|---|---|---|
| **Digital Waveguide** | Dual-polarization delay-line Karplus-Strong with allpass fractional tuning, stiffness dispersion allpass, and frequency-dependent loop loss. | Clean, glassy, very cheap. | ~10% at 32-sample blocks, ~2.6% at 128 |
| **Bilbao FDTD** | Finite-difference solution of the stiff damped string PDE (Bilbao et al., *Real-Time Guitar Synthesis*, DAFx24). Dual polarization (natural beating), Kirchhoff-Carrier pitch glide, fret / fretboard / finger collisions solved non-iteratively via a low-rank Woodbury system, phantom partials and longitudinal attack snap. | Rich, physical attack and fret interaction. | ~26% at 32-sample blocks, ~19% at 128 |

Strings can be **Nylon Classical** or **Steel Acoustic**, which switches density, Young's modulus, tension, and damping profiles. CPU figures are `bench_synth` means on an Apple M4 and include the body convolution.

Measured tuning (`render_audio`, one note per pitch):
- **FDTD** (v64.f): within ±1.3 cents from E2 to G5.
- **Waveguide** (v67.f): sustain within ±0.05 cents from E2 to C6. In the first ~0.5 s, low notes read slightly flat (−2.3 cents at E2, −0.4 at E4) while the detuned vertical plane is still strong.

See the known limitations in [wiki 02](docs/wiki/02_dsp_bilbao_fdtd.md) and [wiki 03](docs/wiki/03_dsp_waveguide.md).

Details: [02 — Bilbao FDTD](docs/wiki/02_dsp_bilbao_fdtd.md) · [03 — Waveguide](docs/wiki/03_dsp_waveguide.md)

---

## Body & Bridge

- **Sympathetic resonance** — a bank of resonators tuned to the six open strings (fundamental + 2 overtones each) is driven by the summed bridge force. Strings that are currently played are excluded, and coupling is reduced under muting.
- **Body models**
  - *Classical Nylon (IR)* and *Gibson Acoustic (IR)* — embedded stereo impulse responses run through zero-latency partitioned convolution (`juce::dsp::Convolution`).
  - *Modal Bank (32-Mode)* — parallel second-order resonators; **Body Size** shifts the mode frequencies (parlor ↔ jumbo).
- **Body Coupling** blends the dry bridge signal (solid-body / DI) with the radiated body.
- **Airborne pick click** — the plectrum snap is added *after* the body so it is not low-passed by the wood.

Details: [04 — Body & Bridge Acoustics](docs/wiki/04_body_and_bridge_acoustics.md)

---

## Articulations & Keyswitches

Keyswitches sit below the guitar range so they never sound. (FL Studio and Yamaha label these one octave higher: C2 / D2 / E2.)

| Key | MIDI | Behavior |
|---|---|---|
| **C1** | 24 | **Palm mute** while held — highs die fast, fundamental chugs for ~75–110 ms. |
| **D1** | 26 | **Hand choke** on press (ringing strings stop within ~10 ms with a slap). While held, new notes play as pitchless **dead "X" notes**. |
| **E1** | 28 | **8th-note auto-strum** while held, synced to host tempo, alternating down/up strokes with a ~2–2.5 ms rake between strings. |

Each keyswitch also has an equivalent toggle parameter in the GUI / DAW. Playable range is **D2–D6** (MIDI 38–86, Drop-D to 22nd fret); other notes are ignored.

Notes are assigned to physical strings like a real guitar: each string is monophonic. Single notes go to their first-position string, or to the nearest idle string within 22 frets. Simultaneous notes (chords) get a joint best-fit assignment; **Strict Fretboard** controls whether physically impossible voicings drop notes.

Details: [05 — Articulations & Keyswitches](docs/wiki/05_articulations_and_keyswitches.md)

---

## Parameters

Defaults match factory preset 1, *Dreadnought Acoustic*, which is loaded on first launch. Eight factory presets are available from the header dropdown and the host's program list.

| Parameter | Range | Default | Effect |
|---|---|---|---|
| **Decay** | 0.0 – 1.0 | 0.78 | String sustain (also shapes palm-mute length). |
| **Brightness** | 0.0 – 1.0 | 0.72 | Hardness of the pick / nail contact. |
| **Pick Position** | 0.05 – 0.50 | 0.22 | Pluck point as a fraction of string length (bridge ↔ 12th fret). |
| **Stiffness** | 0.0 – 1.0 | 0.28 | Inharmonicity / dispersion. |
| **Body Size** | 0.6 – 1.6 | 1.15 | Modal frequency scale (Modal Bank body only). |
| **Body Coupling** | 0.0 – 1.0 | 0.80 | 0 = dry bridge (solid-body), 1 = full acoustic body. |
| **Stereo Width** | 0.0 – 1.0 | 0.75 | Soundboard reflection width (0 = mono). |
| **Master Gain** | 0.0 – 1.0 | 0.80 | Output level. |
| **Engine Model** | choice | Bilbao FDTD | Digital Waveguide (DWG) / Bilbao FDTD (DAFx24). |
| **String Material** | choice | Steel Acoustic | Nylon Classical / Steel Acoustic. |
| **Body Model** | choice | Gibson Acoustic (IR) | Classical Nylon (IR) / Gibson Acoustic (IR) / Modal Bank (32-Mode). Switching crossfades over 20 ms. |
| **Palm Mute** | toggle | off | Same as holding C1. |
| **Full Mute** | toggle | off | Same as holding D1. |
| **Strum 8th** | toggle | off | Same as holding E1. |
| **Strict Fretboard** | toggle | off | On: chord notes with no real string/fret position (22 frets) are dropped. Off: they play on a free string past the fretboard, so up to 6 notes always sound. |
| **Legato (HO/PO)** | toggle | on | Overlapping notes on one string play as hammer-ons / pull-offs. Notes within 40 ms of each other always play as a chord. |

---

## Building

### Prerequisites
- CMake ≥ 3.22, Ninja, Git
- C++17 compiler: Apple Clang (macOS), MSVC `cl.exe` (Windows), Clang or GCC (Linux)
- Linux only: ALSA, JACK, FreeType, OpenGL and X11 development packages (see the [CI workflow](.github/workflows/build_and_validate.yml) for the exact list)

### 1. Fetch JUCE
```bash
git submodule update --init --recursive
```

### 2. Configure & build

**macOS / Linux**
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Windows** — use MSVC with Ninja from a Developer environment. Do not let CMake pick MinGW/MSYS2 GCC; mixing runtimes breaks the ABI.
```cmd
cmd /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" && cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl && cmake --build build --config Release'
```

x86_64 builds compile with AVX2 (`/arch:AVX2 /fp:fast` on MSVC, `-mavx2 -mfma` on GCC/Clang).

### Build outputs

| Target | Output |
|---|---|
| Plugin | `build/HybridSynth_artefacts/Release/{VST3,AU,LV2,Standalone}/` (formats depend on platform) |
| `bench_synth` | `build/bench_synth` — real-time safety & latency benchmark |
| `render_audio` | `build/render_audio` — headless offline renderer to WAV |

### Installing on macOS
```bash
sudo cp -R "build/HybridSynth_artefacts/Release/VST3/Hybrid Synth.vst3" /Library/Audio/Plug-Ins/VST3/
sudo cp -R "build/HybridSynth_artefacts/Release/AU/Hybrid Synth.component" /Library/Audio/Plug-Ins/Components/
```
Then rescan plugins in your DAW. The standalone app can be launched with
`open "build/HybridSynth_artefacts/Release/Standalone/Hybrid Synth.app"`.

---

## Testing & Validation

| Check | Command | Pass criterion |
|---|---|---|
| Real-time safety & latency | `./build/bench_synth` (`--events-only` for the quick checks) | `[RTCheck Result] PASS (0)`: zero heap allocations on the audio thread, in steady-state sweeps and in an event stress test (note-ons, legato, keyswitches, engine/body switches) at 44.1–192 kHz. `[Timing Result] PASS`: sample-accurate MIDI and chord-vs-legato detection. 1/4/6-voice loads within deadline at 32–512 sample blocks. |
| Plugin conformance | `cmake --build build --config Release --target validate` | `pluginval` strictness 8 returns `SUCCESS`. pluginval is not committed: download it from [Tracktion's releases](https://github.com/Tracktion/pluginval/releases) into `tools/pluginval/` (macOS: `tools/pluginval/pluginval.app`) or put it on `PATH`. |
| Spectral comparison | `.venv/bin/python benchmarks/guitarset_eval.py [--plots]` | Multi-scale STFT distance vs. GuitarSet recordings, with real-take and naive-tone reference points. Not a realism score; see [wiki 06 §5](docs/wiki/06_realtime_safety_and_qa.md) |

Offline rendering example:
```bash
./build/render_audio --engine 1 --chord 40,47,52,55,59,64 --vel 0.85 --bodyType 1 --dur 4.0 --out e_major.wav
```

GitHub Actions builds and runs `bench_synth` on Linux, macOS, and Windows for every push and PR. See [06 — Real-Time Safety & QA](docs/wiki/06_realtime_safety_and_qa.md) and [BENCHMARKS.md](BENCHMARKS.md).

---

## Repository Layout

```
src/
  dsp/       String engines, exciter, voice, sympathetic & body resonance, SynthEngine
  plugin/    JUCE AudioProcessor (parameters, MIDI) and editor (GUI)
  bench/     bench_synth and render_audio tools
benchmarks/  GuitarSet spectral evaluation (guitarset_eval.py); legacy spectral_eval.py
resources/   Source body impulse responses (embedded via src/dsp/EmbeddedIRs.h)
docs/        Wiki and theoretical background
JUCE/        JUCE framework (git submodule)
```

---

## Further Reading

- [Developer Wiki](docs/wiki/README.md) — architecture, DSP math, articulations, QA
- [Release history & roadmap](docs/wiki/07_release_and_milestones.md)
- [Theoretical frameworks & references](docs/THEORETICAL_FRAMEWORKS.md)
- [Feature roadmap](TODO.md) · [Competitive analysis](COMPETITIVE_ANALYSIS.md)
- [AGENTS.md](AGENTS.md) — contributor and AI-agent workflow rules
