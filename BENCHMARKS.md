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

## 2. GuitarSet Multi-Scale STFT Benchmark (`benchmarks/guitarset_eval.py`)

Compares both string engines against real GuitarSet recordings: 12 automatically selected isolated notes and 4 annotated phrases. Every test is re-synthesised from the annotations, aligned, and RMS-normalised, and scored with spectral convergence and log-magnitude L1 over five STFT sizes. A second real take of the same pitch and a naive harmonic tone serve as reference points.

```bash
.venv/bin/python benchmarks/guitarset_eval.py [--plots]
```

Method, current results, and how to interpret them (including what the benchmark does *not* show) are in [wiki 06 §5](docs/wiki/06_realtime_safety_and_qa.md).

`benchmarks/spectral_eval.py` is the older chord comparison against downloaded Fender recordings. It uses peak normalisation and guessed chord voicings; it is kept for reference only and is not used for any claims.
