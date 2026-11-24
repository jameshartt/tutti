#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tutti {

// Game datagrams share the unreliable channel with audio on WebTransport.
// Audio packets start with a little-endian u32 sequence number; game
// datagrams start with four 0xFF bytes in that position, which the audio
// sequence counters never emit (see next_audio_sequence). The byte after
// the magic is the game datagram "kind"; the rest is the payload.
//
// On WebRTC, game datagrams travel on their own "game" DataChannel and
// carry no magic: the first byte is the kind.
//
// Everything above the transport layer sees the same thing on both
// transports: [kind u8][payload...].

constexpr size_t kGameMagicSize = 4;
constexpr uint8_t kGameMagic[kGameMagicSize] = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint32_t kReservedAudioSequence = 0xFFFFFFFFu;

// Largest [kind][payload] we will send or accept. Keeps a WebTransport
// datagram (magic + this) under a conservative QUIC datagram budget.
constexpr size_t kMaxGameDatagramSize = 1100;

/// True if a raw WebTransport datagram is a game datagram.
inline bool is_game_datagram(const uint8_t* data, size_t len) {
    return len > kGameMagicSize &&
           std::memcmp(data, kGameMagic, kGameMagicSize) == 0;
}

/// Post-increment an audio sequence counter, skipping the reserved value
/// so an audio packet can never be mistaken for a game datagram.
inline uint32_t next_audio_sequence(uint32_t& counter) {
    uint32_t seq = counter;
    ++counter;
    if (counter == kReservedAudioSequence) counter = 0;
    return seq;
}

} // namespace tutti
