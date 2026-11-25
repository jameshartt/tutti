#include "c_api.h"

#include "spike_world.h"

#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define TG_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define TG_EXPORT
#endif

using tutti::game::CarInput;
using tutti::game::SpikeWorld;

struct tg_world {
    SpikeWorld world;
    std::vector<uint8_t> snapshot;
    std::vector<uint8_t> scratch;
};

extern "C" {

TG_EXPORT tg_world* tg_create(void) { return new tg_world(); }

TG_EXPORT void tg_destroy(tg_world* world) { delete world; }

TG_EXPORT void tg_set_input(tg_world* world, int car, float forward, float right, float brake, float handbrake) {
    CarInput in;
    in.forward = forward;
    in.right = right;
    in.brake = brake;
    in.handbrake = handbrake;
    world->world.set_input(car, in);
}

TG_EXPORT void tg_step(tg_world* world, int ticks) {
    for (int i = 0; i < ticks; ++i) world->world.step();
}

TG_EXPORT void tg_run_script(tg_world* world, int ticks, uint32_t* out_hashes) {
    for (int i = 0; i < ticks; ++i) {
        world->world.step_scripted();
        if (out_hashes) {
            const uint64_t hash = world->world.state_hash();
            out_hashes[2 * i] = static_cast<uint32_t>(hash);
            out_hashes[2 * i + 1] = static_cast<uint32_t>(hash >> 32);
        }
    }
}

TG_EXPORT uint32_t tg_tick(const tg_world* world) { return world->world.tick(); }

TG_EXPORT uint32_t tg_events(const tg_world* world) { return world->world.events(); }

TG_EXPORT void tg_state_hash(const tg_world* world, uint32_t* out) {
    const uint64_t hash = world->world.state_hash();
    out[0] = static_cast<uint32_t>(hash);
    out[1] = static_cast<uint32_t>(hash >> 32);
}

TG_EXPORT int tg_save_state(tg_world* world) {
    world->world.save_state(world->snapshot);
    return static_cast<int>(world->snapshot.size());
}

TG_EXPORT const uint8_t* tg_snapshot_data(const tg_world* world) { return world->snapshot.data(); }

TG_EXPORT int tg_restore_state(tg_world* world) {
    return world->world.restore_state(world->snapshot.data(), world->snapshot.size()) ? 1 : 0;
}

TG_EXPORT int tg_restore_from(tg_world* world, const uint8_t* data, int size) {
    if (size < 0) return 0;
    return world->world.restore_state(data, static_cast<size_t>(size)) ? 1 : 0;
}

TG_EXPORT int tg_state_floats(void) { return SpikeWorld::kBodies * SpikeWorld::kFloatsPerBody; }

TG_EXPORT void tg_read_state(const tg_world* world, float* out) { world->world.read_state(out); }

TG_EXPORT void tg_bench_save(tg_world* world, int iterations) {
    world->world.save_state(world->snapshot);
    for (int i = 0; i < iterations; ++i) world->world.save_state(world->scratch);
}

TG_EXPORT void tg_bench_restore(tg_world* world, int iterations) {
    world->world.save_state(world->snapshot);
    for (int i = 0; i < iterations; ++i) world->world.restore_state(world->snapshot.data(), world->snapshot.size());
}

TG_EXPORT void tg_bench_rollback(tg_world* world, int resim_ticks, int iterations) {
    world->world.save_state(world->snapshot);
    for (int i = 0; i < iterations; ++i) {
        world->world.restore_state(world->snapshot.data(), world->snapshot.size());
        for (int t = 0; t < resim_ticks; ++t) {
            world->world.step_scripted();
            world->world.save_state(world->scratch);
        }
    }
    world->world.restore_state(world->snapshot.data(), world->snapshot.size());
}

TG_EXPORT void tg_bench_step(tg_world* world, int ticks, int iterations) {
    world->world.save_state(world->snapshot);
    for (int i = 0; i < iterations; ++i) {
        world->world.restore_state(world->snapshot.data(), world->snapshot.size());
        for (int t = 0; t < ticks; ++t) world->world.step_scripted();
    }
    world->world.restore_state(world->snapshot.data(), world->snapshot.size());
}

}  // extern "C"
