# Sprint 8: content and balance

**Goal:** Six cars that drive differently but are evenly matched, five tracks, and bots that can fill a match.

**Depends on:** sprints 5 and 7.

## Work items

### 8.1 Six car archetypes
- **Model:** Opus 5.5
- **What:** Handling parameter sets built from one stat budget across top speed, acceleration, grip, mass and stability:
  - **All-rounder:** the baseline from sprint 3.
  - **Grip:** corners hard, lower top speed.
  - **Drift:** slides easily and holds it, slower out of corners.
  - **Bruiser:** heavy, wins shunts, slow to accelerate.
  - **Sprinter:** light and quick off the line, easily pushed around.
  - **Off-roader:** soft suspension, best over jumps and rough sections.
- **Done when:** each is recognisably different to drive within one lap.

### 8.2 Balance harness
- **Model:** Opus 5.5
- **What:** A headless tool that runs every car on every track with the bot and reports lap times, and runs mixed-car bot matches to report win rates. It outputs a table and fails when the spread exceeds a threshold.
- **Done when:** lap times are within about ±1.5% across cars on each track, and no car wins more than its share by a wide margin.

### 8.3 Five tracks
- **Model:** Opus 5.5 for layouts; Haiku 5.5 for prop and pickup placement
- **What:** Original floating stunt tracks, each favouring something different:
  - **Technical:** tight corners, walls.
  - **High speed:** long banked sweepers.
  - **Jumps:** gaps and landings.
  - **Narrow:** no walls, punishing mistakes.
  - **Crossover:** a figure-eight with a loop or tube.
- **Done when:** the bot completes each one, and each has checkpoints, grid slots and pickup points.

### 8.4 Track and car visuals
- **Model:** Sonnet 5.5
- **What:** Original low-poly models for the six cars and a distinct theme and palette for each track. All assets are bundled and served same-origin.
- **Done when:** total download for a match is small enough to start quickly on a phone connection; set the budget once the first track is built.

### 8.5 Bots in real matches
- **Model:** Opus 5.5
- **What:** Bots as seat-fillers: they use pickups, avoid the edge, react to the circle, and come in a few difficulty levels. The host chooses how many.
- **Done when:** a solo player can have a competitive match against three bots.

### 8.6 Balance sign-off
- **Model:** Human
- **What:** Play sessions with friends across all cars and tracks, feeding adjustments back into 8.1 and the handicap values from sprint 5.
- **Done when:** nobody insists on one car.

### 8.7 CI: balance report and asset checks
- **Model:** Sonnet 5.5
- **CI**
- **What:** Run the balance harness on a schedule and when car or track files change, posting the table to the pull request. Validate every track and car file against its schema and enforce the asset size budget.
- **Done when:** a pull request that makes one car faster shows the change in its report.
