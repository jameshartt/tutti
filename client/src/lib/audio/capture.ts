/**
 * Audio capture pipeline.
 *
 * Sets up: getUserMedia → MediaStreamSource → AudioWorklet (capture-processor)
 * The capture worklet writes Int16 PCM to a SharedArrayBuffer ring buffer.
 * The TransportBridge reads from this ring buffer and sends over the network.
 */

import { getAudioContext, getMicrophoneStream } from './context.js';
import { createRingBufferSAB } from './ring-buffer.js';
import { SAMPLES_PER_FRAME } from './types.js';

// Ring buffer capacity: ~100ms of audio at 48kHz
const RING_BUFFER_CAPACITY = SAMPLES_PER_FRAME * 64; // 8192 samples ≈ 170ms

export interface CaptureHandle {
	/** SharedArrayBuffer containing captured audio (Int16 PCM) */
	ringBufferSAB: SharedArrayBuffer;
	/** Stop capture and release resources */
	stop: () => void;
}

/**
 * Start capturing audio from the microphone.
 * Returns a handle with the ring buffer SAB and a stop function.
 */
export async function startCapture(): Promise<CaptureHandle> {
	const ctx = getAudioContext();

	// Register the capture worklet
	await ctx.audioWorklet.addModule('/worklets/capture-processor.js');

	// Get microphone stream
	const stream = await getMicrophoneStream();

	// Create ring buffer
	const ringBufferSAB = createRingBufferSAB(RING_BUFFER_CAPACITY);

	// Create worklet node
	const workletNode = new AudioWorkletNode(ctx, 'capture-processor', {
		numberOfInputs: 1,
		numberOfOutputs: 0,
		channelCount: 1,
		channelCountMode: 'explicit'
	});

	// Initialize worklet with ring buffer
	workletNode.port.postMessage({
		type: 'init',
		ringBufferSAB
	});

	// Connect microphone → worklet
	const source = ctx.createMediaStreamSource(stream);
	source.connect(workletNode);

	return {
		ringBufferSAB,
		stop() {
			source.disconnect();
			workletNode.disconnect();
			stream.getTracks().forEach((t) => t.stop());
		}
	};
}
