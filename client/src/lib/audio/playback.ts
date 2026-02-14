/**
 * Audio playback pipeline.
 *
 * Sets up: AudioWorklet (playback-processor) → AudioContext destination
 * The TransportBridge writes received network audio (Int16 PCM) into
 * a SharedArrayBuffer ring buffer. The playback worklet reads from it.
 */

import { getAudioContext } from './context.js';
import { createRingBufferSAB } from './ring-buffer.js';
import { SAMPLES_PER_FRAME } from './types.js';

// Ring buffer capacity: ~100ms of audio at 48kHz
const RING_BUFFER_CAPACITY = SAMPLES_PER_FRAME * 64; // 8192 samples ≈ 170ms

export interface PlaybackHandle {
	/** SharedArrayBuffer for writing received audio (Int16 PCM) */
	ringBufferSAB: SharedArrayBuffer;
	/** Stop playback and release resources */
	stop: () => void;
}

/**
 * Start audio playback.
 * Returns a handle with the ring buffer SAB and a stop function.
 */
export async function startPlayback(): Promise<PlaybackHandle> {
	const ctx = getAudioContext();

	// Register the playback worklet
	await ctx.audioWorklet.addModule('/worklets/playback-processor.js');

	// Create ring buffer
	const ringBufferSAB = createRingBufferSAB(RING_BUFFER_CAPACITY);

	// Create worklet node
	const workletNode = new AudioWorkletNode(ctx, 'playback-processor', {
		numberOfInputs: 0,
		numberOfOutputs: 1,
		channelCount: 1,
		channelCountMode: 'explicit'
	});

	// Initialize worklet with ring buffer
	workletNode.port.postMessage({
		type: 'init',
		ringBufferSAB
	});

	// Connect worklet → speakers
	workletNode.connect(ctx.destination);

	return {
		ringBufferSAB,
		stop() {
			workletNode.disconnect();
		}
	};
}
