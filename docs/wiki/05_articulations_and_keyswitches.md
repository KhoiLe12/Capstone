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

