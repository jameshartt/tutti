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
const STATS_INTERVAL = 75; // ~200ms at 128 samples/frame @ 48kHz

interface InitMessage {
	type: 'init';
	ringBufferSAB: SharedArrayBuffer;
}

interface ConfigMessage {
	type: 'config';
	prebufferFrames: number;
}

interface DetectTestMessage {
	type: 'detect-test';
}

interface StopDetectTestMessage {
	type: 'stop-detect-test';
}

class PlaybackProcessor extends AudioWorkletProcessor {
	private pointers: Int32Array | null = null;
	private data: Int16Array | null = null;
	private capacity = 0;
	private prebuffering = true;
	private prebufferFrames = 0;
	private tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

	// Diagnostics counters (lightweight integers, no allocations)
	private underrunCount = 0;
	private partialFrameCount = 0;
	private totalFrames = 0;
	private currentFillLevel = 0;
	private statsFrameCounter = 0;

	// Loopback test detection (pre-allocated for zero-allocation on audio thread)
	private detectTest = false;
	private prevTail = new Int16Array(96); // last 96 samples of previous frame
	private scanBuffer = new Int16Array(96 + SAMPLES_PER_FRAME); // [prevTail | current]

	constructor() {
		super();
		this.port.onmessage = (event: MessageEvent) => {
			const msg = event.data as InitMessage | ConfigMessage | DetectTestMessage | StopDetectTestMessage;
			if (msg.type === 'init') {
				this.initRingBuffer((msg as InitMessage).ringBufferSAB);
			} else if (msg.type === 'config') {
				this.prebufferFrames = (msg as ConfigMessage).prebufferFrames;
				this.prebuffering = true;
			} else if (msg.type === 'detect-test') {
				this.detectTest = true;
				this.prevTail.fill(0);
			} else if (msg.type === 'stop-detect-test') {
				this.detectTest = false;
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
			if (available < SAMPLES_PER_FRAME * this.prebufferFrames) {
				outChannel.fill(0);
				this.reportStats();
				return true;
			}
			this.prebuffering = false;

			// Skip-ahead: during startup, data may have accumulated while
			// waiting for the first process() call. Advance the read pointer
			// to keep only the configured cushion, discarding stale samples
			// that would otherwise add permanent buffer latency.
			const targetFill = SAMPLES_PER_FRAME * Math.max(1, this.prebufferFrames);
			if (available > targetFill) {
				const skip = available - targetFill;
				Atomics.store(this.pointers!, 1, (read + skip) % this.capacity);
				this.currentFillLevel = targetFill;
			}
		}

		// Re-read after potential skip-ahead
		const currentWrite = Atomics.load(this.pointers, 0);
		const currentRead = Atomics.load(this.pointers, 1);
		const currentAvailable = (currentWrite - currentRead + this.capacity) % this.capacity;
		const toRead = Math.min(outChannel.length, currentAvailable);

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

		const readPos = Atomics.load(this.pointers, 1);
		const firstChunk = Math.min(toRead, this.capacity - readPos);

		// Read Int16 samples
		this.tempBuffer.set(this.data.subarray(readPos, readPos + firstChunk));
		if (toRead > firstChunk) {
			this.tempBuffer.set(this.data.subarray(0, toRead - firstChunk), firstChunk);
		}

		Atomics.store(this.pointers, 1, (readPos + toRead) % this.capacity);

		// Convert Int16 [-32768, 32767] to Float32 [-1, 1]
		const scale = 1.0 / 32768.0;
		for (let i = 0; i < toRead; i++) {
			outChannel[i] = this.tempBuffer[i] * scale;
		}

		// Zero-fill remainder if we read fewer samples than needed
		for (let i = toRead; i < outChannel.length; i++) {
			outChannel[i] = 0;
		}

		// Loopback test: scan for 3-pulse pattern at any offset,
		// using a sliding window across frame boundaries so detection
		// works regardless of ring buffer read alignment.
		if (this.detectTest) {
			const THRESHOLD = 30000; // near-max Int16

			// Build search window: [previous tail (96) | current frame (toRead)]
			this.scanBuffer.set(this.prevTail);
			this.scanBuffer.set(this.tempBuffer.subarray(0, toRead), 96);
			const scanLen = 96 + toRead;

			for (let i = 0; i <= scanLen - 97; i++) {
				if (
					this.scanBuffer[i] >= THRESHOLD &&
					this.scanBuffer[i + 48] >= THRESHOLD &&
					this.scanBuffer[i + 96] >= THRESHOLD
				) {
					this.port.postMessage({ type: 'test-detected' });
					this.detectTest = false;
					break;
				}
			}

			// Save tail for cross-frame detection on next callback
			if (this.detectTest) {
				if (toRead >= 96) {
					this.prevTail.set(this.tempBuffer.subarray(toRead - 96, toRead));
				} else {
					this.prevTail.copyWithin(0, toRead);
					this.prevTail.set(this.tempBuffer.subarray(0, toRead), 96 - toRead);
				}
			}
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
