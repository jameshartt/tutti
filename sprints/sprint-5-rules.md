# Sprint 5: rules

**Goal:** A complete match can be played from countdown to winner.

**Depends on:** sprint 4.

## Work items

### 5.1 Safe circle and elimination
- **Model:** Opus 5.5
- **What:** A circle centred on the front of the field. A car outside it gets a visible warning and a countdown; if it is still outside when the countdown ends, it explodes. The circle shrinks slowly over a long round. Falling off the track and being destroyed also eliminate. Radius, countdown and shrink rate are parameters.
- **Done when:** a car that brakes to a stop is eliminated, and a car that recovers inside the countdown is not.

### 5.2 Round and match state machine
- **Model:** Opus 5.5
- **What:** Countdown, racing, round end, restart, match end. The last car standing scores 1 point. If every remaining car dies together the round is a draw. The next round starts from the checkpoint behind where the last one ended. A sudden-death shrink starts after a time cap. The match ends at the chosen points target.
- **Done when:** scripted bot matches cover a normal win, a draw, a time-cap finish and a full match, all in tests.

### 5.3 Catch-up and leader handicap
- **Model:** Opus 5.5
- **What:**
  - Slipstream behind another car.
  - Top-speed bonus for trailing cars scaled by the gap, up to about 5%.
  - Top-speed penalty of about 2% for a leader who is well clear.
  - Reduced camera look-ahead for the leader.
  - A short grace period on the circle for the car in last place.
  - Grid order at each restart puts the match leader at the back.

  Every value is a parameter and each effect can be switched off.
- **Done when:** in bot matches with handicaps on, round wins are spread more evenly than with them off.

### 5.4 HUD
- **Model:** Sonnet 5.5
- **What:** Scores and points target, the circle and its warning, the pickup slot, countdown and round banners, and player name tags. It uses the existing design tokens and is readable on a phone.
- **Done when:** every match state has its screen, and nothing overlaps the touch controls.

### 5.5 Spectating
- **Model:** Sonnet 5.5
- **What:** An eliminated player keeps watching the shared camera until the round ends and can still talk.
- **Done when:** elimination never leaves a player on a frozen or empty screen.

### 5.6 Results and rematch
- **Model:** Sonnet 5.5
- **What:** An end-of-match screen with the final scores and buttons for rematch, change map and back to the room.
- **Done when:** a rematch starts without leaving the room.

### 5.7 CI: full bot match
- **Model:** Sonnet 5.5
- **CI**
- **What:** A headless four-bot match to completion in the server job, failing if the match does not end or a rule invariant is broken (for example, two winners in a round).
- **Done when:** the job runs on every pull request in under a minute.
