#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "transport/game_datagram.h"
#include "transport/line_framer.h"
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

} // namespace
} // namespace tutti
