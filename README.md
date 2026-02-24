# Tutti - All Together

Low-latency browser-based music rehearsal for musicians in the same city. Sends uncompressed PCM audio (48kHz/16-bit/mono) with minimal latency, targeting ~15-25ms one-way — equivalent to being 5-8 metres apart.

Named after the Italian musical term meaning "all together."

## Architecture

```
Browser (SvelteKit)                       C++ Server
┌─────────────────────┐                  ┌─────────────────────┐
│ AudioWorklet ←→ SAB │                  │ libwtf    libdatach │
│ TransportBridge     │  UDP datagrams   │ (WT)      (WebRTC)  │
│ WebTransport/WebRTC │◄────────────────►│ SPSC ring buffers   │
└─────────────────────┘  264-byte PCM    │ Mixer (RT thread)   │
                         packets @375/s  └─────────────────────┘
```

- **WebTransport** (primary): Chrome, Firefox, Edge — unreliable QUIC datagrams via libwtf + msquic
- **WebRTC DataChannel** (fallback): Safari, iOS — unreliable unordered DataChannel via libdatachannel
- **Server-side mixing**: each participant gets a custom mix of all others

## Prerequisites

- CMake 3.22+
- C++17 compiler (GCC 12+ or Clang 15+)
- OpenSSL 3.x (`libssl-dev`)
- Node.js 18+
- `openssl` CLI (for cert generation)

## Quick Start

```bash
# 1. Generate TLS certs (needed for WebTransport)
cd server/certs && ./generate.sh && cd ../..

# 2. Build and run the server (sudo grants RT thread priority)
./dev.sh

# 3. In another terminal, start the client
cd client && npm install && npm run dev
```

The server listens on:
- `http://0.0.0.0:8080` — REST API (room listing, join/leave)
- `ws://0.0.0.0:8081` — WebSocket signaling (WebRTC SDP exchange)
- `https://0.0.0.0:4433` — WebTransport (QUIC/UDP audio)

## Dev Script

`./dev.sh` builds the server with WebTransport enabled and runs it under `sudo` for RT thread priority:

```bash
./dev.sh              # build + run
./dev.sh --run-only   # skip build, just run
./dev.sh --build-only # build only, don't run
```

## Server CLI Options

```
tutti-server [options]
  --bind <addr>            Bind address (default: 0.0.0.0)
  --http-port <port>       HTTP API port (default: 8080)
  --ws-port <port>         WebSocket signaling port (default: 8081)
  --wt-port <port>         WebTransport port (default: 4433)
  --max-participants <n>   Max per room (default: 4)
  --hostname <name>        Public hostname for URLs (default: localhost)
  --cert <path>            TLS cert (default: certs/cert.pem)
  --key <path>             TLS key (default: certs/key.pem)
```

## TLS Certificates

WebTransport requires QUIC which requires TLS. For local dev, self-signed certs work — Chrome accepts them via `serverCertificateHashes`.

```bash
cd server/certs && ./generate.sh
```

Generates `cert.pem`, `key.pem`, and `hash.txt` (SHA-256 hash for Chrome). Certs are valid for 14 days. Regenerate when they expire.

## Project Structure

```
tutti/
├── server/
│   ├── CMakeLists.txt
│   ├── cmake/FetchDependencies.cmake
│   ├── certs/                    # TLS certs (generate.sh)
│   ├── src/
│   │   ├── main.cpp
│   │   ├── transport/            # WebTransport + WebRTC transports
│   │   ├── audio/                # Mixer, rooms, SPSC ring buffers
│   │   ├── rooms/                # Room manager, 16 named rooms
│   │   ├── signaling/            # HTTP API + WebSocket signaling
│   │   └── telemetry/            # RTT measurement, latency tracking
│   └── tests/
├── client/                       # SvelteKit app
│   └── src/
│       ├── lib/audio/            # AudioWorklet, ring buffers, transport bridge
│       ├── lib/transport/        # WebTransport + WebRTC implementations
│       ├── lib/components/       # Room, Mixer, Lobby UI
│       └── routes/               # SvelteKit pages
├── shared/protocol.md            # Binary protocol spec
├── dev.sh                        # Build + run script
├── deploy.sh                     # Production provisioning script
├── docker-compose.yml            # Docker Compose stack
├── Caddyfile                     # Caddy reverse proxy config
├── DEPLOY.md                     # Production deployment guide
└── docs/
    ├── PLAN.md                   # Full design document
    └── PROGRESS.md               # Implementation status
```

## API

`GET /api/rooms` — returns all 16 rooms with status:

```json
{
  "rooms": [
    { "name": "Allegro", "participant_count": 0, "max_participants": 4, "claimed": false },
    ...
  ]
}
```

## Build Notes

First build takes several minutes — FetchContent downloads msquic (with OpenSSL submodule), libwtf, libdatachannel, SPSCQueue, and GoogleTest. Subsequent builds are fast.

The WebTransport support is behind a CMake flag (`TUTTI_ENABLE_WEBTRANSPORT`, default OFF). The dev script enables it automatically. To build manually:

```bash
cd server
cmake -B build -DTUTTI_ENABLE_WEBTRANSPORT=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

## Deployment

See [DEPLOY.md](DEPLOY.md) for production deployment instructions (Docker, TLS, SSH hardening, fail2ban).

## Tests

```bash
cd server && ./build/tutti-tests
```
