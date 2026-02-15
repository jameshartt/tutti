# Audio Latency Optimisation

This document tracks the end-to-end audio latency analysis and optimisation work
for Tutti. All measurements are one-way (speaker A → speaker B's ears) on localhost
at 48kHz / 128 samples per frame (~2.67ms render quantum).

## Pipeline overview

```
Mic → Hardware input → AudioWorklet capture → Ring buffer → TransportBridge
    → Network (WebTransport datagram) → Server on_audio_received
    → [Mixer or direct forward] → Network → Client TransportBridge
    → Ring buffer → AudioWorklet playback (prebuffer) → Hardware output → Speaker
```

## Latency budget (localhost, 2 participants)

| Component | Initial | After Round 1 | After Round 2 | Controllable? |
|---|---|---|---|---|
| Hardware input (`baseLatency`) | ~3ms | ~3ms | ~3ms | No (hardware) |
| Capture quantum (avg sample age) | ~1.3ms | ~1.3ms | ~1.3ms | No (browser) |
| Client capture ring buffer | ~170ms | ~21ms | ~21ms | Yes — reduced to 8 frames |
| Capture drain | polling (~4ms) | event-driven (~0ms) | event-driven (~0ms) | Yes — worklet `postMessage` |
| Network (localhost) | ~0.05ms | ~0.05ms | ~0.05ms | No |
| Server mixer wait | ~2.67ms | ~1.3ms avg | **~0.02ms** | Yes — direct forwarding |
| Server mix + send | ~0.5ms | ~0.5ms | ~0.02ms | Partially — bypass for 2p |
| Network (localhost) | ~0.05ms | ~0.05ms | ~0.05ms | No |
| Playback prebuffer | 0ms | 5.3ms (2 frames) | **2.67ms (1 frame)** | Yes |
| Playback quantum wait | ~1.3ms | ~1.3ms | ~1.3ms | No (browser) |
| Hardware output (`outputLatency`) | ~3ms | ~3ms | ~3ms | No (hardware) |

**Headline numbers (2 participants, localhost, default audio device):**

| | One-way | Acoustic equivalent (343 m/s) |
|---|---|---|
| Before optimisation | ~40ms+ | ~14m |
| After Round 1 | ~15.8ms | ~5.4m |
| After Round 2 | **~11.3ms** | **~3.9m** |
| With low-latency USB interface | **~6ms** | **~2.1m** |

## Round 1 changes

Commit: `b4e4278`

1. **Shrink ring buffers** (client + server): 64 → 8 frames. The original 64-frame
   capacity (~170ms) was the initial scaffold default. 8 frames (~21ms) still
   provides comfortable headroom for jitter without adding perceptible latency.

2. **Mixer lock consolidation** (server): Snapshot all participant `shared_ptr`s in
   a single lock acquisition per mix cycle instead of re-locking per participant.
   Eliminated 8+ mutex acquisitions on the RT path.

3. **Decouple send from lock** (server): Collect serialised outputs under the
   participants mutex, then send datagrams after releasing it. Prevents the mixer
   thread from holding the mutex during network I/O.

4. **Precision sleep** (server, Linux): Replaced `std::this_thread::sleep_for` with
   `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME)` for ~50us precision vs 1–4ms
   overshoot from the standard library.

5. **Event-driven capture drain** (client): The capture worklet now sends a
   `postMessage('frame-ready')` notification each quantum. TransportBridge drains
   immediately on notification instead of polling with `setInterval`.

6. **Playback prebuffer** (client): Added a 2-frame prebuffer to prevent
   phase-locked underruns where the worklet read cadence perfectly aligns with
   packet arrival, causing every other quantum to find an empty buffer.

## Round 2 changes

Commit: `6d81577`

1. **Direct forwarding for 2-participant rooms** (server): When exactly 2
   participants are in a room, `on_audio_received()` bypasses the SPSC queue →
   mixer thread → SPSC queue pipeline entirely. The incoming datagram is forwarded
   directly to the other participant on the receive thread.

   - Default gain (1.0): near-zero-copy — `memcpy` the raw packet bytes, overwrite
     the sequence number, send. No deserialisation or sample processing.
   - Custom gain: deserialise, apply gain with int16 clamping, re-serialise, send.
   - Muted: drop silently.

   This eliminates ~1.8ms of average server-side latency (SPSC enqueue + mixer
   sleep + dequeue + re-enqueue + dequeue + serialise).

2. **Event-driven mixer for 3+ participants** (server): Replaced the fixed
   `clock_nanosleep` cadence with `eventfd` + `poll`. Each incoming audio frame
   increments an atomic counter; when all participants have submitted, the eventfd
   wakes the mixer thread immediately. Falls back to a 3ms timeout if not all
   frames arrive.

   On localhost, all frames arrive within ~0.1ms of each other, so the mixer fires
   almost immediately — saving ~1ms avg compared to the fixed 2.67ms sleep.

3. **Reduce playback prebuffer** (client): `PREBUFFER_FRAMES` reduced from 2 to 1
   (5.3ms → 2.67ms). On localhost with near-zero jitter, a single frame provides
   sufficient cushion against timing drift. This removes 2.67ms of permanent
   pipeline latency.

## Hard floor (~6–8ms)

The following components cannot be reduced without changes outside our control:

| Component | Latency | Why it's fixed |
|---|---|---|
| Hardware input (`baseLatency`) | ~3ms | OS audio driver + ADC. Low-latency USB interfaces achieve <1ms. |
| Capture quantum (average sample age) | ~1.3ms | 128-sample AudioWorklet quantum at 48kHz. Samples arrive mid-quantum on average. |
| Playback quantum wait | ~1.3ms | Same — output quantum alignment. |
| Hardware output (`outputLatency`) | ~3ms | OS audio driver + DAC. Same as input. |

**Total hard floor: ~8.6ms** (default device) / **~3.6ms** (low-latency USB interface)

## Future improvement opportunities

### Achievable within the current architecture

- **Adaptive prebuffer**: Dynamically adjust `PREBUFFER_FRAMES` based on observed
  jitter. Use 0 frames on localhost (where jitter is negligible), 1 for LAN, 2+
  for WAN. Could be driven by a rolling jitter estimate from packet timestamps.

- **Opus compression**: Currently sending raw PCM (256 bytes/frame). Opus at
  48kHz/128 samples can compress to ~40–80 bytes, reducing network transit time on
  real networks. Opus also supports its own FEC for packet loss resilience. The
  encode/decode adds ~0.1ms on modern CPUs, which is negligible.

- **Jitter buffer with packet reordering** (client): The current ring buffer is
  FIFO with no sequence awareness. On real networks, out-of-order packets cause
  glitches. A small jitter buffer (1–2 frames) with sequence-based reordering
  would handle this. Increases latency slightly but improves audio quality.

- **Server-side gain in the 2-participant fast path**: Currently queries the
  mixer's gain map under a mutex. If gains were stored as atomics or a lock-free
  structure, this could be fully lock-free.

### Requires architectural changes

- **Native audio I/O via WASM**: Bypass the Web Audio API entirely using a WASM
  module that talks directly to WASAPI (Windows), CoreAudio (macOS), or ALSA/JACK
  (Linux). This could eliminate the AudioWorklet quantum overhead (~2.6ms) and
  reduce hardware I/O latency with smaller buffer sizes (32 or 64 samples).

- **Sub-128-sample AudioWorklet quantum**: The Web Audio spec hardcodes 128
  samples. A future spec revision or browser flag could allow smaller quanta
  (64 or 32 samples), halving or quartering the quantum-related latency.

- **UDP transport**: WebTransport uses QUIC (which runs over UDP but adds framing
  overhead). A native client could use raw UDP datagrams for minimal transport
  overhead, though the gains on localhost are negligible.

- **Hardware-accelerated mixing**: For rooms with many participants, GPU-based
  audio mixing could process all streams in parallel. Unlikely to matter until
  8+ simultaneous streams.
