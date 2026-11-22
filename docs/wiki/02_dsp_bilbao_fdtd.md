# 02 — DSP Engine 1: Stefan Bilbao Elastodynamic Non-Linear FDTD

## 1. Physical PDE Model

Engine 1 implements Stefan Bilbao's non-linear elastodynamic string formulation for acoustic strings undergoing large-amplitude transverse and longitudinal vibrations with stiff boundary contacts.

The continuous equation of motion for transverse string displacement $u(x, t)$ is:
$$\rho A \frac{\partial^2 u}{\partial t^2} = T_0 \frac{\partial^2 u}{\partial x^2} - E I \frac{\partial^4 u}{\partial x^4} - 2 \sigma_0 \frac{\partial u}{\partial t} + 2 \sigma_1 \frac{\partial^3 u}{\partial t \partial x^2} + F_\text{ext}(x, t)$$

Where:
- $\rho$: Material density ($\text{kg/m}^3$)
- $A = \pi r^2$: Cross-sectional area ($\text{m}^2$)
- $T_0$: Equilibrium string tension ($\text{N}$)
- $E$: Young's modulus of elasticity ($\text{Pa}$)
- $I = \frac{\pi}{4} r_\text{stiff}^4$: Area moment of inertia ($\text{m}^4$)
- $\sigma_0$: Air viscosity and boundary damping rate ($\text{s}^{-1}$)
- $\sigma_1$: Internal viscoelastic shear loss rate ($\text{m}^2/\text{s}$ or frequency-dependent damping)
- $F_\text{ext}$: External excitation force (fret collision, plectrum, finger barrier)

---

## 2. Discrete Spatial Grid & Bilbao Stability Criteria

The continuous string $x \in [0, L]$ is discretized into $N$ segments of length $h = L / N$, with time step $k = 1 / f_s$.

### CFL Stability Bound (Bilbao DAFx24 Eq. 47)
To ensure numerical stability across both polarizations without numerical explosion:
$$\text{term}_1 = \frac{\max(T_{0,V}, T_{0,H}) \cdot k}{\rho A} + 4 \sigma_1$$
$$h_\text{min}^2 = \frac{k}{2} \left( \text{term}_1 + \sqrt{\text{term}_1^2 + \frac{16 E I}{\rho A}} \right)$$
$$h_\text{min} = \sqrt{h_\text{min}^2}, \quad N = \left\lfloor \frac{L}{h_\text{min}} \right\rfloor$$
Interior grid points: $N_\text{pts} = N - 1$.

**Grid capacity (v62.f)**: `FdtdString::init(sampleRate)` (called from `prepareToPlay`) reserves every grid-sized buffer for the worst case at that sample rate. Since stiffness and $\sigma_1$ only enlarge $h_\text{min}$, $N \le L/(c k) = f_s / (2 f_0)$, so capacity is $\max(512,\ \lceil f_s / (2 \cdot 70\text{ Hz}) \rceil + 8)$, which covers Drop-D D2 at any rate (e.g. 1380 points at 192 kHz). Nylon D2 needs ~628 points at 96 kHz, so the earlier fixed 512 allocated on the audio thread above 48 kHz. As a backstop, `configureGrid()` caps $N$ at capacity + 1; a cap only enlarges $h$, which keeps the scheme stable.

---

## 3. Dual-Polarization Transverse Modeling ($u_V, u_H$)

Real acoustic guitar strings do not vibrate purely in one plane:
1. **Vertical Polarization ($u_V$)**: Perpendicular to soundboard plate; primary driver of soundboard pumping ($F_V$).
2. **Horizontal Polarization ($u_H$)**: Parallel to soundboard; produces rocking saddle torque ($F_H$).
3. **Polarization Split ($\delta \approx 0.003\text{--}0.010$)**:
   $$T_{0,H} = T_{0,V} \cdot (1 + \delta)^2 \implies f_H = f_V \cdot (1 + \delta)$$
   With the default `polarizationSplit` of $0.0012$ the beat is slow ($\approx 0.1\text{ Hz}$ on E2, $0.4\text{ Hz}$ on E4).
4. **Output coupling (fixed in v62.f)**: `tickPolar()` returns $F_V$ and $F_H$ separately and `SynthEngine` sums $F_V + 0.35\,F_H$ into the bridge (`kHorizontalSaddleCoupling`, the same ratio the waveguide engine uses). Before v62.f the $F_H$ output was computed every sample but dropped by `SynthEngine`, so the H plane never reached the output.

---

## 4. Adaptive Fast-Path Solver (Sherman-Morrison vs Woodbury)

During multi-voice polyphony (up to 384 concurrent vibrating string segments in DAW stress tests), solving a dense non-linear matrix at every sample would exceed real-time deadlines.

We employ an **adaptive dual-path solver**:
- **Fast Path (Free Vibration — 98% of audio frames)**:
  When the string is not colliding with frets or finger barriers, the contact forces $\psi = 0$. The update reduces to an explicit finite difference stencil with localized spatial damping:
  $$u_i^{n+1} = c_0[i] \left[ 2 u_i^n + (c_1 + c_3) Du_i^n - c_2 D^2 u_i^n + c_4[i] u_i^{n-1} - c_3 Du_i^{n-1} \right]$$
  where $c_0[i] = \frac{1}{1 + \sigma_0[i] k}$ and $c_4[i] = \sigma_0[i] k - 1$.
  Written as plain scalar loops (left to compiler auto-vectorization; there are no explicit SIMD intrinsics), with $O(1)$ `std::vector::swap` state rotation (zero array copies).
  The Kirchhoff-Carrier tension term is still applied here through a rank-1 (Sherman-Morrison) update.
- **Non-Linear Contact Path (Fret / Finger Collision)**:
  When any grid point dips below the fret crown height $m_0$, or the finger is engaged and in contact, the full **$4 \times 4$ Woodbury system** (fretboard, frets, finger, Kirchhoff-Carrier) is solved directly, with no nonlinear iteration loops.

### 4.1 First-Principles Spatial Damping Profile $\sigma_0(x)$ (v69.f, refined v70.f)
To eliminate artificial post-filters, brightness cuts, and solver-bypass wavelets:
1. **Palm Muting**: Fleshy heel rests across the last $4.5\text{--}6.5\text{ cm}$ near the bridge ($x \ge L - L_\text{palm}$, $L_\text{palm} = \min(0.065\text{ m}, 0.22L)$). Within this patch, $\sigma_0[i] = \sigma_{0,\text{open}} + \sigma_\text{flesh} \cdot \frac{1}{2}\left[1 - \cos\left(\pi \frac{x - (L - L_\text{palm})}{L_\text{palm}}\right)\right]$, where $\sigma_\text{flesh} = 480.0 + 360.0 \times (1.0 - \text{decay})$. Low fundamental frequencies ($\lambda \gg L_\text{palm}$) sustain with full mass momentum, while high partials dissipate rapidly.
2. **Dead Notes ("X" notes)**: Fretting hand rests loosely across the fretboard covering the full nut-to-fret-14 range ($x \in [0.02L, 0.68L]$) with heavy damping $\sigma_\text{hand} = 850.0\text{ s}^{-1}$. Pluck displacement launches traveling wave packets; the bridge-going wave strikes the saddle, exciting the soundboard body IR once before being dissipated in $< 15\text{ ms}$ by the fretting hand. Airborne tortex clack amplitude is scaled down to $0.25\times$ with $0.35\times$ brightness to yield a deep, woody acoustic thud. Zero synthetic dipole pulses.
3. **Audio-Thread Safety & Efficiency**: $c_0[i]$ and $c_4[i]$ vectors are pre-allocated in `reserveCapacity()`. Switching articulations is $O(N)$ with zero allocations and measured $+0.0\%$ CPU overhead.

---

## 5. Physical String Material & Density Profiles (v46.f)

| Parameter | Nylon Classical | Steel Acoustic (Light 12–53) |
| :--- | :--- | :--- |
| **Material Density ($\rho$)** | $1140\text{ kg/m}^3$ (uniform) | $6200\text{--}7850\text{ kg/m}^3$ (wound hex core to plain steel) |
| **Young's Modulus ($E$)** | $\sim 1.2\text{ GPa}$ | $\sim 200\text{ GPa}$ |
| **Longitudinal Snap Velocity ($c_L$)**| $\sim 1025\text{ m/s}$ | $\sim 5050\text{ m/s}$ |
| **Equilibrium Tension ($T_0$)** | $45\text{--}75\text{ N}$ per string | $105\text{--}145\text{ N}$ per string |
| **Acoustic Character** | Warm, round, fast treble dissipation | Bright, ringing, sustained, metallic snap |

---

## 6. Bilbao FDTD Legato Transition Mechanics (v56.f–v62.f)

When a legato transition occurs on an actively vibrating string, the FDTD grid must adapt to a new vibrating speaking length $L_\text{new}$ without causing discontinuities, clicks, or loss of existing wave energy.

### Grid rebuild (v62.f)
`init()` and `legatoTransition()` share one `configureGrid()`, which rederives **everything** that depends on the parameters or the grid: $\rho A$, $EI$, $T_{0,H}$, the CFL grid (using $\max(T_{0,V}, T_{0,H})$), vector sizes (resized within reserved capacity), fret and finger indices, $\Lambda$ scalings, the saddle filter and the longitudinal resonator. Collision auxiliaries $\psi$ restart from zero because they refer to the old geometry. Before v62.f the legato path changed $N$ without resizing the vectors (writing past `size()` when a pull-off lengthened the grid) and left the fret/finger indices, $\Lambda$ and $T_{0,H}$ from the old note.

Picked and legato notes now get their physical parameters from the same `Voice::makeFdtdParams()`: string geometry, dispersion-compensated tension, register/fret damping, fret clearance $m_0$, phantom/longitudinal coupling, and the note's saddle brightness, which is kept so the timbre does not jump. A palm-muted string keeps its palm damping across legato.

### A. Bridge-Aligned Spatial Grid Remap
On an acoustic guitar, the bridge saddle is stationary at coordinate $x = L$, and fretting shortens the string from the nut end ($x = 0$). Grid points are therefore aligned relative to the bridge:
$$\text{distFromBridge} = (N_\text{pts} - i) \cdot h_\text{new}$$
$$\text{oldGridPos} = N_\text{pts, old} - \frac{\text{distFromBridge}}{h_\text{old}}$$
Existing transverse displacements $u^n$ and $u^{n-1}$ of **both polarizations** (V and H) are linearly interpolated between adjacent old grid indices (with zero boundaries at the old nut/fret and the bridge) and tapered smoothly to zero at the new fretted nut boundary:
$$u_\text{new}(i) = \left[ (1 - \text{frac}) \cdot u_\text{old}(\text{idx}_0) + \text{frac} \cdot u_\text{old}(\text{idx}_1) \right] \cdot \min\left(1, \frac{i + 1}{4}\right)$$

### B. Dynamic Dispersion-Compensated Tension Calibration (v57.f)
To prevent microtonal pitch drift during legato retuning, equilibrium tension $T_0$ is solved dynamically using the elastodynamic dispersion formula:
$$T_0 = \rho A (2 L f_\text{tuned})^2 - \frac{\pi^2 E I}{L^2}$$
Picked and legato notes on the same pitch match closely: a G2→E2 pull-off measures 82.429 Hz against 82.431 Hz for a picked E2 (v62.f).

### C. Fret Contact & Pizzicato Excitation
- **Hammer-On**: Injects a localized downward impact impulse into the first few grid points near the fretted nut:
  $$u_\text{curr}[1..3] += \text{tapAmp}, \quad \text{tapAmp} = -0.000045 \times \text{vel}$$
- **Pull-Off**: Imparts an authentic left-hand pizzicato triangular release pluck centered at $x_\text{release} = L_\text{new} - L_\text{old}$:
  $$u_\text{curr}[i] += \text{pullAmp} \times \text{triangle}(x_i), \quad \text{pullAmp} = 0.00070 \times \text{vel} \times \text{bassBoost}$$
  Low frets ($\text{fret} \le 2$) use $\text{bassBoost} = 1.55\times$ to balance low-register radiation.

### D. Zero-Allocation Scratch Buffering (v58.f, v62.f)
The remap snapshots ($u^n$, $u^{n-1}$ for V and H) live in `saved_*` scratch vectors reserved to the sample-rate grid capacity, so retuning never allocates. This is covered by the `bench_synth` event stress test (pull-offs that grow the grid, at 44.1–192 kHz).

---

## 7. Damping Calibration, Ring-Out & Measured Accuracy (v65.f)

### Register-dependent damping
$\sigma_0$ is now **frequency-independent**, as in the standard Bilbao loss model. All frequency dependence comes from the $\sigma_1$ term and the small fret/nut factor. Before v65.f, `Voice::makeFdtdParams()` multiplied $\sigma_0$ by $(f/82.4)^{0.65}$ (steel) or $^{0.80}$ (nylon). The exponents are kept as `Voice::kFdtdSteelRegisterExponent` / `kFdtdNylonRegisterExponent`, now both 0.

This was calibrated with [`benchmarks/decay_calibration.py`](../../benchmarks/decay_calibration.py). The script fits the decay rate (dB/s) of the **fundamental**, isolated by complex demodulation, over the held part of each note. That excludes the pick click and attack. It uses 87 isolated, held GuitarSet notes (steel-string guitar), each re-rendered alone on the FDTD engine:

| Fit $\text{decay} = a\,(f/82.4)^p$ | $p$ (robust) | 90% CI |
| :--- | ---: | ---: |
| GuitarSet (real) | 0.21 | [−0.14, 0.36] |
| FDTD before v65.f | 0.84 | [0.78, 0.89] |
| FDTD v65.f | 0.34 | [0.22, 0.38] |

- **Level is set by the Decay knob, not the data.** The calibration fixes only the *shape* (how decay scales with pitch). It is anchored at E2, so bass sustain at a given Decay setting is unchanged. GuitarSet's absolute decay (~8 dB/s at E2) reflects its players and guitar, not a target.
- **Nylon has no reference data.** It gets the same frequency-independent $\sigma_0$; its faster treble loss comes from its larger $\sigma_1$. Judge it by ear.

### Ring-out detection
A voice fades out when the string's total energy (`FdtdString::computeDiscreteEnergy()`: kinetic + tension-potential) falls 80 dB below the note's own peak (`Voice::kFdtdSilenceRatio`). The check runs every 32 samples, and the reference resets on note-on and legato.

Before v65.f the check was a fixed threshold on displacement energy $\sum u^2$, every sample. That quantity dips towards zero twice per cycle, when all the energy is kinetic, and short treble strings hold little displacement. As a result:
- D6 (nylon) was cut after 0.39 s, still at −15 dB re. peak.
- E4 was cut at 3.1 s, still at −38 dB.

Now every note rings to −76 to −84 dB re. its post-attack level. Total ring time, nylon / steel: E2 9.5 / 15.0 s, E4 8.7 / 13.3 s, G5 5.7 / 8.7 s, D6 3.9 / 5.8 s.

### Measured tuning
`render_audio`, one note per pitch, FFT peak of the fundamental: within ±1.3 cents from E2 to G5 on both materials. The per-string `centsOffsets` compensate grid dispersion on open strings only.

### Effect on the GuitarSet spectral benchmark (wiki 06 §5)
- **Log-L1 improved slightly**: isolated 0.610 → 0.607, phrases 0.592 → 0.586.
- **Spectral convergence on isolated notes worsened**: 0.592 → 0.629, so the combined score went 1.202 → 1.235. The benchmark renders at Decay 0.75, which now sustains longer than GuitarSet's notes. SC is dominated by fundamental level over time and does not discriminate between systems (wiki 06 §5).
