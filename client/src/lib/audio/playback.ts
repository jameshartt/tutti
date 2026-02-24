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

// Ring buffer capacity: ~21ms of audio at 48kHz (8 frames)
// Capacity controls jitter tolerance, not latency — the playback worklet's
// skip-ahead on prebuffer exit prevents accumulation from adding delay.
const RING_BUFFER_CAPACITY = SAMPLES_PER_FRAME * 8; // 1024 samples ≈ 21ms

export interface PlaybackHandle {
	/** SharedArrayBuffer for writing received audio (Int16 PCM) */
	ringBufferSAB: SharedArrayBuffer;
	/** MessagePort for receiving stats from the playback worklet */
	playbackPort: MessagePort;
	/** Send configuration to the playback worklet */
	sendConfig: (config: { prebufferFrames: number }) => void;
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

	// Create worklet node.
	// Use numberOfInputs: 1 so Safari/WebKit treats this as an active node.
	// Safari's audio graph scheduler skips process() for pure source nodes
	// (numberOfInputs: 0) that have no upstream active source.
	const workletNode = new AudioWorkletNode(ctx, 'playback-processor', {
		numberOfInputs: 1,
		numberOfOutputs: 1,
		channelCount: 1,
		channelCountMode: 'explicit'
	});

	// Connect a silent source to the worklet input to keep Safari's
	// audio graph scheduler calling process(). ConstantSourceNode with
	// offset 0 produces silence but counts as an active source.
	const keepAlive = ctx.createConstantSource();
	keepAlive.offset.value = 0;
	keepAlive.start();
	keepAlive.connect(workletNode);

	// Initialize worklet with ring buffer
	workletNode.port.postMessage({
		type: 'init',
		ringBufferSAB
	});

	// Connect worklet → speakers
	workletNode.connect(ctx.destination);

	console.log(`[Playback] Worklet connected to destination (sampleRate=${ctx.sampleRate}, state=${ctx.state})`);

	return {
		ringBufferSAB,
		playbackPort: workletNode.port,
		sendConfig(config: { prebufferFrames: number }) {
			workletNode.port.postMessage({ type: 'config', ...config });
		},
		stop() {
			keepAlive.stop();
			keepAlive.disconnect();
			workletNode.disconnect();
		}
	};
}
