#pragma once

#include <cstdint>
#include <vector>

struct OpusDecoder;
struct OpusEncoder;

namespace tutti {

/// Codec a participant uses on the wire, both directions.
/// PCM: fixed 264-byte datagrams (8B header + 256B s16le), 375 pkt/s.
/// Opus: variable-size datagrams (8B header + Opus payload), 10ms frames,
///       100 pkt/s at ~32kbps — the weak-link mode.
/// Demux is stateless and per-packet: exactly 264 bytes == PCM,
/// anything else == Opus. kOpusMaxPayload keeps the sizes disjoint.
enum class Codec { Pcm = 0, Opus = 1 };

constexpr size_t kOpusFrameSamples = 480;  // 10ms at 48kHz
constexpr size_t kOpusMaxPayload = 240;    // hard cap; 264 stays unambiguous
constexpr int kOpusBitrate = 32000;

/// Thin RAII wrappers. Each instance is single-threaded by design:
/// decoders live on the network receive path, encoders on the mixer thread.
class OpusDecoderWrapper {
public:
    OpusDecoderWrapper();
    ~OpusDecoderWrapper();
    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    /// Decode one Opus packet into PCM. Returns samples written
    /// (expected kOpusFrameSamples) or -1 on failure.
    int decode(const uint8_t* payload, size_t len, int16_t* out, size_t out_max);

private:
    OpusDecoder* dec_ = nullptr;
};

class OpusEncoderWrapper {
public:
    OpusEncoderWrapper();
    ~OpusEncoderWrapper();
    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    /// Encode exactly kOpusFrameSamples of PCM. Returns payload bytes
    /// written (<= kOpusMaxPayload) or -1 on failure.
    int encode(const int16_t* samples, uint8_t* out, size_t out_max);

private:
    OpusEncoder* enc_ = nullptr;
};

/// Per-participant codec working state. The decoder half is touched only
/// by that participant's network receive callbacks; the encoder half only
/// by the room's mixer thread — no locking needed within each half.
struct ParticipantCodecState {
    // Receive side (network thread)
    OpusDecoderWrapper decoder;
    std::vector<int16_t> in_pending;   // decoded samples awaiting 128-framing

    // Send side (mixer thread)
    OpusEncoderWrapper encoder;
    std::vector<int16_t> out_pending;  // mixed samples awaiting 480-framing
    uint32_t out_timestamp = 0;
};

} // namespace tutti
