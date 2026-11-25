#pragma once

#include <stdint.h>

// C interface to the simulation, the surface the WebAssembly module exports.
// Native tests call it too, so both builds exercise the same entry points.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tg_world tg_world;

tg_world* tg_create(void);
void tg_destroy(tg_world* world);

void tg_set_input(tg_world* world, int car, float forward, float right, float brake, float handbrake);
// Advance `ticks` ticks with the current inputs.
void tg_step(tg_world* world, int ticks);
// Advance `ticks` ticks with the built-in script. If `out_hashes` is not
// null it receives two uint32 per tick (low word, then high word) of the
// state hash after each tick.
void tg_run_script(tg_world* world, int ticks, uint32_t* out_hashes);

uint32_t tg_tick(const tg_world* world);
uint32_t tg_events(const tg_world* world);
// out[0] = low 32 bits, out[1] = high 32 bits.
void tg_state_hash(const tg_world* world, uint32_t* out);

// Snapshot into the world's own buffer; returns its size in bytes.
int tg_save_state(tg_world* world);
// Pointer to the last snapshot, valid until the next tg_save_state.
const uint8_t* tg_snapshot_data(const tg_world* world);
// Restore the last snapshot. Returns 1 on success.
int tg_restore_state(tg_world* world);
// Restore from caller-owned bytes. Returns 1 on success.
int tg_restore_from(tg_world* world, const uint8_t* data, int size);

// Number of floats tg_read_state writes.
int tg_state_floats(void);
void tg_read_state(const tg_world* world, float* out);

// Benchmark helpers: each repeats one operation so the caller can time the
// whole call with a coarse clock. All leave the world at the snapshot taken
// when the call started.
void tg_bench_save(tg_world* world, int iterations);
void tg_bench_restore(tg_world* world, int iterations);
// One rollback = restore, then re-simulate `resim_ticks` scripted ticks,
// saving a snapshot after each as a client keeping a history would.
void tg_bench_rollback(tg_world* world, int resim_ticks, int iterations);
// Plain stepping from the snapshot, for the cost of a tick on its own.
void tg_bench_step(tg_world* world, int ticks, int iterations);

#ifdef __cplusplus
}
#endif
