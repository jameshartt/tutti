# Racer mini-game: sprint plan

A top-down, fully 3D elimination racer for 2–4 players that runs inside a Tutti room and reuses its transports, rooms and voice chat. It takes its mechanics from GTA Online's Tiny Racers mode but is an original game: its own name, cars, tracks, driver and art.

Working title: **Racer** (pick a real name before sprint 3).

## Decisions already made

- **Server-authoritative simulation in C++**, in a new `tutti-game` library, fixed 60 Hz step.
- **Jolt Physics** for vehicles, collisions and ragdolls. Fully 3D; only the camera is top-down.
- **The same sim core is compiled to WASM** (Emscripten) so clients predict with identical physics and roll back on each snapshot.
- **three.js on WebGL2** for rendering, in Chromium, Firefox and WebKit.
- **Game traffic rides the existing transports**: unreliable game datagrams plus `game_*` reliable messages. The audio wire format is not changed.
- **Ragdolls are cosmetic**: the server sends the bail event, each client simulates the tumble. Bailing mid-round eliminates you and leaves your car as a hazard.
- **Bots** fill empty seats and drive the balance harness.
- **CI/CD on GitHub Actions**, introduced in sprint 0 and extended as each sprint adds something to build or test.

## Game rules in brief

- Last car standing wins the round and scores 1 point. First to 3, 5 or 7 wins the match (host's choice, default 5).
- A safe circle surrounds the front of the field. Fall outside it and a timer starts; fail to get back in and you explode.
- Driving off the track, being destroyed, or bailing out also eliminates you.
- The next round restarts from the checkpoint behind where the last one ended.
- Seven pickups: rocket, bomb, machine gun, control invert, light-trail bike, ramp wedge, jump car.
- The back car gets slipstream, better pickup rolls, a small speed bonus and a grace period on the circle. The front car gets no draft, defensive pickups, a small speed penalty when well clear, and less camera look-ahead. The match leader starts at the back of the grid.

## Sprints

| Sprint | File | Outcome |
|---|---|---|
| 0 | [sprint-0-infrastructure.md](sprint-0-infrastructure.md) | Transport and room groundwork, test tooling, CI |
| 1 | [sprint-1-jolt-spike.md](sprint-1-jolt-spike.md) | Go/no-go on Jolt determinism and rollback cost |
| 2 | [sprint-2-sim-core.md](sprint-2-sim-core.md) | Headless game simulation with a bot |
| 3 | [sprint-3-vertical-slice.md](sprint-3-vertical-slice.md) | One car on one track, offline, feeling right |
| 4 | [sprint-4-netcode.md](sprint-4-netcode.md) | Multiplayer driving with prediction and rollback |
| 5 | [sprint-5-rules.md](sprint-5-rules.md) | Circle, elimination, rounds, scoring, HUD |
| 6 | [sprint-6-ragdoll.md](sprint-6-ragdoll.md) | Driver bail-outs |
| 7 | [sprint-7-pickups.md](sprint-7-pickups.md) | All seven pickups |
| 8 | [sprint-8-content-balance.md](sprint-8-content-balance.md) | Six cars, five tracks, balance, bots |
| 9 | [sprint-9-polish-release.md](sprint-9-polish-release.md) | Polish, cross-browser sign-off, automated deploy |

## Verification

- [QUALITY.md](QUALITY.md): how each work item is verified by AI (independent review, acceptance verification, agentic playtests, telemetry targets), and the work items that build that machinery.
- [MANUAL-STEPS.md](MANUAL-STEPS.md): the short list of things that need a person, with exact instructions.

## Order and dependencies

- Sprint 1 gates everything after it. If native and WASM disagree, or rollback is too slow on an iPhone, sprint 4 switches to the fallback described in that sprint's file.
- Sprint 3 depends only on sprint 2, so it can run alongside sprint 0.
- Sprints 6 and 7 are independent of each other and can swap.

## Which model does which work

| Model | ID | Used for |
|---|---|---|
| Fable 5.2 | `claude-fable-5-2` | Work where a subtle mistake is expensive: netcode, determinism, physics integration, anything touching the audio path |
| Opus 5.5 | `claude-opus-5-5` | Substantial but well-understood implementation |
| Sonnet 5.5 | `claude-sonnet-5-5` | Clearly specified UI, tooling, tests and CI |
| Haiku 5.5 | `claude-haiku-5-5` | Mechanical work: data files, boilerplate, docs |
| Human | | Handling feel, balance sign-off, real-device checks, secrets |

## Work item format

Each item has an ID (`sprint.number`), a model, what to build, and how to tell it is done. Items marked **CI** change the pipeline.

## Definition of done for every item

- CI is green on the pull request.
- New behaviour has a test at the level that makes sense (gtest, Vitest or Playwright).
- Existing audio behaviour is unchanged: the mixer tests pass and a two-person voice session still works.
- `shared/protocol.md` is updated if the wire format changed.
