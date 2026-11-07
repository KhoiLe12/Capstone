#!/usr/bin/env python3
"""
benchmarks/guitarset_eval.py

Official GuitarSet Dataset STFT Comparative Benchmark & Inharmonicity Evaluation
Evaluates Hybrid Synth physical modeling engine against real acoustic studio recordings from GuitarSet.

Mathematical Metrics:
  1. Spectral Convergence (SC):
     || |S_ref| - |S_syn| ||_F / || |S_ref| ||_F
  2. Log-Magnitude STFT Distance (L1):
     (1 / N) * || log(|S_ref| + eps) - log(|S_syn| + eps) ||_1
  3. Multi-Scale STFT Objective Loss:
     L_total = (1 / M) * sum_m (SC_m + alpha * L1_m) across window scales {2048, 1024, 512, 256, 128}

Modes:
  1. Isolated Acoustic Events (Single Plucks & Ringing Chords):
     Extracts clean ringing events from GuitarSet and renders matching notes on HybridSynth.
  2. Continuous Performance Resynthesis:
     Transcribes complete 6-string JAMS note sequences into timestamped events, invokes `./build/render_audio`,
     and performs time-frequency spectrogram alignment and metric sweeps.
"""

import os
import sys
import glob
import json
import wave
import subprocess
import numpy as np
import scipy.signal
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

os.environ['MPLCONFIGDIR'] = '/tmp/matplotlib'

DATA_DIR = os.path.join(os.path.dirname(__file__), 'data', 'guitarset')
AUDIO_DIR = os.path.join(DATA_DIR, 'audio_mono-mic')
ANNO_DIR = os.path.join(DATA_DIR, 'annotation')
PLOTS_DIR = os.path.join(os.path.dirname(__file__), 'plots')

STFT_SCALES = [
    {'n_fft': 2048, 'hop_size': 512, 'win_size': 2048}, # Scale 1: Deep fundamental & inharmonicity
    {'n_fft': 1024, 'hop_size': 256, 'win_size': 1024}, # Scale 2: Lower-mid soundboard resonance
    {'n_fft': 512,  'hop_size': 128, 'win_size': 512},  # Scale 3: Upper-body sheen
    {'n_fft': 256,  'hop_size': 64,  'win_size': 256},  # Scale 4: Attack transient / scrape
    {'n_fft': 128,  'hop_size': 32,  'win_size': 128},  # Scale 5: P-wave snap micro-transient
]


def load_wav_slice(path, start_sec, dur_sec, target_sr=44100):
    with wave.open(path, 'rb') as w:
        sr = w.getframerate()
        n_channels = w.getnchannels()
        sampwidth = w.getsampwidth()
        total_frames = w.getnframes()

        start_frame = int(start_sec * sr)
        num_frames = int(dur_sec * sr)

        w.setpos(min(start_frame, total_frames - 1))
        raw = w.readframes(min(num_frames, total_frames - start_frame))

        if sampwidth == 2:
            samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        elif sampwidth == 3:
            a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
            b = np.pad(a, ((0, 0), (1, 0)), mode='constant', constant_values=0)
            samples = b.view('<i4').flatten().astype(np.float32) / (2**31)
        elif sampwidth == 4:
            samples = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
        else:
            raise ValueError(f"Unsupported sample width: {sampwidth}")

        if n_channels > 1:
            samples = samples.reshape(-1, n_channels).mean(axis=1)

        if sr != target_sr:
            num_target = int(len(samples) * target_sr / sr)
            samples = scipy.signal.resample(samples, num_target)

        # Pad to requested duration if needed
        req_samples = int(dur_sec * target_sr)
        if len(samples) < req_samples:
            samples = np.pad(samples, (0, req_samples - len(samples)), mode='constant')
        else:
            samples = samples[:req_samples]

        return samples, target_sr


def parse_jams_notes(jams_path, max_time_sec=None):
    with open(jams_path, 'r') as f:
        data = json.load(f)

    events = []
    duration = data.get('file_metadata', {}).get('duration', 0.0)

    for anno in data.get('annotations', []):
        if anno.get('namespace') == 'note_midi':
            for d in anno.get('data', []):
                t = float(d['time'])
                dur = float(d['duration'])
                midi_val = round(float(d['value']))
                if max_time_sec is not None and t > max_time_sec:
                    continue
                events.append({
                    'time': t,
                    'note': midi_val,
                    'duration': dur,
                    'vel': 0.82
                })

    events.sort(key=lambda x: x['time'])
    return events, duration


def compute_stft(x, n_fft, hop_size, win_size):
    f, t, Zxx = scipy.signal.stft(x, nperseg=win_size, noverlap=win_size - hop_size, nfft=n_fft, boundary=None)
    return np.abs(Zxx)


def multi_scale_stft_loss(x_ref, x_syn, scales=STFT_SCALES, eps=1e-7, floor_db=-75.0):
    min_len = min(len(x_ref), len(x_syn))
    x_ref = x_ref[:min_len]
    x_syn = x_syn[:min_len]

    peak_ref = np.max(np.abs(x_ref)) + eps
    peak_syn = np.max(np.abs(x_syn)) + eps
    x_ref = x_ref / peak_ref
    x_syn = x_syn / peak_syn

    floor_eps = 10.0 ** (floor_db / 20.0) if floor_db is not None else eps
    total_sc = 0.0
    total_log = 0.0

    for s in scales:
        S_ref = compute_stft(x_ref, s['n_fft'], s['hop_size'], s['win_size'])
        S_syn = compute_stft(x_syn, s['n_fft'], s['hop_size'], s['win_size'])

        min_frames = min(S_ref.shape[1], S_syn.shape[1])
        S_ref = S_ref[:, :min_frames]
        S_syn = S_syn[:, :min_frames]

        sc = np.linalg.norm(S_ref - S_syn, ord='fro') / (np.linalg.norm(S_ref, ord='fro') + eps)
        log_l1 = np.mean(np.abs(np.log(np.maximum(S_ref, floor_eps)) - np.log(np.maximum(S_syn, floor_eps))))

        total_sc += sc
        total_log += log_l1

    M = len(scales)
    avg_sc = total_sc / M
    avg_log = total_log / M
    combined = avg_sc + avg_log
    return avg_sc, avg_log, combined


def render_with_events(events, engine_type, dur_sec, out_path, renderer_bin="./build/render_audio"):
    events_csv = "/tmp/guitarset_events.csv"
    with open(events_csv, 'w') as f:
        for ev in events:
            f.write(f"{ev['time']:.4f},{ev['note']},{ev['vel']:.2f},{ev['duration']:.4f}\n")

    cmd = [
        renderer_bin,
        "--engine", str(engine_type),
        "--eventsFile", events_csv,
        "--material", "1.0", # Steel acoustic
        "--bodyType", "1.0", # Acoustic body IR
        "--bodyMix", "0.75",
        "--dur", str(dur_sec),
        "--out", out_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"render_audio failed: {res.stderr}")


def render_isolated_chord(midi_notes, engine_type, dur_sec, out_path, renderer_bin="./build/render_audio"):
    chord_str = ",".join(map(str, midi_notes))
    cmd = [
        renderer_bin,
        "--engine", str(engine_type),
        "--chord", chord_str,
        "--material", "1.0",
        "--bodyType", "1.0",
        "--bodyMix", "0.75",
        "--dur", str(dur_sec),
        "--strum", "18.0",
        "--strumMode", "1",
        "--vel", "0.85",
        "--out", out_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"render_audio failed: {res.stderr}")


def plot_guitarset_comparison(ref_audio, ks_audio, fdtd_audio, title, out_path, sr=44100, dur_sec=3.0):
    min_len = min(len(ref_audio), len(ks_audio), len(fdtd_audio), int(sr * dur_sec))
    ref  = ref_audio[:min_len] / (np.max(np.abs(ref_audio[:min_len])) + 1e-7)
    ks   = ks_audio[:min_len] / (np.max(np.abs(ks_audio[:min_len])) + 1e-7)
    fdtd = fdtd_audio[:min_len] / (np.max(np.abs(fdtd_audio[:min_len])) + 1e-7)

    n_fft = 1024
    hop_size = 256

    _, _, S_ref  = scipy.signal.stft(ref, nperseg=n_fft, noverlap=n_fft - hop_size, nfft=n_fft)
    _, _, S_ks   = scipy.signal.stft(ks, nperseg=n_fft, noverlap=n_fft - hop_size, nfft=n_fft)
    _, _, S_fdtd = scipy.signal.stft(fdtd, nperseg=n_fft, noverlap=n_fft - hop_size, nfft=n_fft)

    min_frames = min(S_ref.shape[1], S_ks.shape[1], S_fdtd.shape[1])
    S_ref_db  = 20 * np.log10(np.abs(S_ref[:, :min_frames]) + 1e-5)
    S_ks_db   = 20 * np.log10(np.abs(S_ks[:, :min_frames]) + 1e-5)
    S_fdtd_db = 20 * np.log10(np.abs(S_fdtd[:, :min_frames]) + 1e-5)
    diff_fdtd_db = np.abs(S_ref_db - S_fdtd_db)

    fig, axes = plt.subplots(4, 1, figsize=(11, 10), sharex=True, sharey=True)

    axes[0].imshow(S_ref_db, aspect='auto', origin='lower', extent=[0, float(min_len) / sr, 0, sr // 2], vmin=-80, vmax=0, cmap='inferno')
    axes[0].set_title(f"1. Real GuitarSet Studio Recording ({title})", fontsize=11, fontweight='bold')
    axes[0].set_ylabel("Freq (Hz)")
    axes[0].set_ylim(0, 10000)

    axes[1].imshow(S_ks_db, aspect='auto', origin='lower', extent=[0, float(min_len) / sr, 0, sr // 2], vmin=-80, vmax=0, cmap='inferno')
    axes[1].set_title(f"2. Hybrid Synth: Digital Waveguide / Karplus-Strong", fontsize=11, fontweight='bold')
    axes[1].set_ylabel("Freq (Hz)")
    axes[1].set_ylim(0, 10000)

    axes[2].imshow(S_fdtd_db, aspect='auto', origin='lower', extent=[0, float(min_len) / sr, 0, sr // 2], vmin=-80, vmax=0, cmap='inferno')
    axes[2].set_title(f"3. Hybrid Synth: Stefan Bilbao Non-Linear FDTD", fontsize=11, fontweight='bold')
    axes[2].set_ylabel("Freq (Hz)")
    axes[2].set_ylim(0, 10000)

    axes[3].imshow(diff_fdtd_db, aspect='auto', origin='lower', extent=[0, float(min_len) / sr, 0, sr // 2], vmin=0, vmax=40, cmap='magma')
    axes[3].set_title(f"4. Bilbao FDTD Absolute Residual Spectral Error (|GuitarSet - FDTD|)", fontsize=11, fontweight='bold')
    axes[3].set_xlabel("Time (seconds)")
    axes[3].set_ylabel("Freq (Hz)")
    axes[3].set_ylim(0, 10000)

    fig.tight_layout()
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    fig.savefig(out_path, dpi=300)
    plt.close(fig)
    print(f"  [Plot Saved] -> {out_path}")


def align_signals(ref, syn, max_shift_samples=4410):
    """Aligns syn to ref by cross-correlation of envelopes within a search window."""
    env_ref = np.abs(scipy.signal.hilbert(ref[:min(len(ref), 44100)]))
    env_syn = np.abs(scipy.signal.hilbert(syn[:min(len(syn), 44100)]))
    
    corr = scipy.signal.correlate(env_ref, env_syn, mode='full')
    lags = scipy.signal.correlation_lags(len(env_ref), len(env_syn), mode='full')
    
    # Restrict to search window
    valid = np.abs(lags) <= max_shift_samples
    best_lag = lags[valid][np.argmax(corr[valid])]
    
    if best_lag > 0:
        syn_aligned = np.pad(syn, (best_lag, 0), mode='constant')[:len(ref)]
    elif best_lag < 0:
        syn_aligned = np.pad(syn[-best_lag:], (0, -best_lag), mode='constant')[:len(ref)]
    else:
        syn_aligned = syn
    return syn_aligned


def run_benchmarks():
    print("=" * 80)
    print("HYBRID SYNTH: OFFICIAL GUITARSET MULTI-SCALE STFT BENCHMARK")
    print("Dataset: GuitarSet (ISMIR 2018, NYU MARL / Queen Mary University of London)")
    print("Audio Source: Real Acoustic Condenser Microphone (audio_mono-mic)")
    print("=" * 80)

    results = []

    # ------------------------------------------------------------------------
    # Benchmark Test 1: Isolated Single Pluck C3 (MIDI 48)
    # File: 00_Funk1-97-C_solo_mic.wav at t=7.431s, ring-out duration 2.5s
    # ------------------------------------------------------------------------
    print("\n[Test 1] Isolated Acoustic Pluck C3 (MIDI 48) - 00_Funk1-97-C_solo")
    audio_path = os.path.join(AUDIO_DIR, "00_Funk1-97-C_solo_mic.wav")
    ref_audio, sr = load_wav_slice(audio_path, start_sec=7.431, dur_sec=2.5)

    tmp_ks = "/tmp/syn_gs_pluck_c3_ks.wav"
    tmp_fdtd = "/tmp/syn_gs_pluck_c3_fdtd.wav"
    render_isolated_chord([48], 0.0, 2.5, tmp_ks)
    render_isolated_chord([48], 1.0, 2.5, tmp_fdtd)

    ks_audio, _ = load_wav_slice(tmp_ks, start_sec=0.0, dur_sec=2.5)
    fdtd_audio, _ = load_wav_slice(tmp_fdtd, start_sec=0.0, dur_sec=2.5)

    # Multi-scale loss
    sc_ks, log_ks, total_ks = multi_scale_stft_loss(ref_audio, ks_audio)
    sc_fdtd, log_fdtd, total_fdtd = multi_scale_stft_loss(ref_audio, fdtd_audio)

    plot_path = os.path.join(PLOTS_DIR, "guitarset_pluck_C3.png")
    plot_guitarset_comparison(ref_audio, ks_audio, fdtd_audio, "Pluck C3 (MIDI 48)", plot_path, dur_sec=2.5)

    results.append({
        'test': 'Pluck C3 (MIDI 48)',
        'source': '00_Funk1-97-C_solo',
        'sc_ks': float(sc_ks), 'log_ks': float(log_ks), 'total_ks': float(total_ks),
        'sc_fdtd': float(sc_fdtd), 'log_fdtd': float(log_fdtd), 'total_fdtd': float(total_fdtd),
        'diff': float(total_ks - total_fdtd)
    })

    # ------------------------------------------------------------------------
    # Benchmark Test 2: Isolated High-Register Acoustic Pluck Bb3 (MIDI 58)
    # File: 00_Funk2-108-Eb_solo_mic.wav at t=13.367s, ring-out duration 2.5s
    # ------------------------------------------------------------------------
    print("\n[Test 2] Isolated Acoustic Pluck Bb3 (MIDI 58) - 00_Funk2-108-Eb_solo")
    audio_path = os.path.join(AUDIO_DIR, "00_Funk2-108-Eb_solo_mic.wav")
    ref_audio, sr = load_wav_slice(audio_path, start_sec=13.367, dur_sec=2.5)

    tmp_ks = "/tmp/syn_gs_pluck_bb3_ks.wav"
    tmp_fdtd = "/tmp/syn_gs_pluck_bb3_fdtd.wav"
    render_isolated_chord([58], 0.0, 2.5, tmp_ks)
    render_isolated_chord([58], 1.0, 2.5, tmp_fdtd)

    ks_audio, _ = load_wav_slice(tmp_ks, start_sec=0.0, dur_sec=2.5)
    fdtd_audio, _ = load_wav_slice(tmp_fdtd, start_sec=0.0, dur_sec=2.5)

    sc_ks, log_ks, total_ks = multi_scale_stft_loss(ref_audio, ks_audio)
    sc_fdtd, log_fdtd, total_fdtd = multi_scale_stft_loss(ref_audio, fdtd_audio)

    plot_path = os.path.join(PLOTS_DIR, "guitarset_pluck_Bb3.png")
    plot_guitarset_comparison(ref_audio, ks_audio, fdtd_audio, "Pluck Bb3 (MIDI 58)", plot_path, dur_sec=2.5)

    results.append({
        'test': 'Pluck Bb3 (MIDI 58)',
        'source': '00_Funk2-108-Eb_solo',
        'sc_ks': float(sc_ks), 'log_ks': float(log_ks), 'total_ks': float(total_ks),
        'sc_fdtd': float(sc_fdtd), 'log_fdtd': float(log_fdtd), 'total_fdtd': float(total_fdtd),
        'diff': float(total_ks - total_fdtd)
    })

    # ------------------------------------------------------------------------
    # Benchmark Test 3: Sustained Acoustic Chord (B / F#, MIDI [47, 54, 59])
    # File: 00_BN2-131-B_comp_mic.wav at t=28.012s, ring-out duration 2.2s
    # ------------------------------------------------------------------------
    print("\n[Test 3] Sustained Acoustic Chord (MIDI [47, 54, 59]) - 00_BN2-131-B_comp")
    audio_path = os.path.join(AUDIO_DIR, "00_BN2-131-B_comp_mic.wav")
    ref_audio, sr = load_wav_slice(audio_path, start_sec=28.012, dur_sec=2.2)

    tmp_ks = "/tmp/syn_gs_chord_b_ks.wav"
    tmp_fdtd = "/tmp/syn_gs_chord_b_fdtd.wav"
    render_isolated_chord([47, 54, 59], 0.0, 2.2, tmp_ks)
    render_isolated_chord([47, 54, 59], 1.0, 2.2, tmp_fdtd)

    ks_audio, _ = load_wav_slice(tmp_ks, start_sec=0.0, dur_sec=2.2)
    fdtd_audio, _ = load_wav_slice(tmp_fdtd, start_sec=0.0, dur_sec=2.2)

    sc_ks, log_ks, total_ks = multi_scale_stft_loss(ref_audio, ks_audio)
    sc_fdtd, log_fdtd, total_fdtd = multi_scale_stft_loss(ref_audio, fdtd_audio)

    plot_path = os.path.join(PLOTS_DIR, "guitarset_chord_B.png")
    plot_guitarset_comparison(ref_audio, ks_audio, fdtd_audio, "Acoustic Chord B (47,54,59)", plot_path, dur_sec=2.2)

    results.append({
        'test': 'Acoustic Chord B [47,54,59]',
        'source': '00_BN2-131-B_comp',
        'sc_ks': float(sc_ks), 'log_ks': float(log_ks), 'total_ks': float(total_ks),
        'sc_fdtd': float(sc_fdtd), 'log_fdtd': float(log_fdtd), 'total_fdtd': float(total_fdtd),
        'diff': float(total_ks - total_fdtd)
    })

    # ------------------------------------------------------------------------
    # Benchmark Test 4: Continuous Performance Resynthesis (Singer-Songwriter Comping)
    # File: 00_SS1-100-C#_comp, first 5.0 seconds
    # ------------------------------------------------------------------------
    print("\n[Test 4] Continuous Acoustic Comping Resynthesis (00_SS1-100-C#_comp, 5.0s)")
    jams_path = os.path.join(ANNO_DIR, "00_SS1-100-C#_comp.jams")
    audio_path = os.path.join(AUDIO_DIR, "00_SS1-100-C#_comp_mic.wav")
    events, total_dur = parse_jams_notes(jams_path, max_time_sec=5.0)

    # Shift events to start at t=0.0
    t0 = events[0]['time']
    for ev in events:
        ev['time'] -= t0
    phrase_dur = 5.0

    ref_audio, sr = load_wav_slice(audio_path, start_sec=t0, dur_sec=phrase_dur)

    tmp_ks = "/tmp/syn_gs_phrase_ss1_ks.wav"
    tmp_fdtd = "/tmp/syn_gs_phrase_ss1_fdtd.wav"
    render_with_events(events, 0.0, phrase_dur, tmp_ks)
    render_with_events(events, 1.0, phrase_dur, tmp_fdtd)

    ks_audio, _ = load_wav_slice(tmp_ks, start_sec=0.0, dur_sec=phrase_dur)
    fdtd_audio, _ = load_wav_slice(tmp_fdtd, start_sec=0.0, dur_sec=phrase_dur)

    ks_aligned = align_signals(ref_audio, ks_audio)
    fdtd_aligned = align_signals(ref_audio, fdtd_audio)

    sc_ks, log_ks, total_ks = multi_scale_stft_loss(ref_audio, ks_aligned)
    sc_fdtd, log_fdtd, total_fdtd = multi_scale_stft_loss(ref_audio, fdtd_aligned)

    plot_path = os.path.join(PLOTS_DIR, "guitarset_phrase_SS1_comp.png")
    plot_guitarset_comparison(ref_audio, ks_aligned, fdtd_aligned, "Singer-Songwriter Comping (5.0s)", plot_path, dur_sec=phrase_dur)

    results.append({
        'test': 'SS1 Comping Phrase (5.0s)',
        'source': '00_SS1-100-C#_comp',
        'sc_ks': float(sc_ks), 'log_ks': float(log_ks), 'total_ks': float(total_ks),
        'sc_fdtd': float(sc_fdtd), 'log_fdtd': float(log_fdtd), 'total_fdtd': float(total_fdtd),
        'diff': float(total_ks - total_fdtd)
    })

    # ------------------------------------------------------------------------
    # Benchmark Test 5: Continuous Melodic Solo Line Resynthesis
    # File: 00_SS1-100-C#_solo, first 6.0 seconds
    # ------------------------------------------------------------------------
    print("\n[Test 5] Continuous Melodic Solo Line Resynthesis (00_SS1-100-C#_solo, 6.0s)")
    jams_path = os.path.join(ANNO_DIR, "00_SS1-100-C#_solo.jams")
    audio_path = os.path.join(AUDIO_DIR, "00_SS1-100-C#_solo_mic.wav")
    events, total_dur = parse_jams_notes(jams_path, max_time_sec=6.0)

    t0 = events[0]['time']
    for ev in events:
        ev['time'] -= t0
    phrase_dur = 6.0

    ref_audio, sr = load_wav_slice(audio_path, start_sec=t0, dur_sec=phrase_dur)

    tmp_ks = "/tmp/syn_gs_phrase_ss1_solo_ks.wav"
    tmp_fdtd = "/tmp/syn_gs_phrase_ss1_solo_fdtd.wav"
    render_with_events(events, 0.0, phrase_dur, tmp_ks)
    render_with_events(events, 1.0, phrase_dur, tmp_fdtd)

    ks_audio, _ = load_wav_slice(tmp_ks, start_sec=0.0, dur_sec=phrase_dur)
    fdtd_audio, _ = load_wav_slice(tmp_fdtd, start_sec=0.0, dur_sec=phrase_dur)

    ks_aligned = align_signals(ref_audio, ks_audio)
    fdtd_aligned = align_signals(ref_audio, fdtd_audio)

    sc_ks, log_ks, total_ks = multi_scale_stft_loss(ref_audio, ks_aligned)
    sc_fdtd, log_fdtd, total_fdtd = multi_scale_stft_loss(ref_audio, fdtd_aligned)

    plot_path = os.path.join(PLOTS_DIR, "guitarset_phrase_SS1_solo.png")
    plot_guitarset_comparison(ref_audio, ks_aligned, fdtd_aligned, "Acoustic Solo Melody (6.0s)", plot_path, dur_sec=phrase_dur)

    results.append({
        'test': 'SS1 Solo Phrase (6.0s)',
        'source': '00_SS1-100-C#_solo',
        'sc_ks': float(sc_ks), 'log_ks': float(log_ks), 'total_ks': float(total_ks),
        'sc_fdtd': float(sc_fdtd), 'log_fdtd': float(log_fdtd), 'total_fdtd': float(total_fdtd),
        'diff': float(total_ks - total_fdtd)
    })

    # ------------------------------------------------------------------------
    # Summary Table
    # ------------------------------------------------------------------------
    print("\n" + "=" * 90)
    print("GUITARSET MULTI-SCALE STFT BENCHMARK RESULTS SUMMARY")
    print(f"{'Benchmark Target':<28} | {'Waveguide SC':<13} | {'Waveguide L1':<13} | {'FDTD SC':<10} | {'FDTD L1':<10} | {'FDTD Adv (ΔL)':<12}")
    print("-" * 90)
    for r in results:
        adv = r['diff']
        adv_str = f"+{adv:.4f}" if adv >= 0 else f"{adv:.4f}"
        print(f"{r['test']:<28} | {r['sc_ks']:<13.4f} | {r['log_ks']:<13.4f} | {r['sc_fdtd']:<10.4f} | {r['log_fdtd']:<10.4f} | {adv_str:<12}")
    print("=" * 90)

    # Save summary markdown report
    report_path = os.path.join(os.path.dirname(__file__), "guitarset_results.json")
    with open(report_path, 'w') as f:
        json.dump(results, f, indent=2)
    print(f"\n[Saved JSON Report] -> {report_path}")

    return results


if __name__ == '__main__':
    run_benchmarks()
