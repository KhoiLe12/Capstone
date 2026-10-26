# Hybrid Physical Modeling Synthesizer — LLM & Developer Wiki

Welcome to the internal engineering knowledge base for the **Hybrid Physical Modeling Synthesizer** (JUCE / VST3 / AU / LV2 / Standalone).

This wiki serves as the persistent technical truth for both human engineers and AI coding agents. It details physical modeling mathematics, discrete DSP algorithms, acoustic coupling networks, real-time safety invariants, and MIDI performance interfaces.

---

## Wiki Navigation & Topic Index

| Topic Document | Key Subjects Covered |
| :--- | :--- |
| **[`01_architecture_overview.md`](01_architecture_overview.md)** | Top-level architecture, 6-string voice allocation, polyphonic bridge scaling, soundboard reflection network, audio thread flow. |
| **[`02_dsp_bilbao_fdtd.md`](02_dsp_bilbao_fdtd.md)** | Engine 1: Stefan Bilbao elastodynamic non-linear FDTD string PDE, dual polarizations ($u_V, u_H$), CFL stability, Woodbury/Sherman-Morrison contact solver. |
| **[`03_dsp_waveguide.md`](03_dsp_waveguide.md)** | Engine 0: Extended Karplus-Strong digital waveguide, allpass fractional pitch tuning, 1st-order stiffness dispersion filter, dynamic loop loss. |
| **[`04_body_and_bridge_acoustics.md`](04_body_and_bridge_acoustics.md)** | Dual-axis bridge saddle force extraction, 6-string sympathetic cross-coupling matrix, partitioned stereo body IR convolution & 32-mode IRCAM Modalys bank. |
| **[`05_articulations_and_keyswitches.md`](05_articulations_and_keyswitches.md)** | Articulations: Open sustain, selective palm muting, unpitched acoustic dead-note ("X" note) bridge dipoles, choke slap mechanics, MIDI keyswitch specification. |
| **[`06_realtime_safety_and_qa.md`](06_realtime_safety_and_qa.md)** | Audio thread zero-allocation invariants, `bench_synth` real-time harness, `pluginval` strictness level 8 verification, CI matrix. |
| **[`07_release_and_milestones.md`](07_release_and_milestones.md)** | Complete version history and roadmap milestones from `v36.f` to current `v55.f`. |

---

## Agent Usage & Maintenance Guidelines

When working in this repository:
1. **Read On-Demand**: Read only the wiki chapter relevant to the current task to preserve context window tokens.
2. **Synchronize Changes**: Whenever a DSP equation is updated, a parameter range is altered, or an articulation is modified, update the corresponding wiki page as part of the task completion checklist.
3. **Preserve Invariants**: Never compromise the zero-allocation invariant (`RTCheck: PASS (0)`) on the audio rendering callback, never introduce artificial synthetic sine generators into physical acoustic loops, and never execute `git push` without explicit user authorization.

