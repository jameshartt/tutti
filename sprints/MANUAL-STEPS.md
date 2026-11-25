# Manual steps

Everything that needs you, in the order it comes up. Each step says when, how long, what to do and what to send back.

Where a step is only an approval, Claude can do the work once you say yes.

## Every sprint

### M0 Read the sprint report
- **When:** the end of each sprint.
- **Time:** 5 minutes.
- **Do:**
  1. Open `sprints/reports/sprint-N.md`.
  2. Look at the "failed or skipped" section.
  3. Reply "approved" or name the item you want reworked.

## Sprint 0

### M1 Approve branch protection (item 0.11) — done
- `main` requires the seven build and test checks to pass before a pull request can merge. Force pushes and deletion are blocked. The rules live in two repository rulesets, `main-1` and `main-2`.
- As the repository owner you can still bypass this. Say "enforce for admins too" to remove that.

### M2 Connect Claude to the repository for automated review (item Q.2) — done
- The review runs on your Claude subscription through the `CLAUDE_CODE_OAUTH_TOKEN` repository secret.
- **If reviews stop working** (the token was revoked or expired):
  1. In a terminal on a machine with Claude Code, run `claude setup-token` and sign in.
  2. On GitHub, open the `tutti` repository → **Settings** → **Secrets and variables** → **Actions**, and update `CLAUDE_CODE_OAUTH_TOKEN` with the new value.
- **To change the review model:** add a repository variable `RACER_REVIEW_MODEL` under the same page's **Variables** tab, for example `claude-opus-5-5`.
- A Console API key in `ANTHROPIC_API_KEY` is supported by the workflow but was refused for billing reasons on this account; that secret can be deleted.

### M2b Let WebKit tests run on this machine (done, no action needed)
- Playwright's WebKit needs three system libraries. They are unpacked without root into `~/.cache/tutti-webkit-libs`.
- To run WebKit tests locally, set `LD_LIBRARY_PATH=$HOME/.cache/tutti-webkit-libs/usr/lib/x86_64-linux-gnu` before `npx playwright test --project=webkit`.

## Sprint 1

### M3 iPhone physics test (item 1.4)
- **When:** after the sprint 1 pull request is merged and deployed.
- **Time:** 2 minutes.
- **Do:**
  1. On your iPhone, open Safari and go to `https://tutti.rocks/dev/physics`.
  2. Tap **Start** and wait about ten seconds for PASS or FAIL.
  3. Tell Claude it is done. The result is logged on the server, so there is nothing to copy; the code under the verdict (for example `PASS-SAME-1.20ms`) is there if you want to read it out.
- **Check:** `SAME` means the phone computed exactly what the server does; the time is the cost of one rollback, which must be under 4 ms.

### M4 Go or no-go on rollback (item 1.5)
- **Time:** 5 minutes.
- **Do:**
  1. Read `sprints/sprint-1-result.md`. It ends with a recommendation.
  2. Reply "full rollback" or "fallback".

## Sprint 3

### M5 Pick the game's name
- **Time:** as long as you like.
- **Do:** Send Claude the name. It replaces the working title "Racer".

### M6 Handling sessions (item 3.6)
- **Time:** 20 minutes each; expect three or four.
- **Do:**
  1. Open the `/dev/racer` link with a gamepad or keyboard.
  2. Drive five laps.
  3. Tell Claude what felt wrong in plain words, for example "too slidey out of slow corners" or "feels light over the jump".
  4. Reload when Claude says the change is in, and repeat.
  5. When it feels right, reply "lock handling".
- **After this:** AI holds the handling to what you approved. You are only asked again if you want it changed.

## Sprint 4

### M7 First multiplayer session
- **Time:** 15 minutes, with one friend.
- **Do:**
  1. Both join the same room and start a match.
  2. Drive side by side and bump each other a few times.
  3. Tell Claude whether the bumps looked the same to both of you, and whether voice stayed clear.

## Sprint 6

### M8 Approve the driver model (item 6.5)
- **Time:** 2 minutes.
- **Do:** Look at the screenshots Claude sends and reply "approved" or say what to change.

## Sprint 8

### M9 Approve car and track visuals (item 8.4)
- **Time:** 5 minutes.
- **Do:** Look at the screenshots Claude sends and reply "approved" or say what to change.

### M10 Balance session with friends (item 8.6)
- **Time:** 45 minutes, with two or three friends.
- **Do:**
  1. Play one match on each of the five tracks.
  2. Everyone switches car between matches.
  3. Afterwards, tell Claude which car or track felt unfair and why, in a sentence each.
  4. When nobody insists on one car, reply "lock balance".

## Sprint 9

### M11 Approve deploy access (item 9.5)
- **Time:** 2 minutes.
- **Do:** Reply "yes, set up deploy". Claude creates a deploy key, installs it on the server, stores it as a repository secret and creates the approval-gated environment.
- **Check:** the repository's Actions tab shows a deploy run waiting for your approval after the next merge.

### M12 Approve each production deploy
- **Time:** 1 minute.
- **Do:**
  1. Open the waiting run from the notification GitHub sends you.
  2. Click **Review deployments**, tick the environment, click **Approve and deploy**.
- **Check:** the run finishes green; it health-checks the site itself.

### M13 Real-device sign-off (item 9.9)
- **Time:** 20 minutes, with two friends.
- **Do:**
  1. One of you joins on an iPhone in Safari, one on Firefox on a computer, one on Chrome or Edge.
  2. Each opens the self-test link first, taps **Start**, and sends the result code.
  3. Play one full match with voice on.
  4. Tell Claude whether anyone's audio broke up or anyone's game stuttered.
