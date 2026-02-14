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
}

export class TransportBridge {
	private captureReader: RingBufferReader;
	private playbackWriter: RingBufferWriter;
	private transport: Transport;
	private running = false;
	private pollTimer: ReturnType<typeof setInterval> | null = null;
	private sendSequence = 0;
	private sendTimestamp = 0;

	// Pre-allocated buffers for zero-allocation on hot path
	private readBuffer = new Int16Array(SAMPLES_PER_FRAME);

	constructor(options: TransportBridgeOptions) {
		this.captureReader = new RingBufferReader(options.captureRingBufferSAB);
		this.playbackWriter = new RingBufferWriter(options.playbackRingBufferSAB);
		this.transport = options.transport;
	}

	/** Start the bridge: begin polling capture buffer and listening for incoming data */
	start(): void {
		if (this.running) return;
		this.running = true;

		// Listen for incoming datagrams
		this.transport.onDatagram((data) => {
			this.handleIncoming(data);
		});

		// Poll capture ring buffer at ~2.67ms intervals (matching audio quantum)
		// Using setInterval is imprecise but sufficient for main thread polling.
		// The real timing is governed by the AudioWorklet's render quantum.
		this.pollTimer = setInterval(() => {
			this.drainCapture();
		}, 2);
	}

	/** Stop the bridge */
	stop(): void {
		this.running = false;
		if (this.pollTimer) {
			clearInterval(this.pollTimer);
			this.pollTimer = null;
		}
	}

	/** Read from capture ring buffer, packetize, and send */
	private drainCapture(): void {
		// Read one frame at a time
		while (this.captureReader.availableRead() >= SAMPLES_PER_FRAME) {
			const read = this.captureReader.read(this.readBuffer);
			if (read < SAMPLES_PER_FRAME) break;

			const packet: AudioPacket = {
				sequence: this.sendSequence++,
				timestamp: this.sendTimestamp,
				samples: this.readBuffer
			};
			this.sendTimestamp += SAMPLES_PER_FRAME;

			const data = serializePacket(packet);
			this.transport.sendDatagram(data);
		}
	}

	/** Handle incoming datagram: deserialize and write to playback ring buffer */
	private handleIncoming(data: Uint8Array): void {
		const packet = deserializePacket(data);
		if (!packet) return;

		// Write received samples to playback ring buffer
		this.playbackWriter.write(packet.samples);
	}
}
