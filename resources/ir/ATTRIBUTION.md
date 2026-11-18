# Body Impulse Response Sources

| File | Body in plugin | Source | License |
| :--- | :--- | :--- | :--- |
| `classical_body.wav` | Classical – Contreras 1971 | Mores, guitar g34 (Manuel Contreras, Madrid 1971, classical, cedar/Rio palisander; anechoic lab) | CC BY 4.0 |
| `flamenco_body.wav` | Flamenco – Conde 1964 | Mores, guitar g40 (Faustino Conde, Madrid 1964, flamenca negra, spruce/Rio palisander; anechoic lab) | CC BY 4.0 |
| `legacy_body.wav` | Legacy Body | Unknown. Added in Sept 2026 by an AI coding agent; it may have been found online or generated. Formerly labelled "Gibson Acoustic". | Unknown |

## Mores dataset (CC BY 4.0)

Robert Mores, *Archive for the acoustical documentation of classical Spanish guitars, flamenco guitars and romantic guitars from private and public collections – bridge mobility*, Zenodo, 2021. DOI: [10.5281/zenodo.4604577](https://doi.org/10.5281/zenodo.4604577). Licensed under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).

**Changes made** (as required by CC BY): `benchmarks/extract_mores_irs.py` derives each IR from the dataset's impulse-hammer and near-field microphone recordings:
- regularised spectral division of microphone by hammer force (centre bridge impact);
- stereo from the bass-side (L) and treble-side (R) microphones, 10 cm above the top plate;
- 40 Hz high-pass;
- onset alignment, 300 ms length with a 60 ms fade, peak normalisation;
- 24-bit / 48 kHz WAV.

In the plugin, IRs are additionally energy-normalised and level-trimmed (Classical +0.24 dB, Flamenco +2.36 dB) to match the Legacy body.

## Regenerating

1. Download `qualified_selected_impulses.mat` from the Zenodo record (33 MB, not committed).
2. `.venv/bin/python benchmarks/extract_mores_irs.py <path/to/qualified_selected_impulses.mat> <out_dir> g34 g40`
3. Copy `mores_g34.wav` → `classical_body.wav` and `mores_g40.wav` → `flamenco_body.wav`.
4. `python3 resources/ir/make_embedded_irs.py` to regenerate `src/dsp/EmbeddedIRs.h`.
