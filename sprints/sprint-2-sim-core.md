# Sprint 2: sim core

**Goal:** A headless game simulation that a bot can drive around a track, running identically on the server and in WASM.

**Depends on:** sprint 1.

## Work items

### 2.1 Simulation skeleton
- **Model:** Fable 5.2
- **What:** The `tutti-game` world: fixed 60 Hz step, a per-player input struct (steer, throttle, brake, handbrake, use pickup, bail), a tick counter, and full state save and restore built on Jolt's state recorder. No wall-clock time and no unseeded randomness inside the step.
- **Done when:** saving at tick N, running to N+120, restoring and re-running produces identical hashes.

### 2.2 Vehicle model
- **Model:** Fable 5.2
- **What:** A car built on Jolt's wheeled vehicle with a handling parameter struct: mass, drive force, top speed, traction at grip and at slide, brake force, handbrake behaviour, suspension, centre of mass. The target is GTA Online's arcade feel: weighty, forgiving, with slides you can hold.
- **Done when:** parameters load from JSON and changing them visibly changes acceleration, grip and drift in a test.

### 2.3 Assists
- **Model:** Opus 5.5
- **What:** Forces layered on the vehicle: speed-scaled downforce, mid-air pitch and roll control, self-righting when flipped and steering is held, and a stability aid that damps spin-outs. Each has a strength parameter.
- **Done when:** with assists on, a scripted hard corner at top speed does not spin the car; with them off, it does.

### 2.4 Track format and collision mesh
- **Model:** Opus 5.5
- **What:** A JSON track format: a centreline spline with width, banking and elevation, wall segments, props, checkpoints, grid slots and pickup spawn points. Shared C++ code turns it into a Jolt mesh deterministically, so server and client build the same geometry. Supports banked turns, jumps, tubes, loops and crossovers.
- **Done when:** the same JSON yields an identical mesh hash natively and in WASM.

### 2.5 Progress, checkpoints and off-track detection
- **Model:** Opus 5.5
- **What:** Per-car distance along the track, checkpoint passing, ranking of cars by progress, and detection of a car that has left the track and fallen below it.
- **Done when:** ranking is correct through a crossover and after a car skips a corner by falling to a lower section.

### 2.6 Bot driver
- **Model:** Opus 5.5
- **What:** A driver that follows the racing line with look-ahead steering and brakes for corners. It produces the same input struct as a player.
- **Done when:** the bot completes ten laps of the test track without leaving it.

### 2.7 Sim test suite
- **Model:** Sonnet 5.5
- **What:** gtest coverage for the items above, plus a golden-hash test that fails if simulation behaviour changes unintentionally.
- **Done when:** the tests run in CI in the server job.

### 2.8 CI: headless bot run
- **Model:** Sonnet 5.5
- **CI**
- **What:** Run the bot for a fixed number of laps in CI and fail if it leaves the track or its lap time moves outside a tolerance.
- **Done when:** the job reports the lap time on every pull request.
