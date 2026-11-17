#!/usr/bin/env python3
"""
benchmarks/decay_calibration.py

Measures how fast the fundamental of a held note decays (dB/s) as a function of pitch, on real
GuitarSet recordings (steel-string acoustic) and on the Hybrid Synth FDTD engine, and fits a
power law  decay(f) = a * (f / 82.4 Hz)^p  to each. Used to calibrate the register-dependent
damping in Voice::makeFdtdParams().

Method:
  * GuitarSet notes held >= MIN_DUR s, with no other note starting during the note (+0.2 s guard)
    and none sounding at its onset. Pitch may be up to 30 cents off-grid (decay, not tuning, is
    measured).
  * The fundamental is isolated by complex demodulation at the annotated pitch (30 ms smoothing);
    its dB envelope is line-fitted from onset + 80 ms to note-off - 50 ms, keeping only the part
    more than 12 dB above the post-note noise floor. This ignores the pick click and attack.
  * Each note is re-rendered alone with render_audio (FDTD, steel, Gibson IR, same duration) and
    measured identically.

Usage: .venv/bin/python benchmarks/decay_calibration.py [--engine 1] [--material 1]
"""

import os
import sys
import tempfile
import subprocess
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guitarset_eval as gs  # noqa: E402  (shared loaders, paths, SR)

MIN_DUR = 0.7
GUARD = 0.2
MAX_CENTS = 30.0
F_REF = 82.4


def isolated_held_notes():
    notes_out = []
    for fname in sorted(os.listdir(gs.ANNO_DIR)):
        if not fname.endswith('.jams'):
            continue
        name = fname[:-5]
        if not os.path.exists(os.path.join(gs.AUDIO_DIR, name + '_mic.wav')):
            continue
        notes = gs.load_notes(name)
        for i, n in enumerate(notes):
            midi = round(n['pitch'])
            if not (gs.SYNTH_MIN_NOTE <= midi <= gs.SYNTH_MAX_NOTE) or n['dur'] < MIN_DUR:
                continue
            if abs(100 * (n['pitch'] - midi)) > MAX_CENTS:
                continue
            t, d = n['time'], n['dur']
            clash = any(j != i and ((t - GUARD <= o['time'] <= t + d + GUARD) or
                                    (o['time'] < t and o['time'] + o['dur'] > t - 0.05))
                        for j, o in enumerate(notes))
            if not clash:
                notes_out.append({'take': name, 'time': t, 'dur': d, 'pitch': n['pitch'], 'midi': midi})
    return notes_out


def fundamental_decay_db_per_s(x, f0, dur):
    """Line-fit slope (dB/s) of the fundamental's envelope during the held part of the note."""
    t = np.arange(len(x)) / gs.SR
    z = x * np.exp(-2j * np.pi * f0 * t)
    k = int(0.03 * gs.SR)
    env = np.abs(np.convolve(z, np.ones(k) / k, 'same'))
    env_db = 20 * np.log10(env + 1e-12)
    floor = np.median(env_db[int((dur + 0.15) * gs.SR):]) if len(x) > int((dur + 0.3) * gs.SR) else env_db.min()
    a, b = int(0.08 * gs.SR), int((dur - 0.05) * gs.SR)
    seg_t, seg = t[a:b], env_db[a:b]
    keep = seg > floor + 12.0
    if keep.sum() < int(0.25 * gs.SR):
        return None
    slope = np.polyfit(seg_t[keep], seg[keep], 1)[0]
    return -slope


def fit_power_law(f, r):
    """Least-squares fit of log(r) = log(a) + p * log(f / F_REF)."""
    p, loga = np.polyfit(np.log(f / F_REF), np.log(r), 1)
    return float(np.exp(loga)), float(p)


def robust_fit(f, r, n_boot=2000, seed=0):
    """Theil-Sen slope in log-log (robust to outliers) plus a bootstrap 90% interval of the LS slope."""
    x, y = np.log(f / F_REF), np.log(r)
    i, j = np.triu_indices(len(x), 1)
    ok = np.abs(x[i] - x[j]) > 1e-6
    p_ts = float(np.median((y[i] - y[j])[ok] / (x[i] - x[j])[ok]))
    a_ts = float(np.exp(np.median(y - p_ts * x)))
    rng = np.random.default_rng(seed)
    boots = []
    for _ in range(n_boot):
        k = rng.integers(0, len(x), len(x))
        boots.append(np.polyfit(x[k], y[k], 1)[0])
    lo, hi = np.percentile(boots, [5, 95])
    return a_ts, p_ts, float(lo), float(hi)


def main():
    engine = int(sys.argv[sys.argv.index('--engine') + 1]) if '--engine' in sys.argv else 1
    material = int(sys.argv[sys.argv.index('--material') + 1]) if '--material' in sys.argv else 1
    notes = isolated_held_notes()
    print(f'{len(notes)} isolated held notes (>= {MIN_DUR} s) across '
          f'{len({n["midi"] for n in notes})} pitches')

    real, synth = [], []
    with tempfile.TemporaryDirectory() as tmp:
        for k, n in enumerate(notes):
            f0 = 440.0 * 2 ** ((n['pitch'] - 69) / 12)
            length = n['dur'] + 0.6
            ref = gs.load_wav(os.path.join(gs.AUDIO_DIR, n['take'] + '_mic.wav'), n['time'], length)
            r = fundamental_decay_db_per_s(ref, f0, n['dur'])
            if r is None or r <= 0:
                continue

            csv = os.path.join(tmp, 'n.csv')
            wav = os.path.join(tmp, 'n.wav')
            with open(csv, 'w') as fh:
                fh.write(f"0.0,{n['midi']},0.80,{n['dur']:.4f}\n")
            subprocess.run([gs.RENDERER, '--engine', str(engine), '--eventsFile', csv, '--material', str(material),
                            '--bodyType', '1', '--bodyMix', '0.75', '--dur', f'{length:.3f}', '--out', wav],
                           check=True, capture_output=True)
            syn = gs.load_wav(wav, 0.0, length)
            s = fundamental_decay_db_per_s(syn, 440.0 * 2 ** ((n['midi'] - 69) / 12), n['dur'])
            if s is None or s <= 0:
                continue
            real.append((f0, r))
            synth.append((f0, s))

    real, synth = np.array(real), np.array(synth)
    print(f'{len(real)} notes measured on both\n')
    a_r, p_r = fit_power_law(real[:, 0], real[:, 1])
    a_s, p_s = fit_power_law(synth[:, 0], synth[:, 1])
    print(f'Real GuitarSet : decay = {a_r:5.2f} dB/s * (f/82.4)^{p_r:.2f}  (least squares)')
    print(f'Synth          : decay = {a_s:5.2f} dB/s * (f/82.4)^{p_s:.2f}  (least squares)')
    for label, arr in (('Real', real), ('Synth', synth)):
        a_ts, p_ts, lo, hi = robust_fit(arr[:, 0], arr[:, 1])
        print(f'{label:6} robust : decay = {a_ts:5.2f} dB/s * (f/82.4)^{p_ts:.2f}   exponent 90% CI [{lo:.2f}, {hi:.2f}]')
    err = np.log(synth[:, 1] / real[:, 1])
    print(f'Per-note synth/real decay ratio: median {np.exp(np.median(err)):.2f}, '
          f'median |log error| {np.median(np.abs(err)):.2f} ({100 * (np.exp(np.median(np.abs(err))) - 1):.0f}% typical deviation)\n')

    print(f"{'band':>14} {'n':>3} {'real dB/s':>10} {'synth dB/s':>11} {'synth/real':>10}")
    edges = [70, 110, 165, 250, 370, 560, 1300]
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (real[:, 0] >= lo) & (real[:, 0] < hi)
        if m.sum() == 0:
            continue
        rm, sm = np.median(real[m, 1]), np.median(synth[m, 1])
        print(f'{lo:5d}-{hi:4d} Hz {m.sum():3d} {rm:10.1f} {sm:11.1f} {sm / rm:10.2f}')


if __name__ == '__main__':
    main()
