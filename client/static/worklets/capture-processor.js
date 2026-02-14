/**
 * AudioWorklet processor for capturing microphone audio.
 *
 * Converts Float32 AudioWorklet samples to Int16 PCM and writes them
 * to a SharedArrayBuffer ring buffer. The main thread (TransportBridge)
 * reads from this ring buffer and sends packets over the network.
 *
 * This processor runs on the audio rendering thread - NO allocations,
 * NO blocking calls, NO exceptions on the hot path.
 */

const SAMPLES_PER_FRAME = 128;

class CaptureProcessor extends AudioWorkletProcessor {
	constructor() {
		super();
		this._pointers = null;
		this._data = null;
		this._capacity = 0;
		this._tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

		this.port.onmessage = (event) => {
			if (event.data.type === 'init') {
				const sab = event.data.ringBufferSAB;
				this._pointers = new Int32Array(sab, 0, 2);
				this._data = new Int16Array(sab, 8);
				this._capacity = this._data.length;
			}
		};
	}

	process(inputs, outputs, parameters) {
		if (!this._pointers || !this._data) return true;

		const input = inputs[0];
		if (!input || !input[0]) return true;

		const samples = input[0];

		// Convert Float32 [-1, 1] to Int16 [-32768, 32767]
		for (let i = 0; i < samples.length; i++) {
			const s = samples[i];
			this._tempBuffer[i] = s >= 1.0 ? 32767 : s <= -1.0 ? -32768 : (s * 32767) | 0;
		}

		// Write to ring buffer (inline for zero-allocation)
		const write = Atomics.load(this._pointers, 0);
		const read = Atomics.load(this._pointers, 1);
		const available = (this._capacity - 1) - ((write - read + this._capacity) % this._capacity);

		const toWrite = Math.min(samples.length, available);
		if (toWrite === 0) return true;

		const firstChunk = Math.min(toWrite, this._capacity - write);
		this._data.set(this._tempBuffer.subarray(0, firstChunk), write);

		if (toWrite > firstChunk) {
			this._data.set(this._tempBuffer.subarray(firstChunk, toWrite), 0);
		}

		Atomics.store(this._pointers, 0, (write + toWrite) % this._capacity);

		return true;
	}
}

registerProcessor('capture-processor', CaptureProcessor);
