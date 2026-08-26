#!/usr/bin/env python3
"""Generate the lobby hold-music loop: an 8-bar bossa nova groove.

Fully synthesized (Karplus-Strong nylon-guitar plucks, sine bass, filtered
noise percussion) so the clip is license-free by construction. Everything is
rendered onto a circular buffer — note tails wrap around the loop boundary —
so the result loops sample-perfectly and the server needs no edge fades.

Output: lobby-loop.pcm (raw 48kHz mono s16le, the format the server streams)
        lobby-loop.wav (same audio, for listening/auditioning)

Usage: python3 generate_lobby_loop.py
"""

import struct
import wave

import numpy as np

FS = 48000
BPM = 132
BEATS = 32  # 8 bars of 4/4
N = round(BEATS * 60 / BPM * FS)  # total samples in the loop
SPB = 60 / BPM * FS  # samples per beat (float; events rounded individually)

rng = np.random.default_rng(4242)
buf = np.zeros(N, dtype=np.float64)


def add_wrapped(start: int, sig: np.ndarray) -> None:
    """Mix sig into buf at start, wrapping past the end (circular render)."""
    start %= N
    first = min(len(sig), N - start)
    buf[start:start + first] += sig[:first]
    if len(sig) > first:
        # Tail wraps to the top of the loop — this is what makes it seamless
        buf[:len(sig) - first] += sig[first:]


def midi_hz(note: int) -> float:
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


def pluck(note: int, dur_s: float, level: float) -> np.ndarray:
    """Karplus-Strong string: noise burst through a decaying comb filter."""
    period = max(2, round(FS / midi_hz(note)))
    n = round(dur_s * FS)
    out = np.empty(n)
    state = rng.uniform(-1.0, 1.0, period)
    # Soften the excitation (nylon rather than steel): 2-pass moving average
    for _ in range(2):
        state = 0.5 * (state + np.roll(state, 1))
    decay = 0.995
    idx = 0
    for i in range(n):
        out[i] = state[idx]
        nxt = (idx + 1) % period
        state[idx] = decay * 0.5 * (state[idx] + state[nxt])
        idx = nxt
    return out * level


def bass_note(note: int, dur_s: float, level: float) -> np.ndarray:
    """Soft upright-ish bass: fundamental + a little 2nd harmonic, exp decay."""
    n = round(dur_s * FS)
    t = np.arange(n) / FS
    f = midi_hz(note)
    env = np.exp(-t * 6.0) * np.minimum(1.0, t * 400.0)  # fast attack
    sig = np.sin(2 * np.pi * f * t) + 0.35 * np.sin(2 * np.pi * 2 * f * t)
    return sig * env * level


def shaker(level: float) -> np.ndarray:
    n = round(0.035 * FS)
    noise = np.diff(rng.uniform(-1, 1, n + 1))  # differenced noise ≈ high-pass
    env = np.exp(-np.arange(n) / (0.010 * FS))
    return noise * env * level


def rim_click(level: float) -> np.ndarray:
    n = round(0.012 * FS)
    t = np.arange(n) / FS
    tone = np.sin(2 * np.pi * 1700 * t) + 0.5 * np.sin(2 * np.pi * 2400 * t)
    env = np.exp(-t * 450.0)
    return tone * env * level


# ── Composition ──────────────────────────────────────────────────────────────
# One chord per bar, gentle I–vi–ii–V in C, twice.
# (guitar voicing, bass root, bass fifth)
PROGRESSION = [
    ([48, 55, 59, 64], 36, 43),  # Cmaj7
    ([45, 55, 60, 64], 33, 40),  # Am7
    ([50, 57, 60, 65], 38, 45),  # Dm7
    ([43, 53, 59, 62], 43, 38),  # G7 (fifth below)
] * 2

# Classic bossa comp figure over 2 bars, positions in 8th notes
COMP_8THS = [0, 3, 6, 9, 12, 14]

for bar, (voicing, b_root, b_fifth) in enumerate(PROGRESSION):
    bar_start = bar * 4 * SPB

    # Guitar: the 2-bar comp pattern, applied on even bars
    if bar % 2 == 0:
        for e in COMP_8THS:
            at = round(bar_start + e * SPB / 2)
            # Chord in the bar the hit lands in (may be the next bar)
            v = PROGRESSION[(bar + e // 8) % len(PROGRESSION)][0]
            for k, note in enumerate(v):
                # Slight strum stagger, gentle humanization
                stagger = k * round(0.006 * FS)
                lvl = 0.16 * rng.uniform(0.85, 1.0)
                add_wrapped(at + stagger, pluck(note, 1.1, lvl))

    # Bass: root on 1, fifth on 3, light pickup 8ths before each
    add_wrapped(round(bar_start), bass_note(b_root, 0.55, 0.42))
    add_wrapped(round(bar_start + 1.5 * SPB), bass_note(b_root, 0.22, 0.22))
    add_wrapped(round(bar_start + 2.0 * SPB), bass_note(b_fifth, 0.55, 0.40))
    add_wrapped(round(bar_start + 3.5 * SPB), bass_note(b_fifth, 0.22, 0.22))

    # Shaker: straight 8ths, accented on the beat
    for e in range(8):
        lvl = 0.055 if e % 2 == 0 else 0.035
        add_wrapped(round(bar_start + e * SPB / 2),
                    shaker(lvl * rng.uniform(0.9, 1.1)))

    # Rim clicks: bossa clave across 2-bar pairs
    if bar % 2 == 0:
        for e in COMP_8THS:
            add_wrapped(round(bar_start + e * SPB / 2), rim_click(0.10))

# ── Master ───────────────────────────────────────────────────────────────────
# Gentle low-pass to take the digital edge off (one-pole, applied circularly
# by processing the loop twice and keeping the second pass)
alpha = 0.25
state = 0.0
for _ in range(2):
    out = np.empty(N)
    for i in range(N):
        state += alpha * (buf[i] - state)
        out[i] = state
smoothed = out

# Normalize to a comfortable hold-music level (~ -10 dBFS peak)
peak = np.max(np.abs(smoothed))
pcm = (smoothed / peak * 0.32 * 32767.0).astype(np.int16)

with open("lobby-loop.pcm", "wb") as f:
    f.write(pcm.tobytes())

with wave.open("lobby-loop.wav", "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(FS)
    w.writeframes(pcm.tobytes())

print(f"loop: {N} samples ({N / FS:.2f}s), peak {np.max(np.abs(pcm))}, "
      f"boundary delta {abs(int(pcm[0]) - int(pcm[-1]))}")
