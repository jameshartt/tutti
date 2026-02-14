#include <gtest/gtest.h>

#include "audio/mixer.h"
#include "transport/transport_interface.h"

namespace tutti {
namespace {

AudioFrame make_frame(int16_t value, uint32_t seq = 0) {
    AudioFrame frame;
    frame.sequence = seq;
    frame.timestamp = seq * kSamplesPerFrame;
    frame.samples.fill(value);
    return frame;
}

TEST(MixerTest, EmptyMixProducesNothing) {
    Mixer mixer(4);
    mixer.mix_cycle();
    // No crash, no output
}

TEST(MixerTest, SingleParticipantNoOutput) {
    // A single participant should receive no output (no one else to mix)
    Mixer mixer(4);
    mixer.add_participant("alice");

    auto frame = make_frame(1000);
    EXPECT_TRUE(mixer.push_input("alice", frame));

    mixer.mix_cycle();

    AudioFrame output;
    EXPECT_FALSE(mixer.pop_output("alice", output));
}

TEST(MixerTest, TwoParticipantsForward) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");

    // Alice sends audio
    auto alice_frame = make_frame(5000, 1);
    EXPECT_TRUE(mixer.push_input("alice", alice_frame));

    // Bob sends audio
    auto bob_frame = make_frame(3000, 1);
    EXPECT_TRUE(mixer.push_input("bob", bob_frame));

    mixer.mix_cycle();

    // Alice should hear Bob's audio (not her own)
    AudioFrame alice_output;
    ASSERT_TRUE(mixer.pop_output("alice", alice_output));
    for (size_t i = 0; i < kSamplesPerFrame; ++i) {
        EXPECT_EQ(alice_output.samples[i], 3000);
    }

    // Bob should hear Alice's audio (not his own)
    AudioFrame bob_output;
    ASSERT_TRUE(mixer.pop_output("bob", bob_output));
    for (size_t i = 0; i < kSamplesPerFrame; ++i) {
        EXPECT_EQ(bob_output.samples[i], 5000);
    }
}

TEST(MixerTest, ThreeParticipantsMixing) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");
    mixer.add_participant("carol");

    mixer.push_input("alice", make_frame(1000));
    mixer.push_input("bob", make_frame(2000));
    mixer.push_input("carol", make_frame(3000));

    mixer.mix_cycle();

    // Alice hears bob + carol = 2000 + 3000 = 5000
    AudioFrame out;
    ASSERT_TRUE(mixer.pop_output("alice", out));
    EXPECT_EQ(out.samples[0], 5000);

    // Bob hears alice + carol = 1000 + 3000 = 4000
    ASSERT_TRUE(mixer.pop_output("bob", out));
    EXPECT_EQ(out.samples[0], 4000);

    // Carol hears alice + bob = 1000 + 2000 = 3000
    ASSERT_TRUE(mixer.pop_output("carol", out));
    EXPECT_EQ(out.samples[0], 3000);
}

TEST(MixerTest, GainControl) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");

    // Alice sets Bob's gain to 0.5
    mixer.set_gain("alice", "bob", 0.5f);

    mixer.push_input("bob", make_frame(10000));
    mixer.mix_cycle();

    AudioFrame out;
    ASSERT_TRUE(mixer.pop_output("alice", out));
    EXPECT_EQ(out.samples[0], 5000);
}

TEST(MixerTest, MuteControl) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");

    // Alice mutes Bob
    mixer.set_mute("alice", "bob", true);

    mixer.push_input("bob", make_frame(10000));
    mixer.mix_cycle();

    // Alice should get no output (Bob is muted)
    AudioFrame out;
    EXPECT_FALSE(mixer.pop_output("alice", out));
}

TEST(MixerTest, ClampingPreventsOverflow) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");
    mixer.add_participant("carol");

    // Both send max positive values
    mixer.push_input("bob", make_frame(30000));
    mixer.push_input("carol", make_frame(30000));

    mixer.mix_cycle();

    // Alice should hear clamped sum (30000 + 30000 = 60000 → clamped to 32767)
    AudioFrame out;
    ASSERT_TRUE(mixer.pop_output("alice", out));
    EXPECT_EQ(out.samples[0], std::numeric_limits<int16_t>::max());
}

TEST(MixerTest, RemoveParticipant) {
    Mixer mixer(4);
    mixer.add_participant("alice");
    mixer.add_participant("bob");

    EXPECT_EQ(mixer.participant_count(), 2u);
    mixer.remove_participant("bob");
    EXPECT_EQ(mixer.participant_count(), 1u);

    // Push to removed participant fails gracefully
    EXPECT_FALSE(mixer.push_input("bob", make_frame(1000)));
}

TEST(MixerTest, PacketSerialization) {
    AudioPacket pkt;
    pkt.sequence = 42;
    pkt.timestamp = 5376; // 42 * 128
    for (size_t i = 0; i < kSamplesPerFrame; ++i) {
        pkt.samples[i] = static_cast<int16_t>(i * 100);
    }

    uint8_t buf[kAudioPacketSize];
    pkt.serialize(buf);

    auto decoded = AudioPacket::deserialize(buf, kAudioPacketSize);
    EXPECT_EQ(decoded.sequence, 42u);
    EXPECT_EQ(decoded.timestamp, 5376u);
    for (size_t i = 0; i < kSamplesPerFrame; ++i) {
        EXPECT_EQ(decoded.samples[i], static_cast<int16_t>(i * 100));
    }
}

TEST(MixerTest, ShortPacketDeserialize) {
    uint8_t buf[4] = {0};
    auto pkt = AudioPacket::deserialize(buf, 4);
    EXPECT_EQ(pkt.sequence, 0u);
    EXPECT_EQ(pkt.timestamp, 0u);
}

} // namespace
} // namespace tutti
