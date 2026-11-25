# Sprint 1 report

**Status:** in review. One measurement (iPhone) and the decision are waiting on manual steps M3 and M4.
**Branch:** `racer/sprint-1-jolt`.
**Result note:** [`../sprint-1-result.md`](../sprint-1-result.md).

## Work items

| Item | Status | Evidence |
|---|---|---|
| 1.1 Jolt in the native build | Done | `server/game/`, library target `tutti-game`. `tutti-game-spike` steps 60 s of game time and prints state hashes. Jolt 5.7.0 through FetchContent with the cross-platform deterministic option. |
| 1.2 Emscripten build | Done | A stage in `client/Dockerfile` builds the module; `scripts/build-game-wasm.sh` copies it to `client/static/game/`. The test page `/dev/physics` loads it in Chromium, Firefox and WebKit. |
| 1.3 Determinism test | Done | `e2e/physics-determinism.spec.ts`: all 3600 per-tick hashes equal the native build's in all three engines. Covers collisions, a jump and a rollover (asserted). |
| 1.4 Rollback cost benchmark | Desktop done, iPhone pending (M3) | 1.3 to 1.8 ms per rollback in desktop browsers against a 4 ms budget. |
| 1.5 Decision | Written up, decision pending (M4) | `sprints/sprint-1-result.md` recommends full rollback. |
| 1.6 CI: WASM build and determinism check | Done, pending first CI run | New `Game physics` job: native build and tests, WebAssembly build, native-against-Node comparison, artifact for the end-to-end job, which compares each browser against native. Locally, changing gravity in the WebAssembly build alone made all three browsers fail at tick 1. |

## Test results (local)

| Suite | Result |
|---|---|
| `tutti-game` unit tests | 8 of 8 pass; also clean under ASan + UBSan |
| Native against WebAssembly in Node | 3600 of 3600 ticks identical |
| Physics determinism, end to end | Chromium, Firefox, WebKit pass |
| Server unit tests | 51 of 51 pass |
| Client type-check, unit tests, build | 0 errors; 27 of 27 pass; builds |
| Other end-to-end tests | pass in all three browsers |
| Production client image (`docker compose build client`) | builds, contains the module |

## Also in this pull request

- Removed the weak "fuzzer reached the handlers" check from CI that the previous review flagged as able to fail by chance; the checked-in seed inputs already guarantee it.
- `main` now requires seven checks (thread sanitizer and fuzzing added). Documents updated.

## Not done or not verified

- iPhone numbers and the ARM determinism check (M3).
- The server does not link `tutti-game` yet (`TUTTI_BUILD_GAME` is off by default); that starts in sprint 2.
- The `Game physics` job is not yet a required check.
- A deploy now compiles the WebAssembly module on the server the first time, which adds a few minutes to that deploy.
- All items were done by Fable in one session, not the mix of models in the plan.
