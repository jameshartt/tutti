/**
 * AudioWorklet processor for playing back received audio.
 *
 * Reads Int16 PCM samples from a SharedArrayBuffer ring buffer
 * (filled by TransportBridge from network data) and converts to
 * Float32 for the AudioWorklet output.
 *
 * This processor runs on the audio rendering thread - NO allocations,
 * NO blocking calls, NO exceptions on the hot path.
 */

const SAMPLES_PER_FRAME = 128;
// Prebuffer: accumulate this many frames before starting playback.
// Prevents phase-locked underruns where the read/write cadence aligns
// such that every other quantum finds an empty buffer.
const PREBUFFER_FRAMES = 2; // ~5ms at 48kHz

class PlaybackProcessor extends AudioWorkletProcessor {
	constructor() {
		super();
		this._pointers = null;
		this._data = null;
		this._capacity = 0;
		this._prebuffering = true;
		this._tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

		this.port.onmessage = (event) => {
			if (event.data.type === 'init') {
				const sab = event.data.ringBufferSAB;
				this._pointers = new Int32Array(sab, 0, 2);
				this._data = new Int16Array(sab, 8);
				this._capacity = this._data.length;
				this._prebuffering = true;
			}
		};
	}

	process(inputs, outputs, parameters) {
		if (!this._pointers || !this._data) return true;

		const output = outputs[0];
		if (!output || !output[0]) return true;

		const outChannel = output[0];

		// Read from ring buffer (inline for zero-allocation)
		const write = Atomics.load(this._pointers, 0);
		const read = Atomics.load(this._pointers, 1);
		const available = (write - read + this._capacity) % this._capacity;

		// Prebuffer: wait until enough data accumulates before starting
		// playback, so we have a cushion against timing jitter.
		if (this._prebuffering) {
			if (available < SAMPLES_PER_FRAME * PREBUFFER_FRAMES) {
				outChannel.fill(0);
				return true;
			}
			this._prebuffering = false;
		}

		const toRead = Math.min(outChannel.length, available);

		if (toRead === 0) {
			outChannel.fill(0);
			return true;
		}

		const firstChunk = Math.min(toRead, this._capacity - read);

		this._tempBuffer.set(this._data.subarray(read, read + firstChunk));
		if (toRead > firstChunk) {
			this._tempBuffer.set(this._data.subarray(0, toRead - firstChunk), firstChunk);
		}

		Atomics.store(this._pointers, 1, (read + toRead) % this._capacity);

		// Convert Int16 [-32768, 32767] to Float32 [-1, 1]
		const scale = 1.0 / 32768.0;
		for (let i = 0; i < toRead; i++) {
			outChannel[i] = this._tempBuffer[i] * scale;
		}

		for (let i = toRead; i < outChannel.length; i++) {
			outChannel[i] = 0;
		}

		return true;
	}
}

registerProcessor('playback-processor', PlaybackProcessor);
