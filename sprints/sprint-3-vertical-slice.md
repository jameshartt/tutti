# Sprint 3: offline vertical slice

**Goal:** One car on one track in the browser, offline, that is fun to drive. Handling is settled here before anything is built on it.

**Depends on:** sprint 2. Can run alongside sprint 0.

## Work items

### 3.1 Renderer and WASM bridge
- **Model:** Opus 5.5
- **What:** Add three.js. Load the sim WASM, step it from a fixed-timestep loop, and render with interpolation between ticks. Build the visible track from the same JSON as the collision mesh.
- **Done when:** the scene holds 60 frames per second on a mid-range laptop in all three engines.

### 3.2 Camera
- **Model:** Opus 5.5
- **What:** A high top-down camera that follows the front of the field, zooms to keep nearby cars in frame, and rotates smoothly with the track direction. Look-ahead distance is a parameter so the leader's can be reduced later.
- **Done when:** the camera stays stable through jumps, loops and crossovers without snapping.

### 3.3 Input
- **Model:** Sonnet 5.5
- **What:** One input layer feeding the sim's input struct from keyboard, Gamepad API (analogue steering and triggers, Y to bail) and on-screen touch controls.
- **Done when:** all three work, and a gamepad connected mid-session is picked up.

### 3.4 Dev route and tuning panel
- **Model:** Sonnet 5.5
- **What:** A `/dev/racer` route that runs the game offline, with a panel to edit handling and assist parameters live and export them as JSON.
- **Done when:** a parameter change takes effect without a reload.

### 3.5 Placeholder car and test track
- **Model:** Sonnet 5.5
- **What:** A simple original low-poly car and a test track that includes a banked turn, a jump, a hairpin and a section with no walls.
- **Done when:** both load in the dev route.

### 3.6 Handling tuning
- **Model:** Human playtesting; Opus 5.5 applies changes
- **What:** Repeated drive-and-adjust passes until the car feels like GTA Online's arcade handling under a top-down camera, without the twitchy spin-outs the original mode was criticised for.
- **Done when:** you and at least one friend agree it feels right, and the parameters are committed as the baseline car.

### 3.7 CI: rendering smoke test
- **Model:** Sonnet 5.5
- **CI**
- **What:** A Playwright test that opens the dev route in each engine, drives with scripted input for a few seconds, and checks that the car moved and no errors were logged.
- **Done when:** it runs in the e2e job in all three engines.
