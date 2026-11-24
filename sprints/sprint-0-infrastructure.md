# Sprint 0: infrastructure groundwork

**Goal:** Tutti can carry game traffic next to audio without disturbing it, and every later change is checked by CI.

**Depends on:** nothing.

## Work items

### 0.1 Buffer partial lines in the WebTransport reliable stream
- **Model:** Opus 5.5
- **What:** `server/src/transport/wt_transport.cpp` splits incoming stream data with `getline` per receive event and does not keep a partial line for the next event. Add a per-session buffer so a message split across QUIC receive events is reassembled.
- **Done when:** a gtest feeds one JSON message in two fragments and the callback fires once with the whole message; a 16 KB message survives a round trip.

### 0.2 Game datagram channel
- **Model:** Fable 5.2
- **What:** Add `sendGameDatagram` / `onGameDatagram` to the client `Transport` interface and the equivalent on the server `TransportSession`.
  - WebRTC: a third DataChannel `game`, unordered with no retransmits. `connect()` does not wait for it, so joining audio is never held up by game plumbing; the transport reports when game datagrams are ready.
  - WebTransport: game datagrams start with a reserved 4-byte magic in the position of the audio sequence number, followed by a kind byte. The audio sequence counter must skip that value.
  - Demux happens in the transport layer, before `TransportBridge.handleIncoming` and before `SessionBinder::on_datagram` hands data to the room's audio path.
- **Done when:** audio packets are byte-for-byte unchanged; game datagrams never reach the audio decoder and audio never reaches game handlers; tests cover both directions on both transports.

### 0.3 Count game datagrams as activity in the reaper
- **Model:** Sonnet 5.5
- **What:** `Room::reap_stale_participants` only treats audio as activity, so a room of muted racers is removed after 15 seconds. Update the activity timestamps on game datagrams as well.
- **Done when:** a test with two bound participants exchanging only game datagrams survives past the inactivity timeout.

### 0.4 `game_*` message routing and an empty `GameSession`
- **Model:** Opus 5.5
- **What:** Route reliable messages whose type starts with `game_` from `SessionBinder::on_message` to a `GameSession` owned by `Room`. On the client, dispatch them from `handleControlMessage` to a game message bus. `GameSession` only logs for now and can broadcast with `Room::broadcast_control`.
- **Done when:** a `game_ping` sent by one client is echoed to every participant in the room, and unknown `game_*` types are logged and ignored.

### 0.5 Server time in `pong` and a client clock offset
- **Model:** Sonnet 5.5
- **What:** `shared/protocol.md` documents `server_t` in `pong` but the server does not send it. Add it, and keep a smoothed clock-offset estimate in `rtt-monitor.ts`.
- **Done when:** the client exposes a server-time estimate that stays within a few milliseconds over a local connection.

### 0.6 Client test tooling
- **Model:** Sonnet 5.5
- **What:** Add Vitest for unit tests and Playwright with Chromium, Firefox and WebKit projects. Add `test` and `test:e2e` scripts. Write one smoke test: load the lobby and list rooms.
- **Done when:** both scripts pass locally in all three engines.

### 0.7 Server tests for the binder and demux
- **Model:** Sonnet 5.5
- **What:** gtest coverage for `SessionBinder` (bind, unknown types, `game_*` routing) and the datagram demux, using the existing `FakeSession`.
- **Done when:** the tests are part of `tutti-tests` and pass.

### 0.8 Update the protocol document
- **Model:** Haiku 5.5
- **What:** Record the game datagram framing, the `game` DataChannel, `game_*` messages and `server_t` in `shared/protocol.md`. Remove or mark the fields that are documented but not implemented.
- **Done when:** the document matches the code.

### 0.9 CI: pull-request pipeline
- **Model:** Sonnet 5.5
- **CI**
- **What:** Add `.github/workflows/ci.yml`, run on pull requests and pushes to `main`:
  - **server job:** configure with WebTransport off, build, run `tutti-tests`. Cache the FetchContent dependencies.
  - **client job:** `npm ci`, `npm run check`, `npm run test`, `npm run build`.
  - **e2e job:** start the server and client, run Playwright in all three engines, upload traces on failure.
- **Done when:** a pull request shows three green checks, and a deliberately broken test turns the right one red.

### 0.10 CI: container build
- **Model:** Sonnet 5.5
- **CI**
- **What:** The WebTransport-enabled server only builds inside the Docker image, not on a developer machine. Add a job that builds both Docker images with layer caching so that configuration is checked on every pull request.
- **Done when:** both images build in CI in a reasonable time on a warm cache.

### 0.11 CI: branch protection
- **Model:** Human
- **CI**
- **What:** Require the CI checks before merging to `main`.
- **Done when:** a pull request with a failing check cannot be merged.
