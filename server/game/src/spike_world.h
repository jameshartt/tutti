#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Sprint 1 spike: a small Jolt scene used to measure whether client-side
// rollback is viable. Four wheeled cars, ten loose boxes, a ground mesh with
// a jump ramp and a one-sided wedge, inside four walls.
//
// Nothing here may depend on the platform's maths library or on wall-clock
// time: the same inputs must give bit-identical state natively and in WASM.

namespace tutti::game {

struct CarInput {
    float forward = 0.0f;    // -1 (reverse) .. 1
    float right = 0.0f;      // -1 (left) .. 1
    float brake = 0.0f;      // 0 .. 1
    float handbrake = 0.0f;  // 0 .. 1
};

// Things the scripted run is supposed to exercise; set once they happen.
enum SpikeEvent : uint32_t {
    kEventCarHitCar = 1u << 0,
    kEventCarHitLoose = 1u << 1,
    kEventJump = 1u << 2,      // a car had all four wheels off the ground for 0.25 s
    kEventRollover = 1u << 3,  // a car's roof pointed below the horizon
    kEventCarHitWall = 1u << 4,
};

class SpikeWorld {
public:
    static constexpr int kCars = 4;
    static constexpr int kLooseBodies = 10;
    static constexpr int kBodies = kCars + kLooseBodies;
    static constexpr int kTickRate = 60;
    // Floats per body from read_state(): position 3, rotation 4,
    // linear velocity 3, angular velocity 3.
    static constexpr int kFloatsPerBody = 13;

    SpikeWorld();
    ~SpikeWorld();
    SpikeWorld(const SpikeWorld&) = delete;
    SpikeWorld& operator=(const SpikeWorld&) = delete;

    // The input the built-in script gives `car` at `tick`. Integer maths and
    // table lookups only.
    static CarInput scripted_input(uint32_t tick, int car);

    void set_input(int car, const CarInput& input);

    // Advance one tick (1/60 s) with the current inputs.
    void step();
    // Apply the script's inputs for the current tick, then step.
    void step_scripted();

    uint32_t tick() const;
    uint32_t events() const;

    // 64-bit hash of everything that defines the simulation's future:
    // every body's transform and velocities, every wheel, engine and gearbox.
    uint64_t state_hash() const;

    // Full snapshot, enough to continue bit-identically after restore_state.
    void save_state(std::vector<uint8_t>& out) const;
    bool restore_state(const uint8_t* data, size_t size);

    // kBodies * kFloatsPerBody floats: cars first, then loose bodies.
    void read_state(float* out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tutti::game
