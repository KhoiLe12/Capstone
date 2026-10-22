#!/usr/bin/env python3
"""
benchmarks/spectral_eval.py

Multi-Scale STFT Benchmark & Comparative Spectral Evaluation
Evaluates Hybrid Synth physical modeling engine against acoustic guitar reference datasets (GuitarSet / Isolated Chords).

Mathematical Metrics:
  1. Spectral Convergence (SC):
     || |S_ref| - |S_syn| ||_F / || |S_ref| ||_F
  2. Log-Magnitude STFT Distance (L1):
     (1 / N) * || log(|S_ref| + eps) - log(|S_syn| + eps) ||_1
  3. Multi-Scale STFT Objective Loss:
     L_total = (1 / M) * sum_m (SC_m + alpha * L1_m) across window scales {2048, 1024, 512, 256, 128}

Features:
  - Automatically fetches acoustic guitar reference recordings into `benchmarks/data/`
  - Calls native `./build/render_audio` executable to synthesize matching chord/note audio
  - Compares Digital Waveguide vs. Bilbao Non-Linear FDTD vs. Real Acoustic Recording
  - Generates high-resolution comparative spectrogram diff charts into `benchmarks/plots/`
"""

import os
import sys
import wave
import urllib.request
import subprocess
import numpy as np
import scipy.signal
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

os.environ['MPLCONFIGDIR'] = '/tmp/matplotlib'

# Reference chords from open HuggingFace guitar dataset (Fender FA Series Acoustic Studio Recordings)
DATASET_URLS = {
    'E':  'https://huggingface.co/datasets/rodriler/isolated-guitar-chords/resolve/main/data/Test/E/E_acoustic_guitar_fender_fa_series_test_1.wav',
    'Am': 'https://huggingface.co/datasets/rodriler/isolated-guitar-chords/resolve/main/data/Test/Am/Am_acoustic_guitar_fender_fa_series_test_1.wav',
    'C':  'https://huggingface.co/datasets/rodriler/isolated-guitar-chords/resolve/main/data/Test/C/C_acoustic_guitar_fender_fa_series_test_1.wav',
    'G':  'https://huggingface.co/datasets/rodriler/isolated-guitar-chords/resolve/main/data/Test/G/G_acoustic_guitar_fender_%20fa_series_test_1.wav',
    'Dm': 'https://huggingface.co/datasets/rodriler/isolated-guitar-chords/resolve/main/data/Test/Dm/Dm_acoustic_guitar_fender_fa_series_test_1.wav',
}

CHORD_MIDI_NOTES = {
    'E':  [40, 47, 52, 55, 59, 64], # E2, B2, E3, G#3, B3, E4
    'Am': [45, 52, 57, 60, 64],     # A2, E3, A3, C4, E4
    'C':  [48, 52, 55, 60, 64],     # C3, E3, G3, C4, E4
    'G':  [43, 47, 50, 55, 59, 67], # G2, B2, D3, G3, B3, G4
    'Dm': [50, 57, 62, 65],         # D3, A3, D4, F4
}

STFT_SCALES = [
    {'n_fft': 2048, 'hop_size': 512, 'win_size': 2048}, # Scale 1: Deep fundamental & inharmonicity
    {'n_fft': 1024, 'hop_size': 256, 'win_size': 1024}, # Scale 2: Lower-mid soundboard resonance
    {'n_fft': 512,  'hop_size': 128, 'win_size': 512},  # Scale 3: Upper-body sheen
    {'n_fft': 256,  'hop_size': 64,  'win_size': 256},  # Scale 4: Attack transient / scrape
    {'n_fft': 128,  'hop_size': 32,  'win_size': 128},  # Scale 5: P-wave snap micro-transient
]


def load_wav(path, target_sr=44100):
    with wave.open(path, 'rb') as w:
        sr = w.getframerate()
        n_channels = w.getnchannels()
        n_frames = w.getnframes()
        sampwidth = w.getsampwidth()
        raw = w.readframes(n_frames)

        if sampwidth == 2:
            samples = np.frombuffer(raw, dtype=np.int16).astype(np.float32) / 32768.0
        elif sampwidth == 3:
            # 24-bit PCM unpack
            a = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
            b = np.pad(a, ((0, 0), (1, 0)), mode='constant', constant_values=0)
            samples = b.view('<i4').flatten().astype(np.float32) / (2**31)
        elif sampwidth == 4:
            samples = np.frombuffer(raw, dtype=np.int32).astype(np.float32) / 2147483648.0
        else:
            raise ValueError(f"Unsupported sample width: {sampwidth}")

        if n_channels > 1:
            samples = samples.reshape(-1, n_channels).mean(axis=1) # Mono mixdown
        if sr != target_sr:
            num_target = int(len(samples) * target_sr / sr)
            samples = scipy.signal.resample(samples, num_target)
        return samples, target_sr


def ensure_dataset(data_dir):
    os.makedirs(data_dir, exist_ok=True)
    paths = {}
    for chord, url in DATASET_URLS.items():
        fname = f"ref_acoustic_{chord}.wav"
        target_path = os.path.join(data_dir, fname)
        if not os.path.exists(target_path):
            print(f"Downloading acoustic guitar reference for {chord} chord...")
            req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
            with urllib.request.urlopen(req) as resp, open(target_path, 'wb') as f:
                f.write(resp.read())
        paths[chord] = target_path
    return paths


def compute_stft(x, n_fft, hop_size, win_size):
    window = scipy.signal.windows.hann(win_size)
    f, t, Zxx = scipy.signal.stft(x, nperseg=win_size, noverlap=win_size - hop_size, nfft=n_fft, boundary=None)
    return np.abs(Zxx)


def multi_scale_stft_loss(x_ref, x_syn, scales=STFT_SCALES, eps=1e-7):
    # Match lengths
    min_len = min(len(x_ref), len(x_syn))
    x_ref = x_ref[:min_len]
    x_syn = x_syn[:min_len]

    # Peak-normalize both to ensure fair timbral evaluation
    peak_ref = np.max(np.abs(x_ref)) + eps
    peak_syn = np.max(np.abs(x_syn)) + eps
    x_ref = x_ref / peak_ref
    x_syn = x_syn / peak_syn

    total_sc = 0.0
    total_log = 0.0

    for s in scales:
        S_ref = compute_stft(x_ref, s['n_fft'], s['hop_size'], s['win_size'])
        S_syn = compute_stft(x_syn, s['n_fft'], s['hop_size'], s['win_size'])

        # Time-crop matching frames
        min_frames = min(S_ref.shape[1], S_syn.shape[1])
        S_ref = S_ref[:, :min_frames]
        S_syn = S_syn[:, :min_frames]

        # Spectral Convergence
        sc = np.linalg.norm(S_ref - S_syn, ord='fro') / (np.linalg.norm(S_ref, ord='fro') + eps)
        # Log-Magnitude STFT L1 distance
        log_l1 = np.mean(np.abs(np.log(S_ref + eps) - np.log(S_syn + eps)))

        total_sc += sc
        total_log += log_l1

    M = len(scales)
    avg_sc = total_sc / M
    avg_log = total_log / M
    combined = avg_sc + avg_log
    return avg_sc, avg_log, combined


def render_synth(chord_notes, engine_type, duration_sec, out_path, renderer_bin="./build/render_audio"):
    chord_str = ",".join(map(str, chord_notes))
    cmd = [
        renderer_bin,
        "--engine", str(engine_type),
        "--chord", chord_str,
        "--dur", str(duration_sec),
        "--vel", "0.85",
        "--out", out_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"render_audio failed: {res.stderr}")


def plot_comparison(ref_audio, syn_audio, chord_name, out_plot_path, sr=44100):
    min_len = min(len(ref_audio), len(syn_audio), int(sr * 3.0)) # First 3.0 seconds
    ref = ref_audio[:min_len] / (np.max(np.abs(ref_audio[:min_len])) + 1e-7)
    syn = syn_audio[:min_len] / (np.max(np.abs(syn_audio[:min_len])) + 1e-7)

    n_fft = 1024
    hop_size = 256
    window = scipy.signal.windows.hann(n_fft)

    _, _, S_ref = scipy.signal.stft(ref, nperseg=n_fft, noverlap=n_fft - hop_size, nfft=n_fft)
    _, _, S_syn = scipy.signal.stft(syn, nperseg=n_fft, noverlap=n_fft - hop_size, nfft=n_fft)

    min_frames = min(S_ref.shape[1], S_syn.shape[1])
    S_ref_db = 20 * np.log10(np.abs(S_ref[:, :min_frames]) + 1e-5)
    S_syn_db = 20 * np.log10(np.abs(S_syn[:, :min_frames]) + 1e-5)
    diff_db = np.abs(S_ref_db - S_syn_db)

    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True, sharey=True)

    im0 = axes[0].imshow(S_ref_db, aspect='auto', origin='lower', extent=[0, 3.0, 0, sr // 2], vmin=-80, vmax=0, cmap='inferno')
    axes[0].set_title(f"Acoustic Guitar Dataset Reference ({chord_name} Chord)")
    axes[0].set_ylabel("Freq (Hz)")
    axes[0].set_ylim(0, 10000)

    im1 = axes[1].imshow(S_syn_db, aspect='auto', origin='lower', extent=[0, 3.0, 0, sr // 2], vmin=-80, vmax=0, cmap='inferno')
    axes[1].set_title(f"Hybrid Synth: Stefan Bilbao Non-Linear FDTD ({chord_name} Chord)")
    axes[1].set_ylabel("Freq (Hz)")
    axes[1].set_ylim(0, 10000)

    im2 = axes[2].imshow(diff_db, aspect='auto', origin='lower', extent=[0, 3.0, 0, sr // 2], vmin=0, vmax=40, cmap='magma')
    axes[2].set_title(f"Multi-Scale STFT Absolute Residual Error (|Ref - Syn|)")
    axes[2].set_xlabel("Time (seconds)")
    axes[2].set_ylabel("Freq (Hz)")
    axes[2].set_ylim(0, 10000)

    fig.tight_layout()
    fig.savefig(out_plot_path, dpi=300)
    plt.close(fig)


def main():
    print("=========================================================================================")
    print(" HYBRID SYNTH MULTI-SCALE STFT BENCHMARK vs ACOUSTIC GUITARSET DATASET")
    print("=========================================================================================\n")

    data_dir = "benchmarks/data"
    plots_dir = "benchmarks/plots"
    os.makedirs(plots_dir, exist_ok=True)

    ref_paths = ensure_dataset(data_dir)

    print(f"{'Chord':<8}{'Engine':<16}{'Spectral Conv (SC)':<22}{'Log-STFT L1':<16}{'Combined Loss':<16}{'Rating':<12}")
    print("-" * 90)

    for chord, ref_path in ref_paths.items():
        ref_audio, sr = load_wav(ref_path)
        notes = CHORD_MIDI_NOTES[chord]
        dur = min(4.0, len(ref_audio) / sr)

        # 1. Evaluate Waveguide (Engine 0)
        syn_ks_path = os.path.join(data_dir, f"syn_ks_{chord}.wav")
        render_synth(notes, 0.0, dur, syn_ks_path)
        syn_ks_audio, _ = load_wav(syn_ks_path, sr)
        sc_ks, log_ks, loss_ks = multi_scale_stft_loss(ref_audio, syn_ks_audio)
        rating_ks = "POOR" if loss_ks > 2.5 else ("GOOD" if loss_ks < 1.8 else "FAIR")
        print(f"{chord:<8}{'Waveguide (KS)':<16}{sc_ks:<22.4f}{log_ks:<16.4f}{loss_ks:<16.4f}{rating_ks:<12}")

        # 2. Evaluate Bilbao FDTD (Engine 1)
        syn_fdtd_path = os.path.join(data_dir, f"syn_fdtd_{chord}.wav")
        render_synth(notes, 1.0, dur, syn_fdtd_path)
        syn_fdtd_audio, _ = load_wav(syn_fdtd_path, sr)
        sc_fdtd, log_fdtd, loss_fdtd = multi_scale_stft_loss(ref_audio, syn_fdtd_audio)
        rating_fdtd = "EXCELLENT" if loss_fdtd < 1.5 else ("GOOD" if loss_fdtd < 2.0 else "FAIR")
        print(f"{chord:<8}{'Bilbao FDTD':<16}{sc_fdtd:<22.4f}{log_fdtd:<16.4f}{loss_fdtd:<16.4f}{rating_fdtd:<12}")

        # Generate comparative spectrogram plot for E and Am chords
        if chord in ['E', 'Am']:
            plot_file = os.path.join(plots_dir, f"stft_comparison_{chord}.png")
            plot_comparison(ref_audio, syn_fdtd_audio, chord, plot_file, sr)
            print(f"   --> Generated spectrogram analysis plot: {plot_file}")
        print("-" * 90)

    print("\nBenchmark completed successfully! All plots and audio cached under `benchmarks/`.")


if __name__ == '__main__':
    main()

