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
const STATS_INTERVAL = 75; // ~200ms at 128 samples/frame @ 48kHz

class CaptureProcessor extends AudioWorkletProcessor {
	constructor() {
		super();
		this._pointers = null;
		this._data = null;
		this._capacity = 0;
		this._tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

		// Diagnostics counters (lightweight integers, no allocations)
		this._droppedFrames = 0;
		this._totalFrames = 0;
		this._currentFillLevel = 0;
		this._statsFrameCounter = 0;
		this._peakLevel = 0;

		// Input gain (mic boost): 1.0 = unity, up to 4.0 = +12dB
		this._inputGain = 1.0;

		// Loopback test injection
		this._injectTest = false;

		this.port.onmessage = (event) => {
			if (event.data.type === 'init') {
				const sab = event.data.ringBufferSAB;
				this._pointers = new Int32Array(sab, 0, 2);
				this._data = new Int16Array(sab, 8);
				this._capacity = this._data.length;
			} else if (event.data.type === 'inject-test') {
				this._injectTest = true;
			} else if (event.data.type === 'input-gain') {
				this._inputGain = event.data.gain;
			}
		};
	}

	process(inputs, outputs, parameters) {
		if (!this._pointers || !this._data) return true;

		const input = inputs[0];
		if (!input || !input[0]) return true;

		const samples = input[0];
		this._totalFrames++;

		// Track peak from raw input (pre-gain) for level meter
		for (let i = 0; i < samples.length; i++) {
			const abs = samples[i] < 0 ? -samples[i] : samples[i];
			if (abs > this._peakLevel) this._peakLevel = abs;
		}

		// Convert Float32 [-1, 1] to Int16 [-32768, 32767], applying input gain
		for (let i = 0; i < samples.length; i++) {
			const s = samples[i] * this._inputGain;
			this._tempBuffer[i] = s >= 1.0 ? 32767 : s <= -1.0 ? -32768 : (s * 32767) | 0;
		}

		// Loopback test: replace frame with distinctive 3-pulse pattern
		if (this._injectTest) {
			this._injectTest = false;
			this._tempBuffer.fill(0);
			// 3 pulses at samples 0, 48, 96 — a ~2ms fingerprint
			this._tempBuffer[0] = 32767;
			this._tempBuffer[48] = 32767;
			this._tempBuffer[96] = 32767;
		}

		// Write to ring buffer (inline for zero-allocation)
		const write = Atomics.load(this._pointers, 0);
		const read = Atomics.load(this._pointers, 1);
		const available = (this._capacity - 1) - ((write - read + this._capacity) % this._capacity);
		this._currentFillLevel = available;

		const toWrite = Math.min(samples.length, available);
		if (toWrite === 0) {
			this._droppedFrames++;
			this._reportStats();
			return true;
		}

		const firstChunk = Math.min(toWrite, this._capacity - write);
		this._data.set(this._tempBuffer.subarray(0, firstChunk), write);

		if (toWrite > firstChunk) {
			this._data.set(this._tempBuffer.subarray(firstChunk, toWrite), 0);
		}

		Atomics.store(this._pointers, 0, (write + toWrite) % this._capacity);
		Atomics.notify(this._pointers, 0);

		// Notify main thread that a frame is available for sending
		this.port.postMessage({ type: 'frame-ready' });

		this._reportStats();
		return true;
	}

	_reportStats() {
		this._statsFrameCounter++;
		if (this._statsFrameCounter >= STATS_INTERVAL) {
			this._statsFrameCounter = 0;
			this.port.postMessage({
				type: 'stats',
				droppedFrames: this._droppedFrames,
				totalFrames: this._totalFrames,
				fillLevel: this._currentFillLevel,
				peakLevel: this._peakLevel
			});
			this._peakLevel = 0;
		}
	}
}

registerProcessor('capture-processor', CaptureProcessor);
