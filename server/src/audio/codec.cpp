#include "codec.h"

#include <opus.h>

namespace tutti {

OpusDecoderWrapper::OpusDecoderWrapper() {
    int err = 0;
    dec_ = opus_decoder_create(48000, 1, &err);
    if (err != OPUS_OK) dec_ = nullptr;
}

OpusDecoderWrapper::~OpusDecoderWrapper() {
    if (dec_) opus_decoder_destroy(dec_);
}

int OpusDecoderWrapper::decode(const uint8_t* payload, size_t len,
                               int16_t* out, size_t out_max) {
    if (!dec_) return -1;
    int n = opus_decode(dec_, payload, static_cast<opus_int32>(len),
                        out, static_cast<int>(out_max), 0);
    return n > 0 ? n : -1;
}

OpusEncoderWrapper::OpusEncoderWrapper() {
    int err = 0;
    enc_ = opus_encoder_create(48000, 1, OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                               &err);
    if (err != OPUS_OK) {
        enc_ = nullptr;
        return;
    }
    opus_encoder_ctl(enc_, OPUS_SET_BITRATE(kOpusBitrate));
    opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(10));
}

OpusEncoderWrapper::~OpusEncoderWrapper() {
    if (enc_) opus_encoder_destroy(enc_);
}

int OpusEncoderWrapper::encode(const int16_t* samples, uint8_t* out,
                               size_t out_max) {
    if (!enc_) return -1;
    if (out_max > kOpusMaxPayload) out_max = kOpusMaxPayload;
    int n = opus_encode(enc_, samples, static_cast<int>(kOpusFrameSamples),
                        out, static_cast<opus_int32>(out_max));
    return n > 0 ? n : -1;
}

} // namespace tutti
