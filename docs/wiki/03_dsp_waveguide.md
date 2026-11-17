# 03 — DSP Engine 0: Extended Karplus-Strong Digital Waveguide

## 1. Overview & Operational Principles

Engine 0 implements an **Extended Karplus-Strong Digital Waveguide (DWG)**. It simulates the round-trip propagation of acoustic transverse wave packets along an idealized 1D transmission line.

While Engine 1 (Bilbao FDTD) numerically solves the full elastodynamic PDE along a physical spatial grid, Engine 0 represents wave propagation as lumped bidirectional delay lines terminated by reflection and loss filters.

- **CPU Efficiency** (`bench_synth`, Apple M4, 48 kHz, body convolution included): ~10% of the deadline for 6 voices at 32-sample blocks, ~2.6% at 128. The multi-track stress test sustains ~70–80 six-string tracks at 128-sample blocks across all cores.
- **Application**: Ideal for fast acoustic mockups, mobile/low-power platforms, and pristine, glassy nylon/steel articulations.

---

## 2. Delay Line Architecture & Sub-Sample Fractional Tuning

The fundamental pitch $f_0$ requires an exact round-trip delay length $D$ in samples:
$$D = \frac{f_s}{f_0}$$

Because $D$ is rarely an integer, rounding to the nearest sample causes unacceptable microtonal pitch errors (up to 30–50 cents on high frets).

### Fractional Delay Allpass Filter
The delay line is partitioned into an integer delay $N = \lfloor D - \tau_\text{filters} \rfloor$ and a fractional delay $\eta \in [0, 1)$:
$$A(z) = \frac{a + z^{-1}}{1 + a z^{-1}}, \quad a = \frac{1 - \eta}{1 + \eta}$$
- **Measured tuning (v64.f)**: the engine is consistently **sharp**: +2.9 cents at E2, +1.5 at E3, +1.1 at E4, +0.7 at C5, +0.3 at C6. The figures are the same with the body removed, so the detuning is real, not a measurement artifact. It is not a constant-sample delay error. The likely cause is that the loss and dispersion filters' phase delay at $f_0$ differs from the DC group delay used for compensation; not yet fixed. (Earlier docs claimed ±0.05 cents; that was never measured.)
- Unity gain $|A(e^{j\omega})| = 1.0$ at all frequencies; avoids numerical energy loss in the pitch-tuning stage.

---

## 3. String Inharmonicity Dispersion Modeling

Real acoustic strings have non-zero bending stiffness ($E I > 0$). Higher partials travel faster than the fundamental, producing inharmonic overtones:
$$f_n \approx n f_0 \sqrt{1 + B n^2}, \quad B = \frac{\pi^3 E I}{8 L^2 T_0}$$

In Engine 0, stiffness is modeled via a cascaded first-order allpass dispersion filter:
$$D(z) = \frac{-C + z^{-1}}{1 - C z^{-1}}$$
- $C = 0$: Pure harmonic string (plucked classical nylon).
- $C > 0$: Steel wire dispersion (high partials arrive earlier, creating acoustic snap and twang).
- The group delay of the dispersion stage is subtracted from the main loop delay to preserve exact fundamental pitch $f_0$.

---

## 4. Loop Loss & Damping Filters

At each round-trip reflection, acoustic energy is dissipated by air friction, internal string damping, and bridge impedance absorption.

### 1st-Order Lowpass Loop Filter
$$H_\text{loss}(z) = (1 - S) + S z^{-1}$$
- $S \in [0, 0.5]$ controls the frequency-dependent decay rate.
- High frequencies decay exponentially faster than low fundamentals.
- **DC Group Delay Compensation**: $H_\text{loss}(z)$ introduces an exact group delay of $S$ samples at $\omega = 0$, which is subtracted during allpass fractional pitch tuning.

### Dynamic Tension Modulation (Pitch Settle): not active
`KarplusStrong` keeps a `tensionOffset` state, but `trigger()` sets it to 0 and `tick()` never reads it, so the waveguide engine has **no** pitch settle. It was disabled to keep nylon tuning stable. Amplitude-dependent pitch glide exists only in the FDTD engine, through its Kirchhoff-Carrier nonlinearity.

---

## 5. Waveguide Legato Transition Mechanics (v56.f–v58.f)

When a legato transition occurs on Engine 0 (Digital Waveguide):

1. **Continuous Delay Line Retuning**:
   - The loop delay length and allpass fractional filter coefficients are updated to the new frequency $f_\text{new}$ via `setFrequency()`.
   - Existing circulating delay line contents (`delayLineV`, `delayLineH`) are preserved rather than reset, maintaining ongoing acoustic oscillation.
2. **Fret Contact Energy Scaling**:
   - Fret hand contact dissipates a small fraction of circulating wave momentum:
     - Hammer-On: $\text{energyRetention} = 0.80$ ($20\%$ absorption from finger pad damping).
     - Pull-Off: $\text{energyRetention} = 0.92$ ($8\%$ absorption during quick release).
3. **Mechanical Saddle Impulse Injection**:
   - Warm fret tap impulse injected into write head (`delayLineV`):
     $$\text{tapAmp} = \text{vel} \times (0.040 \text{ hammer} \mid 0.085 \text{ pull})$$
   - Direct airborne plectrum attack bursts are zeroed, ensuring purely mechanical legato articulation.


