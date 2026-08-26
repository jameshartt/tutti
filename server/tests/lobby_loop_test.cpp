#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include "audio/room.h"

namespace tutti {
namespace {

using namespace std::chrono_literals;

/// Transport session that records every datagram it is sent
class FakeSession : public TransportSession {
public:
    bool send_datagram(const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        datagrams_.emplace_back(data, data + len);
        return true;
    }
    bool send_reliable(const std::string&) override { return true; }
    void close() override {}
    std::string id() const override { return "fake"; }
    std::string remote_address() const override { return "127.0.0.1"; }
    bool is_connected() const override { return true; }

    size_t datagram_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return datagrams_.size();
    }

    /// Peak |sample| across the most recent `n` datagrams
    int max_amplitude_recent(size_t n) {
        std::lock_guard<std::mutex> lock(mutex_);
        int peak = 0;
        size_t start = datagrams_.size() > n ? datagrams_.size() - n : 0;
        for (size_t i = start; i < datagrams_.size(); ++i) {
            auto pkt = AudioPacket::deserialize(datagrams_[i].data(),
                                                datagrams_[i].size());
            for (auto s : pkt.samples) {
                peak = std::max(peak, std::abs(static_cast<int>(s)));
            }
        }
        return peak;
    }

private:
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> datagrams_;
};

std::vector<int16_t> loud_test_loop() {
    // One second of a loud square-ish wave, trivially loopable for the test
    std::vector<int16_t> loop(kSampleRate);
    for (size_t i = 0; i < loop.size(); ++i) {
        loop[i] = (i / 100) % 2 == 0 ? 20000 : -20000;
    }
    return loop;
}

TEST(LobbyLoopTest, StreamsToSoloParticipantAfterDelay) {
    Room::set_lobby_loop_samples(loud_test_loop());
    Room room("test-room", 4);
    room.set_lobby_loop_timing(200ms, 100ms);

    auto session = std::make_shared<FakeSession>();
    room.start();
    ASSERT_TRUE(room.add_participant("alice", "Alice", session));

    // Before the delay expires: silence
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(session->datagram_count(), 0u);

    // After delay + fade-in: streaming at ~375 packets/s at full level
    std::this_thread::sleep_for(600ms);
    size_t count = session->datagram_count();
    EXPECT_GT(count, 100u);
    EXPECT_GT(session->max_amplitude_recent(20), 15000);

    room.stop();
    Room::set_lobby_loop_samples({});
}

TEST(LobbyLoopTest, FadesOutWhenSecondParticipantJoins) {
    Room::set_lobby_loop_samples(loud_test_loop());
    Room room("test-room", 4);
    room.set_lobby_loop_timing(100ms, 100ms);

    auto session = std::make_shared<FakeSession>();
    room.start();
    ASSERT_TRUE(room.add_participant("alice", "Alice", session));
    std::this_thread::sleep_for(400ms);
    ASSERT_GT(session->datagram_count(), 50u);

    // Second (unbound) participant joins — loop should fade out and stop
    ASSERT_TRUE(room.add_participant("bob", "Bob", nullptr));
    std::this_thread::sleep_for(300ms);
    size_t after_fade = session->datagram_count();
    // The tail of the fade should be near-silent
    EXPECT_LT(session->max_amplitude_recent(3), 3000);

    // And no further packets once the fade completes
    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(session->datagram_count(), after_fade);

    room.stop();
    Room::set_lobby_loop_samples({});
}

TEST(LobbyLoopTest, NoLoopForUnboundSoloParticipant) {
    Room::set_lobby_loop_samples(loud_test_loop());
    Room room("test-room", 4);
    room.set_lobby_loop_timing(100ms, 100ms);

    room.start();
    // Joined via HTTP but no transport bound — nothing to stream to
    ASSERT_TRUE(room.add_participant("ghost", "Ghost", nullptr));
    std::this_thread::sleep_for(300ms);

    auto stats = room.audio_stats();
    EXPECT_EQ(stats.lobby_loop_frames, 0u);

    room.stop();
    Room::set_lobby_loop_samples({});
}

} // namespace
} // namespace tutti
