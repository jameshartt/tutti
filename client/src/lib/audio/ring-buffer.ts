/**
 * SPSC Ring Buffer backed by SharedArrayBuffer.
 *
 * Based on ringbuf.js by Paul Adenot (Mozilla).
 * Zero-allocation on the audio thread - critical for glitch-free AudioWorklet communication.
 *
 * Layout in SharedArrayBuffer:
 *   [0..3]   write pointer (Uint32, atomic)
 *   [4..7]   read pointer (Uint32, atomic)
 *   [8..]    ring buffer data
 *
 * Producer (capture worklet) writes samples and advances write pointer.
 * Consumer (transport bridge or playback worklet) reads samples and advances read pointer.
 */

const HEADER_SIZE = 8; // 2 × Uint32 for read/write pointers

/**
 * Create a SharedArrayBuffer for an SPSC ring buffer.
 * @param capacity Number of Int16 samples the ring buffer can hold
 */
export function createRingBufferSAB(capacity: number): SharedArrayBuffer {
	// Each sample is 2 bytes (Int16) + 8 bytes header
	return new SharedArrayBuffer(HEADER_SIZE + capacity * 2);
}

/**
 * Writer side of the SPSC ring buffer.
 * Used in the capture AudioWorklet (producer) and transport bridge (producer for playback).
 */
export class RingBufferWriter {
	private readonly pointers: Int32Array;
	private readonly data: Int16Array;
	private readonly capacity: number;

	constructor(sab: SharedArrayBuffer) {
		this.pointers = new Int32Array(sab, 0, 2); // [write_ptr, read_ptr]
		this.data = new Int16Array(sab, HEADER_SIZE);
		this.capacity = this.data.length;
	}

	/** Number of samples available to write */
	availableWrite(): number {
		const write = Atomics.load(this.pointers, 0);
		const read = Atomics.load(this.pointers, 1);
		// Leave one slot empty to distinguish full from empty
		return (this.capacity - 1) - ((write - read + this.capacity) % this.capacity);
	}

	/**
	 * Write samples to the ring buffer.
	 * @returns Number of samples actually written
	 */
	write(samples: Int16Array): number {
		const available = this.availableWrite();
		const toWrite = Math.min(samples.length, available);
		if (toWrite === 0) return 0;

		const write = Atomics.load(this.pointers, 0);

		// Copy in up to two chunks (wrap around)
		const firstChunk = Math.min(toWrite, this.capacity - write);
		this.data.set(samples.subarray(0, firstChunk), write);

		if (toWrite > firstChunk) {
			this.data.set(samples.subarray(firstChunk, toWrite), 0);
		}

		// Advance write pointer (atomic store for visibility to reader)
		Atomics.store(this.pointers, 0, (write + toWrite) % this.capacity);
		return toWrite;
	}
}

/**
 * Reader side of the SPSC ring buffer.
 * Used in the playback AudioWorklet (consumer) and transport bridge (consumer for capture).
 */
export class RingBufferReader {
	private readonly pointers: Int32Array;
	private readonly data: Int16Array;
	private readonly capacity: number;

	constructor(sab: SharedArrayBuffer) {
		this.pointers = new Int32Array(sab, 0, 2);
		this.data = new Int16Array(sab, HEADER_SIZE);
		this.capacity = this.data.length;
	}

	/** Number of samples available to read */
	availableRead(): number {
		const write = Atomics.load(this.pointers, 0);
		const read = Atomics.load(this.pointers, 1);
		return (write - read + this.capacity) % this.capacity;
	}

	/**
	 * Read samples from the ring buffer into destination.
	 * @returns Number of samples actually read
	 */
	read(dest: Int16Array): number {
		const available = this.availableRead();
		const toRead = Math.min(dest.length, available);
		if (toRead === 0) return 0;

		const read = Atomics.load(this.pointers, 1);

		// Copy in up to two chunks (wrap around)
		const firstChunk = Math.min(toRead, this.capacity - read);
		dest.set(this.data.subarray(read, read + firstChunk));

		if (toRead > firstChunk) {
			dest.set(this.data.subarray(0, toRead - firstChunk), firstChunk);
		}

		// Advance read pointer
		Atomics.store(this.pointers, 1, (read + toRead) % this.capacity);
		return toRead;
	}
}
