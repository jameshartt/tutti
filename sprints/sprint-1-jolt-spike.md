# Sprint 1: Jolt spike

**Goal:** Decide, with measurements, whether full client rollback on Jolt is viable. This is a go/no-go gate for sprint 4.

**Depends on:** sprint 0 for CI only.

## Work items

### 1.1 Jolt in the native build
- **Model:** Opus 5.5
- **What:** Add Jolt Physics through FetchContent with its cross-platform deterministic option on. Build a minimal scene: a ground mesh and four wheeled vehicles driven by scripted inputs. Keep it in a new `server/game/` directory with its own library target, `tutti-game`.
- **Done when:** a command-line program steps the scene for 60 seconds of game time and prints a state hash.

### 1.2 Emscripten build of the same scene
- **Model:** Opus 5.5
- **What:** Build `tutti-game` to single-threaded WASM with a small C API (create, step, set input, save state, restore state, read state). Produce the WASM in a Docker stage and copy it into the client's static assets so it is served same-origin.
- **Done when:** a test page loads the module in all three engines and prints the same kind of state hash.

### 1.3 Determinism test
- **Model:** Fable 5.2
- **What:** Run the same scripted inputs natively and in Chromium, Firefox and WebKit, hashing the full physics state every tick. Include collisions, a jump and a rollover.
- **Done when:** the hashes match on every tick across all four, or the first diverging tick and its cause are identified.

### 1.4 Rollback cost benchmark
- **Model:** Fable 5.2
- **What:** Measure save, restore and re-simulation of 10 ticks with four cars and ten loose bodies. Report the time per rollback on a desktop browser and on a real iPhone.
- **Done when:** there are numbers for each platform. The target is a full rollback inside 4 ms on the iPhone, leaving room in a 16.7 ms frame.

### 1.5 Decision
- **Model:** Fable 5.2 writes it up; a human decides
- **What:** A short note in this folder recording the results and which path sprint 4 takes:
  - **Full rollback** if hashes match and the cost is within budget.
  - **Fallback** otherwise: the server stays authoritative, the client predicts only its own car and blends towards snapshots, accepting softer collisions.
- **Done when:** `sprint-1-result.md` exists and sprint 4's file is updated if the fallback was chosen.

### 1.6 CI: WASM build and determinism check
- **Model:** Sonnet 5.5
- **CI**
- **What:** Add a job that builds the WASM module, caches the Emscripten toolchain, and publishes the module as an artifact for the client and e2e jobs. Add the determinism test from 1.3 to the e2e job so native and the three engines are compared on every pull request.
- **Done when:** changing a physics constant in one build only makes the determinism check fail.
