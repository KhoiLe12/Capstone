# 05 — Articulations, Dead Notes & Keyswitch Specification

## 0. First-Principles Physical Modeling Invariant

> [!IMPORTANT]
> **Core Architectural Rule**: Every playing articulation, boundary condition, damping mechanic, and excitation in the synthesizer **MUST be physically modeled** within the simulation framework (FDTD elastodynamic PDE solver and bridge/body acoustics).
> 
> Heuristic "DSP shortcuts" (such as synthetic oscillators, post-filtering EQ curves, or bypassing string solvers with artificial wavelets) are prohibited unless a specific physical interaction is mathematically impossible or provably non-convergent in real-time. If an approximation is ever required, the exact mathematical bottleneck and physical trade-off must be documented here and disclosed to the user.
> 
> **Mandatory CPU Weight Invariant**: For every added physical modeling feature or PDE extension, the agent must measure and report the exact CPU load increase (via `./build/bench_synth` latency and deadline percentage sweeps).

### Physical Articulation Architecture & CPU Feasibility Analysis

| Articulation | Physical Mechanism in PDE / Body | Pure Physical Modeling Status | Estimated CPU Weight Impact | Feasibility & Real-Time Viability |
| :--- | :--- | :--- | :--- | :--- |
| **Palm Muting** | Fleshy palm contact resting across the last $4.5\text{--}6.5\text{ cm}$ near bridge: localized spatial loss profile $\sigma_0(x)$ on string nodes $x \in [L - \Delta x, L]$. | **Implemented (v69.f, refined v70.f)** | **$+0.0\%$ measured** (`bench_synth`: 28.60% @ 48k block 32) | Fully optimal. Naturally preserves fundamental wave momentum while rapidly absorbing high-frequency reflections. |
| **Dead Notes ("X" notes)** | Fretting hand rests across the neck: broad spatial damping patch $\sigma_\text{hand}(x)$ over $x \in [0.02L, 0.68L]$. Run FDTD solver normally. | **Implemented (v69.f, refined v70.f)** | **$+0.0\%$ measured** (terminates into silence in $< 15\text{ ms}$) | Fully optimal. Eliminates artificial click generators; strike energy pumps body IR naturally. |
| **Natural Harmonics** | Light finger touch at integer nodal points ($x = L/2, L/3, L/4, L/5$): localized nodal damping $\sigma_\text{touch}(x)$ suppressing anti-nodal partials. | Planned physical implementation | **$< +0.2\%$** (boundary damp applied to 1 discrete node) | Fully optimal. Emergent crystalline bell tones from modal zero-crossings. |
| **Pinch Harmonics** | Plectrum release followed immediately ($\sim 2\text{ ms}$) by localized thumb-flesh damping at non-integer nodal position. | Planned physical implementation | **$< +0.2\%$** | Fully optimal. |
| **String Bending** | Lateral string deflection over fret wire increases longitudinal string elongation $\Delta L(t)$ and nominal tension $T_0(t) = T_0 + EA \frac{\Delta L}{L}$. | Planned physical implementation | **$\sim +0.0\%$** (modulates nominal tension parameter $T_0$ already present in Bilbao loop) | Fully optimal. Smooth continuous pitch bends without spatial grid resizing. |
| **Flamenco Golpe (Body Knocks)** | Direct mechanical soundboard impact force $f_\text{golpe}(t)$ injected into bridge/body IR convolution engine in parallel with string force. | Planned physical implementation | **$< +0.1\%$** (direct summation into convolution input buffer) | Fully optimal. Directly excites authentic measured body IR wood/cavity modes. |
| **Dynamic Nonlinear Plectrum** | Dynamic elastoplastic contact force $f_\text{pick}(t) = K [y - u]_+^\alpha + R \frac{d}{dt}[y - u]_+$ interacting with node $x_\text{pick}$. | Planned migration from static displacement + bandpass clack | **$+2.0\%\text{--}4.5\%$** (solving 1 nonlinear contact force term per voice per sample) | High real-time viability using Sherman-Morrison explicit projection. |

---

## 1. Physical Articulation Modes

The synthesizer features 4 distinct physical playing articulations, modeled via boundary damping conditions and mechanical impulse coupling:

### A. Open String Sustain (`MuteMode::Open`)
- Full, unrestricted string boundary reflection at nut/fret and bridge.
- Natural harmonic decay across partials ($\sigma_0 \approx 0.45\text{--}1.25\text{ s}^{-1}$).
- Full sympathetic resonance coupling and dual-axis polar radiation.

### B. Selective Acoustic Palm Muting (`MuteMode::Palm`)
- **Physical Context**: The fleshy heel of the picking hand rests lightly on the bridge saddles right where the strings terminate.
- **First-Principles Spatial Loss Profile (v69.f, refined v70.f)**:
  - The speaking string length anchors at the fret and bridge.
  - Spatial flesh damping is localized strictly to the bridge termination zone $x \in [L - L_\text{palm}, L]$, where $L_\text{palm} = \min(0.065\text{ m}, 0.22L)$ ($6.5\text{ cm}$, matching anatomical human hand contact):
    $$\sigma_0(x) = \begin{cases} \sigma_{0,\text{open}}, & x \le L - L_\text{palm} \\ \sigma_{0,\text{open}} + \sigma_\text{flesh} \cdot \frac{1}{2}\left[1 - \cos\left(\pi \frac{x - (L - L_\text{palm})}{L_\text{palm}}\right)\right], & x > L - L_\text{palm} \end{cases}$$
    where $\sigma_\text{flesh} = 480.0 + 360.0 \times (1.0 - \text{decay})$.
  - Long fundamental wavelengths ($\lambda = 2L \gg L_\text{palm}$) have small amplitude near the boundary node, preserving mass momentum and sustaining a deep, low-frequency thump for $\sim 45\text{--}70\text{ ms}$.
  - Short harmonic wavelengths ($\lambda < 0.1\text{ m}$, partials $> 300\text{ Hz}$) undergo complete absorption inside the flesh patch within 1 round-trip reflection.
  - **Zero artificial post-EQ or heuristic brightness cuts**: The acoustic chug emerges purely from spatial PDE loss.

### C. Zero-Pitch Acoustic Dead Notes / "X" Notes (`MuteMode::Full`)
- **Physical Context**: The fretting hand rests loosely across the fretboard, damping all strings simultaneously.
- **First-Principles Spatial Loss Profile (v69.f, refined v70.f)**:
  - **Full String Solver Execution**: String solvers are NOT reset or bypassed. The string is plucked normally with plectrum displacement under an overdamped spatial boundary condition.
  - Spatial damping covers the entire neck zone from nut to fret 14: $x \in [0.02L, 0.68L]$ (eliminating the undamped segment near the nut that previously produced a high-pitched ring):
    $$\sigma_0(x) = \begin{cases} \sigma_{0,\text{open}} + \sigma_\text{hand} \cdot \sin\left(\pi \frac{x - 0.02L}{0.66L}\right), & 0.02L \le x \le 0.68L \\ \sigma_{0,\text{open}}, & \text{otherwise} \end{cases}$$
    where $\sigma_\text{hand} = 850.0\text{ s}^{-1}$.
  - **Bridge Saddle Mechanical Force Pulse**: The wave launched toward the bridge strikes the saddle at $t \approx 0.4\text{ ms}$, injecting an authentic mechanical force pulse into the body soundboard IR. The leftward wave and reflected bridge wave enter the hand damping patch and dissipate in $< 15\text{ ms}$, preventing any coherent periodic pitch ($f_0$) from circulating.
  - **Tactile Plectrum De-Harshing**: Direct airborne plectrum snap amplitude scaled down to $0.25\times$ and resonant click brightness softened by $0.35\times$, eliminating brittle $7.2\text{ kHz}$ plastic snap and producing a warm, woody acoustic fretboard clack.
  - **Zero Synthetic Ricker Pulses**: Synthetic dipole wavelets are completely eliminated.

### D. Acoustic Hand Choke (Slap on Ringing Strings)
- Rapidly attenuates ringing string energy to zero within $\sim 10\text{ ms}$ (`chokeCoeff = exp(-1.0 / (0.010 * fs))`).
- Injects a mechanical hand slap impulse into the bridge saddle, pumping the acoustic body IR naturally. **Zero synthetic sine oscillators**.

### E. Legato Articulations: Hammer-Ons & Pull-Offs (v56.f–v58.f)
- **Physical Context**: Fretting finger descends onto a higher fret (Hammer-On) or snaps off to a lower fret (Pull-Off) on an already vibrating string without right-hand plectrum pick re-excitation.
- **DAW Piano Roll Overlapping Note Protocol (v57.f)**:
  - Supports standard DAW piano roll overlapping note phrasing (FL Studio, Ableton Live, Logic Pro, Cubase, Studio One, Reaper).
  - When notes overlap during monophonic lead lines, the synthesizer automatically executes a seamless acoustic legato transition on the vibrating string.
  - Ascending overlapping intervals execute **Hammer-Ons**; descending overlapping intervals execute **Pull-Offs**.
  - **Note-Off Handover**: Sustained legato transitions gracefully ignore the earlier note's Note-Off message, transferring voice ownership to the new pitch without cutoffs.
- **Cross-Boundary Fretting Reach (v58.f)**:
  - In standard guitar string allocation, adjacent scale notes often span default string boundaries (e.g. $C_4 \to D_4$, $F \to G$, $A \to B$).
  - When exactly 1 note is actively held on physical string $s$, the engine allows cross-boundary notes to hammer on or pull off on string $s$ if physically reachable ($0 \le \text{fret} \le 19$) within a melodic fretting span ($\text{interval} \le 3$ semitones).
- **Chord Protection via a 40 ms Chord Window (v58.f, revised v64.f)**:
  - Legato transitions require the held note to have started at least **40 ms** earlier (`kLegatoChordWindowSec`). Notes closer together than that, such as a rolled chord played live on a keyboard, always play as a chord. Each hammer-on restarts the window, so legato runs faster than 25 notes/s are re-picked.
  - Note onsets are sample-accurate (v64.f), so the window is independent of host buffer size.
  - v58.f–v63.f used a 160-sample (~3.5 ms) window. That was too short for live playing: in a chord rolled 8 ms apart (E3, G3, C3), G3 hammered onto E3's string and E3 disappeared (−85 dB). `bench_synth` now checks this case.
- **Chords and the fretboard (v65.f)**: simultaneous notes are assigned to strings jointly over a 22-fret neck. **Strict Fretboard** decides what happens to notes no guitarist could finger at the same time: drop them (on) or play them on a free string past the fretboard (off, default). Details in wiki 01 §2.
- **Legato toggle (v64.f)**: the `legato` parameter ("Legato (HO/PO)", default on) switches hammer-on / pull-off detection off entirely, for rhythm parts with slow arpeggiated chords.
  - Simultaneous chord voicings drawn at the same piano roll tick have 0 ms onset difference, guaranteeing chord notes are never stolen or collapsed into legato runs.
- **Acoustic Mechanics & Synthesis**:
  - **Zero Plectrum Snap**: Direct airborne pick attack, flexural tortex eigenmodes, and wound wire rasp chirps are completely bypassed.
  - **Wave Momentum Continuity**: Bridge-aligned spatial grid mapping smoothly maps propagating wave packets between old and new vibrating lengths without discontinuity jumps.
  - **Dynamic Dispersion-Compensated Tension $T_0$ (v57.f)**: Exact sub-Hertz fundamental pitch tracking via dynamic tension calculation:
    $$T_0 = \rho A (2 L f_\text{tuned})^2 - \frac{\pi^2 E I}{L^2}$$
  - **Hammer-On Mechanics**: Fretting finger impacts the fret wire, injecting a soft downward impulse ($\text{tapAmp} = -0.000045 \times \text{vel}$) right at the boundary nut ($x = 0$).
  - **Calibrated Full-Body Pull-Off Mechanics (v57.f)**: The fretting finger releasing from the higher fret snaps laterally across the string at $x_\text{release} = L_\text{new} - L_\text{old}$, creating a triangular displacement wave ($\text{pullAmp} = 0.00070 \times \text{vel} \times \text{bassBoost}$). Low frets ($\text{fret} \le 2$) use $\text{bassBoost} = 1.55\times$ to overcome bridge boundary impedance and match hammer-on levels within $\pm 0.7\text{ dB}$.

---

## 2. MIDI Keyswitch Specification

Keyswitches are placed below the playable guitar register so they never trigger unwanted notes:

| Keyswitch Note | Standard MIDI Note | FL Studio / Yamaha Octave | Function & Behavior |
| :---: | :---: | :---: | :--- |
| **C1** | MIDI 24 | C2 | **Palm Mute Hold**: While held, all incoming and ringing notes engage selective palm mute. Releasing C1 restores open sustain. |
| **D1** | MIDI 26 | D2 | **Full Mute / Hand Choke**: When struck, immediately chokes all ringing strings with an acoustic hand slap. While held, incoming notes play as unpitched dead notes ("X" notes). |
| **E1** | MIDI 28 | E2 | **8th-Note Auto-Strum**: While held, held chord notes are automatically strummed at 8th-note intervals synchronized to DAW host BPM, alternating downstrokes and upstrokes. |

### Strict Playable Note Range
- **Minimum Note**: $D_2$ (MIDI 38 — Drop D tuning support) or $E_2$ (MIDI 40 — standard acoustic low E).
- **Maximum Note**: $D_6$ (MIDI 86 — 22nd fret high E string).
- Any MIDI notes below 38 (excluding assigned keyswitches) or above 86 are rejected at `SynthEngine::noteOn` to eliminate extraneous artifacts.

---

## 3. Acoustic Strumming Engine (`triggerStrum`)

When strumming chords (via E1 keyswitch or GUI `Strum` toggle):
- **Downstroke**: Strummed from bass string to treble string ($E_2 \to E_4$).
- **Upstroke**: Strummed from treble string to bass string ($E_4 \to E_2$). Velocity scaled to $0.82\times$, brightness scaled to $1.15\times$.
- **Organic Wrist Rake Warp**:
  $$t_\text{warp} = 0.60 u + 0.40 u^2 \quad (u \in [0, 1])$$
  Models initial contact drag on wound strings followed by whipping wrist acceleration through treble strings.
- **Rake Interval**: $\sim 2.0\text{--}2.5\text{ ms}$ per string.

