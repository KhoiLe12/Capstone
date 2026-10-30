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
Capacity is pre-reserved to $kMaxGridSize = 512$ points to guarantee zero heap allocations up to $96\text{ kHz}$.

---

## 3. Dual-Polarization Transverse Modeling ($u_V, u_H$)

Real acoustic guitar strings do not vibrate purely in one plane:
1. **Vertical Polarization ($u_V$)**: Perpendicular to soundboard plate; primary driver of soundboard pumping ($F_V$).
2. **Horizontal Polarization ($u_H$)**: Parallel to soundboard; produces rocking saddle torque ($F_H$).
3. **Polarization Split ($\delta \approx 0.003\text{--}0.010$)**:
   $$T_{0,H} = T_{0,V} \cdot (1 + \delta)^2 \implies f_H = f_V \cdot (1 + \delta)$$
   This generates the natural, organic acoustic chorus/beating ($0.2\text{--}1.0\text{ Hz}$) heard on real instruments without artificial LFO chorus plugins.

---

## 4. Adaptive Fast-Path Solver (Sherman-Morrison vs Woodbury)

During multi-voice polyphony (up to 384 concurrent vibrating string segments in DAW stress tests), solving a dense non-linear matrix at every sample would exceed real-time deadlines.

We employ an **adaptive dual-path solver**:
- **Fast Path (Free Vibration — 98% of audio frames)**:
  When the string is not colliding with frets or finger barriers, the contact forces $\psi = 0$. The update reduces to an explicit finite difference stencil:
  $$u^{n+1} = \frac{1}{1 + \sigma_0 k} \left[ B u^n + C u^{n-1} \right]$$
  Vectorized with AVX2 SIMD / ARM NEON with $O(1)$ pointer rotation (zero array copies).
- **Non-Linear Contact Path (Fret / Finger Collision)**:
  When string displacement exceeds fret clearance $m_0$ or finger contact barrier $b_0$, an exact conservative contact solver is engaged using the **Sherman-Morrison formula** (rank-1 contact) or a **$4 \times 4$ Woodbury matrix**, eliminating non-linear iteration loops.

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

## 6. Bilbao FDTD Legato Transition Mechanics (v56.f–v58.f)

When a legato transition occurs on an actively vibrating string, the FDTD grid must adapt to a new vibrating speaking length $L_\text{new}$ without causing discontinuities, clicks, or loss of existing wave energy.

### A. Bridge-Aligned Spatial Grid Remap
On an acoustic guitar, the bridge saddle is stationary at coordinate $x = L$, and fretting shortens the string from the nut end ($x = 0$). Grid points are therefore aligned relative to the bridge:
$$\text{distFromBridge} = (N_\text{pts} - i) \cdot h_\text{new}$$
$$\text{oldGridPos} = N_\text{pts, old} - \frac{\text{distFromBridge}}{h_\text{old}}$$
Existing transverse displacements $u^n$ and $u^{n-1}$ are linearly interpolated between adjacent old grid indices and tapered smoothly to zero at the new fretted nut boundary:
$$u_\text{new}(i) = \left[ (1 - \text{frac}) \cdot u_\text{old}(\text{idx}_0) + \text{frac} \cdot u_\text{old}(\text{idx}_1) \right] \cdot \min\left(1, \frac{i + 1}{4}\right)$$

### B. Dynamic Dispersion-Compensated Tension Calibration (v57.f)
To prevent microtonal pitch drift during legato retuning, equilibrium tension $T_0$ is solved dynamically using the elastodynamic dispersion formula:
$$T_0 = \rho A (2 L f_\text{tuned})^2 - \frac{\pi^2 E I}{L^2}$$
Ensures identical fundamental pitch accuracy ($< 0.1\text{ Hz}$ error) between picked notes and legato hammer/pull notes.

### C. Fret Contact & Pizzicato Excitation
- **Hammer-On**: Injects a localized downward impact impulse into the first few grid points near the fretted nut:
  $$u_\text{curr}[1..3] += \text{tapAmp}, \quad \text{tapAmp} = -0.000045 \times \text{vel}$$
- **Pull-Off**: Imparts an authentic left-hand pizzicato triangular release pluck centered at $x_\text{release} = L_\text{new} - L_\text{old}$:
  $$u_\text{curr}[i] += \text{pullAmp} \times \text{triangle}(x_i), \quad \text{pullAmp} = 0.00070 \times \text{vel} \times \text{bassBoost}$$
  Low frets ($\text{fret} \le 2$) use $\text{bassBoost} = 1.55\times$ to balance low-register radiation.

### D. Zero-Allocation Scratch Buffering (v58.f)
To prevent heap allocations (`malloc`) on the audio thread during grid remaps, `saved_curr` and `saved_prev` scratch vectors are pre-reserved to `kMaxGridSize = 512` in `init()`, guaranteeing `RTCheck PASS (0)`.


