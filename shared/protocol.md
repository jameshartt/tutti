# Tutti Binary Protocol

## Audio Datagram Format

Audio is sent over unreliable datagrams (WebTransport datagrams or WebRTC
DataChannel unreliable/unordered) in one of two codecs:

- **PCM** (default): uncompressed, fixed 264-byte packets — lowest latency
- **Opus** (weak-link mode): 10ms frames at 32kbps, variable-size packets —
  ~48kbps instead of ~800kbps, for links that cannot carry PCM

Demux is **stateless and per-packet by size**: exactly 264 bytes (or more)
is PCM; a packet of 9–248 bytes is Opus (8-byte header + payload capped at
240 bytes, keeping the ranges disjoint). This means codec switches mid-
stream can never garble audio — each packet identifies itself.

Clients switch codec with the `{"type":"codec"}` control message (see
below). The server transcodes as needed: Opus participants are decoded into
the PCM mixer and their outbound mix is re-encoded, so PCM and Opus
participants coexist in one room. The 2-person direct-forward fast path
applies only when both participants use PCM.

### PCM Packet Layout (264 bytes total)

```
Offset  Size  Field           Description
──────  ────  ─────           ───────────
0       4     sequence        uint32 LE – monotonically increasing per-session
4       4     timestamp       uint32 LE – sample offset from session start (wraps at ~24h)
8       256   samples         128 × int16 LE – mono PCM audio samples
```

### Audio Parameters

| Parameter      | Value                                |
|----------------|--------------------------------------|
| Sample rate    | 48,000 Hz (44,100 Hz on some iOS)    |
| Bit depth      | 16-bit signed integer (little-endian)|
| Channels       | 1 (mono)                             |
| Frame size     | 128 samples = 2.67ms at 48kHz       |
| Bytes/frame    | 256 (128 × 2)                        |
| Header size    | 8 bytes                              |
| Total packet   | 264 bytes                            |
| Packets/sec    | ~375 at 48kHz                        |
| Bandwidth      | ~99 KB/s (~792 kbps) per direction   |

### Opus Packet Layout (9–248 bytes)

```
Offset  Size   Field           Description
──────  ────   ─────           ───────────
0       4      sequence        uint32 LE – shares the session's sequence space
4       4      timestamp       uint32 LE – sample offset (increments by 480/frame)
8       1–240  payload         One Opus frame: 48kHz mono, 10ms (480 samples)
```

Encoder settings: `OPUS_APPLICATION_RESTRICTED_LOWDELAY`, 32kbps, inband
FEC with 10% expected loss. ~100 packets/s.

### Sequence Number

- Starts at 0 when session begins
- Increments by 1 per packet sent
- Used for packet loss detection (gaps in sequence)
- Does NOT wrap; uint32 allows ~133 days at 375 pps

### Timestamp

- Sample offset from session start: `packet_index * 128`
- Used for jitter measurement and reordering if needed
- Wraps at 2^32 samples = ~24.8 hours at 48kHz

## Control Messages (Reliable Channel)

Control messages are sent over WebTransport bidirectional streams or WebSocket.
They use JSON for simplicity in Phase 1 (may migrate to binary if needed).

### Client → Server

```json
// Set gain for a specific participant in your mix
{"type": "gain", "participant_id": "uuid", "value": 0.75}

// Mute/unmute a participant in your mix
{"type": "mute", "participant_id": "uuid", "muted": true}

// Ping for latency measurement
{"type": "ping", "id": 12345, "t": 1700000000000}

// Switch wire codec (both directions for this participant).
// Sent when the client's link monitor detects a weak/recovered link.
{"type": "codec", "codec": "opus"}   // or "pcm"

// Client-side audio health beacon (every ~10s), logged server-side
{"type": "client_stats", "underruns": 0, "gaps": 0, "rtt": 23.5, "codec": "pcm", ...}
```

### Server → Client

```json
// Room state update
{"type": "room_state", "participants": [
  {"id": "uuid", "name": "Alice", "joined_at": 1700000000}
]}

// Participant joined
{"type": "participant_joined", "id": "uuid", "name": "Bob"}

// Participant left
{"type": "participant_left", "id": "uuid"}

// Pong for latency measurement
{"type": "pong", "id": 12345, "client_t": 1700000000000, "server_t": 1700000000001}

// Server processing time report
{"type": "server_timing", "mix_us": 45}

// Vacate request notification
{"type": "vacate_request"}
```

## REST API

### GET /api/rooms

List all rooms with status.

```json
{
  "rooms": [
    {
      "name": "Allegro",
      "participant_count": 2,
      "max_participants": 4,
      "claimed": false
    }
  ]
}
```

### POST /api/rooms/:name/join

Join a room.

**Request:**
```json
{"alias": "Alice", "password": "optional"}
```

**Response (200):**
```json
{
  "participant_id": "uuid",
  "session_token": "token",
  "wt_url": "https://server:4433/wt",
  "ws_url": "wss://server:4433/ws"
}
```

**Response (401):** Password required or incorrect.
**Response (409):** Room is full.

### POST /api/rooms/:name/leave

Leave a room.

### POST /api/rooms/:name/claim

Set a password on a room (first joiner or current participant).

**Request:**
```json
{"password": "secret"}
```

### POST /api/rooms/:name/vacate-request

Request current occupants to vacate. 24-hour cooldown per source IP.

**Response (200):** Request sent.
**Response (429):** Cooldown active.
