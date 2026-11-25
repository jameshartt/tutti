#include "c_api.h"
#include "spike_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using tutti::game::SpikeWorld;

namespace {

std::vector<uint64_t> run_script(SpikeWorld& world, int ticks) {
    std::vector<uint64_t> hashes;
    hashes.reserve(static_cast<size_t>(ticks));
    for (int i = 0; i < ticks; ++i) {
        world.step_scripted();
        hashes.push_back(world.state_hash());
    }
    return hashes;
}

}  // namespace

TEST(SpikeWorldTest, TwoRunsGiveIdenticalHashesEveryTick) {
    SpikeWorld a;
    SpikeWorld b;
    const auto ha = run_script(a, 1200);
    const auto hb = run_script(b, 1200);
    for (size_t i = 0; i < ha.size(); ++i) ASSERT_EQ(ha[i], hb[i]) << "first difference at tick " << i + 1;
}

TEST(SpikeWorldTest, StateActuallyChanges) {
    SpikeWorld world;
    const auto hashes = run_script(world, 120);
    for (size_t i = 1; i < hashes.size(); ++i) EXPECT_NE(hashes[i], hashes[i - 1]) << "tick " << i + 1;
}

TEST(SpikeWorldTest, ScriptExercisesCollisionsJumpAndRollover) {
    SpikeWorld world;
    run_script(world, 3600);
    const uint32_t events = world.events();
    EXPECT_TRUE(events & tutti::game::kEventCarHitCar);
    EXPECT_TRUE(events & tutti::game::kEventCarHitLoose);
    EXPECT_TRUE(events & tutti::game::kEventJump);
    EXPECT_TRUE(events & tutti::game::kEventRollover);
}

TEST(SpikeWorldTest, InputsChangeTheOutcome) {
    SpikeWorld a;
    SpikeWorld b;
    tutti::game::CarInput throttle;
    throttle.forward = 1.0f;
    for (int car = 0; car < SpikeWorld::kCars; ++car) a.set_input(car, throttle);
    for (int i = 0; i < 60; ++i) {
        a.step();
        b.step();
    }
    EXPECT_NE(a.state_hash(), b.state_hash());
}

// The property rollback depends on: restoring a snapshot and replaying the
// same inputs lands on exactly the same states, at any point in the run.
TEST(SpikeWorldTest, RestoreAndReplayIsBitIdentical) {
    for (int start : {1, 130, 240, 300, 700, 1500}) {
        SpikeWorld world;
        run_script(world, start);
        std::vector<uint8_t> snapshot;
        world.save_state(snapshot);
        const uint64_t hash_at_save = world.state_hash();

        const auto first = run_script(world, 120);
        ASSERT_TRUE(world.restore_state(snapshot.data(), snapshot.size()));
        EXPECT_EQ(world.tick(), static_cast<uint32_t>(start));
        EXPECT_EQ(world.state_hash(), hash_at_save);
        const auto second = run_script(world, 120);
        for (size_t i = 0; i < first.size(); ++i)
            ASSERT_EQ(first[i], second[i]) << "snapshot at tick " << start << ", replay tick " << i + 1;
    }
}

TEST(SpikeWorldTest, SnapshotRestoresIntoAFreshWorld) {
    SpikeWorld source;
    run_script(source, 400);
    std::vector<uint8_t> snapshot;
    source.save_state(snapshot);
    const auto expected = run_script(source, 200);

    SpikeWorld target;
    ASSERT_TRUE(target.restore_state(snapshot.data(), snapshot.size()));
    const auto actual = run_script(target, 200);
    for (size_t i = 0; i < expected.size(); ++i) ASSERT_EQ(expected[i], actual[i]) << "tick " << i + 1;
}

TEST(SpikeWorldTest, TruncatedSnapshotIsRejected) {
    SpikeWorld world;
    run_script(world, 10);
    std::vector<uint8_t> snapshot;
    world.save_state(snapshot);
    EXPECT_FALSE(world.restore_state(snapshot.data(), snapshot.size() / 2));
}

TEST(SpikeWorldTest, CApiMatchesTheClass) {
    SpikeWorld reference;
    const auto expected = run_script(reference, 300);

    tg_world* world = tg_create();
    std::vector<uint32_t> hashes(600);
    tg_run_script(world, 300, hashes.data());
    for (int i = 0; i < 300; ++i) {
        const uint64_t hash = (static_cast<uint64_t>(hashes[2 * i + 1]) << 32) | hashes[2 * i];
        ASSERT_EQ(hash, expected[static_cast<size_t>(i)]) << "tick " << i + 1;
    }
    EXPECT_EQ(tg_tick(world), 300u);

    // A rollback benchmark leaves the world where it started
    uint32_t before[2];
    uint32_t after[2];
    tg_state_hash(world, before);
    tg_bench_rollback(world, 10, 3);
    tg_state_hash(world, after);
    EXPECT_EQ(before[0], after[0]);
    EXPECT_EQ(before[1], after[1]);

    std::vector<float> state(static_cast<size_t>(tg_state_floats()));
    tg_read_state(world, state.data());
    EXPECT_NE(state[2], 0.0f);  // car 0 has moved along Z
    tg_destroy(world);
}
