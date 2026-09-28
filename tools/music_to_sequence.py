#!/usr/bin/env python3
"""Use short-time FFT energy and onset peaks to generate a LuxFlux sequence.

Requires NumPy and ffmpeg. Beat onsets are heuristic, not musical beat labels.
"""
import argparse
import json
import pathlib
import subprocess
import wave

import numpy as np
from luxflux_json_server import encode_sequence


def analyze(samples, sample_rate, max_beats=60):
    window_size, hop = 1024, 220
    if len(samples) < window_size:
        raise ValueError("Audio excerpt is too short")
    windows = np.lib.stride_tricks.sliding_window_view(samples, window_size)[::hop]
    magnitude = np.abs(np.fft.rfft(windows * np.hanning(window_size), axis=1))
    frequencies = np.fft.rfftfreq(window_size, 1 / sample_rate)
    bands = np.stack([np.sqrt(np.mean(magnitude[:, (frequencies >= low) &
                           (frequencies < high)] ** 2, axis=1))
                      for low, high in ((40, 250), (250, 2000), (2000, 8000))], axis=1)
    normalized = np.clip(bands / np.maximum(np.percentile(bands, 95, axis=0), 1e-9), 0, 1)
    # Spectral flux: rises in spectrum magnitude highlight drum/beat attacks.
    compressed = np.log1p(magnitude)
    flux = np.r_[0, np.maximum(np.diff(compressed, axis=0), 0).sum(axis=1)]
    times = (np.arange(len(flux)) * hop + window_size / 2) / sample_rate
    threshold = max(float(np.percentile(flux, 65)), 1e-8)
    candidates = [i for i in range(1, len(flux) - 1)
                  if flux[i] > threshold and flux[i] >= flux[i - 1] and flux[i] > flux[i + 1]]
    chosen = []
    for index in sorted(candidates, key=lambda i: flux[i], reverse=True):
        if all(abs(times[index] - times[other]) >= 0.25 for other in chosen):
            chosen.append(index)
            if len(chosen) >= max_beats:
                break
    return times, normalized, sorted(float(times[index]) for index in chosen)


def make_sequence(samples, sample_rate=22050, led_count=9, name="default", max_frames=128):
    if led_count < 3:
        raise ValueError("Frequency mapping requires at least 3 LEDs")
    times, bands, beats = analyze(samples, sample_rate, (max_frames - 2) // 2)
    duration_ms = round(len(samples) * 1000 / sample_rate)
    beat_ms = sorted({round(beat * 1000) for beat in beats if 0 < beat * 1000 < duration_ms - 80})
    boundaries = sorted({0, duration_ms, *beat_ms,
                         *(min(beat + 80, duration_ms) for beat in beat_ms)})
    frames = []
    counts = [led_count // 3, led_count // 3, led_count - 2 * (led_count // 3)]
    for start, end in zip(boundaries, boundaries[1:]):
        index = min(int(np.searchsorted(times, start / 1000)), len(times) - 1)
        pulse = any(beat <= start < beat + 80 for beat in beat_ms)
        groups = []
        for band, count in enumerate(counts):
            strength = int(20 + 140 * bands[index, band])
            rgb = [0, 0, 0]
            rgb[band] = strength
            if pulse:
                rgb = [min(255, channel + 95) for channel in rgb]
            groups.append({"count": count, "rgb": rgb})
        remaining = end - start
        while remaining:
            step = min(remaining, 10000)
            frames.append({"duration_ms": step, "groups": groups})
            remaining -= step
    sequence = {"name": name, "led_count": led_count, "frames": frames,
                "analysis": {"duration_ms": duration_ms, "beat_onsets_ms": beat_ms,
                             "method": "short-time FFT + spectral-flux onset peaks",
                             "bands_hz": [[40, 250], [250, 2000], [2000, 8000]]}}
    encode_sequence(sequence, max_frames=max_frames)
    return sequence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("audio_file", type=pathlib.Path)
    parser.add_argument("output_json", type=pathlib.Path)
    parser.add_argument("--start", type=float, default=0, help="excerpt start, seconds")
    parser.add_argument("--seconds", type=float, default=24)
    parser.add_argument("--led-count", type=int, default=9)
    parser.add_argument("--name", default="default")
    parser.add_argument("--excerpt", type=pathlib.Path, help="prepared WAV to play with this sequence")
    args = parser.parse_args()
    if args.start < 0 or not 0 < args.seconds <= 120 or not 3 <= args.led_count <= 256:
        parser.error("start must be nonnegative, seconds 0–120, and LED count 3–256")
    try:
        decoded = subprocess.run(["ffmpeg", "-v", "error", "-ss", str(args.start),
                                  "-i", str(args.audio_file), "-t", str(args.seconds),
                                  "-ac", "1", "-ar", "22050", "-f", "s16le", "-"],
                                 check=True, capture_output=True).stdout
        samples = np.frombuffer(decoded, dtype="<i2").astype(float) / 32768
        sequence = make_sequence(samples, led_count=args.led_count, name=args.name)
        if args.excerpt:
            args.excerpt.parent.mkdir(parents=True, exist_ok=True)
            with wave.open(str(args.excerpt), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(22050)
                wav.writeframes(decoded)
            sequence["analysis"]["audio_excerpt"] = str(args.excerpt)
        args.output_json.parent.mkdir(parents=True, exist_ok=True)
        args.output_json.write_text(json.dumps(sequence, indent=2) + "\n", encoding="utf-8")
        print(f"Generated {len(sequence['frames'])} frames; "
              f"{len(sequence['analysis']['beat_onsets_ms'])} detected onsets; "
              f"{sequence['analysis']['duration_ms'] / 1000:.2f} seconds")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"Analysis failed: {exc}")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
