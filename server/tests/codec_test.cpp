#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <opus.h>
#include <thread>
#include <vector>

#include "audio/room.h"

namespace tutti {
namespace {

using namespace std::chrono_literals;

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

    std::vector<std::vector<uint8_t>> snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return datagrams_;
    }

private:
    std::mutex mutex_;
    std::vector<std::vector<uint8_t>> datagrams_;
};

/// Serialize a PCM tone packet
void make_pcm_packet(uint32_t seq, int16_t amplitude, uint8_t* buf) {
    AudioPacket pkt;
    pkt.sequence = seq;
    pkt.timestamp = seq * kSamplesPerFrame;
    for (size_t i = 0; i < kSamplesPerFrame; ++i) {
        pkt.samples[i] = static_cast<int16_t>(
            amplitude * std::sin(2.0 * M_PI * 440.0 * i / kSampleRate));
    }
    pkt.serialize(buf);
}

TEST(CodecTest, OpusListenerReceivesOpusMix) {
    Room::set_lobby_loop_samples({});
    Room room("codec-room", 4);
    room.start();

    auto a = std::make_shared<FakeSession>();  // Opus listener
    auto b = std::make_shared<FakeSession>();  // PCM sender
    auto c = std::make_shared<FakeSession>();  // third body → mixer path
    ASSERT_TRUE(room.add_participant("a", "A", a));
    ASSERT_TRUE(room.add_participant("b", "B", b));
    ASSERT_TRUE(room.add_participant("c", "C", c));
    room.set_participant_codec("a", Codec::Opus);

    // B streams a PCM tone at roughly real-time pace for ~400ms
    uint8_t buf[kAudioPacketSize];
    for (uint32_t i = 0; i < 150; ++i) {
        make_pcm_packet(i, 8000, buf);
        room.on_audio_received("b", buf, kAudioPacketSize);
        std::this_thread::sleep_for(2667us);
    }

    size_t opus_count = 0;
    for (const auto& d : a->snapshot()) {
        if (d.size() != kAudioPacketSize) {
            EXPECT_GT(d.size(), kAudioHeaderSize);
            EXPECT_LE(d.size(), kAudioHeaderSize + kOpusMaxPayload);
            opus_count++;
        }
    }
    // ~400ms of audio at 100 Opus packets/s → expect a healthy number
    EXPECT_GT(opus_count, 10u);

    auto stats = room.audio_stats();
    EXPECT_GT(stats.opus_frames_out, 10u);

    room.stop();
}

TEST(CodecTest, OpusSenderIsMixedForPcmListeners) {
    Room::set_lobby_loop_samples({});
    Room room("codec-room", 4);
    room.start();

    auto a = std::make_shared<FakeSession>();  // Opus sender
    auto b = std::make_shared<FakeSession>();  // PCM listener
    auto c = std::make_shared<FakeSession>();
    ASSERT_TRUE(room.add_participant("a", "A", a));
    ASSERT_TRUE(room.add_participant("b", "B", b));
    ASSERT_TRUE(room.add_participant("c", "C", c));
    room.set_participant_codec("a", Codec::Opus);

    // A streams an Opus-encoded tone at ~100 packets/s for ~400ms
    int err = 0;
    OpusEncoder* enc = opus_encoder_create(48000, 1,
                                           OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                           &err);
    ASSERT_EQ(err, OPUS_OK);
    opus_encoder_ctl(enc, OPUS_SET_BITRATE(32000));

    int16_t tone[kOpusFrameSamples];
    uint8_t pkt[kAudioHeaderSize + kOpusMaxPayload];
    for (uint32_t i = 0; i < 40; ++i) {
        for (size_t s = 0; s < kOpusFrameSamples; ++s) {
            size_t abs_s = i * kOpusFrameSamples + s;
            tone[s] = static_cast<int16_t>(
                8000 * std::sin(2.0 * M_PI * 440.0 * abs_s / kSampleRate));
        }
        int n = opus_encode(enc, tone, kOpusFrameSamples,
                            pkt + kAudioHeaderSize, kOpusMaxPayload);
        ASSERT_GT(n, 0);
        uint32_t ts = i * kOpusFrameSamples;
        std::memcpy(pkt, &i, sizeof(i));
        std::memcpy(pkt + 4, &ts, sizeof(ts));
        room.on_audio_received("a", pkt, kAudioHeaderSize + n);
        std::this_thread::sleep_for(10ms);
    }
    opus_encoder_destroy(enc);

    // B should have received 264-byte mixed PCM carrying the tone
    int peak = 0;
    size_t pcm_count = 0;
    for (const auto& d : b->snapshot()) {
        if (d.size() != kAudioPacketSize) continue;
        pcm_count++;
        auto p = AudioPacket::deserialize(d.data(), d.size());
        for (auto s : p.samples) peak = std::max(peak, std::abs(static_cast<int>(s)));
    }
    EXPECT_GT(pcm_count, 20u);
    EXPECT_GT(peak, 2000);

    auto stats = room.audio_stats();
    EXPECT_GT(stats.opus_frames_in, 20u);

    room.stop();
}

} // namespace
} // namespace tutti
