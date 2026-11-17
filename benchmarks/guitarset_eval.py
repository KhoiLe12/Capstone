#!/usr/bin/env python3
"""
benchmarks/guitarset_eval.py

GuitarSet multi-scale STFT comparison of the Hybrid Synth string engines against real
acoustic guitar recordings (GuitarSet, ISMIR 2018: audio_mono-mic + JAMS note annotations).

Metrics (averaged over STFT sizes {2048, 1024, 512, 256, 128}, 75% overlap):
  SC  (spectral convergence)  = || |S_ref| - |S_syn| ||_F / || |S_ref| ||_F      (linear magnitude)
  L1  (log-magnitude distance) = mean | log|S_ref| - log|S_syn| |                 (floor -80 dB re. ref peak)
  Combined = SC + L1. Lower is better for all three.

Method:
  * Every test is re-synthesised from the JAMS note annotations of its window (onsets, durations,
    simultaneous chord notes), via `render_audio --eventsFile`. Velocity is not annotated in
    GuitarSet, so all notes use the same velocity.
  * Synth output is aligned to the reference by envelope cross-correlation and loudness-matched by
    RMS before scoring, so the metrics compare spectro-temporal shape, not level or latency.
  * Isolated notes are selected automatically from all 360 takes: within +/-15 cents of an
    equal-tempered MIDI pitch (the synth has no fine-tune or pitch bend), held >= 1.0 s, no other
    note starting 0.3 s before or inside the 1.5 s window and none still sounding at the onset.
    Only pitches with a clean note in at least two different takes are tested, so every isolated
    test has a real-vs-real reference score.
  * Two reference points put the synth numbers in context:
      - "Real (other take)": a different GuitarSet recording of the same pitch scored against the
        reference. This is roughly the best score achievable given player/take variation.
      - "Naive tone": a decaying additive harmonic tone with the same events. A physical model
        that does not clearly beat this is not being distinguished by the metric.

Usage:
  .venv/bin/python benchmarks/guitarset_eval.py [--plots]
Requires ./build/render_audio and the dataset in benchmarks/data/guitarset/.
"""

import os
import sys
import json
import wave
import tempfile
import subprocess
import numpy as np
import scipy.signal

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA_DIR = os.path.join(HERE, 'data', 'guitarset')
AUDIO_DIR = os.path.join(DATA_DIR, 'audio_mono-mic')
ANNO_DIR = os.path.join(DATA_DIR, 'annotation')
PLOTS_DIR = os.path.join(HERE, 'plots')
RENDERER = os.path.join(ROOT, 'build', 'render_audio')
RESULTS_JSON = os.path.join(HERE, 'guitarset_results.json')

SR = 44100
STFT_SIZES = [2048, 1024, 512, 256, 128]
SYNTH_MIN_NOTE, SYNTH_MAX_NOTE = 40, 86

ISOLATED_WINDOW_SEC = 1.5
ISOLATED_MAX_CENTS = 15.0
ISOLATED_MIN_DUR_SEC = 1.0
ISOLATED_GUARD_SEC = 0.3

PHRASES = [
    ('00_SS1-100-C#_comp', 5.0, 'Singer-songwriter comping'),
    ('00_SS1-100-C#_solo', 6.0, 'Singer-songwriter solo'),
    ('00_BN1-129-Eb_comp', 5.0, 'Bossa nova comping'),
    ('00_Rock1-90-C#_comp', 5.0, 'Rock comping'),
]

ENGINES = [('waveguide', 0), ('fdtd', 1)]


# ---------------------------------------------------------------------------
# Audio I/O
# ---------------------------------------------------------------------------

def load_wav(path, start_sec=0.0, dur_sec=None):
    with wave.open(path, 'rb') as w:
        sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        start = min(int(start_sec * sr), n)
        count = n - start if dur_sec is None else min(int(dur_sec * sr), n - start)
        w.setpos(start)
        raw = w.readframes(count)
    if sw == 2:
        x = np.frombuffer(raw, np.int16).astype(np.float64) / 32768.0
    elif sw == 3:
        a = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        x = np.pad(a, ((0, 0), (1, 0))).view('<i4').ravel().astype(np.float64) / 2**31
    elif sw == 4:
        x = np.frombuffer(raw, np.int32).astype(np.float64) / 2**31
    else:
        raise ValueError(f'Unsupported sample width {sw}')
    x = x.reshape(-1, ch).mean(axis=1)
    if sr != SR:
        x = scipy.signal.resample_poly(x, SR, sr)
    if dur_sec is not None:
        need = int(dur_sec * SR)
        x = np.pad(x, (0, max(0, need - len(x))))[:need]
    return x


# ---------------------------------------------------------------------------
# Annotations
# ---------------------------------------------------------------------------

def load_notes(name):
    """All annotated notes of a take as dicts (time, pitch [fractional MIDI], dur, string)."""
    with open(os.path.join(ANNO_DIR, name + '.jams')) as f:
        jams = json.load(f)
    notes = []
    for anno in jams['annotations']:
        if anno.get('namespace') != 'note_midi':
            continue
        string = anno.get('annotation_metadata', {}).get('data_source')
        for d in anno['data']:
            notes.append({'time': float(d['time']), 'pitch': float(d['value']),
                          'dur': float(d['duration']), 'string': string})
    notes.sort(key=lambda n: n['time'])
    return notes


def window_events(notes, t0, dur):
    """Notes starting inside [t0, t0 + dur), re-timed to the window and rounded to MIDI."""
    events = []
    for n in notes:
        if t0 - 1e-3 <= n['time'] < t0 + dur:
            events.append({'time': max(0.0, n['time'] - t0), 'note': int(round(n['pitch'])),
                           'dur': n['dur'], 'pitch': n['pitch']})
    return events


def find_isolated_notes():
    """Clean single notes across all takes, one per MIDI pitch (spread across the register)."""
    candidates = []
    for fname in sorted(os.listdir(ANNO_DIR)):
        if not fname.endswith('.jams'):
            continue
        name = fname[:-5]
        if not os.path.exists(os.path.join(AUDIO_DIR, name + '_mic.wav')):
            continue
        notes = load_notes(name)
        for i, n in enumerate(notes):
            midi = round(n['pitch'])
            cents = 100.0 * (n['pitch'] - midi)
            if not (SYNTH_MIN_NOTE <= midi <= SYNTH_MAX_NOTE):
                continue
            if abs(cents) > ISOLATED_MAX_CENTS or n['dur'] < ISOLATED_MIN_DUR_SEC:
                continue
            t = n['time']
            clash = False
            for j, o in enumerate(notes):
                if j == i:
                    continue
                # Another note starting near/inside the window, or still sounding at the onset
                if t - ISOLATED_GUARD_SEC <= o['time'] <= t + ISOLATED_WINDOW_SEC:
                    clash = True
                    break
                if o['time'] < t and o['time'] + o['dur'] > t - 0.05:
                    clash = True
                    break
            if not clash:
                candidates.append({'take': name, 'time': t, 'midi': midi, 'cents': cents,
                                   'dur': n['dur'], 'pitch': n['pitch']})
    return candidates


# ---------------------------------------------------------------------------
# Synthesis
# ---------------------------------------------------------------------------

def render(events, engine, dur, tmpdir, tag):
    csv_path = os.path.join(tmpdir, f'{tag}.csv')
    out_path = os.path.join(tmpdir, f'{tag}.wav')
    with open(csv_path, 'w') as f:
        for ev in events:
            f.write(f"{ev['time']:.4f},{ev['note']},0.80,{ev['dur']:.4f}\n")
    cmd = [RENDERER, '--engine', str(engine), '--eventsFile', csv_path,
           '--material', '1', '--bodyType', '1', '--bodyMix', '0.75',
           '--dur', f'{dur:.3f}', '--out', out_path]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f'render_audio failed: {res.stderr}')
    return load_wav(out_path, 0.0, dur)


def naive_tone(events, dur):
    """Decaying additive harmonic tone (1/n partials, faster decay for higher partials)."""
    n = int(dur * SR)
    y = np.zeros(n)
    t_all = np.arange(n) / SR
    for ev in events:
        f0 = 440.0 * 2 ** ((ev['note'] - 69) / 12)
        start = int(ev['time'] * SR)
        t = t_all[: n - start]
        env_off = np.where(t < ev['dur'], 1.0, np.exp(-(t - ev['dur']) / 0.05))
        note = np.zeros_like(t)
        for k in range(1, 30):
            fk = k * f0
            if fk > 0.45 * SR:
                break
            note += (1.0 / k) * np.exp(-t / (1.2 / np.sqrt(k))) * np.sin(2 * np.pi * fk * t)
        y[start:] += note * env_off
    return y


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

def align(ref, syn, max_shift_sec):
    """Shift syn to best match ref's amplitude envelope (cross-correlation)."""
    win = int(0.005 * SR)
    kernel = np.ones(win) / win
    env_ref = np.convolve(np.abs(ref), kernel, 'same')
    env_syn = np.convolve(np.abs(syn), kernel, 'same')
    corr = scipy.signal.correlate(env_ref, env_syn, mode='full', method='fft')
    lags = scipy.signal.correlation_lags(len(env_ref), len(env_syn), mode='full')
    ok = np.abs(lags) <= int(max_shift_sec * SR)
    lag = int(lags[ok][np.argmax(corr[ok])])
    if lag > 0:
        return np.pad(syn, (lag, 0))[:len(ref)]
    if lag < 0:
        return np.pad(syn[-lag:], (0, -lag))[:len(ref)]
    return syn[:len(ref)]


def rms_normalise(x):
    return x / (np.sqrt(np.mean(x ** 2)) + 1e-12)


def multi_scale_stft(ref, syn):
    n = min(len(ref), len(syn))
    ref, syn = rms_normalise(ref[:n]), rms_normalise(syn[:n])
    sc_sum = l1_sum = 0.0
    for size in STFT_SIZES:
        hop = size // 4
        _, _, Zr = scipy.signal.stft(ref, nperseg=size, noverlap=size - hop, boundary=None)
        _, _, Zs = scipy.signal.stft(syn, nperseg=size, noverlap=size - hop, boundary=None)
        Sr, Ss = np.abs(Zr), np.abs(Zs)
        floor = Sr.max() * 10 ** (-80 / 20)
        sc_sum += np.linalg.norm(Sr - Ss) / (np.linalg.norm(Sr) + 1e-12)
        l1_sum += np.mean(np.abs(np.log(np.maximum(Sr, floor)) - np.log(np.maximum(Ss, floor))))
    m = len(STFT_SIZES)
    return {'sc': sc_sum / m, 'l1': l1_sum / m, 'combined': (sc_sum + l1_sum) / m}


def score_all(ref, events, dur, tmpdir, tag, max_shift_sec):
    scores = {}
    for label, engine in ENGINES:
        syn = render(events, engine, dur, tmpdir, f'{tag}_{label}')
        scores[label] = multi_scale_stft(ref, align(ref, syn, max_shift_sec))
    scores['naive_tone'] = multi_scale_stft(ref, align(ref, naive_tone(events, dur), max_shift_sec))
    return scores


# ---------------------------------------------------------------------------
# Optional plot
# ---------------------------------------------------------------------------

def plot(ref, tracks, title, path):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(len(tracks) + 1, 1, figsize=(10, 2.4 * (len(tracks) + 1)), sharex=True)
    for ax, (label, x) in zip(axes, [('GuitarSet reference', ref)] + tracks):
        _, _, Z = scipy.signal.stft(rms_normalise(x), nperseg=1024, noverlap=768)
        ax.imshow(20 * np.log10(np.abs(Z) + 1e-6), aspect='auto', origin='lower',
                  extent=[0, len(x) / SR, 0, SR / 2], vmin=-90, vmax=0, cmap='inferno')
        ax.set_ylim(0, 8000)
        ax.set_title(label, fontsize=10)
        ax.set_ylabel('Hz')
    axes[-1].set_xlabel('s')
    fig.suptitle(title)
    fig.tight_layout()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fig.savefig(path, dpi=150)
    plt.close(fig)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    make_plots = '--plots' in sys.argv
    if not os.path.exists(RENDERER):
        sys.exit(f'Missing {RENDERER}; build the render_audio target first.')
    results = {'isolated': [], 'phrases': []}

    with tempfile.TemporaryDirectory() as tmpdir:
        # ---- Isolated notes ------------------------------------------------
        candidates = find_isolated_notes()
        by_pitch = {}
        for c in candidates:
            by_pitch.setdefault(c['midi'], []).append(c)
        # Only pitches with clean notes in >= 2 takes, so each test has a real-vs-real reference
        chosen = sorted(m for m in by_pitch if len({c['take'] for c in by_pitch[m]}) >= 2)
        print(f'Isolated-note candidates: {len(candidates)} across {len(by_pitch)} pitches; testing {chosen}')

        for midi in chosen:
            group = sorted(by_pitch[midi], key=lambda c: abs(c['cents']))
            c = group[0]
            ref = load_wav(os.path.join(AUDIO_DIR, c['take'] + '_mic.wav'), c['time'], ISOLATED_WINDOW_SEC)
            events = [{'time': 0.0, 'note': midi, 'dur': c['dur']}]
            scores = score_all(ref, events, ISOLATED_WINDOW_SEC, tmpdir, f'iso_{midi}', 0.05)

            other = next((o for o in group[1:] if o['take'] != c['take']), None)
            if other is not None:
                ref2 = load_wav(os.path.join(AUDIO_DIR, other['take'] + '_mic.wav'), other['time'], ISOLATED_WINDOW_SEC)
                scores['real_other_take'] = multi_scale_stft(ref, align(ref, ref2, 0.05))

            entry = {'midi': midi, 'take': c['take'], 'time': round(c['time'], 3),
                     'cents': round(c['cents'], 1), 'other_take': other['take'] if other else None,
                     'scores': scores}
            results['isolated'].append(entry)
            print(f"  MIDI {midi:2d} ({c['take']} @ {c['time']:.2f}s, {c['cents']:+.1f} c): "
                  + '  '.join(f"{k} {v['combined']:.3f}" for k, v in scores.items()))
            if make_plots:
                tracks = [(lbl, align(ref, render(events, eng, ISOLATED_WINDOW_SEC, tmpdir, f'p{midi}{lbl}'), 0.05))
                          for lbl, eng in ENGINES]
                plot(ref, tracks, f'Isolated MIDI {midi}', os.path.join(PLOTS_DIR, f'guitarset_note_{midi}.png'))

        # ---- Continuous phrases -------------------------------------------
        for take, dur, desc in PHRASES:
            if not os.path.exists(os.path.join(AUDIO_DIR, take + '_mic.wav')):
                print(f'  (skipping {take}: audio missing)')
                continue
            notes = load_notes(take)
            t0 = notes[0]['time']
            events = [e for e in window_events(notes, t0, dur) if SYNTH_MIN_NOTE - 2 <= e['note'] <= SYNTH_MAX_NOTE]
            ref = load_wav(os.path.join(AUDIO_DIR, take + '_mic.wav'), t0, dur)
            scores = score_all(ref, events, dur, tmpdir, f'phr_{take}', 0.1)
            off_tune = float(np.mean([abs(100 * (e['pitch'] - e['note'])) for e in events]))
            results['phrases'].append({'take': take, 'description': desc, 'start': round(t0, 3),
                                       'duration': dur, 'num_notes': len(events),
                                       'mean_abs_cents_from_midi': round(off_tune, 1), 'scores': scores})
            print(f"  {take} ({len(events)} notes, mean |{off_tune:.0f}| c off-grid): "
                  + '  '.join(f"{k} {v['combined']:.3f}" for k, v in scores.items()))
            if make_plots:
                tracks = [(lbl, align(ref, render(events, eng, dur, tmpdir, f'pp{lbl}'), 0.1)) for lbl, eng in ENGINES]
                plot(ref, tracks, desc, os.path.join(PLOTS_DIR, f'guitarset_{take}.png'))

    # ---- Summary ------------------------------------------------------------
    def summarise(entries, key):
        vals = [e['scores'][key] for e in entries if key in e['scores']]
        if not vals:
            return None
        return {m: float(np.mean([v[m] for v in vals])) for m in ('sc', 'l1', 'combined')} | {'n': len(vals)}

    summary = {}
    for group in ('isolated', 'phrases'):
        summary[group] = {k: summarise(results[group], k)
                          for k in ('real_other_take', 'fdtd', 'waveguide', 'naive_tone')}
        summary[group] = {k: v for k, v in summary[group].items() if v}
    for group in ('isolated', 'phrases'):
        entries = results[group]
        summary[group]['fdtd_beats_waveguide'] = {
            m: int(sum(e['scores']['fdtd'][m] < e['scores']['waveguide'][m] for e in entries))
            for m in ('sc', 'l1', 'combined')} | {'n': len(entries)}
    results['summary'] = summary
    results['method'] = {'stft_sizes': STFT_SIZES, 'normalisation': 'RMS', 'alignment': 'envelope xcorr',
                         'synth_settings': 'steel strings, Gibson IR body, body mix 0.75, velocity 0.80'}

    print('\nMean scores (lower is better)')
    print(f"{'group':10} {'system':16} {'n':>3} {'SC':>7} {'logL1':>7} {'combined':>9}")
    for group, rows in summary.items():
        for k, v in rows.items():
            if k == 'fdtd_beats_waveguide':
                print(f"{group:10} FDTD better than waveguide in: SC {v['sc']}/{v['n']}, "
                      f"logL1 {v['l1']}/{v['n']}, combined {v['combined']}/{v['n']} tests")
                continue
            print(f"{group:10} {k:16} {v['n']:3d} {v['sc']:7.3f} {v['l1']:7.3f} {v['combined']:9.3f}")

    with open(RESULTS_JSON, 'w') as f:
        json.dump(results, f, indent=2)
    print(f'\nSaved {RESULTS_JSON}')


if __name__ == '__main__':
    main()
