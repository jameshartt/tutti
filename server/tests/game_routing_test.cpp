#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "audio/room.h"
#include "rooms/room_manager.h"
#include "transport/session_binder.h"

namespace tutti {
namespace {

using namespace std::chrono_literals;
using json = nlohmann::json;

/// Transport session that records everything it is sent
class RecordingSession : public TransportSession {
public:
    explicit RecordingSession(std::string id) : id_(std::move(id)) {}

    bool send_datagram(const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        audio_.emplace_back(data, data + len);
        return true;
    }
    bool send_game_datagram(const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lock(mutex_);
        game_.emplace_back(data, data + len);
        return true;
    }
    bool send_reliable(const std::string& msg) override {
        std::lock_guard<std::mutex> lock(mutex_);
        reliable_.push_back(msg);
        return true;
    }
    void close() override {}
    std::string id() const override { return id_; }
    std::string remote_address() const override { return "127.0.0.1"; }
    bool is_connected() const override { return true; }

    std::vector<json> reliable_of_type(const std::string& type) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<json> out;
        for (const auto& m : reliable_) {
            auto j = json::parse(m, nullptr, false);
            if (!j.is_discarded() && j.value("type", "") == type) out.push_back(j);
        }
        return out;
    }
    std::vector<std::vector<uint8_t>> game() {
        std::lock_guard<std::mutex> lock(mutex_);
        return game_;
    }
    size_t audio_count() {
        std::lock_guard<std::mutex> lock(mutex_);
        return audio_.size();
    }

private:
    std::string id_;
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> audio_;
    std::vector<std::vector<uint8_t>> game_;
    std::vector<std::string> reliable_;
};

/// Two participants joined and bound to the same room through the binder,
/// exactly as the HTTP join + transport bind flow does it.
class GameRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        manager_ = std::make_shared<RoomManager>(4);
        manager_->initialize_default_rooms();
        binder_ = std::make_unique<SessionBinder>(manager_);
        cb_ = binder_->make_callbacks();
        room_name_ = manager_->list_rooms().front().name;

        a_ = join_and_bind("sess-a", "Alice", a_id_);
        b_ = join_and_bind("sess-b", "Bob", b_id_);
    }

    std::shared_ptr<RecordingSession> join_and_bind(const std::string& sid,
                                                    const std::string& alias,
                                                    std::string& out_pid) {
        auto result = manager_->join_room(room_name_, alias, "", nullptr,
                                          out_pid);
        EXPECT_EQ(result, RoomManager::JoinResult::Success);
        auto session = std::make_shared<RecordingSession>(sid);
        cb_.on_session_open(session);
        json bind = {{"type", "bind"},
                     {"participant_id", out_pid},
                     {"room", room_name_}};
        cb_.on_message(session.get(), bind.dump());
        return session;
    }

    std::shared_ptr<RoomManager> manager_;
    std::unique_ptr<SessionBinder> binder_;
    TransportCallbacks cb_;
    std::string room_name_;
    std::string a_id_, b_id_;
    std::shared_ptr<RecordingSession> a_, b_;
};

TEST_F(GameRoutingTest, BindDeliversRoomState) {
    EXPECT_FALSE(a_->reliable_of_type("room_state").empty());
    EXPECT_FALSE(b_->reliable_of_type("room_state").empty());
}

TEST_F(GameRoutingTest, GamePingIsBroadcastToEveryParticipantWithSender) {
    cb_.on_message(a_.get(), R"({"type":"game_ping","n":7})");

    for (auto* s : {a_.get(), b_.get()}) {
        auto pings = s->reliable_of_type("game_ping");
        ASSERT_EQ(pings.size(), 1u);
        EXPECT_EQ(pings[0].value("from", ""), a_id_);
        EXPECT_EQ(pings[0].value("n", 0), 7);
    }
}

TEST_F(GameRoutingTest, UnknownGameTypeIsIgnored) {
    cb_.on_message(a_.get(), R"({"type":"game_does_not_exist"})");
    EXPECT_TRUE(a_->reliable_of_type("game_does_not_exist").empty());
    EXPECT_TRUE(b_->reliable_of_type("game_does_not_exist").empty());
    EXPECT_TRUE(a_->reliable_of_type("error").empty());
}

TEST_F(GameRoutingTest, UnknownNonGameTypeIsIgnored) {
    cb_.on_message(a_.get(), R"({"type":"definitely_not_a_thing"})");
    cb_.on_message(a_.get(), "not json at all");
    EXPECT_TRUE(a_->reliable_of_type("error").empty());
}

TEST_F(GameRoutingTest, MalformedJsonNeverThrowsFromBoundOrUnboundSessions) {
    auto stranger = std::make_shared<RecordingSession>("sess-m");
    cb_.on_session_open(stranger);
    // Valid JSON of the wrong shape used to throw json::type_error, which
    // would unwind through a C callback and kill the process.
    const char* nasty[] = {
        "[1]", "7", "null", "\"str\"", R"({"type":5})", R"({"type":null})",
        R"({"type":["game_ping"]})", R"({"type":"gain","participant_id":7})",
        R"({"type":"gain","participant_id":"x","value":"loud"})",
        R"({"type":"mute","participant_id":"x","muted":"yes"})",
        R"({"type":"codec","codec":3})",
        R"({"type":"bind","participant_id":5,"room":[]})", "{", ""};
    for (const char* m : nasty) {
        EXPECT_NO_THROW(cb_.on_message(a_.get(), m)) << m;
        EXPECT_NO_THROW(cb_.on_message(stranger.get(), m)) << m;
    }
    // Still alive and still working afterwards
    cb_.on_message(a_.get(), R"({"type":"game_ping","n":1})");
    EXPECT_EQ(b_->reliable_of_type("game_ping").size(), 1u);
}

TEST_F(GameRoutingTest, OversizedGameMessagesAreNotRelayed) {
    json big = {{"type", "game_ping"}, {"pad", std::string(kMaxGameMessageSize, 'x')}};
    cb_.on_message(a_.get(), big.dump());
    EXPECT_TRUE(a_->reliable_of_type("game_ping").empty());
    EXPECT_TRUE(b_->reliable_of_type("game_ping").empty());

    json ok = {{"type", "game_ping"}, {"pad", std::string(1024, 'x')}};
    cb_.on_message(a_.get(), ok.dump());
    EXPECT_EQ(b_->reliable_of_type("game_ping").size(), 1u);
}

TEST_F(GameRoutingTest, GameMessageFromUnboundSessionIsDropped) {
    auto stranger = std::make_shared<RecordingSession>("sess-x");
    cb_.on_session_open(stranger);
    cb_.on_message(stranger.get(), R"({"type":"game_ping"})");
    EXPECT_TRUE(a_->reliable_of_type("game_ping").empty());
    EXPECT_TRUE(b_->reliable_of_type("game_ping").empty());
}

TEST_F(GameRoutingTest, PongCarriesServerTime) {
    cb_.on_message(a_.get(), R"({"type":"ping","id":3,"t":123.5})");
    auto pongs = a_->reliable_of_type("pong");
    ASSERT_EQ(pongs.size(), 1u);
    EXPECT_EQ(pongs[0].value("id", 0), 3);
    EXPECT_DOUBLE_EQ(pongs[0].value("t", 0.0), 123.5);
    ASSERT_TRUE(pongs[0].contains("server_t"));
    EXPECT_GT(pongs[0]["server_t"].get<double>(), 0.0);
}

TEST_F(GameRoutingTest, EchoGameDatagramReturnsToSenderOnly) {
    const uint8_t dg[] = {0x00, 0xDE, 0xAD, 0xBE, 0xEF};
    cb_.on_game_datagram(a_.get(), dg, sizeof(dg));

    auto got = a_->game();
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(got[0], std::vector<uint8_t>(dg, dg + sizeof(dg)));
    EXPECT_TRUE(b_->game().empty());
}

TEST_F(GameRoutingTest, GameDatagramsNeverReachTheAudioPath) {
    auto room = manager_->get_room(room_name_);
    auto before = room->audio_stats();

    // Sized like a PCM packet and like an Opus packet: if either leaked
    // into on_audio_received it would be counted or forwarded.
    std::vector<uint8_t> pcm_sized(kAudioPacketSize, 0x01);
    std::vector<uint8_t> opus_sized(60, 0x01);
    for (int i = 0; i < 20; ++i) {
        cb_.on_game_datagram(a_.get(), pcm_sized.data(), pcm_sized.size());
        cb_.on_game_datagram(a_.get(), opus_sized.data(), opus_sized.size());
    }

    auto after = room->audio_stats();
    EXPECT_EQ(after.frames_in, before.frames_in);
    EXPECT_EQ(after.fast_path_forwards, before.fast_path_forwards);
    EXPECT_EQ(after.opus_frames_in, before.opus_frames_in);
    EXPECT_EQ(b_->audio_count(), 0u);
    EXPECT_EQ(room->game().datagrams_received(), 40u);
}

TEST_F(GameRoutingTest, AudioDatagramsNeverReachTheGameSession) {
    auto room = manager_->get_room(room_name_);
    AudioPacket pkt{};
    uint8_t buf[kAudioPacketSize];
    for (uint32_t i = 0; i < 20; ++i) {
        pkt.sequence = i;
        pkt.serialize(buf);
        cb_.on_datagram(a_.get(), buf, sizeof(buf));
    }
    EXPECT_EQ(room->game().datagrams_received(), 0u);
    EXPECT_TRUE(a_->game().empty());
    EXPECT_TRUE(b_->game().empty());
    // ...and audio still flows (2 bound participants: fast-path forward)
    EXPECT_EQ(b_->audio_count(), 20u);
}

TEST_F(GameRoutingTest, OversizedAndEmptyGameDatagramsAreDropped) {
    auto room = manager_->get_room(room_name_);
    std::vector<uint8_t> huge(4000, 0x00);
    cb_.on_game_datagram(a_.get(), huge.data(), huge.size());
    cb_.on_game_datagram(a_.get(), huge.data(), 0);
    EXPECT_EQ(room->game().datagrams_received(), 0u);
    EXPECT_TRUE(a_->game().empty());
}

TEST_F(GameRoutingTest, GameDatagramFromUnboundSessionIsDropped) {
    auto room = manager_->get_room(room_name_);
    auto stranger = std::make_shared<RecordingSession>("sess-y");
    cb_.on_session_open(stranger);
    const uint8_t dg[] = {0x00, 0x01};
    cb_.on_game_datagram(stranger.get(), dg, sizeof(dg));
    EXPECT_EQ(room->game().datagrams_received(), 0u);
    EXPECT_TRUE(stranger->game().empty());
}

// ── Reaper ──────────────────────────────────────────────────────────────

TEST(GameReaperTest, GameDatagramsKeepSilentParticipantsAlive) {
    Room room("ReaperTest");
    auto a = std::make_shared<RecordingSession>("a");
    auto b = std::make_shared<RecordingSession>("b");
    ASSERT_TRUE(room.add_participant("pa", "A", a));
    ASSERT_TRUE(room.add_participant("pb", "B", b));

    const auto timeout = 150ms;
    const uint8_t dg[] = {0x00};

    // Only game datagrams, no audio at all, for 3x the inactivity timeout
    auto until = std::chrono::steady_clock::now() + 3 * timeout;
    while (std::chrono::steady_clock::now() < until) {
        room.on_game_datagram("pa", dg, sizeof(dg));
        room.on_game_datagram("pb", dg, sizeof(dg));
        EXPECT_EQ(room.reap_stale_participants(timeout), 0u);
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(room.participant_count(), 2u);
}

TEST(GameReaperTest, ReceivingGameDatagramsAlsoCountsAsActivity) {
    Room room("ReaperTest3");
    auto a = std::make_shared<RecordingSession>("a");
    auto b = std::make_shared<RecordingSession>("b");
    ASSERT_TRUE(room.add_participant("pa", "A", a));
    ASSERT_TRUE(room.add_participant("pb", "B", b));

    const auto timeout = 150ms;
    const uint8_t dg[] = {0x00};

    // pb sends nothing at all (a spectator); the server only sends to it
    auto until = std::chrono::steady_clock::now() + 3 * timeout;
    while (std::chrono::steady_clock::now() < until) {
        room.on_game_datagram("pa", dg, sizeof(dg));
        EXPECT_TRUE(room.send_game_datagram("pb", dg, sizeof(dg)));
        EXPECT_EQ(room.reap_stale_participants(timeout), 0u);
        std::this_thread::sleep_for(20ms);
    }
    EXPECT_EQ(room.participant_count(), 2u);
    EXPECT_FALSE(b->game().empty());
}

TEST(GameReaperTest, FullySilentParticipantsAreStillReaped) {
    Room room("ReaperTest2");
    ASSERT_TRUE(room.add_participant(
        "pa", "A", std::make_shared<RecordingSession>("a")));
    ASSERT_TRUE(room.add_participant(
        "pb", "B", std::make_shared<RecordingSession>("b")));

    std::this_thread::sleep_for(200ms);
    EXPECT_EQ(room.reap_stale_participants(150ms), 2u);
    EXPECT_EQ(room.participant_count(), 0u);
}

TEST_F(GameRoutingTest, GameMessageFloodIsRateLimited) {
    // Far more than the burst, sent as fast as possible
    const int sent = static_cast<int>(kGameMessageBurst) * 10;
    for (int i = 0; i < sent; i++) {
        cb_.on_message(a_.get(), R"({"type":"game_ping","n":1})");
    }
    size_t relayed = b_->reliable_of_type("game_ping").size();
    EXPECT_GE(relayed, static_cast<size_t>(kGameMessageBurst));
    // Burst plus whatever refilled while the loop ran (well under a second)
    EXPECT_LE(relayed, static_cast<size_t>(kGameMessageBurst + kGameMessageRate));

    // The limit is per session: the other participant is unaffected
    cb_.on_message(b_.get(), R"({"type":"game_ping","n":2})");
    auto from_b = a_->reliable_of_type("game_ping");
    ASSERT_FALSE(from_b.empty());
    EXPECT_EQ(from_b.back().value("from", ""), b_id_);

    // And it does not touch other control messages
    cb_.on_message(a_.get(), R"({"type":"ping","id":9,"t":1.0})");
    EXPECT_EQ(a_->reliable_of_type("pong").size(), 1u);
}

TEST_F(GameRoutingTest, UnknownGameTypesAreLoggedSanitisedAndThrottled) {
    auto room = manager_->get_room(room_name_);
    ASSERT_TRUE(room);

    testing::internal::CaptureStdout();
    // A type carrying a newline, trying to forge a second log line
    json forged = {{"type", "game_x\n[Tutti] forged line"}};
    cb_.on_message(a_.get(), forged.dump());
    for (int i = 0; i < 50; i++) {
        cb_.on_message(a_.get(), R"({"type":"game_nope"})");
    }
    std::string log = testing::internal::GetCapturedStdout();

    EXPECT_EQ(room->game().unknown_messages(), 51u);
    EXPECT_NE(log.find("Ignoring unknown message type 'game_x?[Tutti] forged line'"),
              std::string::npos) << log;
    EXPECT_EQ(log.find("\n[Tutti] forged line"), std::string::npos);
    size_t lines = 0;
    for (char c : log) lines += (c == '\n');
    EXPECT_EQ(lines, GameSession::kUnknownLogLimit);
}

} // namespace
} // namespace tutti
