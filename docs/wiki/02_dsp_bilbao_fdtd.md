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

