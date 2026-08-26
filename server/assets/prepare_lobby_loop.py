#!/usr/bin/env python3
"""Prepare the lobby hold-music loop from a real recorded performance.

Source: "Rozenbaum Chords Bossa.wav" by FullMetalJedi
        https://freesound.org/people/FullMetalJedi/sounds/400698/
        License: Creative Commons 0 (public domain dedication) — an original
        piece performed by the uploader on nylon-string acoustic guitar, so
        both the composition and the performance rights are waived.

A real performance cannot loop sample-perfectly, so per-iteration fades are
baked into the clip itself: it mixes in over the first ~1.25s and out over
the last ~1.25s, with a short breath of silence appended, so each pass
begins and ends from nothing. (The server ALSO supports runtime edge fades
via kLoopEdgeFadeSamples in room.cpp; baking them in here keeps the asset
WYSIWYG — the .wav next to it is exactly what listeners hear.)

For the fully-synthesized fallback loop, see generate_lobby_loop.py.

Usage: python3 prepare_lobby_loop.py <input-audio-file>
Output: lobby-loop.pcm (raw 48kHz mono s16le) + lobby-loop.wav (audition copy)
"""

import sys
import wave

import numpy as np
import soundfile as sf

FS = 48000
FADE_S = 1.25       # mix in/out duration at the clip boundaries
TAIL_SILENCE_S = 0.75  # breath between iterations
TRIM_THRESHOLD = 0.005  # of full scale, for edge silence trimming
TARGET_PEAK = 0.32  # ~ -10 dBFS, matches the previous hold-music level

data, rate = sf.read(sys.argv[1], dtype="float64")

# Downmix to mono
if data.ndim == 2:
    data = data.mean(axis=1)

# Resample if needed (linear interpolation is fine for hold music)
if rate != FS:
    n_out = round(len(data) * FS / rate)
    data = np.interp(np.linspace(0, len(data) - 1, n_out),
                     np.arange(len(data)), data)

# Trim leading/trailing silence so the fades act on actual music
loud = np.flatnonzero(np.abs(data) > TRIM_THRESHOLD)
if len(loud) == 0:
    sys.exit("input is silent")
data = data[loud[0]:loud[-1] + 1]

# Bake in the per-iteration mixes: in from nothing, out to nothing
fade = round(FADE_S * FS)
fade = min(fade, len(data) // 4)
ramp = np.linspace(0.0, 1.0, fade)
data[:fade] *= ramp
data[-fade:] *= ramp[::-1]
data = np.concatenate([data, np.zeros(round(TAIL_SILENCE_S * FS))])

# Normalize
data = data / np.max(np.abs(data)) * TARGET_PEAK
pcm = (data * 32767.0).astype(np.int16)

with open("lobby-loop.pcm", "wb") as f:
    f.write(pcm.tobytes())

with wave.open("lobby-loop.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(FS)
    w.writeframes(pcm.tobytes())

print(f"loop: {len(pcm)} samples ({len(pcm) / FS:.2f}s), "
      f"peak {np.max(np.abs(pcm))}, fade {fade / FS:.2f}s each end")
