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

interface InitMessage {
	type: 'init';
	ringBufferSAB: SharedArrayBuffer;
}

class PlaybackProcessor extends AudioWorkletProcessor {
	private pointers: Int32Array | null = null;
	private data: Int16Array | null = null;
	private capacity = 0;
	private prebuffering = true;
	private tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

	// Diagnostics counters (lightweight integers, no allocations)
	private underrunCount = 0;
	private partialFrameCount = 0;
	private totalFrames = 0;
	private currentFillLevel = 0;
	private statsFrameCounter = 0;

	constructor() {
		super();
		this.port.onmessage = (event: MessageEvent) => {
			const msg = event.data;
			if (msg.type === 'init') {
				this.initRingBuffer(msg.ringBufferSAB);
			}
		};
	}

	private initRingBuffer(sab: SharedArrayBuffer): void {
		this.pointers = new Int32Array(sab, 0, 2);
		this.data = new Int16Array(sab, 8);
		this.capacity = this.data.length;
		this.prebuffering = true;
	}

	process(_inputs: Float32Array[][], outputs: Float32Array[][], _parameters: Record<string, Float32Array>): boolean {
		if (!this.pointers || !this.data) return true;

		const output = outputs[0];
		if (!output || !output[0]) return true;

		const outChannel = output[0]; // Mono channel 0
		this.totalFrames++;

		// Read from ring buffer (inline for zero-allocation)
		const write = Atomics.load(this.pointers, 0);
		const read = Atomics.load(this.pointers, 1);
		const available = (write - read + this.capacity) % this.capacity;
		this.currentFillLevel = available;

		// Prebuffer: wait until enough data accumulates before starting
		// playback, so we have a cushion against timing jitter.
		if (this.prebuffering) {
			if (available < SAMPLES_PER_FRAME * PREBUFFER_FRAMES) {
				outChannel.fill(0);
				this.reportStats();
				return true;
			}
			this.prebuffering = false;
		}

		const toRead = Math.min(outChannel.length, available);

		if (toRead === 0) {
			// Underrun - output silence
			this.underrunCount++;
			outChannel.fill(0);
			this.reportStats();
			return true;
		}

		if (toRead < outChannel.length) {
			this.partialFrameCount++;
		}

		const firstChunk = Math.min(toRead, this.capacity - read);

		// Read Int16 samples
		this.tempBuffer.set(this.data.subarray(read, read + firstChunk));
		if (toRead > firstChunk) {
			this.tempBuffer.set(this.data.subarray(0, toRead - firstChunk), firstChunk);
		}

		Atomics.store(this.pointers, 1, (read + toRead) % this.capacity);

		// Convert Int16 [-32768, 32767] to Float32 [-1, 1]
		const scale = 1.0 / 32768.0;
		for (let i = 0; i < toRead; i++) {
			outChannel[i] = this.tempBuffer[i] * scale;
		}

		// Zero-fill remainder if we read fewer samples than needed
		for (let i = toRead; i < outChannel.length; i++) {
			outChannel[i] = 0;
		}

		this.reportStats();
		return true; // Keep processor alive
	}

	private reportStats(): void {
		this.statsFrameCounter++;
		if (this.statsFrameCounter >= STATS_INTERVAL) {
			this.statsFrameCounter = 0;
			this.port.postMessage({
				type: 'stats',
				underruns: this.underrunCount,
				partialFrames: this.partialFrameCount,
				totalFrames: this.totalFrames,
				fillLevel: this.currentFillLevel,
				prebuffering: this.prebuffering
			});
		}
	}
}

registerProcessor('playback-processor', PlaybackProcessor);
