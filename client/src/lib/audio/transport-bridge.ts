/**
 * Transport Bridge: connects the audio pipeline to the network transport.
 *
 * Capture path:  Ring buffer (from capture worklet) → Worker → serialize → transport.send()
 * Playback path: transport.onReceive() → deserialize → ring buffer (to playback worklet)
 *
 * The capture side runs in a dedicated Web Worker that uses Atomics.waitAsync()
 * to wake the instant the capture worklet writes new data. If the Worker fails
 * to start, we fall back to setInterval(2ms) polling on the main thread.
 *
 * The playback path stays on the main thread (adding a Worker hop would
 * increase playback latency).
 */

import { RingBufferReader, RingBufferWriter } from './ring-buffer.js';
import {
	SAMPLES_PER_FRAME,
	AUDIO_PACKET_SIZE,
	AUDIO_HEADER_SIZE,
	AUDIO_PAYLOAD_SIZE,
	serializePacket,
	deserializePacket,
	type AudioPacket
} from './types.js';
import type { Transport } from '../transport/transport.js';
import { OpusEncoderPipe, OpusDecoderPipe } from './opus.js';

const OPUS_MAX_PAYLOAD = 240;

export interface TransportBridgeOptions {
	/** SharedArrayBuffer from capture pipeline (read from) */
	captureRingBufferSAB: SharedArrayBuffer;
	/** SharedArrayBuffer from playback pipeline (write to) */
	playbackRingBufferSAB: SharedArrayBuffer;
	/** Network transport (WebTransport or WebRTC) */
	transport: Transport;
	/** MessagePort from capture worklet for frame-ready notifications */
	capturePort?: MessagePort;
}

export class TransportBridge {
	private captureReader: RingBufferReader;
	private playbackWriter: RingBufferWriter;
	private transport: Transport;
	private capturePort: MessagePort | null;
	private captureRingBufferSAB: SharedArrayBuffer;
	private running = false;
	private pollTimer: ReturnType<typeof setInterval> | null = null;
	private worker: Worker | null = null;
	private sendSequence = 0;
	private sendTimestamp = 0;
	private incomingCount = 0;
	private lastRecvSequence = -1;
	private seqGaps = 0; // total missing packets (server drop or network loss)
	private seqReordered = 0; // packets that arrived late/out of order
	private loopbackEnabled = false;
	private micMuted = false;
	private codecMode: 'pcm' | 'opus' = 'pcm';
	private opusEnc: OpusEncoderPipe | null = null;
	private opusDec: OpusDecoderPipe | null = null;

	// Pre-allocated buffers for zero-allocation on hot path (fallback only)
	private readBuffer = new Int16Array(SAMPLES_PER_FRAME);

	constructor(options: TransportBridgeOptions) {
		this.captureReader = new RingBufferReader(options.captureRingBufferSAB);
		this.playbackWriter = new RingBufferWriter(options.playbackRingBufferSAB);
		this.transport = options.transport;
		this.capturePort = options.capturePort ?? null;
		this.captureRingBufferSAB = options.captureRingBufferSAB;
	}

	/** Start the bridge: begin polling capture buffer and listening for incoming data */
	start(): void {
		if (this.running) return;
		this.running = true;

		// Listen for incoming datagrams (playback path — always on main thread)
		this.transport.onDatagram((data) => {
			this.handleIncoming(data);
		});

		// Try to start capture drain in a dedicated Worker
		try {
			this.worker = new Worker(
				new URL('./bridge-worker.ts', import.meta.url),
				{ type: 'module' }
			);

			this.worker.onmessage = (event: MessageEvent) => {
				const msg = event.data;
				if (msg.type === 'packet') {
					if (this.codecMode === 'opus' && this.opusEnc) {
						// Feed the packet's PCM payload to the encoder; it emits
						// framed Opus datagrams via its own callback
						this.opusEnc.encode(
							new Int16Array(msg.data, AUDIO_HEADER_SIZE, SAMPLES_PER_FRAME)
						);
					} else {
						this.transport.sendDatagram(new Uint8Array(msg.data));
					}
					this.sendSequence++;
				} else if (msg.type === 'loopback') {
					this.playbackWriter.write(new Int16Array(msg.samples));
				}
			};

			this.worker.onerror = (err) => {
				console.warn('[TransportBridge] Worker error, falling back to polling:', err.message);
				this.worker = null;
				this.startPollingFallback();
			};

			// Initialize the Worker with the capture SAB
			this.worker.postMessage({
				type: 'init',
				captureRingBufferSAB: this.captureRingBufferSAB
			});

			console.log('[TransportBridge] Capture bridge Worker started');
		} catch (err) {
			console.warn('[TransportBridge] Worker creation failed, falling back to polling:', err);
			this.worker = null;
			this.startPollingFallback();
		}
	}

	/** Fallback: poll capture ring buffer on main thread */
	private startPollingFallback(): void {
		this.pollTimer = setInterval(() => {
			this.drainCapture();
		}, 2);
		console.log('[TransportBridge] Using polling fallback (2ms interval)');
	}

	/**
	 * Switch the outgoing wire codec. Incoming demux is stateless (by packet
	 * size), so only the encoder needs managing. The caller is responsible
	 * for telling the server via a {"type":"codec"} control message.
	 */
	setCodec(mode: 'pcm' | 'opus'): void {
		if (mode === this.codecMode) return;
		this.codecMode = mode;
		if (mode === 'opus') {
			this.opusEnc = new OpusEncoderPipe((pkt) => this.transport.sendDatagram(pkt));
		} else {
			this.opusEnc?.close();
			this.opusEnc = null;
		}
	}

	getCodec(): 'pcm' | 'opus' {
		return this.codecMode;
	}

	/** Stop the bridge */
	stop(): void {
		this.running = false;
		this.opusEnc?.close();
		this.opusEnc = null;
		this.opusDec?.close();
		this.opusDec = null;

		if (this.worker) {
			this.worker.postMessage({ type: 'stop' });
			this.worker.terminate();
			this.worker = null;
		}

		if (this.pollTimer) {
			clearInterval(this.pollTimer);
			this.pollTimer = null;
		}
	}

	/** Mute/unmute mic (still drains buffer, but skips sending) */
	setMicMuted(muted: boolean): void {
		this.micMuted = muted;
		if (this.worker) {
			this.worker.postMessage({ type: 'mute', muted });
		}
	}

	/** Enable/disable local loopback (for pipeline latency testing) */
	setLoopback(enabled: boolean): void {
		this.loopbackEnabled = enabled;
		if (this.worker) {
			this.worker.postMessage({ type: 'loopback', enabled });
		}
	}

	/** Get transport packet counters for diagnostics */
	getStats(): {
		packetsSent: number;
		packetsReceived: number;
		seqGaps: number;
		seqReordered: number;
	} {
		return {
			packetsSent: this.sendSequence,
			packetsReceived: this.incomingCount,
			seqGaps: this.seqGaps,
			seqReordered: this.seqReordered
		};
	}

	/** Read from capture ring buffer, packetize, and send (fallback path) */
	private drainCapture(): void {
		while (this.captureReader.availableRead() >= SAMPLES_PER_FRAME) {
			const read = this.captureReader.read(this.readBuffer);
			if (read < SAMPLES_PER_FRAME) break;

			// When muted, still drain the buffer but don't send over network
			if (!this.micMuted) {
				if (this.codecMode === 'opus' && this.opusEnc) {
					this.opusEnc.encode(this.readBuffer.slice());
					this.sendSequence++;
				} else {
					const packet: AudioPacket = {
						sequence: this.sendSequence++,
						timestamp: this.sendTimestamp,
						samples: this.readBuffer
					};
					this.transport.sendDatagram(serializePacket(packet));
				}
				this.sendTimestamp += SAMPLES_PER_FRAME;
			} else {
				this.sendTimestamp += SAMPLES_PER_FRAME;
			}

			if (this.loopbackEnabled) {
				this.playbackWriter.write(this.readBuffer);
			}
		}
	}

	/** Track sequence continuity: gaps mean the server dropped mixed
	 *  frames or the network lost packets — both audible as glitches. */
	private trackSequence(sequence: number): void {
		if (this.lastRecvSequence >= 0) {
			const delta = sequence - this.lastRecvSequence;
			if (delta > 1) {
				this.seqGaps += delta - 1;
			} else if (delta <= 0) {
				this.seqReordered++;
			}
		}
		if (sequence > this.lastRecvSequence) {
			this.lastRecvSequence = sequence;
		}
	}

	/** Handle incoming datagram: stateless per-packet demux by size —
	 *  exactly one PCM packet is PCM, a shorter framed datagram is Opus —
	 *  so server codec switches can never garble the stream. */
	private handleIncoming(data: Uint8Array): void {
		if (data.byteLength !== AUDIO_PACKET_SIZE) {
			this.handleIncomingOpus(data);
			return;
		}

		const packet = deserializePacket(data);
		if (!packet) return;
		this.trackSequence(packet.sequence);

		// While the loopback test runs, it owns the playback ring: mixing in
		// network audio (e.g. the solo-room hold music) would double the
		// producer rate, pin the ring full, and garble both streams. Packet
		// and sequence stats still run so counters stay truthful.
		if (!this.loopbackEnabled) {
			this.playbackWriter.write(packet.samples);
		}

		// Periodic diagnostic: log every ~1s to confirm data is flowing
		this.incomingCount++;
		if (this.incomingCount === 1 || this.incomingCount % 375 === 0) {
			console.log(`[TransportBridge] Received ${this.incomingCount} packets, latest seq=${packet.sequence}, ${data.byteLength} bytes`);
		}
	}

	/** Opus datagram: 8B header (u32 seq, u32 timestamp-in-samples) + payload */
	private handleIncomingOpus(data: Uint8Array): void {
		if (
			data.byteLength <= AUDIO_HEADER_SIZE ||
			data.byteLength > AUDIO_HEADER_SIZE + OPUS_MAX_PAYLOAD
		) {
			return;
		}
		const dv = new DataView(data.buffer, data.byteOffset);
		const sequence = dv.getUint32(0, true);
		const timestamp = dv.getUint32(4, true);
		this.trackSequence(sequence);

		if (!this.opusDec) {
			this.opusDec = new OpusDecoderPipe((samples) => {
				if (!this.loopbackEnabled) {
					this.playbackWriter.write(samples);
				}
			});
		}
		// slice() detaches from the transport's buffer (EncodedAudioChunk copies,
		// but subarray views over reused receive buffers are risky cross-task)
		this.opusDec.decode(timestamp, data.slice(AUDIO_HEADER_SIZE));
		this.incomingCount++;
	}
}
