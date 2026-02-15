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
const PREBUFFER_FRAMES = 1; // ~2.67ms at 48kHz
const STATS_INTERVAL = 75; // ~200ms at 128 samples/frame @ 48kHz

class PlaybackProcessor extends AudioWorkletProcessor {
	constructor() {
		super();
		this._pointers = null;
		this._data = null;
		this._capacity = 0;
		this._prebuffering = true;
		this._tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

		// Diagnostics counters (lightweight integers, no allocations)
		this._underrunCount = 0;
		this._partialFrameCount = 0;
		this._totalFrames = 0;
		this._currentFillLevel = 0;
		this._statsFrameCounter = 0;

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
		this._totalFrames++;

		// Read from ring buffer (inline for zero-allocation)
		const write = Atomics.load(this._pointers, 0);
		const read = Atomics.load(this._pointers, 1);
		const available = (write - read + this._capacity) % this._capacity;
		this._currentFillLevel = available;

		// Prebuffer: wait until enough data accumulates before starting
		// playback, so we have a cushion against timing jitter.
		if (this._prebuffering) {
			if (available < SAMPLES_PER_FRAME * PREBUFFER_FRAMES) {
				outChannel.fill(0);
				this._reportStats();
				return true;
			}
			this._prebuffering = false;

			// Skip-ahead: during startup, data may have accumulated while
			// waiting for the first process() call. Advance the read pointer
			// to keep only 1 frame of cushion, discarding stale samples that
			// would otherwise add permanent buffer latency.
			const targetFill = SAMPLES_PER_FRAME;
			if (available > targetFill) {
				const skip = available - targetFill;
				Atomics.store(this._pointers, 1, (read + skip) % this._capacity);
				// Update local state to reflect the skip
				this._currentFillLevel = targetFill;
			}
		}

		// Re-read after potential skip-ahead
		const currentWrite = Atomics.load(this._pointers, 0);
		const currentRead = Atomics.load(this._pointers, 1);
		const currentAvailable = (currentWrite - currentRead + this._capacity) % this._capacity;
		const toRead = Math.min(outChannel.length, currentAvailable);

		if (toRead === 0) {
			this._underrunCount++;
			outChannel.fill(0);
			this._reportStats();
			return true;
		}

		if (toRead < outChannel.length) {
			this._partialFrameCount++;
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

		this._reportStats();
		return true;
	}

	_reportStats() {
		this._statsFrameCounter++;
		if (this._statsFrameCounter >= STATS_INTERVAL) {
			this._statsFrameCounter = 0;
			this.port.postMessage({
				type: 'stats',
				underruns: this._underrunCount,
				partialFrames: this._partialFrameCount,
				totalFrames: this._totalFrames,
				fillLevel: this._currentFillLevel,
				prebuffering: this._prebuffering
			});
		}
	}
}

registerProcessor('playback-processor', PlaybackProcessor);
