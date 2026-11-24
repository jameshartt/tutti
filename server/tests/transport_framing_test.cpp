#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "transport/game_datagram.h"
#include "transport/line_framer.h"
#include "transport/log_safe.h"
#include "transport/rate_limiter.h"
#include "transport/transport_interface.h"

namespace tutti {
namespace {

std::vector<std::string> feed_all(LineFramer& f,
                                  const std::vector<std::string>& fragments) {
    std::vector<std::string> lines;
    for (const auto& frag : fragments) {
        f.feed(frag.data(), frag.size(),
               [&](const std::string& line) { lines.push_back(line); });
    }
    return lines;
}

TEST(LineFramerTest, MessageSplitAcrossTwoFragmentsDeliveredOnce) {
    LineFramer f;
    auto lines = feed_all(f, {R"({"type":"bi)", "nd\",\"room\":\"A\"}\n"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], R"({"type":"bind","room":"A"})");
    EXPECT_EQ(f.buffered(), 0u);
}

TEST(LineFramerTest, NothingDeliveredUntilNewlineArrives) {
    LineFramer f;
    EXPECT_TRUE(feed_all(f, {"abc", "def"}).empty());
    EXPECT_EQ(f.buffered(), 6u);
    auto lines = feed_all(f, {"\n"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "abcdef");
}

TEST(LineFramerTest, SeveralMessagesInOneFragmentPlusPartialTail) {
    LineFramer f;
    auto lines = feed_all(f, {"one\ntwo\nthr", "ee\n"});
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0], "one");
    EXPECT_EQ(lines[1], "two");
    EXPECT_EQ(lines[2], "three");
}

TEST(LineFramerTest, SixteenKilobyteMessageSurvivesByteByByteDelivery) {
    std::string big(16 * 1024, 'x');
    big[0] = '{';
    LineFramer f;
    std::vector<std::string> lines;
    std::string wire = big + "\n";
    for (char c : wire) {
        f.feed(&c, 1, [&](const std::string& l) { lines.push_back(l); });
    }
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], big);
}

TEST(LineFramerTest, EmptyLinesAndCarriageReturnsAreSkipped) {
    LineFramer f;
    auto lines = feed_all(f, {"\n\na\r\n\n"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "a");
}

TEST(LineFramerTest, OversizedLineIsDroppedAndFramingRecovers) {
    LineFramer f(8);
    auto lines = feed_all(f, {"0123456789", "abc\nok\n"});
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0], "ok");
}

TEST(GameDatagramTest, MagicPrefixIdentifiesGameDatagrams) {
    uint8_t game[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x42};
    EXPECT_TRUE(is_game_datagram(game, sizeof(game)));
    // Magic alone, with no kind byte, is not a game datagram
    EXPECT_FALSE(is_game_datagram(game, 4));
}

TEST(GameDatagramTest, AudioPacketsAreNeverGameDatagrams) {
    // Every sequence number the audio counters can produce
    uint32_t counter = 0xFFFFFFF0u;
    for (int i = 0; i < 64; ++i) {
        AudioPacket pkt{};
        pkt.sequence = next_audio_sequence(counter);
        uint8_t buf[kAudioPacketSize];
        pkt.serialize(buf);
        EXPECT_FALSE(is_game_datagram(buf, sizeof(buf)))
            << "sequence " << pkt.sequence;
    }
}

TEST(GameDatagramTest, AudioSequenceSkipsReservedValueAndWraps) {
    uint32_t counter = 0xFFFFFFFDu;
    EXPECT_EQ(next_audio_sequence(counter), 0xFFFFFFFDu);
    EXPECT_EQ(next_audio_sequence(counter), 0xFFFFFFFEu);
    EXPECT_EQ(next_audio_sequence(counter), 0u);
    EXPECT_EQ(next_audio_sequence(counter), 1u);
}

/// Records which callback each datagram reached, for the shared-channel
/// demux used by the WebTransport server.
struct DemuxRecorder {
    std::vector<std::vector<uint8_t>> audio, game;
    TransportCallbacks callbacks;
    DemuxRecorder() {
        callbacks.on_datagram = [this](TransportSession*, const uint8_t* d, size_t n) {
            audio.emplace_back(d, d + n);
        };
        callbacks.on_game_datagram = [this](TransportSession*, const uint8_t* d, size_t n) {
            game.emplace_back(d, d + n);
        };
    }
};

TEST(DatagramDemuxTest, GameDatagramReachesOnlyTheGameCallbackWithMagicStripped) {
    DemuxRecorder r;
    std::vector<uint8_t> dg = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 1, 2, 3};
    dispatch_datagram(r.callbacks, nullptr, dg.data(), dg.size());
    EXPECT_TRUE(r.audio.empty());
    ASSERT_EQ(r.game.size(), 1u);
    EXPECT_EQ(r.game[0], (std::vector<uint8_t>{0x00, 1, 2, 3}));
}

TEST(DatagramDemuxTest, AudioSizedGameDatagramsStillNeverReachAudio) {
    DemuxRecorder r;
    // Same lengths as a PCM packet and an Opus packet, but with the magic
    for (size_t len : {size_t{264}, size_t{100}}) {
        std::vector<uint8_t> dg(len, 0x11);
        std::fill(dg.begin(), dg.begin() + 4, 0xFF);
        dispatch_datagram(r.callbacks, nullptr, dg.data(), dg.size());
    }
    EXPECT_TRUE(r.audio.empty());
    EXPECT_EQ(r.game.size(), 2u);
}

TEST(DatagramDemuxTest, AudioPacketReachesOnlyTheAudioCallbackUnchanged) {
    DemuxRecorder r;
    std::vector<uint8_t> pcm(264);
    for (size_t i = 0; i < pcm.size(); i++) pcm[i] = static_cast<uint8_t>(i * 7);
    pcm[0] = 0xFE; pcm[1] = 0xFF; pcm[2] = 0xFF; pcm[3] = 0xFF; // highest audio sequence
    dispatch_datagram(r.callbacks, nullptr, pcm.data(), pcm.size());
    EXPECT_TRUE(r.game.empty());
    ASSERT_EQ(r.audio.size(), 1u);
    EXPECT_EQ(r.audio[0], pcm);
}

TEST(DatagramDemuxTest, OversizedAndBareMagicDatagramsAreDropped) {
    DemuxRecorder r;
    std::vector<uint8_t> big(kGameMagicSize + kMaxGameDatagramSize + 1, 0xFF);
    dispatch_datagram(r.callbacks, nullptr, big.data(), big.size());
    EXPECT_TRUE(r.game.empty());
    EXPECT_TRUE(r.audio.empty());

    std::vector<uint8_t> largest(kGameMagicSize + kMaxGameDatagramSize, 0xFF);
    dispatch_datagram(r.callbacks, nullptr, largest.data(), largest.size());
    EXPECT_EQ(r.game.size(), 1u);

    // Four magic bytes and nothing else is not a game datagram; it falls
    // through as a (too short) audio datagram, which the audio path rejects
    std::vector<uint8_t> bare(4, 0xFF);
    dispatch_datagram(r.callbacks, nullptr, bare.data(), bare.size());
    EXPECT_EQ(r.game.size(), 1u);
    EXPECT_EQ(r.audio.size(), 1u);
}

TEST(DatagramDemuxTest, MissingCallbacksAreSafe) {
    TransportCallbacks none;
    std::vector<uint8_t> game = {0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    std::vector<uint8_t> audio(264, 0);
    dispatch_datagram(none, nullptr, game.data(), game.size());
    dispatch_datagram(none, nullptr, audio.data(), audio.size());
}

TEST(LogSafeTest, ControlCharactersAreReplacedAndLongTextTruncated) {
    EXPECT_EQ(log_safe("game_ok"), "game_ok");
    EXPECT_EQ(log_safe("a\nb\rc\x1b[0m"), "a?b?c?[0m");
    EXPECT_EQ(log_safe(std::string(200, 'x'), 8), "xxxxxxxx...");
    EXPECT_EQ(log_safe("caf\xC3\xA9"), "caf??");
}

TEST(TokenBucketTest, AllowsBurstThenRefillsAtRate) {
    TokenBucket bucket(10.0, 5.0);
    auto t0 = TokenBucket::Clock::now(); // after construction, so time only moves forward
    for (int i = 0; i < 5; i++) EXPECT_TRUE(bucket.allow(t0));
    EXPECT_FALSE(bucket.allow(t0));
    // 100 ms at 10 per second refills exactly one token
    auto t1 = t0 + std::chrono::milliseconds(100);
    EXPECT_TRUE(bucket.allow(t1));
    EXPECT_FALSE(bucket.allow(t1));
    // A long idle period never exceeds the burst
    auto t2 = t1 + std::chrono::seconds(60);
    for (int i = 0; i < 5; i++) EXPECT_TRUE(bucket.allow(t2));
    EXPECT_FALSE(bucket.allow(t2));
}

} // namespace
} // namespace tutti
