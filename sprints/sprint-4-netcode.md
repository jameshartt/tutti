# Sprint 4: netcode

**Goal:** Two to four players drive together in a Tutti room with voice running, and collisions feel solid at realistic latency.

**Depends on:** sprints 0, 1 and 3.

This file assumes full rollback. If sprint 1 chose the fallback, items 4.3 and 4.4 become "predict the local car only and blend towards snapshots".

## Work items

### 4.1 Server `GameSession`
- **Model:** Fable 5.2
- **What:** A game thread per active match at normal priority, kept off the core the mixer threads are pinned to. It steps the sim at 60 Hz, buffers inputs by tick with a small jitter allowance, and repeats the last input when one is missing.
- **Done when:** a four-bot match runs for ten minutes with no change in the mixer's cadence statistics.

### 4.2 Snapshots and inputs on the wire
- **Model:** Fable 5.2
- **What:** Input datagrams at 60 Hz carrying the last few ticks of input for loss tolerance. Snapshot datagrams at 30 Hz with quantised position, orientation, velocities and wheel state for each car, plus projectiles, kept under 1,100 bytes. Reliable `game_*` messages for events that must not be lost.
- **Done when:** a four-car snapshot with ten projectiles fits the limit, and the encoder and decoder round-trip in tests on both sides.

### 4.3 Client prediction and rollback
- **Model:** Fable 5.2
- **What:** On each snapshot, restore the world to the server state at that tick, re-apply unacknowledged local inputs up to the present, and smooth the visual correction over a few frames.
- **Done when:** at 80 ms round trip with 2% loss, the local car shows no visible snapping in normal driving.

### 4.4 Remote cars under latency
- **Model:** Fable 5.2
- **What:** Remote cars are simulated forward in the same rollback using their last known input, so contact between cars is predicted instead of arriving late.
- **Done when:** a side-by-side shunt at 80 ms round trip looks the same to both players within a small tolerance.

### 4.5 In-room lobby
- **Model:** Sonnet 5.5
- **What:** From a room, any participant can propose a match. A lobby screen handles map vote, car selection, points-to-win and ready-up, using the existing design tokens. The game opens at `room/[name]/race` and keeps the room's transport and audio alive.
- **Done when:** four players can start a match and return to the room afterwards without reconnecting audio.

### 4.6 Network-condition harness
- **Model:** Opus 5.5
- **What:** A debug option on the server to add delay, jitter and loss to game datagrams per participant, plus an on-screen graph of round trip, snapshot age and rollback depth.
- **Done when:** conditions can be changed during a match.

### 4.7 Joins, leaves and reconnects
- **Model:** Opus 5.5
- **What:** A player who drops mid-match is eliminated for the round and replaced by a bot if bots are enabled. A reconnecting player rejoins at the next round. Late joiners spectate until the next match.
- **Done when:** closing a tab mid-round does not stall the match for the others.

### 4.8 CI: multiplayer end-to-end test
- **Model:** Sonnet 5.5
- **CI**
- **What:** A Playwright test that puts two browser contexts in one room, starts a match, drives both with scripted input and checks that each sees the other move. Run it with a Chromium–Firefox pair and a Chromium–WebKit pair so both transports are covered.
- **Done when:** the test runs in the e2e job and uploads a trace and video on failure.
