# Sprint 9: polish and release

**Goal:** The game is live on the production site, works in Chromium, Firefox and Safari, and deploys automatically.

**Depends on:** all earlier sprints.

## Work items

### 9.1 Sound
- **Model:** Opus 5.5
- **What:** Engine, tyres, impacts, explosions and pickup sounds played through the existing audio context, mixed under the voice chat and kept off the capture path so they are not sent to other players. A separate volume control.
- **Done when:** voice latency and quality are unchanged with game sound playing.

### 9.2 Performance pass
- **Model:** Fable 5.2
- **What:** Profile the sim, rollback and renderer on an iPhone and a low-end laptop. Reduce draw calls, cap rollback depth, and add a quality setting that lowers effects and resolution.
- **Done when:** a four-player match with pickups holds the target frame rate on the iPhone.

### 9.3 Controls polish
- **Model:** Sonnet 5.5
- **What:** Touch control layout and sizing, gamepad remapping, key rebinding, and a controls screen.
- **Done when:** settings persist between sessions.

### 9.4 Full cross-browser matrix
- **Model:** Sonnet 5.5
- **CI**
- **What:** Extend the multiplayer Playwright test to every pairing of the three engines and to a four-player match.
- **Done when:** the full matrix runs nightly and the fast subset runs on pull requests.

### 9.5 CD: automated deploy
- **Model:** Sonnet 5.5; a human adds the secrets
- **CI**
- **What:** A deploy workflow that runs after CI passes on `main`, behind a GitHub environment that requires manual approval:
  1. Connect to the server over SSH with a deploy key stored as a repository secret.
  2. Pull the commit and run `deploy.sh`.
  3. Check `/api/health` on the public site and fail the workflow if it is not healthy.

  Also allow a manual run for a chosen commit, which serves as the rollback path.
- **Done when:** approving a run deploys the site with no commands typed by hand, and re-running an older commit rolls back.

### 9.6 CD: build images in CI
- **Model:** Sonnet 5.5
- **CI**
- **What:** Build the server and client images in CI, push them to the GitHub container registry tagged with the commit, and have the server pull them instead of building on the VPS. This shortens deploys and means the image that passed CI is the one that runs.
- **Done when:** a deploy no longer compiles anything on the server.

### 9.7 Deployment configuration
- **Model:** Sonnet 5.5
- **What:** Any changes the game needs in the Dockerfiles, compose file, Caddyfile and `deploy.sh`, including serving the WASM with the right content type and long-lived caching for hashed assets.
- **Done when:** a fresh deploy to a clean server works.

### 9.8 Documentation
- **Model:** Haiku 5.5
- **What:** Update the README and `shared/protocol.md`, and add a short page on how to add a track or a car.
- **Done when:** a new contributor could add a track from the docs alone.

### 9.9 Real-device sign-off
- **Model:** Human
- **What:** A full match over the internet with a real iPhone on Safari, a Firefox desktop and a Chromium desktop, with voice on.
- **Done when:** the match completes and nobody's audio breaks up.
