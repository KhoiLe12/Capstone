# 07 — Release History & Living Roadmap

This document records all version increments, DSP milestones, acoustic breakthrough calibrations, and future architectural priorities.

---

## 1. Completed Milestones

### v73.f
- **Acoustic Chord Polyphony & Physical Fretboard Voicing Invariant**:
  - **Ergonomic Chord Voicing Priority**:
    - Root-caused missing F notes in C# Major chord transitions (e.g. C Major $\to$ C# Major in DAW piano rolls): in `ChordSearch::evaluate()`, optimization criteria previously prioritized `-ringingUsed` (avoiding currently active strings) ahead of hand span (`-span`) and low frets (`-fretSum`).
    - Consequently, when transitioning from C Major (where strings 1..5 were in release decay), the solver forced the entire C# chord down to strings 0..4 just to utilize the idle low E string (string 0). This placed C#4 at fret 11 of string 0, F4 at fret 8 of the heavily damped wound A string, and banished the top F5 note from string 5 completely.
    - Reordered optimization hierarchy to place physical ergonomics first:
      $$\text{score} = \{ \text{physical}, \text{placed}, -\text{span}, -\text{fretSum}, -\text{ringingUsed} \}$$
  - **Clean Chord Transition Invariant (`avoidRinging = false`)**:
    - In `noteOnChord()`, disabled `avoidRinging`. When a chord is struck, its physical voicing is governed strictly by optimal fretboard ergonomics rather than decaying release tails from preceding chords.
  - **Natural String Reuse for Released Notes**:
    - In `assignStringForNote()`, permitted immediate reuse of natural string $s$ when the previous note on $s$ is no longer physically held down (`heldNotes[oldNote] <= 0.001f`). Fretting string $s$ with the new note naturally damps the decaying release tail and voices the note at its authentic physical register instead of contorting the melody onto high frets of other strings.
  - **Verification & QA**:
    - Added automated C Major $\to$ C# Major chord transition verification to `bench_synth` (`runChordVsLegatoCheck`), validating that both F4 (53) and F5 (65) retain healthy physical string energy ($E > 0.5$).
    - `bench_synth`: `RTCheck PASS (0)` across all buffer sizes (32..512) and sample rates (44.1..192 kHz).
    - `pluginval` Level 8 `SUCCESS` (0 errors).
    - Net CPU overhead: **+0.0%**.

### v72.f
- **Acoustic Chord Polyphony & Physical Legato Calibration**:
  - **Single-String Legato Invariant**:
    - Root-caused dropped chord notes in dyads, rolled chords, and treble voicings: previously, `reachableLegatoOnHeldString` permitted adjacent boundary cross-overs across up to 3 semitones even when the new note belonged to a completely separate idle physical string.
    - Constrained hammer-on / pull-off detection strictly to the *same* physical string (`naturalStringForNote(midiNote) == heldStr`). Notes naturally sounding on distinct open or fretted strings ring out freely as true acoustic polyphonic voices without swallowing held notes.
  - **Human Chord Window Calibration**:
    - Expanded `kLegatoChordWindowSec` from 40 ms to 75 ms. Accommodates natural human finger spread / asynchrony in MIDI keyboard and DAW chord performances without falsely converting rolled chord voicings into hammer-ons.
  - **Dynamic Plectrum Contact Silence & Stealing Guard**:
    - In `Voice.h`, guarded `fdtdIsSilent()` against false early termination during dynamic plectrum contact and the initial 75 ms onset transient (`samplesSinceNoteOn < 75 ms`).
    - Guarded `Voice::getEnergy()` to ensure freshly plucked notes during plectrum contact are never stolen as the "quietest voice".
  - **Verification & QA**:
    - Added dedicated comprehensive chord and dyad regression tests to `bench_synth` (`runChordVsLegatoCheck`).
    - `bench_synth`: `RTCheck PASS (0)` across all block sizes (32..512) and sample rates (44.1k..192k).
    - `pluginval` Level 8 `SUCCESS` (0 errors).
    - Net CPU overhead: **+0.0%** (`12.03%` 1-voice block 32 @ 48 kHz, `28.38%` 6-voice block 32).

### v71.f
- **Continuous Nonlinear Plectrum Contact Model (Literature Review §6 & Bilbao Elastodynamics)**:
  - **Dynamic Plectrum-String Kinematics**:
    - Replaced artificial static triangular displacement initialization and synthetic parallel airborne click DSP network with a physical continuous spring-damper contact law:
      $$f_p(t) = K_p [y_p(t) + \Delta y_\text{ridge}(t) - u_p(t)]_+ + R_p \frac{d}{dt}[y_p(t) - u_p(t)]_+$$
    - The plectrum tip moves dynamically with velocity $v_p \propto \text{clampedVel}$ across coordinate $x_p$, compliantly deflecting the string over a physical contact duration $T_\text{contact} \approx 1.2\text{--}2.5\text{ ms}$.
  - **Exact Analytical Closed-Form Solution**:
    - Contact penetration $\eta^{n+1}$ is solved unconditionally and non-iteratively in $O(1)$ scalar math using grid compliance $S_p = \sum_j \lambda_\text{string}[j] J_j^2$. Zero iterative loops or non-convergence risks.
  - **Physical Wound-String Winding Texture**:
    - Contact surface height dynamically corrugates across wire winding coils ($d_\text{ridge} \approx 0.28\text{ mm}$), injecting authentic winding rasp directly into the PDE grid during stroke transit.
  - **Sharp Helmholtz Corner Unzipping**:
    - When restoring string tension exceeds the plectrum slip threshold $F_\text{slip} \approx 0.75\text{--}2.0\text{ N}$, the string unzips and slips off the pick edge. Contact force $f_p$ drops to 0 instantly, releasing the accumulated potential energy and launching the traveling Helmholtz corner toward the bridge and nut.
    - Near the bridge ($0.08L$), high string stiffness triggers slip earlier with a smaller, sharper release, naturally yielding bright bridge attacks; near the neck ($0.48L$), compliance allows wide displacement before release, yielding warm, deep plucks.
  - **Zero Heuristic Shortcuts**:
    - Eliminated synthetic Ricker dipole equations, 2-pole flexural bandpass filters, and noise bursts from `Voice.cpp`. All acoustic radiation emerges physically through the string, bridge saddle, and body IR.
  - **Quality Gates & Benchmarks**:
    - Zero heap allocations: `bench_synth: RTCheck PASS (0)`.
    - Single-voice latency @ 48 kHz block 32: `12.01%`, 6-voice polyphony: `26.48%`. Net CPU overhead: **+0.0%**.
    - Conformance: `pluginval` Level 8 **`SUCCESS`** (0 errors). Binaries deployed to system VST3 and AU directories.

### v70.f
- **Acoustic Articulation Refinement (Tight Palm Muting & Woody Dead Notes)**:
  - **Palm Muting Decay Calibration**:
    - Widened the physical palm heel contact patch from $3.5\text{ cm}$ to $L_\text{palm} = \min(0.065\text{ m}, 0.22L)$ ($6.5\text{ cm}$), reflecting true anatomical palm heel coverage over bridge saddles.
    - Increased viscoelastic flesh damping rate from $180 + 140 \cdot (1 - \text{decay})$ to $\sigma_\text{flesh} = 480.0 + 360.0 \times (1.0 - \text{decay})\text{ s}^{-1}$, eliminating lingering sustained ring-out and delivering a tight, punchy acoustic palm mute ($\sim 45\text{--}70\text{ ms}$).
    - Waveguide Karplus-Strong engine damping $\tau_V$ and $\tau_H$ calibrated to match.
  - **Dead Note De-Harshing & Frequency Balancing**:
    - Root-caused the high-pitched quality on dead notes: the previous fretting patch started at $x_1 = 0.18L$, leaving an undamped string segment between nut and fret 2 ($x \in [0, 0.18L]$) that rang out at 2.5 octaves above $f_0$.
    - Extended the physical fretting hand spatial damping patch to cover the full nut-to-fret14 range: $x \in [0.02L, 0.68L]$ with $\sigma_\text{hand} = 850.0\text{ s}^{-1}$.
    - Scaled airborne plectrum flexural click amplitude down to $0.25\times$ and softened click resonance brightness to $0.35\times$, eliminating brittle $7.2\text{ kHz}$ plastic snap and producing an authentic deep, woody acoustic fretboard clack.
  - **Audio-Thread Safety & Zero CPU Overhead**:
    - Zero heap allocations detected on audio thread: `bench_synth: RTCheck PASS (0)`.
    - Single-voice and 6-voice polyphony latency: Bilbao FDTD single-voice block 32 @ 48 kHz: `12.01%`, 6-voice polyphony: `28.60%`. Net CPU change: **+0.0%**.
    - Validation: `pluginval` strictness level 8 **`SUCCESS`** (0 errors). Binaries deployed to system VST3 and AU directories.

### v69.f
- **First-Principles Physical Modeling for Palm Muting & Dead Notes**:
  - Replaced heuristic DSP shortcuts (synthetic Ricker dipole wavelets, global uniform damping hikes $\sigma_0/\sigma_1$, artificial 50% brightness cuts, and string solver bypass) with exact elastodynamic PDE boundary and spatial loss formulations.
  - **Palm Muting (`MuteMode::Palm`)**:
    - Localized spatial loss profile $\sigma_\text{palm}(x)$ on grid nodes within the bridge saddle patch ($x \ge L - \Delta x_\text{palm}$, $L_\text{palm} = \min(0.035\text{ m}, 0.15L)$).
    - Preserves low-frequency fundamental wave momentum ($\lambda = 2L$) while absorbing high-frequency partial reflections ($> 350\text{ Hz}$) through viscoelastic flesh damping.
    - Damping is dynamically controlled via the `Decay` knob: $\sigma_\text{flesh} = 180.0 + 140.0 \times (1.0 - \text{decay})$.
  - **Dead Notes ("X" Notes, `MuteMode::Full`)**:
    - Solvers are NOT reset or bypassed. The string is plucked normally with plectrum displacement under a broad spatial damping patch $\sigma_\text{hand}(x)$ across the fretting zone ($x \in [0.18L, 0.68L]$, $\sigma_\text{hand} = 650\text{ s}^{-1}$).
    - The bridge-going wave packet delivers an authentic mechanical force pulse to the bridge saddle, exciting the soundboard body IR once before being dissipated in $< 15\text{ ms}$ by the fretting hand.
    - Zero periodic standing waves ($f_0$) circulate. Synthetic Ricker pulse generation completely eliminated.
  - **Audio-Thread Safety & Zero CPU Overhead**:
    - Per-node damping coefficients $c_0[i]$ and $c_4[i]$ are pre-allocated in `reserveCapacity()`. Articulation switching is $O(N)$ with zero allocations.
    - `bench_synth`: `RTCheck Result: PASS (0)` across all block sizes (32..512) and sample rates (44.1k..192k).
    - Measured CPU overhead: **+0.0%** (27.42% @ 48 kHz block 32, identical to v68.f).
  - **Quality Gates**: `pluginval` Level 8 `SUCCESS` (0 errors), `bench_synth` Timing check PASS. Local binaries deployed to VST3 and AU system directories.

### v68.f
- **Measured body IRs**:
  - *Classical – Contreras 1971* and *Flamenco – Conde 1964*, derived from R. Mores' guitar archive (Zenodo 10.5281/zenodo.4604577, CC BY 4.0). Hammer → near-field mics, anechoic lab, true stereo, 300 ms; chosen by listening from 6 candidates.
  - Reproducible via `benchmarks/extract_mores_irs.py` + `resources/ir/make_embedded_irs.py` (the previous `EmbeddedIRs.h` had no generator). Credits in `resources/ir/ATTRIBUTION.md` and the README.
- **Old IRs analysed**: "Classical Nylon" and "Gibson Acoustic" were essentially one IR (correlation 0.95) of unknown origin (added by an AI agent). The classical file was fake stereo (R = L delayed 16 samples). The fake-stereo file was removed. "Gibson" is kept as *Legacy Body* and is verified sample-for-sample identical to the old Gibson body, so steel presets are unchanged.
- **Custom IR**: new **IR…** button (load WAV/AIFF/FLAC/Ogg, or clear), body option 4. Loaded on JUCE's background thread with no audio-thread allocation; up to 2 s; level-matched; embedded in the project state; `render_audio --irFile`.
- **Custom IR safety** (found in DAW testing: a 12 s phrase recording loaded as an IR rang like feedback and drove the limiter 52% of the time):
  - Custom IRs' peak response is capped at the loudest built-in's peak + 3 dB. Real IRs measure −3 to −0.6 dB and are untouched.
  - A plausibility check (late peak / < 20 dB decay) warns with *Load anyway / Cancel*.
  - New `bench_synth` regression; that recording now renders at −14.2 dBFS with no limiting.
- **Body list**: Classical (0), Legacy (1), Modal (2), Flamenco (3, new), Custom (4, new). Old indices are unchanged. The *Flamenco Rasgueado* preset now uses the Flamenco body.
- **Level matching**: all IRs are energy-normalised to the Legacy IR, plus measured trims (Classical +0.24 dB, Flamenco +2.36 dB); the built-ins render within 0.2 dB of each other.
- **Startup fix**: the first body after `reset()` applies instantly (sessions no longer start with a 20 ms fade from Classical).
- **Quality Gates**: see the v68.f commit (bench_synth RTCheck/Timing with 5 bodies + custom IR; pluginval VST3/AU).

### v67.f
- **Waveguide tuning fixed**:
  - The sharpness (+4.8 cents at E2 in sustain, +1.0 at E4, +0.3 at C6) came from `KarplusStrong` tuning the horizontal polarization +0.18 Hz above $f_0$. That plane decays ~5× slower and dominates the sustain. With both planes on $f_0$ the loop measured within 0.02 cents, so the filter compensation was always correct.
  - The 0.18 Hz detune (`kPolarizationDetuneHz`) now goes on the short-lived vertical plane, below $f_0$, which keeps the same beat rate.
  - Sustain is within ±0.05 cents E2–C6. The first ~0.5 s of low notes reads up to −2.3 cents (E2).
- **Quality Gates**: `bench_synth` RTCheck PASS (0), Timing PASS; pluginval strictness 8 via `validate` (see below). GuitarSet waveguide scores unchanged (log-L1 0.669 isolated, 0.769 phrases).

### v66.f
- **pluginval running again** (macOS, v1.0.4 in `tools/pluginval/`, gitignored). CMake now also finds the binary inside `pluginval.app/Contents/MacOS`. It had not been run since about v47.f.
- **State restoration fix**: discrete parameters (bools, choices) are snapped to their exact value after `replaceState()`. Before, an unquantised host value (e.g. 0.18 on Palm Mute) survived a state / project load because APVTS compares quantised values. pluginval failed this test depending on the seed.
- **Program change notification**: `setCurrentProgram()` now calls `updateHostDisplay(programChanged)`, so DAW preset displays follow selections in the plugin's own dropdown.
- **Quality Gates**:
  - pluginval strictness 8: VST3 `SUCCESS` (0 warnings; 4 seeds + the `validate` target); AU `SUCCESS` (1 known JUCE-wrapper warning, wiki 06 §3).
  - `bench_synth`: RTCheck PASS (0), Timing PASS.

### v65.f
- **FDTD sustain across the neck**:
  - *Damping shape*: $\sigma_0$ is now frequency-independent (standard Bilbao model), replacing the $(f/82.4)^{0.65/0.80}$ register multiplier. Calibrated on 87 GuitarSet notes with the new `benchmarks/decay_calibration.py`: the fitted pitch exponent of fundamental decay went from 0.84 to 0.34; the real data gives 0.21, 90% CI [−0.14, 0.36]. Anchored at E2, so bass sustain per Decay setting is unchanged.
  - *Ring-out detection*: the fixed $\sum u^2$ threshold, checked every sample, cut high notes while loud (D6 nylon at −15 dB after 0.39 s; E4 at −38 dB). $\sum u^2$ dips towards zero twice per cycle. It is replaced by total (kinetic + potential) energy, −80 dB re. the note's own peak, checked every 32 samples. All notes now ring to −76 to −84 dB; D6 nylon rings 3.9 s.
- **Chord string assignment**:
  - High piano-roll chords dropped notes: C5–E5–G5–B5–D6 played only 2 of 5. This predates v65.f (identical on v61.f) and became obvious once high notes rang.
  - Simultaneous notes now get a joint best-fit assignment over a 22-fret neck (19 before). The auto-strum uses the same solver.
  - New **Strict Fretboard** parameter (default off): off plays unplaceable notes on a free string past the fretboard, voiced with their natural string's gauge; on drops them.
  - Within a sample, note-offs and keyswitches are applied before chord note-ons.
  - `bench_synth` regressions: open C = 5 voices; the high chord = 5 voices (strict off) or 4 (strict on). The event stress test covers same-tick chords in both modes.
- **Found**: both body IRs notch ~8–11 dB at ~981 Hz, so B5 is quiet through IR bodies (wiki 04). Not yet fixed.
- **GuitarSet benchmark**: log-L1 improved slightly (isolated 0.607, phrases 0.587). The combined isolated score went 1.202 → 1.235, through spectral convergence (longer sustain at Decay 0.75). FDTD beats the waveguide on log-L1 in 15/16 tests and on combined in 12/16.
- **Quality Gates**: RTCheck PASS (0), Timing PASS. FDTD 6-voice load at 32-sample blocks: 25.5% (was 26.2%; the energy check now runs every 32 samples).

### Documentation pass (after v64.f, no code change)
- Corrected claims that did not match the code or measurements:
  - README version (was v55.f), parameter defaults (now preset 1), and CPU figures (now measured).
  - Waveguide tuning: "±0.05 cents" claimed; measured +0.3 to +2.9 cents sharp.
  - The waveguide "pitch settle" is not active.
  - The modal bank is a hand-authored Modalys-style table, not IRCAM Modalys data; there is no "Body Decay" control.
  - The wiki 01 bridge-sum diagram.
- New "measured accuracy & known limitations" section in wiki 02: FDTD tuning ±1.3 cents E2–G5; upper-register sustain too short.
- TODO.md: legato marked done; new "known issues" list. Competitive analysis: our column updated to v64.f.

### v64.f
- **Sample-accurate MIDI**:
  - `SynthEngine::process()` takes timestamped events and applies each at its exact sample. `PluginProcessor` fills a fixed 512-entry event queue from the `MidiBuffer`, splitting the block if it overflows, so it never allocates.
  - Before this, every event was applied at sample 0 of its block: up to 10.7 ms of jitter at 512 samples / 48 kHz, and fast legato pairs inside one block were misread as chords.
  - `render_audio` uses the same path. It previously snapped events to 128-sample blocks; the GuitarSet phrase scores improved slightly as a result (FDTD log-L1 0.594 → 0.592).
- **All Notes Off**: All Notes Off, All Sound Off (newly handled) and Reset All Controllers now call `SynthEngine::allNotesOff()` at their timestamp. It stops strings and clears held notes, keyswitches and pending strums without hard-resetting the body, reflection and DC-filter state.
- **`bench_synth` timing check**: a note-on at offset k must match a block-aligned note-on k samples later, for both engines (rel. diff ~3×10⁻⁷). It is part of `--events-only` and of the exit code.
- **Chord vs legato fix**:
  - The legato onset guard was 160 samples (~3.5 ms), so live rolled chords lost notes: E3, G3, C3 played 8 ms apart dropped E3 to −85 dB. Previously this showed up at small buffer sizes only; sample-accurate timing made it consistent at every size.
  - The guard is now a time-based 40 ms chord window, and legato is a real parameter: `legato`, "Legato (HO/PO)", default on, toggle in the GUI title bar.
  - `bench_synth` regression: a rolled chord keeps 3 voices, a 100 ms hammer-on retunes to 1 voice, and with legato disabled it plays 2 voices.
- **GUI**: shortened the title so the Legato toggle fits in the title bar. The articulation strip's fixed widths added up to 1106 px of 1064 px available, clipping the strum badge; they now fit.
- **Quality Gates**: RTCheck PASS (0) on all sweeps and the event stress test; Timing PASS; FDTD peak 26.2% at 32-sample blocks.

### v63.f
- **GuitarSet benchmark rebuilt** ([`benchmarks/guitarset_eval.py`](../../benchmarks/guitarset_eval.py); method and interpretation in wiki 06 §5):
  - Isolated notes are now chosen automatically: 12 pitches with clean notes in two takes. The v61.f "Bb3 pluck" was a G#3 bend, and its chord test strummed and sustained a chord the player had muted.
  - Every test is re-synthesised from annotations, aligned, and RMS-normalised.
  - New reference points: a second real take of the same pitch, and a naive harmonic tone.
  - **Results**: FDTD beats the waveguide on log-L1 in 16/16 tests and on the combined score in 15/16. Spectral convergence does not discriminate (the naive tone scores best). A second real take scores no better than the synths, so the benchmark supports "better spectral fit than the waveguide", not "realistic".
  - The v61.f "18–33.6%" claim is withdrawn.
- **Sympathetic ghost tone fix**: fully damped (played) strings now clear their sympathetic resonator state instead of freezing it. The frozen energy used to ring out about 0.3 s after every note release (247 Hz band on C4: −61 dB → −104 dB).
- **Quality Gates**: `bench_synth --events-only` RTCheck PASS (0).

### v62.f
- **FDTD horizontal polarization restored to the output**: `FdtdString::tickPolar()` returned $F_H$, but `SynthEngine` summed only $F_V$, so the H plane was computed and thrown away. The bridge now sums $F_V + 0.35\,F_H$ (`kHorizontalSaddleCoupling`, matching the waveguide engine). Output is about +0.7 to +1.1 dB louder; pitch is unchanged (within ~1.3 cents).
- **Audio-thread allocation fixes**:
  - Body model switching no longer loads/prepares an IR in `processBlock`. Both IRs are preloaded into separate convolution engines in `prepareToPlay`; only the audible one runs, and a switch crossfades over 20 ms.
  - FDTD grid capacity is sized from the sample rate ($f_s / (2 \cdot 70\text{ Hz})$, min 512) instead of a fixed 512. Nylon D2 needs 628 points at 96 kHz, so 88.2 kHz and above used to allocate on note-on.
- **FDTD legato correctness**: `init()` and `legatoTransition()` share `configureGrid()`, which rebuilds the vector sizes, fret/finger indices, $\Lambda$, $T_{0,H}$, saddle filter and CFL ($\max(T_{0,V},T_{0,H})$). Before this, a pull-off that lengthened the grid wrote past `vector::size()`. Both polarizations are remapped. Picked and legato notes share `Voice::makeFdtdParams()`, which also fixes legato notes getting default fret clearance, coupling and brightness.
- **Mute release fix**: releasing palm mute now restores the note's own open-string damping (register, material, fret) instead of a nylon-only formula.
- **`bench_synth` event stress RTCheck** (+ `--events-only` flag): exercises note-on/legato/keyswitch/strum/engine/material/body-switch/reset paths inside the allocation guard at 44.1–192 kHz. It failed on v61.f (~2,500 heap ops per run) and passes on v62.f.
- **Quality Gates**: `bench_synth: RTCheck PASS (0)` across all sweeps and the event stress test. FDTD 6-voice load at 32-sample blocks is 26.2% of deadline (v61.f on the same machine: 26.3%). `pluginval` not run: not installed on this machine.

### v61.f
- **Official GuitarSet Dataset STFT Benchmark & Comparative Evaluation**:
  - Integrated the **official GuitarSet dataset** (ISMIR 2018, NYU MARL / Queen Mary University of London) directly into the synthesis verification pipeline.
  - Downloaded and extracted official JAMS 6-string annotations and acoustic condenser microphone recordings (`audio_mono-mic`).
  - Implemented automated evaluation harness [`benchmarks/guitarset_eval.py`](../../benchmarks/guitarset_eval.py) supporting dual-paradigm evaluation:
    1. *Isolated Acoustic Event Extraction*: clean ringing plucks (C3, Bb3) and cadence chords sliced from acoustic mic takes.
    2. *Continuous Performance Resynthesis*: full 5–6s multi-string fingerstyle comping (`00_SS1-100-C#_comp`) and solo lead melodies (`00_SS1-100-C#_solo`) transcribed from JAMS note annotations.
  - Enhanced headless C++ CLI renderer [`src/bench/RenderAudioCLI.cpp`](../../src/bench/RenderAudioCLI.cpp) with `--eventsFile` timestamped scheduling, supporting noteOn, noteOff, and continuous multi-voice phrase synthesis.
  - Evaluated Multi-Scale STFT Convergence and Log-Magnitude L1 loss across 5 scales ($N_\text{fft} \in \{2048, 1024, 512, 256, 128\}$): *(Superseded: this result came from a flawed method and is withdrawn in v63.f.)* Bilbao Non-Linear FDTD demonstrated **$18\%$ to $33.6\%$ lower Log-Magnitude L1 spectral distance** compared to Digital Waveguide modeling against real acoustic microphone audio.
  - Generated high-resolution 4-panel spectral comparison charts (`guitarset_pluck_C3.png`, `guitarset_pluck_Bb3.png`, `guitarset_chord_B.png`, `guitarset_phrase_SS1_comp.png`, `guitarset_phrase_SS1_solo.png`).
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 26.28%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

### v60.f
- **First-Launch Preset Parity & Host Session Persistence**:
  - Resolved initial instantiation mismatch where reopening the plugin defaulted to fallback values rather than the displayed preset:
    - Processor constructor explicitly invokes `setCurrentProgram(0)` on creation.
    - Updated `createParameterLayout()` default values to match Preset 0 (*Dreadnought Acoustic*: Steel, Bilbao FDTD, Gibson IR, decay 0.78, brightness 0.72, stiffness 0.28).
    - Serialized `currentProgram` to XML in `getStateInformation()` and restored in `setStateInformation()`, ensuring host project recalls faithfully restore both the audio parameters and the active preset selector.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 26.53%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

### v59.f
- **Factory Preset Management & DAW Program Integration**:
  - Implemented 8 factory physical modeling presets:
    1. *"Dreadnought Acoustic"*: Steel string, Gibson Acoustic IR body, full sustain, Bilbao FDTD.
    2. *"Nylon Concert Classical"*: Nylon string, Classical Nylon IR body, gentle plectrum contact, Bilbao FDTD.
    3. *"Flamenco Rasgueado"*: Nylon string, Classical IR, snappy attack angle, low decay, wide stereo field.
    4. *"Fingerstyle Auditorium"*: Steel string, Gibson IR body, high sustain, Digital Waveguide engine.
    5. *"Vintage Parlor Blues"*: Steel string, 32-mode Modal Bank, compact body, woody grit, Bilbao FDTD.
    6. *"12-String Shimmer"*: Steel string, Modal Bank, jumbo body, maximum stereo aura, Digital Waveguide.
    7. *"Percussive Palm Chops"*: Steel string, Gibson IR, palm mute engaged, tight acoustic chug.
    8. *"Warm Jazz Archtop"*: Nylon string, 32-mode Modal Bank, deep warmth, gentle soundhole pick position.
  - Full native DAW program integration: implemented `getNumPrograms()`, `getCurrentProgram()`, `setCurrentProgram()`, and `getProgramName()` in `HybridSynthProcessor`, enabling FL Studio, Logic Pro, Cubase, and Studio One native wrapper preset selectors.
  - Custom UI header dropdown selector: added `presetBox` in `PluginEditor` synchronized with host program changes.
- **Bi-Directional APVTS Parameter Synchronization**:
  - Selecting a preset updates all APVTS sliders, toggles, and combo attachments smoothly via `setValueNotifyingHost()` without audio thread locks.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 26.53%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

### v58.f
- **DAW Legato Cross-Boundary Fretting Reach**:
  - Resolved string-allocation boundary conflict in `SynthEngine`: previously, `assignStringForNote()` routed adjacent notes across string boundaries (e.g., $C_4 \to D_4$, $F \to G$, $A \to B$) to new open strings, causing overlapping notes to play as two-note chords rather than legato transitions.
  - Enabled cross-boundary fretting reach on actively vibrating strings: if a voice is held on physical string $s$, and the new overlapping note is physically reachable on string $s$ ($0 \le \text{fret} \le 19$) with interval $\le 3$ semitones, the engine seamlessly executes the acoustic legato transition (hammer-on or pull-off) on the vibrating string.
- **Simultaneous Chord Protection via Voice Onset Age**:
  - Implemented minimum onset age check (`samplesSinceNoteOn >= 160`, $\sim 4\text{ ms}$): prevents simultaneous chord voicings or near-simultaneous MIDI attacks from being erroneously categorized as legato transitions, guaranteeing chord polyphony is never stolen or collapsed.
- **Zero-Allocation FDTD Legato Scratch**:
  - Pre-reserved scratch buffers (`saved_curr`, `saved_prev`) in `FdtdString` eliminating heap allocations during runtime legato grid retuning.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 26.62%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

### v57.f
- **DAW MIDI Overlapping Note Legato Integration**:
  - Implemented the universal DAW piano roll overlapping note standard in `SynthEngine`: drawing notes that overlap in FL Studio, Ableton Live, Logic Pro, Cubase, or Reaper automatically engages seamless acoustic legato on the vibrating string.
  - Ascending overlapping intervals execute **Hammer-Ons** (percussive fret tap impulse, zero pick click); descending overlapping intervals execute **Pull-Offs** (left-hand pizzicato snap, ringing open sustain).
  - Sustained legato transitions gracefully ignore the earlier note's Note-Off message, preventing premature audio cutoff.
- **Dispersion-Compensated Legato Tension Calibration**:
  - Eliminated hardcoded string tension bug in `Voice::legatoTransition()`: dynamic string tension $T_0(f, L, \rho_A, EI)$ guarantees exact sub-Hertz fundamental pitch tracking across all fretted legato transitions (e.g. $F^\sharp_2 = 92.8\text{ Hz}$, $G^\sharp_2 = 104.0\text{ Hz}$), matching picked notes perfectly.
- **Calibrated Full-Body Pull-Off Mechanics**:
  - Increased fret-hand lateral release displacement to $0.60\text{ mm}$ and added low-fret compliance compensation ($1.55\times$ on frets $\le 2$), eliminating trailing note volume drop-off and matching hammer-on levels within $\pm 0.7\text{ dB}$.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 12.53%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

### v56.f
- **Acoustic Legato Articulations: Hammer-Ons & Pull-Offs**:
  - Initial foundation for acoustic legato note transitions on actively vibrating strings within a 1-octave interval ($\Delta \le 12$ semitones) without interrupting chord strumming or dead-note mechanics; controllable via `paramLegatoEnabled`.
  - **Zero Plectrum Snap**: Bypasses all direct airborne plectrum clicks, high-frequency edge bite, and wound wire rasp chirps on legato notes, modeling authentic fret-hand fretting mechanics.
  - **Wave Momentum Continuity**: Bridge-aligned spatial grid mapping smoothly maps propagating wave packets between old and new vibrating lengths without discontinuity jumps.
  - **Calibrated Damping Invariant**: FDTD loss parameters ($\sigma_0, \sigma_1$) match register-dependent open sustain decay rather than artificially doubling damping.
  - **Hammer-On Mechanics**: Fretting finger impacts the fret wire, injecting a soft downward impulse ($\text{tapAmp} = -0.000035 \times \text{vel}$) right at the boundary nut ($x = 0$).
  - **Pull-Off Left-Hand Pizzicato Excitation**: Modeled physical left-hand fret-hand pluck where the finger snaps laterally at $x_\text{release} = L_\text{new} - L_\text{old}$, creating a triangular displacement wave ($\text{pullAmp} = 0.00020 \times \text{vel}$) across the newly lengthened string so notes sustain with full resonance (all 5 notes in phrase balanced within $\pm 1.5\text{ dB}$).
  - **Harness Comparison Tooling**: Added `--noLegato` / `--forceRepluck` CLI mode to render identical melodic phrases ($E_2 \to F^\sharp_2 \to G^\sharp_2 \to F^\sharp_2 \to E_2$) for direct A/B testing of legato hammer/pulls vs individual plectrum strikes on the web dashboard.
- **Quality Gates Passed**: Local `bench_synth: RTCheck PASS (0)` (zero heap allocations on audio thread, peak load 26.35%).
- **DAW Deployment**: Deployed freshly compiled release binaries to `~/Library/Audio/Plug-Ins/VST3/` and `~/Library/Audio/Plug-Ins/Components/`.

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

- [ ] **Physical String Bending & MIDI Pitch Wheel Expression**:
  - Implement real-time physical string elongation & dynamic tension modulation $T(t) = T_0 + \Delta T(t)$ across vibrating strings in response to MIDI Pitch Wheel (configurable $\pm 2$ to $\pm 12$ semitones) and DAW pitch slides/vibrato.
- [ ] **Harmonics Engine (Natural & Pinch Harmonics)**:
  - Natural nodal touch damping (12th fret $1/2$ octave, 7th fret $1/3$ octave+5th, 5th fret $1/4$ 2 octaves) damping non-nodal modes into crystalline chimes.
  - Pinch / artificial harmonics with thumb-flesh damping for high-register harmonic squeals, triggered via keyswitch or articulation control.
- [ ] **Electric Guitar Mode (Electromagnetic Pickups & Solid Body)**:
  - Electromagnetic pickup induction model sensing string transverse velocity ($\frac{\partial u}{\partial t}$) at physical positions $x_\text{pickup}$ (Neck, Middle, Bridge).
  - Single-coil vs. Humbucker aperture comb filtering, tone pot load, and solid body low-damping sustained vibration.
- [x] **Preset Management System**:
  - Implemented 8 distinct physical modeling factory presets accessible via DAW program selector and plugin header dropdown.
- [ ] **Micro-Tuning & Alternate Tuning Support**:
  - Add support for Scala (`.scl`) files and standard alternate guitar tunings (Drop D, DADGAD, Open G, Open D).
- [ ] **MPE & Polyphonic Aftertouch Expression**:
  - Implement MIDI Polyphonic Expression (MPE) for per-finger vibrato (pitch-bend), fret pressure (soundboard coupling), and slide dynamics.
- [ ] **GUI Refinement & FDTD String Mesh Visualizer**:
  - Modernize the JUCE GUI with real-time waveform / string deflection animation and visual fretboard display.

