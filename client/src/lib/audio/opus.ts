/**
 * Opus compressed audio mode, via WebCodecs.
 *
 * Weak links (mobile, 1-bar 5G) cannot carry Tutti's 800kbps uncompressed
 * PCM. This module provides the ~48kbps fallback: 10ms Opus frames at
 * 32kbps, 100 packets/s instead of 375.
 *
 * Wire format (must stay in sync with server/src/audio/codec.h):
 *   PCM  = exactly 264-byte datagrams (8B header + 256B s16le)
 *   Opus = 8B header (u32 seq LE, u32 timestamp-in-samples LE) + payload,
 *          payload capped at 240 bytes so sizes never collide with PCM.
 * Demux is stateless and per-packet, so codec switches can't garble audio.
 */

const OPUS_MAX_PAYLOAD = 240;

export async function opusSupported(): Promise<boolean> {
	if (typeof AudioEncoder === 'undefined' || typeof AudioDecoder === 'undefined') {
		return false;
	}
	try {
		const enc = await AudioEncoder.isConfigSupported({
			codec: 'opus',
			sampleRate: 48000,
			numberOfChannels: 1,
			bitrate: 32000
		});
		const dec = await AudioDecoder.isConfigSupported({
			codec: 'opus',
			sampleRate: 48000,
			numberOfChannels: 1
		});
		return Boolean(enc.supported && dec.supported);
	} catch {
		return false;
	}
}

/** Feeds 128-sample capture frames in; emits framed Opus packets. */
export class OpusEncoderPipe {
	private encoder: AudioEncoder;
	private seq = 0;
	private wireTimestamp = 0; // samples
	private inputTimestamp = 0; // microseconds, required by AudioData

	constructor(private onPacket: (packet: Uint8Array) => void) {
		this.encoder = new AudioEncoder({
			output: (chunk) => {
				if (chunk.byteLength > OPUS_MAX_PAYLOAD) return; // keep demux unambiguous
				const pkt = new Uint8Array(8 + chunk.byteLength);
				const dv = new DataView(pkt.buffer);
				dv.setUint32(0, this.seq++, true);
				dv.setUint32(4, this.wireTimestamp, true);
				// chunk.duration is µs; convert to samples at 48kHz
				this.wireTimestamp += Math.round(((chunk.duration ?? 10000) * 48) / 1000);
				chunk.copyTo(pkt.subarray(8));
				this.onPacket(pkt);
			},
			error: (e) => console.warn('[Opus] encoder error:', e)
		});
		const config: AudioEncoderConfig = {
			codec: 'opus',
			sampleRate: 48000,
			numberOfChannels: 1,
			bitrate: 32000
		};
		try {
			// 10ms frames: 100 pkt/s, ~2.5x less per-packet overhead than 2.5ms
			this.encoder.configure({
				...config,
				opus: { frameDuration: 10000 }
			} as AudioEncoderConfig);
		} catch {
			this.encoder.configure(config); // browser default framing
		}
	}

	encode(samples: Int16Array<ArrayBuffer>): void {
		if (this.encoder.state !== 'configured') return;
		const data = new AudioData({
			format: 's16',
			sampleRate: 48000,
			numberOfFrames: samples.length,
			numberOfChannels: 1,
			timestamp: this.inputTimestamp,
			data: samples
		});
		this.inputTimestamp += (samples.length / 48000) * 1e6;
		this.encoder.encode(data);
		data.close();
	}

	close(): void {
		try {
			this.encoder.close();
		} catch {
			// already closed
		}
	}
}

/** Feeds framed Opus packets in; emits Int16 PCM for the playback ring. */
export class OpusDecoderPipe {
	private decoder: AudioDecoder;

	constructor(private onPcm: (samples: Int16Array) => void) {
		this.decoder = new AudioDecoder({
			output: (audioData) => {
				const n = audioData.numberOfFrames;
				const out = new Int16Array(n);
				try {
					audioData.copyTo(out, { planeIndex: 0, format: 's16' });
				} catch {
					// Some engines only expose f32-planar
					const f32 = new Float32Array(n);
					audioData.copyTo(f32, { planeIndex: 0, format: 'f32-planar' });
					for (let i = 0; i < n; i++) {
						out[i] = Math.max(-32768, Math.min(32767, Math.round(f32[i] * 32768)));
					}
				}
				audioData.close();
				this.onPcm(out);
			},
			error: (e) => console.warn('[Opus] decoder error:', e)
		});
		this.decoder.configure({ codec: 'opus', sampleRate: 48000, numberOfChannels: 1 });
	}

	decode(timestampSamples: number, payload: Uint8Array): void {
		if (this.decoder.state !== 'configured') return;
		this.decoder.decode(
			new EncodedAudioChunk({
				type: 'key',
				timestamp: (timestampSamples / 48000) * 1e6,
				data: payload
			})
		);
	}

	close(): void {
		try {
			this.decoder.close();
		} catch {
			// already closed
		}
	}
}
