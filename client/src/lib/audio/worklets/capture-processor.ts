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

// Constants duplicated here because AudioWorklet has no module imports
const SAMPLES_PER_FRAME = 128;
const STATS_INTERVAL = 75; // ~200ms at 128 samples/frame @ 48kHz

/** Message types sent to/from the worklet */
interface InitMessage {
	type: 'init';
	ringBufferSAB: SharedArrayBuffer;
}

class CaptureProcessor extends AudioWorkletProcessor {
	private pointers: Int32Array | null = null;
	private data: Int16Array | null = null;
	private capacity = 0;
	private tempBuffer = new Int16Array(SAMPLES_PER_FRAME);

	// Diagnostics counters (lightweight integers, no allocations)
	private droppedFrames = 0;
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
		// Layout: [write_ptr(4), read_ptr(4), data...]
		this.pointers = new Int32Array(sab, 0, 2);
		this.data = new Int16Array(sab, 8);
		this.capacity = this.data.length;
	}

	process(inputs: Float32Array[][], _outputs: Float32Array[][], _parameters: Record<string, Float32Array>): boolean {
		if (!this.pointers || !this.data) return true;

		const input = inputs[0];
		if (!input || !input[0]) return true;

		const samples = input[0]; // Mono channel 0
		this.totalFrames++;

		// Convert Float32 [-1, 1] to Int16 [-32768, 32767]
		for (let i = 0; i < samples.length; i++) {
			const s = samples[i];
			// Clamp and convert
			this.tempBuffer[i] = s >= 1.0 ? 32767 : s <= -1.0 ? -32768 : (s * 32767) | 0;
		}

		// Write to ring buffer (inline for zero-allocation)
		const write = Atomics.load(this.pointers, 0);
		const read = Atomics.load(this.pointers, 1);
		const available = (this.capacity - 1) - ((write - read + this.capacity) % this.capacity);
		this.currentFillLevel = available;

		const toWrite = Math.min(samples.length, available);
		if (toWrite === 0) {
			// Buffer full - drop frame
			this.droppedFrames++;
			this.reportStats();
			return true;
		}

		const firstChunk = Math.min(toWrite, this.capacity - write);
		this.data.set(this.tempBuffer.subarray(0, firstChunk), write);

		if (toWrite > firstChunk) {
			this.data.set(this.tempBuffer.subarray(firstChunk, toWrite), 0);
		}

		Atomics.store(this.pointers, 0, (write + toWrite) % this.capacity);

		// Notify main thread that a frame is available for sending
		this.port.postMessage({ type: 'frame-ready' });

		this.reportStats();
		return true; // Keep processor alive
	}

	private reportStats(): void {
		this.statsFrameCounter++;
		if (this.statsFrameCounter >= STATS_INTERVAL) {
			this.statsFrameCounter = 0;
			this.port.postMessage({
				type: 'stats',
				droppedFrames: this.droppedFrames,
				totalFrames: this.totalFrames,
				fillLevel: this.currentFillLevel
			});
		}
	}
}

registerProcessor('capture-processor', CaptureProcessor);
