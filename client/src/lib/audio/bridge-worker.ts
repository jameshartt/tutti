/**
 * Capture Bridge Worker.
 *
 * Reads captured audio from the SharedArrayBuffer ring buffer, serializes
 * packets, and posts them to the main thread for transmission. Uses
 * Atomics.waitAsync() to wake the instant the capture worklet writes new
 * data — no polling, no event-loop jitter.
 *
 * Messages received:
 *   init      { captureRingBufferSAB }  — set up reader, start poll loop
 *   mute      { muted: boolean }        — drain buffer but skip packet creation
 *   loopback  { enabled: boolean }      — also post raw samples back
 *   stop                                — exit poll loop
 *
 * Messages posted:
 *   packet    { data: ArrayBuffer }     — serialized 264-byte audio packet (transferred)
 *   loopback  { samples: ArrayBuffer }  — raw Int16 samples (transferred)
 */

import { RingBufferReader } from './ring-buffer.js';
import {
	SAMPLES_PER_FRAME,
	AUDIO_PACKET_SIZE,
	AUDIO_HEADER_SIZE,
	AUDIO_PAYLOAD_SIZE,
	type AudioPacket
} from './types.js';

let reader: RingBufferReader | null = null;
let pointers: Int32Array | null = null;
let running = false;
let muted = false;
let loopbackEnabled = false;
let sendSequence = 0;
let sendTimestamp = 0;

const readBuffer = new Int16Array(SAMPLES_PER_FRAME);

/** Serialize a packet into a fresh ArrayBuffer (transferable). */
function serializeTransferable(packet: AudioPacket): ArrayBuffer {
	const buf = new ArrayBuffer(AUDIO_PACKET_SIZE);
	const view = new DataView(buf);
	view.setUint32(0, packet.sequence, true);
	view.setUint32(4, packet.timestamp, true);
	new Uint8Array(buf).set(
		new Uint8Array(packet.samples.buffer, packet.samples.byteOffset, AUDIO_PAYLOAD_SIZE),
		AUDIO_HEADER_SIZE
	);
	return buf;
}

/** Drain all available frames from the capture ring buffer. */
function drainCapture(): void {
	if (!reader) return;

	while (reader.availableRead() >= SAMPLES_PER_FRAME) {
		const read = reader.read(readBuffer);
		if (read < SAMPLES_PER_FRAME) break;

		if (!muted) {
			const packet: AudioPacket = {
				sequence: sendSequence++,
				timestamp: sendTimestamp,
				samples: readBuffer
			};
			sendTimestamp += SAMPLES_PER_FRAME;

			const data = serializeTransferable(packet);
			self.postMessage({ type: 'packet', data }, [data]);
		} else {
			sendTimestamp += SAMPLES_PER_FRAME;
		}

		if (loopbackEnabled) {
			const copy = readBuffer.buffer.slice(0);
			self.postMessage({ type: 'loopback', samples: copy }, [copy]);
		}
	}
}

/** Main async loop using Atomics.waitAsync to sleep until the worklet writes. */
async function pollLoop(): Promise<void> {
	if (!pointers) return;

	while (running) {
		// Snapshot the current write pointer
		const wp = Atomics.load(pointers, 0);

		// Drain anything available right now
		drainCapture();

		if (!running) break;

		// Sleep until the worklet's Atomics.notify wakes us
		const result = Atomics.waitAsync(pointers, 0, wp);
		if (result.async) {
			await result.value;
		}
		// If result.async is false, the value already changed — loop immediately
	}
}

self.onmessage = (event: MessageEvent) => {
	const msg = event.data;

	switch (msg.type) {
		case 'init': {
			const sab = msg.captureRingBufferSAB as SharedArrayBuffer;
			reader = new RingBufferReader(sab);
			pointers = new Int32Array(sab, 0, 2);
			running = true;
			pollLoop();
			break;
		}
		case 'mute':
			muted = msg.muted;
			break;
		case 'loopback':
			loopbackEnabled = msg.enabled;
			break;
		case 'stop':
			running = false;
			break;
	}
};
