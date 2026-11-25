# Sprint 1 result: is client rollback on Jolt viable?

**Recommendation: full rollback**, subject to one number still missing: the rollback cost on a real iPhone (manual step M3).

## What was tested

A scene with four wheeled cars, ten loose crates, a ground mesh with a jump ramp and a one-sided wedge, inside four walls. A fixed input script drives it for 60 seconds of game time (3600 ticks at 60 Hz): ten seconds of set pieces, then pseudo-random driving. The run includes car-to-car hits, cars ploughing through the crates, a jump, a rollover and wall hits; a test fails if any of those stops happening.

After every tick the full physics state is hashed: position, rotation and both velocities of all 14 bodies, each wheel's spin, angle, suspension length and ground contact, and each car's engine speed and gear.

Jolt Physics 5.7.0, built with its cross-platform deterministic option, scalar WebAssembly (no SIMD), Emscripten 7.0.1.

## Determinism

| Build | Result |
|---|---|
| Native, x86-64, GCC 15 | reference |
| WebAssembly in Node 24 (V8) | identical on all 3600 ticks |
| WebAssembly in Chromium | identical on all 3600 ticks |
| WebAssembly in Firefox | identical on all 3600 ticks |
| WebAssembly in WebKit (Playwright build, Linux) | identical on all 3600 ticks |

Also checked:

- Saving a snapshot, running on, restoring and replaying the same inputs gives identical hashes, from six different points in the run, and when the snapshot is restored into a freshly created world.
- The comparison can fail: with gravity changed from 9.81 to 9.80 in the WebAssembly build only, all three browsers reported a difference at tick 1.

Not yet checked: real Safari on an iPhone (ARM). The test page compares against the reference on the device, so M3 covers it.

## Rollback cost

One rollback = restore a snapshot, then re-simulate 10 ticks, saving a snapshot after each. Measured from tick 240, with crates in motion. Snapshot size: 5670 bytes.

| Platform | One rollback | One tick | Save | Restore |
|---|---|---|---|---|
| Native (Threadripper 2950X, 2018) | 0.80 ms | 0.070 ms | 0.009 ms | 0.006 ms |
| Chromium, same machine | 1.34 ms | 0.123 ms | 0.011 ms | 0.008 ms |
| Firefox, same machine | 1.80 ms | 0.156 ms | 0.016 ms | 0.013 ms |
| WebKit, same machine | 1.36 ms | 0.122 ms | 0.013 ms | 0.006 ms |
| iPhone, Safari | not measured yet (M3) | | | |

The budget is 4 ms on the iPhone. The desktop figures are well inside it, on a processor whose single-core speed is below a recent iPhone's, so the iPhone is expected to pass; that is an expectation, not a measurement.

## Caveats

- The spike has four cars. A full match may have more, plus pickups and projectiles; cost grows roughly with the number of moving bodies in contact. At the measured rate there is room for about twice the load inside the budget on this desktop.
- The cost is almost all re-simulation (10 ticks); saving and restoring are negligible. Rolling back fewer ticks is proportionally cheaper.
- The steering angle of a wheel is not part of Jolt's snapshot (it is recomputed from the input each tick), so it is left out of the hash. It does not affect the simulation.
- Determinism holds only while both sides run the same Jolt version, the same build flags and the same order of operations (bodies created in the same order). CI now compares native against WebAssembly in Node and in all three browsers on every pull request, so a change that breaks this is caught.
- The WebAssembly module is 1.0 MB (about 300 KB compressed).

## Decision

- [ ] **Full rollback** (recommended): the client runs the same simulation, predicts every car, and re-simulates when the server corrects it.
- [ ] **Fallback**: the client predicts only its own car and blends towards server snapshots.

Decided by: _pending (manual step M4)_
