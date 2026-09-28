"""Synthetic onset and music timeline validation; no hardware/audio playback."""
import pathlib
import sys
import numpy as np

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
from music_to_sequence import analyze, make_sequence

rate = 22050
samples = np.zeros(rate * 6)
rng = np.random.default_rng(42)
expected = np.arange(0.5, 5.6, 0.5)
for onset in expected:
    start = int(onset * rate)
    pulse = rng.normal(0, 0.4, 1500) * np.exp(-np.arange(1500) / 300)
    samples[start:start + len(pulse)] += pulse
times, bands, detected = analyze(samples, rate)
assert len(detected) >= 10
assert all(min(abs(onset - found) for found in detected) < 0.05 for onset in expected)
sequence = make_sequence(samples, rate)
assert sum(frame["duration_ms"] for frame in sequence["frames"]) == 6000
assert len(sequence["frames"]) <= 128
assert all(sum(group["count"] for group in frame["groups"]) == 9
           for frame in sequence["frames"])
silent = make_sequence(np.zeros(rate * 24), rate)
assert silent["analysis"]["beat_onsets_ms"] == []
assert sum(frame["duration_ms"] for frame in silent["frames"]) == 24000
print("Music analysis tests passed: synthetic 120 BPM attacks, silence, and timeline limits")
