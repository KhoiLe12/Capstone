# 05 — Articulations, Dead Notes & Keyswitch Specification

## 1. Physical Articulation Modes

The synthesizer features 4 distinct physical playing articulations, modeled via boundary damping conditions and mechanical impulse coupling:

### A. Open String Sustain (`MuteMode::Open`)
- Full, unrestricted string boundary reflection at nut/fret and bridge.
- Natural harmonic decay across partials ($\sigma_0 \approx 0.45\text{--}1.25\text{ s}^{-1}$).
- Full sympathetic resonance coupling and dual-axis polar radiation.

### B. Selective Acoustic Palm Muting (`MuteMode::Palm`)
- **Physical Context**: The fleshy heel of the picking hand rests lightly on the bridge saddles right where the strings terminate.
- **Acoustic Physics**:
  - The speaking string length still anchors at the fret and bridge, allowing the low fundamental and 2nd harmonic ($80\text{--}250\text{ Hz}$) to sustain with authentic mass momentum.
  - Fleshy contact creates steep viscoelastic shear loss ($\sigma_1 \times 4.0$), rapidly dissipating high partials ($> 350\text{ Hz}$).
  - **Calibrated Damping Rate (v55.f)**:
    $$\text{effSigma0} = (9.5\text{--}11.0) + 8.5 \times (1.0 - \text{decay})$$
    Produces a tight, punchy acoustic "chug" lasting $\sim 75\text{--}110\text{ ms}$. Dynamically controllable via the plugin's `Decay` knob.
  - Plectrum dipole impulse is cushioned: `muteClickScale = 0.38f`.

### C. Zero-Pitch Acoustic Dead Notes / "X" Notes (`MuteMode::Full`)
- **Physical Context**: The fretting hand rests loosely across the fretboard, damping all strings simultaneously.
- **Acoustic Physics (v54.f / v55.f)**:
  - **No Standing Wave / Zero Pitch ($f_0$)**: Waves cannot reflect between nut and bridge. String solvers are kept at rest (`string.reset()`, `fdtdString.reset()`).
  - **Bridge Saddle Mechanical Force Pulse**: The plectrum strike delivers a non-oscillating, mass-dependent bipolar dipole mechanical impulse into the saddle:
    $$F_\text{dead}(t) = -u \cdot e^{-0.5 u^2} \cdot A_\text{dead}, \quad u = \frac{t - t_\text{center}}{\sigma_\text{pulse}}$$
    Wound bass strings deliver a wider impulse ($\tau \approx 1.35\text{ ms}$) pumping the body cavity mode ($A_0 \approx 105\text{ Hz}$); treble strings deliver a narrower pulse ($\tau \approx 0.75\text{ ms}$) pumping the plate mode ($T_1 \approx 200\text{ Hz}$).
  - **Tactile Plectrum Presence (v55.f)**: Full $1.0\times$ plectrum snap amplitude with dual flexural eigenmodes ($4.2\text{ kHz} / 7.2\text{ kHz}$ on steel; $2.8\text{ kHz} / 4.8\text{ kHz}$ on nylon) and wound wire pre-release rasp ($1800\text{--}3400\text{ Hz}$).

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
- **Simultaneous Chord Protection via Voice Onset Age (v58.f)**:
  - Legato transitions require the held note to have been sounding for at least $\sim 4\text{ ms}$ (`samplesSinceNoteOn >= 160` at 44.1k/48k).
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

