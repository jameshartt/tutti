# Quality evaluation

How the work in these sprints gets verified, with AI doing as much of it as possible. The few steps that need a person are in [MANUAL-STEPS.md](MANUAL-STEPS.md).

## Principles

1. **The author never marks its own work.** Every work item is checked by a separate agent session that has not seen the author's reasoning.
2. **Evidence, not claims.** A check passes only with something that was actually run: command output, a test report, a screenshot, a telemetry table.
3. **Turn judgement into numbers where possible.** "Feels right" is decided once by a person, recorded as telemetry targets, and guarded by AI from then on.
4. **A person is asked only for what AI cannot observe:** how the game feels in the hands, real hardware, whether art is acceptable, and approvals.

## The five verification layers

Every work item passes through the layers that apply to it, in order.

### Layer 1: deterministic gates (CI)

The checks already listed in each sprint file: gtest, Vitest, Playwright in three engines, the native-versus-WASM determinism comparison, golden state hashes, headless bot runs and the balance harness. If these fail, nothing else runs.

### Layer 2: independent code review

A fresh agent reviews the pull request diff for correctness bugs, with the reviewer at least as capable as the author:

| Author | Reviewer |
|---|---|
| Haiku 5.5 or Sonnet 5.5 | Opus 5.5 |
| Opus 5.5 | Fable 5.2 |
| Fable 5.2 | Fable 5.2 in a new session |

The reviewer gets the diff and the work item text, not the author's summary. Findings are posted on the pull request and block the merge until fixed or explicitly dismissed with a reason.

### Layer 3: acceptance verification

A second fresh agent takes the item's **Done when** line and proves or disproves each clause by running things itself. It writes a table of clause, evidence and verdict. It may not accept "the author says the tests pass"; it re-runs them.

Extra rules for sensitive areas:

- **Anything touching the transport or audio path:** re-run the mixer tests, and run a two-client voice session with a fake microphone while a four-bot match is running, checking that the mixer cadence statistics and audio packet loss are unchanged from `main`.
- **Anything in the sim core:** confirm the golden hashes changed only if the item says behaviour should change.
- **Anything parsing network input:** run the fuzz targets (see Q.3).

### Layer 4: agentic playtest

An agent plays the real game in real browsers and judges the result.

- **Driving:** Playwright launches the game in each engine and feeds input from the bot or a script.
- **Looking:** it captures screenshots and short frame sequences at set moments, and a vision-capable model answers specific yes/no questions about them. Examples:
  - Is the local car visible and inside the frame?
  - Does any HUD element overlap the touch controls?
  - Is the eliminated car's driver visible outside the car?
  - Is the safe circle drawn around the leading car?
- **Measuring:** the game writes telemetry, and the agent compares it with the targets below.

### Layer 5: sprint report

At the end of each sprint an agent writes `sprints/reports/sprint-N.md`:

- every work item with its verdict and a link to its evidence,
- anything that failed or was skipped, stated plainly,
- the manual steps still open, by ID.

Reading that report is the only review a person has to do per sprint.

## Telemetry targets

These replace repeated human playtesting. Values marked "baseline" are captured from the build a person approves in sprint 3 or sprint 8, then held within a tolerance.

| Area | Measure | Target |
|---|---|---|
| Handling | Time from rest to top speed, per car | Baseline ±5% |
| Handling | Spin-outs per lap by the bot at race pace | Baseline, and never more than 1 in 20 laps |
| Handling | Speed kept through the reference hairpin | Baseline ±5% |
| Handling | Rollovers per lap on walled tracks | Near zero |
| Netcode | Visible correction of the local car at 80 ms round trip, 2% loss | Below a set distance per second; set in sprint 4 |
| Netcode | Difference between two clients' view of a shunt | Within the tolerance set in sprint 4 |
| Netcode | Rollback time on the slowest target device | Within the sprint 1 budget |
| Rules | Rounds ending with every player eliminated by the circle in the first 10 seconds | Rare; the original mode's main complaint |
| Rules | Median round length in four-bot matches | In a band set in sprint 5 |
| Fairness | Round wins by grid slot over many bot matches | No slot wins far more than its share |
| Balance | Lap-time spread across cars, per track | About ±1.5% |
| Balance | Match win rate per car in mixed bot matches | No car far above its share |
| Audio | Mixer cadence and audio loss during a match | Unchanged from `main` |
| Performance | Frame time at the 95th percentile, per engine | Within the 60 fps budget |

## Quality work items

These build the verification machinery. Each is assigned to the sprint where it is first needed.

### Q.1 Verifier and reviewer agent definitions (sprint 0)
- **Model:** Opus 5.5
- **What:** Two agent definitions in `.claude/agents/`: a reviewer and an acceptance verifier, with the rules from layers 2 and 3 and a fixed output format. A short runbook for invoking them on a pull request.
- **Done when:** running the verifier on a deliberately incomplete item returns a fail with the missing clause named.

### Q.2 Review in CI (sprint 0)
- **Model:** Sonnet 5.5
- **What:** A workflow that runs the reviewer and verifier on each pull request and posts their tables as comments. Needs manual step M2.
- **Done when:** a pull request gets both comments without anyone asking.

### Q.3 Sanitizers and fuzzing (sprint 0, extended in sprint 4)
- **Model:** Opus 5.5
- **What:** CI builds of the server tests under address, undefined-behaviour and thread sanitizers. Fuzz targets for everything that parses bytes from the network: the datagram demux, reliable message parsing, and later the input and snapshot decoders.
- **Done when:** the sanitizer jobs are green and each fuzz target runs for a fixed time on every pull request.

### Q.4 Game telemetry and replay recording (sprint 2)
- **Model:** Opus 5.5
- **What:** The sim can record inputs to a replay file and play them back exactly. It emits the handling and rules measures in the table above as JSON.
- **Done when:** a replay reproduces the same state hashes, and the telemetry file validates against a schema.

### Q.5 Playtest agent (sprint 3)
- **Model:** Fable 5.2
- **What:** The layer 4 harness: launch, drive, capture, ask the vision questions, read telemetry, and write a pass/fail report with the screenshots attached. Questions live in a file per sprint so they are reviewed like code.
- **Done when:** it catches a deliberately broken build (car off-screen, HUD overlapping controls).

### Q.6 Baseline capture (sprint 3, repeated in sprint 8)
- **Model:** Sonnet 5.5
- **What:** A command that records the telemetry of the approved build as the baseline and commits it. Later changes are compared against it in CI.
- **Done when:** changing a handling parameter makes the comparison fail and names the measure that moved.

### Q.7 Network chaos scenarios (sprint 4)
- **Model:** Fable 5.2
- **What:** A fixed set of delay, jitter and loss profiles run automatically with two to four clients, producing the netcode measures in the table.
- **Done when:** the scenarios run nightly and their results appear in the sprint report.

### Q.8 Audio guard (sprint 4)
- **Model:** Opus 5.5
- **What:** The fake-microphone voice check from layer 3 as an automated test, comparing mixer statistics with and without a match running.
- **Done when:** it fails if the game thread is pinned to the mixer's core on purpose.

### Q.9 Device self-test page (sprint 1, extended each sprint)
- **Model:** Opus 5.5
- **What:** A `/dev/selftest` page that runs the checks that only real hardware can answer (determinism hash, rollback benchmark, transport connection, frame time) and shows one large PASS or FAIL with a short result code. It sends the full result to the server log so an agent can read it.
- **Done when:** a person's whole job on a device is "open the link, tap Start, read out the code".

### Q.10 Sprint report generator (sprint 0)
- **Model:** Sonnet 5.5
- **What:** A command that gathers the verifier tables, CI results and open manual steps into `sprints/reports/sprint-N.md`.
- **Done when:** the sprint 0 report is generated by it.

## What AI cannot verify

- Whether the handling is fun and feels like GTA Online. A person decides once; telemetry guards it afterwards.
- Behaviour on a real iPhone and over a real internet connection. The self-test page reduces this to a few taps.
- Whether the art and the name are good.
- Whether a match is fun with friends.
- Approvals to deploy and to change repository settings.
