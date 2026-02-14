#pragma once

#include <rigtorp/SPSCQueue.h>
#include <array>
#include <cstdint>
#include "transport/transport_interface.h"

namespace tutti {

/// Audio frame: a fixed-size buffer of kSamplesPerFrame int16 samples.
/// Used as the element type in SPSC queues between network and mixer threads.
struct AudioFrame {
    uint32_t sequence = 0;
    uint32_t timestamp = 0;
    std::array<int16_t, kSamplesPerFrame> samples{};

    static AudioFrame from_packet(const AudioPacket& pkt) {
        AudioFrame frame;
        frame.sequence = pkt.sequence;
        frame.timestamp = pkt.timestamp;
        std::copy(std::begin(pkt.samples), std::end(pkt.samples),
                  frame.samples.begin());
        return frame;
    }

    AudioPacket to_packet() const {
        AudioPacket pkt;
        pkt.sequence = sequence;
        pkt.timestamp = timestamp;
        std::copy(samples.begin(), samples.end(), std::begin(pkt.samples));
        return pkt;
    }
};

/// SPSC ring buffer for audio frames.
/// Wraps rigtorp::SPSCQueue with audio-specific convenience.
/// Producer: network receive thread. Consumer: mixer RT thread (or vice versa).
class AudioRingBuffer {
public:
    /// Capacity in frames. Default ~100ms of buffer at 48kHz/128 samples.
    explicit AudioRingBuffer(size_t capacity = 64)
        : queue_(capacity) {}

    /// Non-blocking push. Returns false if full (drop frame).
    bool try_push(const AudioFrame& frame) {
        return queue_.try_push(frame);
    }

    /// Non-blocking push with move.
    bool try_push(AudioFrame&& frame) {
        return queue_.try_push(std::move(frame));
    }

    /// Non-blocking pop. Returns nullptr if empty.
    AudioFrame* front() {
        return queue_.front();
    }

    /// Pop the front element (must call front() first to check).
    void pop() {
        queue_.pop();
    }

    /// Try to pop into destination. Returns false if empty.
    bool try_pop(AudioFrame& out) {
        auto* f = queue_.front();
        if (!f) return false;
        out = *f;
        queue_.pop();
        return true;
    }

    /// Approximate number of items in queue (not exact in concurrent use)
    size_t size_approx() const {
        return queue_.size();
    }

private:
    rigtorp::SPSCQueue<AudioFrame> queue_;
};

} // namespace tutti
