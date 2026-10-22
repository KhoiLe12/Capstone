## 1. Automated Real-Time DSP & System Benchmark Suite (`BenchSynth`)

The physical modeling engine is evaluated using an automated, native C++ benchmark harness (`src/bench/BenchSynth.cpp`) coupled with industry-standard compliance runners.

### 1.1 Audio Thread Hard Memory Safety (Zero-Allocation Enforcement)
* **Mechanism:** Global overload of `operator new`, `operator delete`, and sized deallocations monitored via a thread-local execution guard (`g_realtimeThreadActive`).
* **Pass Criterion:** Zero dynamic heap allocations (0 bytes) during active buffer processing (`processBlock()`, `tick()`, `process()`). Any allocation or vector resizing triggers an immediate test failure.

### 1.2 Algorithmic Execution Budget & Tail Latency (Waveguide vs. Bilbao FDTD)
* **Engines Evaluated:** Digital Waveguide (Karplus-Strong) vs. Bilbao Non-Linear Finite-Difference Time-Domain (FDTD).
* **Workload Configurations:** 1 voice (solo), 4 voices (arpeggio), and 6 voices (full guitar chord).
* **Buffer Sweep:** 32, 64, 128, 256, and 512 samples at 48 kHz.
* **Deterministic Metrics:**
  - Hardware Deadline: $\tau_{\text{deadline}} = (\text{BlockSize} / f_s) \times 10^6\ \mu\text{s}$
  - Mean, P99 (99th percentile), and Absolute Peak Execution Time ($\mu\text{s}$)
  - Real-Time CPU Load: $(\tau_{\text{mean}} / \tau_{\text{deadline}}) \times 100\%$
  - Computational Throughput (MSamples/sec)

### 1.3 Workstation Multi-Track Scaling (`BenchThreadPool`)
* **Mechanism:** Headless simulation of a full DAW mixing environment using a zero-allocation worker thread pool matching host physical/logical core counts.
* **Load Scaling:** 1, 4, 8, 16, 24, 32, 48, and 64 concurrent tracks playing continuous 6-string chords (up to 384 concurrently vibrating physical strings).
* **Metrics:** Multithreaded scaling efficiency, deadline overrun detection at 128 samples (2.66 ms), and projection of maximum concurrent track ceilings.

### 1.4 Single-Core Serial Capacity (Auxiliary / Sub-Bus Saturation)
* **Mechanism:** Serial execution of 1, 2, 4, 6, and 8 simultaneous 6-string Bilbao FDTD tracks pinned to a single CPU thread.
* **Purpose:** Simulates worst-case DAW routing graphs (unthreaded serial aux/master busses) to establish per-core voice saturation limits.

### 1.5 Plugin Standard Conformance (`pluginval`)
* **Harness:** Integrated CMake validation target running Tracktion/Steinberg `pluginval` across sample rates (44.1 kHz – 192 kHz) and non-power-of-two buffer sizes (32 – 2048 samples).

---

## 2. Multi-Scale STFT vs. Acoustic Dataset Benchmark (`benchmarks/spectral_eval.py`)

A quantitative objective spectral distance and timbral benchmark comparing the synthesizer's output against studio recordings of authentic acoustic guitars.

### 2.1 Mathematical Formulation
Evaluates multi-resolution Short-Time Fourier Transforms across 5 distinct window scales ($M \in \{2048, 1024, 512, 256, 128\}$) to capture both short-time attack transients (nail/plectrum slip) and long-time low-frequency decay:
1. **Spectral Convergence (SC)**:
   $$\mathcal{L}_{\text{SC}} = \frac{\| |S_{\text{ref}}| - |S_{\text{syn}}| \|_F}{\| |S_{\text{ref}}| \|_F}$$
2. **Log-Magnitude STFT Distance ($\mathcal{L}_1$)**:
   $$\mathcal{L}_{\text{log}} = \frac{1}{N} \| \log(|S_{\text{ref}}| + \epsilon) - \log(|S_{\text{syn}}| + \epsilon) \|_1$$
3. **Multi-Scale Total Distance**:
   $$\mathcal{L}_{\text{total}} = \frac{1}{M} \sum_{m=1}^M \left( \mathcal{L}_{\text{SC}}^{(m)} + \mathcal{L}_{\text{log}}^{(m)} \right)$$

### 2.2 Execution & Outputs
* **Command:**
  ```bash
  .venv/bin/python3 benchmarks/spectral_eval.py
  ```
* **Renderer:** Uses native `./build/render_audio` CLI executable to synthesize bit-exact audio matching reference chord notes and lengths.
* **Output Plots:** Saves publication-quality 300 DPI comparative spectrogram figures (Dataset Reference vs. Digital Waveguide vs. Bilbao FDTD vs. Absolute Error Residual) to:
  - `benchmarks/plots/stft_comparison_E.png`
  - `benchmarks/plots/stft_comparison_Am.png`

