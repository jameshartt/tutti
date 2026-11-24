# Sprint 0 report

**Status:** pull request #1 open; all five CI checks green on the first run.
**Branch:** `racer/sprint-0`.

## Work items

| Item | Status | Evidence |
|---|---|---|
| 0.1 Buffer partial lines in the WebTransport stream | Done | `LineFramerTest.*` (6 tests): split message delivered once; 16 KB message byte by byte; oversized line dropped and framing recovers. Exercised for real by the WebTransport end-to-end test. |
| 0.2 Game datagram channel | Done | `GameDatagramTest.*`, `GameRoutingTest.GameDatagramsNeverReachTheAudioPath`, `AudioDatagramsNeverReachTheGameSession`; client `game-datagram.test.ts`, `game-link.test.ts`; end to end on both transports in Chromium and Firefox, including the largest allowed datagram (1100 bytes). |
| 0.3 Game datagrams count as activity | Done | `GameReaperTest.GameDatagramsKeepSilentParticipantsAlive` and `FullySilentParticipantsAreStillReaped`. |
| 0.4 `game_*` routing and empty `GameSession` | Done | `GameRoutingTest.GamePingIsBroadcastToEveryParticipantWithSender`, `UnknownGameTypeIsIgnored`; end to end. |
| 0.5 Server time in `pong`, client clock offset | Done | `GameRoutingTest.PongCarriesServerTime`; `server-clock.test.ts` (5 tests); end-to-end "clock" step. |
| 0.6 Client test tooling | Done | Vitest: 16 tests pass. Playwright: Chromium, Firefox and WebKit pass. |
| 0.7 Server tests for binder and demux | Done | 24 new tests in `game_routing_test.cpp` and `transport_framing_test.cpp`. |
| 0.8 Protocol document | Done | `shared/protocol.md` now describes game datagrams, the `game` channel, `game_*` messages and `server_t`; unimplemented `session_token` and `server_timing` removed. |
| 0.9 CI pull-request pipeline | Done, except the deliberate-failure check | `.github/workflows/ci.yml`: server, sanitizers, client and end to end in Chromium, Firefox and WebKit all green on pull request #1. A deliberately broken test has not been tried. |
| 0.10 CI container build | Done | `images` job green in 9m38s on a cold cache, including the WebTransport end-to-end test. |
| 0.11 Branch protection | Open | Manual step M1, after the first CI run. |
| Q.1 Reviewer and verifier agents | Done | `.claude/agents/racer-reviewer.md`, `racer-verifier.md`. The reviewer was used on this sprint. |
| Q.2 Review in CI | Done | `.github/workflows/review.yml` posts a review and an acceptance check on each pull request. It uses a Claude subscription token (`CLAUDE_CODE_OAUTH_TOKEN`); API keys were refused for billing reasons that were never explained. |
| Q.3 Sanitizers and fuzzing | Half done | ASan + UBSan build option and CI job; all 39 tests pass under them. Fuzz targets and a thread-sanitizer job are not written yet. |
| Q.9 Device self-test page | First version done | `/dev/selftest`; used by the end-to-end tests. |
| Q.10 Sprint report generator | Not started | This report was written by hand. |

## Test results (local)

| Suite | Result |
|---|---|
| Server unit tests | 49 of 49 pass (15 pre-existing audio tests + 34 new) |
| Server unit tests under ASan + UBSan | pass in CI, no reports |
| Client type-check | 0 errors |
| Client unit tests | 24 of 24 pass |
| Client production build | succeeds |
| End to end, WebRTC, native server | Chromium pass, Firefox pass, WebKit pass |
| End to end, WebTransport and WebRTC, Docker server | Chromium pass, Firefox pass |

## Independent review

A fresh agent reviewed the changes without the author's reasoning. Verdict: fail, on two findings; no defects found in the audio path, server transport code or wire formats.

| Finding | Action |
|---|---|
| Self-test paired pongs with the wrong pings, giving a wrong clock offset | Fixed: pongs matched by id, each message consumed once, clock step sanity-checked. |
| `connect()` on WebRTC does not wait for the game channel, contrary to the work item text | Kept by design so audio joins are never delayed; work item text updated. |
| A client could flood `game_ping` and hold locks the audio path needs | Fixed: 16 KB cap on game messages, 64 KB on all control messages; parsing moved outside the binder lock; broadcasts send outside the room lock. No rate limit yet. |
| Self-test joined a fixed room and could disturb a rehearsal | Fixed: it picks an empty room unless one is named. |
| WebTransport server code had no automated coverage | Covered by the Docker end-to-end run and the CI `images` job. |
| Pre-existing: valid JSON of the wrong shape (for example `{"type":5}`) threw through a C callback and could kill the server | Fixed, with a test of 14 malformed inputs. |

The fixes were then reviewed again by the CI reviewer and acceptance check on pull request #1. Their findings and what was done:

| Finding (CI review) | Action |
|---|---|
| Unknown `game_*` types were logged unescaped and unthrottled, so a client could forge or flood log lines | Fixed: client text is sanitised, and only the first 10 and then every 1000th are logged. Tested. |
| No rate limit on `game_*` messages | Fixed: 60 per second sustained, burst of 120, per session. Tested. |
| The "never reaches the audio path" tests bypassed the real demux | Fixed: the server demux is now one shared function with its own tests, and a client test drives the real WebTransport read loops with a fake connection. |
| `npm run test` failed on a fresh checkout | Fixed: the script runs `svelte-kit sync` first. |
| A `game` channel opening after the signaling socket closed was dropped | Fixed by reading; not covered by a test. |
| Protocol document omitted the new size limits, the `bind` message and the server's timestamp behaviour; wrong `ws_url` example | Fixed. |
| Clock estimate accuracy was only checked loosely | The end-to-end tests now require better than ±25 ms against a local server. |
| A partial line left over when a client opens a second control stream corrupted the next message | Fixed: the framer resets on a new stream. Tested at the framer level. |
| Nothing tested the real ping → pong → server clock wiring | Fixed: new test drives `RTTMonitor` and checks the exported clock, including the sign of the offset. |
| The clock estimate steps when its best sample leaves the 16-sample window | Not fixed. Nothing uses the clock yet; to be settled in sprint 4 when tick sync is built. |
| 16 KB message over a real transport, a deliberately broken test in CI (0.9), verifier run on an incomplete item (Q.1) | Not done. |

## Beyond the plan

- Fixed 17 type-check errors that already existed in the client, with type-only changes, so CI can require a clean check.
- `ws_signaling` would have created a duplicate session when a third DataChannel opened; it now fires "ready" once.
- Dev and proxy ports are configurable by environment variable, because port 8080 on this machine is taken.

## Not done or not verified

- WebKit is covered by Playwright's WebKit build on Linux only; real Safari on an iPhone is manual step M3.
- No two-person voice session was run with the new build; audio is covered only by the 15 existing tests and the demux tests.
- Fuzz targets, thread sanitizer and the report generator.
- Work items assigned to Opus, Sonnet and Haiku in the plan were mostly done by Fable in one session, except 0.6 (Sonnet). The review was done by a separate Fable session.

## Open manual steps

- **M1** Approve branch protection (after the first green CI run).
