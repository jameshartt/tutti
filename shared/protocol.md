# Tutti Binary Protocol

## Audio Datagram Format

All audio is sent as uncompressed PCM over unreliable datagrams (WebTransport datagrams or WebRTC DataChannel unreliable/unordered).

### Packet Layout (264 bytes total)

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
