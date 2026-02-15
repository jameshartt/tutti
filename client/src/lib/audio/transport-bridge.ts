/**
 * Transport Bridge: connects the audio pipeline to the network transport.
 *
 * Capture path:  Ring buffer (from capture worklet) → serialize → transport.send()
 * Playback path: transport.onReceive() → deserialize → ring buffer (to playback worklet)
 *
 * Runs on the main thread. Polls the capture ring buffer at the audio frame rate
 * and dispatches packets. Incoming packets are written directly to the playback
 * ring buffer.
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
	private running = false;
	private pollTimer: ReturnType<typeof setInterval> | null = null;
	private sendSequence = 0;
	private sendTimestamp = 0;
	private incomingCount = 0;
	private loopbackEnabled = false;

	// Pre-allocated buffers for zero-allocation on hot path
	private readBuffer = new Int16Array(SAMPLES_PER_FRAME);

	constructor(options: TransportBridgeOptions) {
		this.captureReader = new RingBufferReader(options.captureRingBufferSAB);
		this.playbackWriter = new RingBufferWriter(options.playbackRingBufferSAB);
		this.transport = options.transport;
		this.capturePort = options.capturePort ?? null;
	}

	/** Start the bridge: begin polling capture buffer and listening for incoming data */
	start(): void {
		if (this.running) return;
		this.running = true;

		// Listen for incoming datagrams
		this.transport.onDatagram((data) => {
			this.handleIncoming(data);
		});

		if (this.capturePort) {
			// Event-driven: capture worklet notifies us when a frame is written
			this.capturePort.onmessage = (event) => {
				if (event.data?.type === 'frame-ready') {
					this.drainCapture();
				}
			};
		} else {
			// Fallback: poll capture ring buffer at ~2.67ms intervals
			this.pollTimer = setInterval(() => {
				this.drainCapture();
			}, 2);
		}
	}

	/** Stop the bridge */
	stop(): void {
		this.running = false;
		if (this.capturePort) {
			this.capturePort.onmessage = null;
		}
		if (this.pollTimer) {
			clearInterval(this.pollTimer);
			this.pollTimer = null;
		}
	}

	/** Enable/disable local loopback (for pipeline latency testing) */
	setLoopback(enabled: boolean): void {
		this.loopbackEnabled = enabled;
	}

	/** Get transport packet counters for diagnostics */
	getStats(): { packetsSent: number; packetsReceived: number } {
		return {
			packetsSent: this.sendSequence,
			packetsReceived: this.incomingCount
		};
	}

	/** Read from capture ring buffer, packetize, and send */
	private drainCapture(): void {
		while (this.captureReader.availableRead() >= SAMPLES_PER_FRAME) {
			const read = this.captureReader.read(this.readBuffer);
			if (read < SAMPLES_PER_FRAME) break;

			const packet: AudioPacket = {
				sequence: this.sendSequence++,
				timestamp: this.sendTimestamp,
				samples: this.readBuffer
			};
			this.sendTimestamp += SAMPLES_PER_FRAME;

			// serializePacket returns a fresh Uint8Array each call —
			// required because datagramWriter.write() is async and may
			// hold the reference until the QUIC stack copies the data.
			this.transport.sendDatagram(serializePacket(packet));

			// Local loopback: write captured frame back to playback buffer
			// so the pipeline latency test can detect it without needing
			// the server to echo audio back to the sender.
			if (this.loopbackEnabled) {
				this.playbackWriter.write(this.readBuffer);
			}
		}
	}

	/** Handle incoming datagram: deserialize and write to playback ring buffer */
	private handleIncoming(data: Uint8Array): void {
		const packet = deserializePacket(data);
		if (!packet) return;

		// Write received samples to playback ring buffer
		this.playbackWriter.write(packet.samples);

		// Periodic diagnostic: log every ~1s to confirm data is flowing
		this.incomingCount++;
		if (this.incomingCount === 1 || this.incomingCount % 375 === 0) {
			console.log(`[TransportBridge] Received ${this.incomingCount} packets, latest seq=${packet.sequence}, ${data.byteLength} bytes`);
		}
	}
}
