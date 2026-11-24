# Sprint 6: ragdoll driver

**Goal:** Drivers jump out of their cars and tumble with ragdoll physics: on pressing Y, on falling off the track, and when the match ends.

**Depends on:** sprint 5. Independent of sprint 7.

## Work items

### 6.1 Ragdoll and bail event in the sim core
- **Model:** Fable 5.2
- **What:** A Jolt ragdoll of about 11 bodies for an original low-poly driver. A bail event records the tick, the car, the exit side and the car's velocity. The ragdoll is not part of the authoritative state: it is spawned from the event and simulated locally, so it must never feed back into car physics.
- **Done when:** spawning and simulating ragdolls leaves the authoritative state hash unchanged.

### 6.2 Abandoned cars and elimination
- **Model:** Opus 5.5
- **What:** When a player bails mid-round, they are eliminated and the car keeps rolling under server simulation with no input, as a hazard to others. It is removed when it stops, falls off, or the round ends.
- **Done when:** an abandoned car can knock another player off the track, and the bailing player cannot win that round.

### 6.3 Client ragdoll simulation and rendering
- **Model:** Opus 5.5
- **What:** On a bail event, spawn the ragdoll at the car with inherited velocity plus an exit impulse, simulate it in a separate client-side Jolt world that includes the track and the cars as moving obstacles, and drive a skinned mesh from it. Cars push ragdolls; ragdolls do not push cars.
- **Done when:** a bail at top speed sends the driver tumbling along the track, and a passing car knocks them aside.

### 6.4 Triggers
- **Model:** Sonnet 5.5
- **What:** Bail on gamepad Y, on the F or Y key, and on a touch button. Automatic bail when the car has left the track and is falling. Everyone bails when the match ends, the winner last.
- **Done when:** all three triggers work on all input types.

### 6.5 Driver model and rig
- **Model:** Sonnet 5.5 for the import pipeline; a human sources or approves the art
- **What:** An original low-poly driver, rigged to match the ragdoll bodies, tinted to match the player's car.
- **Done when:** the driver is visible in the car from the top-down camera and the rig follows the ragdoll without stretching.

### 6.6 Performance guard
- **Model:** Opus 5.5
- **What:** Cap the number of live ragdolls, put them to sleep when still, and remove them at round restart. Measure the cost on an iPhone.
- **Done when:** four simultaneous ragdolls at match end do not drop the frame rate below target on the iPhone.

### 6.7 CI: bail test
- **Model:** Sonnet 5.5
- **CI**
- **What:** A server test that a bail eliminates the player and leaves the state hash unaffected by ragdolls, and a Playwright test that pressing the bail key shows the driver outside the car.
- **Done when:** both run in CI.
