# Music-to-light prototype

The computer performs short-time FFT analysis before uploading a sequence.
The ESP32 plays the precomputed frames; it does not decode audio or analyze a
microphone. Bass (40–250 Hz), mids (250–2,000 Hz), and treble (2,000–8,000 Hz)
control red, green, and blue regions of the strip. Spectral-flux peaks create
80 ms pulses. Peaks are heuristic musical attacks, not guaranteed downbeats.

## Prepared demo

The included WAV is a 24-second excerpt of **Disco Medusae** by Kevin MacLeod,
starting 30 seconds into the original. See
[music attribution](../tools/music_demo/ATTRIBUTION.md) for the source and CC BY
4.0 license. The matching JSON contains 127 frames and stays within the default
firmware limits for 9 LEDs.

Install `ffmpeg` (including `ffplay`) with your OS package manager, and install
the Python dependencies:

```bash
python3 -m pip install -r tests/requirements-mdns.txt -r tests/requirements-music.txt
```

Configure the ESP32's server host to the computer's LAN IP, then run:

```bash
python3 tools/luxflux_json_server.py tools/sequences/disco_medusae_beats.json \
    --device archimedes --audio tools/music_demo/disco_medusae_24s.wav \
    --audio-delay-ms 250
```

The script uploads all frames, sends `EOF`, waits the selected delay, and
launches `ffplay` once. It never plays audio after a rejected transfer. The
prepared WAV must be used for this JSON: playing the full original song would
start at a different point in the music.

## Analyze another song

```bash
python3 tools/music_to_sequence.py path/to/song.mp3 tools/sequences/song_beats.json \
    --start 30 --seconds 24 --excerpt /tmp/song_excerpt.wav
python3 tools/luxflux_json_server.py tools/sequences/song_beats.json \
    --device archimedes --audio /tmp/song_excerpt.wav --audio-delay-ms 250
python3 tests/music_sequence_test.py
python3 tests/music_transfer_test.py
```

The generator keeps the strongest, separated onset peaks to fit 128 frames.
This means it can omit weaker attacks. An excerpt without attacks still gets
valid frequency-color frames. Long excerpts can require more frames than the
device permits; shorten the excerpt if generation fails validation.

## Timing limits

This is approximate synchronization. `EOF` is not a scheduled start command,
and the protocol has no final activation acknowledgement. The firmware
currently displays 100 ms of completion status and 150 ms of activation
status; NVS writes, Wi-Fi latency, queued status events, player startup, and
audio output buffering add variable delay. Adjust `--audio-delay-ms` to
calibrate the initial offset. The existing firmware schedules frames from
their actual render time, so task polling and per-frame tick rounding can
accumulate drift. This Python prototype makes no firmware timing changes.

The audio plays once while the ESP32 continues looping its stored sequence.
There is no pause, seek, or automatic stop synchronization. For precise
synchronization or several ESP32s, a future protocol should acknowledge a
prepared sequence and schedule a shared future start time, with clocks and
audio-device latency accounted for.
