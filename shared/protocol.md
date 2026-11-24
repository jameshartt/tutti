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
- Never takes the value `0xFFFFFFFF`, which is reserved to mark game
  datagrams (see below). After `0xFFFFFFFE` the next sequence is `0`
  (~133 days at 375 pps)

### Timestamp

- Sample offset from session start: `packet_index * 128`
- Used for jitter measurement and reordering if needed
- Wraps at 2^32 samples = ~24.8 hours at 48kHz

## Game Datagram Format

Game traffic uses unreliable datagrams too, kept strictly apart from audio.
Above the transport layer a game datagram is always:

```
[kind: u8][payload: 0–1099 bytes]
```

`kind + payload` is at most 1100 bytes. How it is carried depends on the
transport:

- **WebTransport** has one datagram channel, shared with audio. A game
  datagram is prefixed with four `0xFF` bytes:

  ```
   0        4      5
  +--------+------+----------------+
  | FF×4   | kind | payload        |
  +--------+------+----------------+
  ```

  Those four bytes sit where an audio packet carries its sequence number,
  and no audio packet ever has sequence `0xFFFFFFFF`, so the two cannot be
  confused. Receivers check the prefix first and strip it; anything without
  it goes to the audio path unchanged.

- **WebRTC** uses a third DataChannel labelled `game` (`ordered: false`,
  `maxRetransmits: 0`), created by the client alongside `audio` and
  `control`. Datagrams on it carry no prefix. The channel is optional: a
  session is established as soon as `audio` and `control` are open, and
  game datagrams become available when `game` opens.

Game datagrams from a session that has not sent `bind` are dropped. They
count as activity for the server's inactivity reaper, exactly like audio.

### Kinds

| Kind | Name | Direction | Meaning |
|------|------|-----------|---------|
| `0x00` | Echo | both | Diagnostic. The server sends the datagram straight back to the sender. Used by `/dev/selftest` and the end-to-end tests. |

Unknown kinds are dropped silently.

## Control Messages (Reliable Channel)

Control messages are JSON. On WebTransport they travel on one
client-opened bidirectional stream, one message per line (`\n`-terminated);
a message may be split across stream reads, and both ends reassemble it.
On WebRTC they travel on the reliable `control` DataChannel, one message
per DataChannel message.

### Client → Server

```json
// Set gain for a specific participant in your mix
{"type": "gain", "participant_id": "uuid", "value": 0.75}

// Mute/unmute a participant in your mix
{"type": "mute", "participant_id": "uuid", "muted": true}

// Ping for latency measurement and clock sync. `t` is the client's
// send time in ms on its own monotonic clock; it is echoed back untouched.
{"type": "ping", "id": 12345, "t": 81234.5}

// Switch wire codec (both directions for this participant).
// Sent when the client's link monitor detects a weak/recovered link.
{"type": "codec", "codec": "opus"}   // or "pcm"

// Client-side audio health beacon (every ~10s), logged server-side
{"type": "client_stats", "underruns": 0, "gaps": 0, "rtt": 23.5, "codec": "pcm", ...}

// Any type starting with "game_" is routed to the room's game session.
// Unknown game_ types are logged and ignored.
// Diagnostic: relayed to every participant in the room (see below).
{"type": "game_ping", "n": 7}
```

Types the server does not recognise are ignored.

### Server → Client

```json
// Room state update
{"type": "room_state", "participants": [
  {"id": "uuid", "name": "Alice"}
]}

// Participant joined
{"type": "participant_joined", "id": "uuid", "name": "Bob"}

// Participant left
{"type": "participant_left", "id": "uuid"}

// Pong: the ping echoed back with its type changed, plus `server_t`, the
// server's monotonic clock in ms (fractional) at the moment it replied.
// Clients estimate the clock offset as server_t - (t + rtt / 2).
{"type": "pong", "id": 12345, "t": 81234.5, "server_t": 5123456.789}

// Bind failed
{"type": "error", "error": "room_not_found"}   // or "participant_not_found"

// game_ping relayed to everyone in the room, stamped with the sender
{"type": "game_ping", "n": 7, "from": "uuid"}

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
