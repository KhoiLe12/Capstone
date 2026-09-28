# Theoretical Frameworks & Acoustic Engineering Documentation
## Capstone Project: Physically Modeled Hybrid Acoustic Guitar Synthesizer

This document compiles the complete theoretical foundations, mathematical derivations, acoustic physics models, and digital signal processing (DSP) architectures implemented in this project.

---

## Table of Contents
1. [Core Digital Waveguide & Karplus-Strong Theory](#1-core-digital-waveguide--karplus-strong-theory)
2. [2D Dual-Polarization Modeling & Double-Decay Dynamics](#2-2d-dual-polarization-modeling--double-decay-dynamics)
3. [Physical Exciter & Nonlinear Contact Mechanics](#3-physical-exciter--nonlinear-contact-mechanics)
4. [Bridge Saddle Boundary Conditions & Mechanical Impedance](#4-bridge-saddle-boundary-conditions--mechanical-impedance)
5. [Acoustic Soundboard Resonance & Body Convolution](#5-acoustic-soundboard-resonance--body-convolution)
6. [Modal Resonator Bank (IRCAM Modalys Formulation)](#6-modal-resonator-bank-ircam-modalys-formulation)
7. [Polyphonic Impedance Headroom & Signal Conditioning](#7-polyphonic-impedance-headroom--signal-conditioning)
8. [Canonical Academic References](#8-canonical-academic-references)

---

## 1. Core Digital Waveguide & Karplus-Strong Theory

### 1.1 The 1D Wave Equation
The transverse motion of an ideal, lossless, flexible string under tension $T$ and linear mass density $\mu$ is governed by the 1D wave equation:

$$\frac{\partial^2 y}{\partial t^2} = c^2 \frac{\partial^2 y}{\partial x^2}$$

where $c = \sqrt{T / \mu}$ is the transverse wave propagation speed.

D'Alembert's traveling-wave solution expresses the displacement $y(x, t)$ as the superposition of two non-dispersive waves traveling in opposite directions:

$$y(x, t) = y^+(t - x/c) + y^-(t + x/c)$$

### 1.2 Digital Waveguide Discretization
In the digital domain, a lossless string terminated by rigid boundaries ($y(0, t) = y(L, t) = 0$) is modeled by two delay lines of length $N = f_s / (2 f_0)$ samples circulating in a closed loop with phase inversion ($\times -1$) at the boundary terminations:

$$y[n] = y^+[n] + y^-[n]$$

The total round-trip loop delay $N$ determines the fundamental frequency $f_0$:

$$N = \frac{f_s}{f_0} = D_{\text{integer}} + D_{\text{fractional}}$$

### 1.3 Fractional Delay Allpass Interpolation
Because the required delay $N$ rarely falls on an integer sample, fractional sample delays are achieved using a 1st-order Thiran / Lagrange allpass filter $A_{\text{frac}}(z)$:

$$A_{\text{frac}}(z) = \frac{\eta + z^{-1}}{1 + \eta z^{-1}}$$

where the coefficient $\eta$ is derived from the desired fractional delay $d \in [0, 1)$:

$$\eta \approx \frac{1 - d}{1 + d}$$

This guarantees exact pitch tuning across all microtonal frequencies without introducing numerical damping or amplitude loss at the fundamental.

### 1.4 Frequency-Dependent Damping (Loss Filter)
Real acoustic strings experience internal viscoelastic friction and air viscous drag, causing higher harmonics to decay significantly faster than the fundamental. This is modeled using a 1st-order one-zero lowpass filter:

$$H_{\text{loss}}(z) = (1 - S) + S z^{-1}, \quad S \in (0, 0.5)$$

* **Low notes (long delay lines)**: $S \approx 0.32$ (prevents buzzing accumulation of hundreds of high harmonics).
* **High notes (short delay lines)**: $S \approx 0.16$ (preserves bell-like treble chime).

### 1.5 Inharmonicity & Dispersion (Euler-Bernoulli Beam Model)
Real strings possess non-zero bending stiffness ($EI$). The governing equation becomes the Euler-Bernoulli stiff string equation:

$$\mu \frac{\partial^2 y}{\partial t^2} = T \frac{\partial^2 y}{\partial x^2} - E I \frac{\partial^4 y}{\partial x^4}$$

This causes wave velocity to increase with frequency ($c(\omega) = \sqrt{T/\mu + (EI/\mu)\omega^2}$), shifting partials sharp according to:

$$f_n \approx n f_0 \sqrt{1 + B n^2}, \quad B = \frac{\pi^3 E I}{8 T L^2}$$

In our engine, this dispersion is synthesized using a 1st-order allpass filter in the feedback loop:

$$H_{\text{disp}}(z) = \frac{D + z^{-1}}{1 + D z^{-1}}$$

For classical nylon strings, bending stiffness is minimal ($D \in [-0.015, 0.0]$), preserving pure harmonic integers without metallic/banjo twang.

---

## 2. 2D Dual-Polarization Modeling & Double-Decay Dynamics

### 2.1 The Two Orthogonal Planes of Vibration
A real guitar string does not vibrate in a single plane. It moves in two orthogonal polarizations:
1. **Vertical Polarization ($y_V$)**: Perpendicular to the soundboard top plate.
2. **Horizontal Polarization ($y_H$)**: Parallel to the soundboard top plate.

```
                  Vertical (y_V)
                  ▲   (Flexible saddle drive, fast decay)
                  │
                  │
   Horizontal ────┼────► (y_H) (Rigid saddle, slow singing sustain)
                  │
                  ▼
         [ Soundboard Top Plate ]
```

### 2.2 Boundary Impedance Anisotropy & Double-Decay
The guitar bridge saddle is mechanically compliant in the vertical direction (driving the top plate) but extremely stiff in the horizontal plane:
* **Vertical Waveguide**: High energy transfer to the body $\implies$ shorter decay time $\tau_V \approx 0.25\text{--}0.85\text{ s}$.
* **Horizontal Waveguide**: Low energy transfer $\implies$ prolonged singing sustain $\tau_H \approx 1.20\text{--}4.50\text{ s}$.

The loop gains are physics-calibrated across all frequencies $f_0$:

$$g_V = \exp\left(-\frac{1}{f_0 \cdot \tau_V}\right), \quad g_H = \exp\left(-\frac{1}{f_0 \cdot \tau_H}\right)$$

This creates the classic **double-decay** envelope profile characteristic of master-grade acoustic instruments: a punchy, woody attack followed by a long, singing sustain floor.

### 2.3 Anisotropic Micro-Detuning & Acoustic Shimmer
Because bridge compliance differs between vertical and horizontal axes, the effective string length differs slightly, introducing a micro-detuning $\Delta f \approx 0.18\text{ Hz}$:

$$f_H = f_0 + 0.18\text{ Hz}$$

As the two polarizations circulate and couple into the saddle, they slowly beat against one another, creating organic acoustic shimmer without artificial LFO chorus.

---

## 3. Physical Exciter & Nonlinear Contact Mechanics

### 3.1 Initial Plucked String Displacement Profile
When a finger or plectrum plucks a string at coordinate $x_p \in (0, L)$, the initial static string shape is a piecewise linear triangle:

$$y(x, 0) = \begin{cases} 
\frac{h}{x_p} x, & 0 \le x \le x_p \\
\frac{h}{L - x_p} (L - x), & x_p < x \le L
\end{cases}$$

Spatial comb-filtering occurs naturally: harmonics with nodes at $x_p$ ($n = L / x_p$) receive zero energy.

### 3.2 Fingernail / Plectrum Slip Dynamics
As the string is drawn by the finger and reaches the release threshold, it slips over the edge of the fingernail or plectrum. This injects a localized high-frequency friction scrape and release snap:

$$\Delta y_{\text{snap}}[n] = w[n] \cdot S_{\text{snap}} \cdot \left[(1 - \alpha_{\text{scrape}}) + \alpha_{\text{scrape}} \cdot \mathcal{N}_{\text{diff}}[n]\right]$$

where $\mathcal{N}_{\text{diff}}[n] = \text{noise}[n] - h_f \cdot \text{noise}[n-1]$ represents differentiated friction noise over the string's winding ridges.

### 3.3 Dynamic Velocity Response
Physical plucking displacement and spectral brightness scale nonlinearly with strike velocity:

$$h \propto 0.30 + 0.70 \cdot (\text{velocity})^{1.15}$$
$$f_{\text{brightness}} = 800\text{ Hz} + (\text{velocity} \cdot \text{brightness})^{1.35} \cdot 13200\text{ Hz}$$

---

## 4. Bridge Saddle Boundary Conditions & Mechanical Impedance

### 4.1 Bridge Force vs. String Displacement
The soundboard is driven by the dynamic transverse force $F_b(t)$ exerted by the string on the bridge saddle, **not** by string displacement:

$$F_b(t) = -T \left. \frac{\partial y}{\partial x} \right|_{x = L}$$

Using the traveling-wave relationship $\frac{\partial y}{\partial x} = -\frac{1}{c} \frac{\partial y}{\partial t}$ and wave impedance $Z_0 = \sqrt{T \mu} = T / c$:

$$F_b(t) = \frac{T}{c} \frac{\partial y}{\partial t} = Z_0 \, v(L, t)$$

In discrete time, the spatial slope is computed from string velocity normalized by angular frequency:

$$F_{\text{spatial}}[n] = (x[n] - x[n-1]) \cdot \left(\frac{f_s}{2 \pi f_0}\right)$$

The factor $(f_s / 2\pi f_0)$ cancels the frequency dependency of the discrete backward difference, maintaining uniform bridge drive amplitude ($\approx 0.8\text{ peak}$) across all octaves (E2 to E5).

### 4.2 Bone Saddle Mechanical Impedance Filter
A real bone saddle and rosewood bridge have finite mass and viscoelastic compliance. They cannot transmit infinitely fast transients. This terminating bridge impedance is modeled by a 1st-order lowpass filter at $f_c \approx 4.5\text{ kHz}$:

$$v_{\text{saddle}}[n] = (1 - \beta) \cdot F_{\text{spatial}}[n] + \beta \cdot v_{\text{saddle}}[n-1]$$

$$\beta = \exp\left(-\frac{2\pi \cdot 4500}{f_s}\right)$$

This eliminates discrete differentiator hash while preserving full physical acoustic punch.

### 4.3 Saddle Force Summing
The composite bridge force driving the soundboard incorporates direct vertical drive plus saddle rocking cross-coupling:

$$F_{\text{bridge}}[n] = v_{\text{saddle}, V}[n] + 0.35 \cdot v_{\text{saddle}, H}[n]$$

---

## 5. Acoustic Soundboard Resonance & Body Convolution

### 5.1 Acoustic Pressure Transfer Function
The acoustic body radiation is governed by the soundboard mechanical mobility and radiation efficiency transfer function:

$$H(s) = \frac{P(s)}{F_b(s)}$$

where $P(s)$ is radiated acoustic pressure at the listener and $F_b(s)$ is bridge force.

### 5.2 Zero-Latency Partitioned Convolution
Implemented via `juce::dsp::Convolution` configured with `Latency { 0 }`. It partitions the impulse response into:
1. **Head (Zero Latency)**: Direct time-domain FIR convolution for the current block ($0\text{ samples}$ latency).
2. **Tail (Frequency Domain)**: Uniformly partitioned FFT overlap-add convolution for the reverberant soundboard decay.

### 5.3 Acoustic Dipole Rolloff & Helmholtz Resonator ($A0$)
Below the air cavity resonance ($A0 \approx 100\text{ Hz}$), acoustic radiation from the soundhole and top plate are $180^\circ$ out of phase, creating an acoustic dipole with a steep $-12\text{ dB/octave}$ cancellation rolloff. This was formulated with a 2nd-order highpass filter ($f_c \approx 92\text{ Hz}$, $Q = 0.707$), preventing low-E (82 Hz) resonant accumulation.

### 5.4 Acoustic Radiation Blending
To seamlessly transition from direct DI bridge pickup to fully radiated acoustic mic soundboard:

$$y_{\text{out}}[n] = D(c) \cdot x_{\text{bridge}}[n] + W(c) \cdot x_{\text{convolved}}[n]$$

$$D(c) = (1 - c)^2, \quad W(c) = 0.75 \cdot \sqrt{c}, \quad c = \text{BodyCoupling} \in [0, 1]$$

---

## 6. Modal Resonator Bank (IRCAM Modalys Formulation)

In addition to empirical IR convolution, the synthesizer implements a 32-mode parallel modal soundboard derived from IRCAM Modalys finite-element measurements of spruce and cedar soundboards.

Each mode $i$ is realized as an exact 2nd-order resonant bandpass biquad:

$$H_i(z) = \frac{g_i \cdot (1 - r_i^2) \cdot z^{-1}}{1 - 2 r_i \cos(\omega_i) z^{-1} + r_i^2 z^{-2}}$$

where:
$$\omega_i = \frac{2\pi \cdot (f_i / \text{bodySize})}{f_s}$$
$$r_i = \exp\left(-\frac{\pi \cdot f_i}{Q_i \cdot \text{bodyDamping} \cdot f_s}\right)$$

Stereo radiation incorporates acoustic cross-soundboard diffusion via a 16-sample ($0.33\text{ ms}$) Haas delay network.

---

## 7. Polyphonic Impedance Headroom & Signal Conditioning

### 7.1 Acoustic Soundboard Polyphonic Power Scaling
When multiple strings vibrate simultaneously, the mechanical impedance of the soundboard distributes the energy among the active strings. Incoherent polyphonic summing scales with:

$$\text{polyScale} = \frac{1}{\sqrt{N_{\text{active}}}}$$

* 1 string: $1.00\times$ (full dynamic punch on single-note lines)
* 4 strings: $0.50\times$ (prevents chord buildup)
* 6 strings: $0.408\times$ (clean 6-string strumming)

### 7.2 DC Blocking Filter
Mechanical differentiation and nonlinear excitation can introduce minor DC offsets. A single-pole DC blocker is placed at the master output:

$$y_{\text{dc}}[n] = x[n] - x[n-1] + R \cdot y_{\text{dc}}[n-1], \quad R = 0.9974 \implies f_c \approx 10\text{ Hz}$$

### 7.3 Transparent Musical Soft Saturation
To prevent hard digital clipping on extreme velocity plucks while maintaining linear acoustic transparency under normal playing:

$$\text{softLimit}(x) = \begin{cases}
x, & |x| \le x_{\text{th}} \\
\text{sgn}(x) \left[ x_{\text{th}} + (x_{\text{ceil}} - x_{\text{th}}) \tanh\left(\frac{|x| - x_{\text{th}}}{x_{\text{ceil}} - x_{\text{th}}}\right) \right], & |x| > x_{\text{th}}
\end{cases}$$

with $x_{\text{th}} = 0.75$ and $x_{\text{ceil}} = 0.98$. Normal guitar passages remain $100\%$ linear, while peak transients compress smoothly with zero odd-order square-wave fuzz.

---

## 8. Canonical Academic References

1. **Karplus, K., & Strong, A. (1983)**. *Digital Synthesis of Plucked-String and Drum Timbres*. Computer Music Journal, 7(2), 43–55.
2. **Jaffe, D. A., & Smith, J. O. (1983)**. *Extensions of the Karplus-Strong Plucked-String Algorithm*. Computer Music Journal, 7(2), 56–69.
3. **Smith, J. O. (1992)**. *Physical Modeling Using Digital Waveguides*. Computer Music Journal, 16(4), 74–91.
4. **Smith, J. O. (2010)**. *Physical Audio Signal Processing: For Virtual Musical Instruments and Digital Audio Effects*. W3K Publishing / CCRMA, Stanford University.
5. **Weinreich, G. (1977)**. *Coupled piano strings*. The Journal of the Acoustical Society of America, 62(6), 1474–1484. *(Foundational theory on orthogonal dual-polarization and double-decay dynamics).*
6. **Fletcher, N. H., & Rossing, T. D. (1998)**. *The Physics of Musical Instruments* (2nd ed.). Springer-Verlag, New York. *(Chapter 9: The Acoustic Guitar — soundboard modes, Helmholtz air coupling, and bridge impedance).*
7. **Woodhouse, J. (2004)**. *On the acoustics of the acoustic guitar*. Acta Acustica united with Acustica, 90(5), 928–944.
8. **Chaigne, A., & Doutaut, V. (1997)**. *Numerical simulations of guitar strings: Interaction with the frets and the bridge*. The Journal of the Acoustical Society of America, 101(5), 2969–2978.
9. **Karjalainen, M., Välimäki, V., & Tolonen, T. (1998)**. *Plucked-string models: from the Karplus-Strong algorithm to digital waveguides and beyond*. Computer Music Journal, 22(3), 17–32.
10. **Bilbao, S. (2009)**. *Numerical Sound Synthesis: Finite Difference Schemes and Simulation in Musical Acoustics*. John Wiley & Sons.
