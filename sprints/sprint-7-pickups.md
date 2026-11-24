# Sprint 7: pickups

**Goal:** All seven pickups work in multiplayer.

**Depends on:** sprint 5. Independent of sprint 6.

The mechanics match the original mode; the names, models and effects are our own.

## Work items

### 7.1 Pickup framework
- **Model:** Opus 5.5
- **What:** Spawn points from the track file, collection, a one-item slot, respawn timers, and a seeded roll weighted by race position so the back car gets stronger items and the leader gets defensive ones. Everything is part of the authoritative, deterministic state.
- **Done when:** the roll distribution by position matches the configured weights over many trials in a test.

### 7.2 Rocket, bomb and machine gun
- **Model:** Opus 5.5
- **What:**
  - **Rocket:** fired forward, or backward when the player is holding reverse, travelling in a straight line and destroying what it hits.
  - **Bomb:** dropped behind the car, detonating after a short fuse or on contact and destroying nearby cars.
  - **Machine gun:** fires forward for 5 seconds, wearing down the car it hits.

  Explosions apply real impulses to nearby cars.
- **Done when:** each can eliminate a bot, and near misses shove cars without destroying them.

### 7.3 Control invert
- **Model:** Sonnet 5.5
- **What:** For 10 seconds, every other player's accelerate and brake are swapped. Applied in the sim so prediction stays correct, with a clear on-screen warning for those affected.
- **Done when:** affected clients predict correctly through the start and end of the effect.

### 7.4 Vehicle transforms
- **Model:** Fable 5.2
- **What:** Three timed transforms that replace the car's body and handling in place and restore it afterwards, without breaking rollback:
  - **Light-trail bike, 5 seconds:** leaves a trail that destroys cars touching it.
  - **Ramp wedge, 10 seconds:** a sloped front that launches cars it drives under.
  - **Jump car, 10 seconds:** can hop, deploy a parachute to glide, and fire rockets.

  Swapping bodies mid-simulation must conserve momentum and never leave a car intersecting the track.
- **Done when:** each transform starts and ends cleanly at speed, in contact with another car, and in mid-air, with matching state hashes on server and client.

### 7.5 Effects and feedback
- **Model:** Sonnet 5.5
- **What:** Visual effects, HUD icons and timers for each pickup, and placeholder sounds.
- **Done when:** a new player can tell what they are holding and what just hit them.

### 7.6 CI: pickup scenarios
- **Model:** Sonnet 5.5
- **CI**
- **What:** Scripted server scenarios for each pickup, including the edge cases in 7.4, added to the sim test suite and the golden-hash check.
- **Done when:** they run in the server job.
