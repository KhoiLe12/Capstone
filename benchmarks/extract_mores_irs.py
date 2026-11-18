#!/usr/bin/env python3
"""
benchmarks/extract_mores_irs.py

Derives guitar body impulse responses (bridge force -> near-field sound pressure) from

    Robert Mores, "Archive for the acoustical documentation of classical Spanish guitars,
    flamenco guitars and romantic guitars from private and public collections - bridge
    mobility", Zenodo, 2021. DOI: 10.5281/zenodo.4604577. License: CC BY 4.0.

Download `qualified_selected_impulses.mat` from the record above (not committed; 33 MB).

Data layout (from the dataset's Method.pdf):
    qualified_selected_impulses[guitar 1..65, sample 1..144000, channel 1..6], 48 kHz
    samples: three 1 s takes, hammer impact at the bridge bass side / centre / treble side
    channels: 1 impulse hammer (force), 2-3 bridge accelerometers,
              4 mic "upper east", 5 mic treble side, 6 mic bass side
              (mics 10 cm above the top plate; bass/treble mics 20 cm apart over the bridge)
    Guitars g34-g44 were measured in an anechoic laboratory.

Processing (our changes to the data, as required by CC BY):
    1. Transfer function mic / hammer by regularised spectral division (Tikhonov, eps relative
       to the hammer's peak power), centre impact position.
    2. Stereo IR: left = bass-side mic (ch 6), right = treble-side mic (ch 5).
    3. 40 Hz 2nd-order high-pass (hammer and near-field mics are unreliable below).
    4. Onset-aligned (16-sample pre-roll), truncated to LENGTH_S with a raised-cosine fade over
       the last FADE_S, peak-normalised to -1 dBFS. The plugin energy-normalises on load.
    5. Written as 24-bit 48 kHz WAV.

Usage:
    .venv/bin/python benchmarks/extract_mores_irs.py <path/to/qualified_selected_impulses.mat> <out_dir> [g34 g40 ...]
"""

import os
import sys
import wave
import numpy as np
import scipy.io
import scipy.signal

FS = 48000
LENGTH_S = 0.300
FADE_S = 0.060
PRE_ROLL = 16
REG_EPS = 1e-3
CENTRE = 1                     # impact position index: 0 bass, 1 centre, 2 treble
MIC_LEFT, MIC_RIGHT = 6, 5     # bass-side mic, treble-side mic (1-based channel numbers)

GUITARS = {
    34: 'Manuel Contreras 1971, classical (cedar/Rio palisander)',
    35: 'Manuel Lopez Bellido 1978, classical (cedar/Rio palisander)',
    36: 'Jose Lopez Bellido 2001, classical (spruce/Indian palisander)',
    40: 'Faustino Conde 1964, flamenca negra (spruce/Rio palisander)',
    41: 'Valeriano Bernal 1987, flamenca blanca (spruce/cypress)',
    43: 'Jose Ramirez 1975, flamenca blanca (spruce/cypress)',
}


def transfer_ir(hammer, mic, n_fft):
    F = np.fft.rfft(hammer, n_fft)
    M = np.fft.rfft(mic, n_fft)
    eps = REG_EPS * np.max(np.abs(F)) ** 2
    return np.fft.irfft(M * np.conj(F) / (np.abs(F) ** 2 + eps), n_fft)


def hammer_bandwidth(hammer):
    spec = 20 * np.log10(np.abs(np.fft.rfft(hammer)) + 1e-12)
    fr = np.fft.rfftfreq(len(hammer), 1 / FS)
    ref = np.median(spec[(fr > 50) & (fr < 300)])
    above = (fr > 300) & (spec < ref - 20)
    return float(fr[np.argmax(above)]) if above.any() else float(fr[-1])


def make_ir(data, guitar):
    take = data[guitar - 1, CENTRE * FS:(CENTRE + 1) * FS, :].astype(np.float64)
    if not np.isfinite(take).all():
        raise ValueError(f'g{guitar:02d}: missing data')
    hammer = take[:, 0]
    chans = [transfer_ir(hammer, take[:, c - 1], 2 * FS)[:FS] for c in (MIC_LEFT, MIC_RIGHT)]
    ir = np.stack(chans, axis=1)

    sos = scipy.signal.butter(2, 40, 'highpass', fs=FS, output='sos')
    ir = scipy.signal.sosfilt(sos, ir, axis=0)

    env = np.max(np.abs(ir), axis=1)
    onset = int(np.argmax(env > 0.05 * env.max()))
    start = max(0, onset - PRE_ROLL)
    n = int(LENGTH_S * FS)
    ir = ir[start:start + n]
    fade = int(FADE_S * FS)
    ir[-fade:] *= (0.5 * (1 + np.cos(np.pi * np.arange(fade) / fade)))[:, None]
    ir *= 10 ** (-1 / 20) / np.max(np.abs(ir))

    tail = ir[int(0.25 * FS):]
    stats = {
        'hammer_bw_hz': hammer_bandwidth(hammer),
        'onset_ms': onset / FS * 1000,
        'tail_db': float(20 * np.log10(np.sqrt(np.mean(tail ** 2)) + 1e-12)),
        'lr_correlation': float(np.corrcoef(ir[:, 0], ir[:, 1])[0, 1]),
    }
    return ir, stats


def write_wav24(path, x):
    pcm = np.clip(np.round(x * (2 ** 23 - 1)), -2 ** 23, 2 ** 23 - 1).astype('<i4')
    raw = pcm.reshape(-1).view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(x.shape[1])
        w.setsampwidth(3)
        w.setframerate(FS)
        w.writeframes(raw)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mat, out_dir = sys.argv[1], sys.argv[2]
    wanted = [int(a.lstrip('g')) for a in sys.argv[3:]] or sorted(GUITARS)
    data = scipy.io.loadmat(mat, variable_names=['qualified_selected_impulses'])['qualified_selected_impulses']
    os.makedirs(out_dir, exist_ok=True)
    for g in wanted:
        ir, st = make_ir(data, g)
        path = os.path.join(out_dir, f'mores_g{g:02d}.wav')
        write_wav24(path, ir)
        print(f"g{g:02d} {GUITARS.get(g, '')}: hammer usable to {st['hammer_bw_hz']:.0f} Hz, "
              f"onset {st['onset_ms']:.2f} ms, tail (250-300 ms) {st['tail_db']:.1f} dBFS, "
              f"L/R correlation {st['lr_correlation']:.2f} -> {path}")


if __name__ == '__main__':
    main()
