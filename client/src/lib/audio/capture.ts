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

// Ring buffer capacity: ~21ms of audio at 48kHz (8 frames)
const RING_BUFFER_CAPACITY = SAMPLES_PER_FRAME * 8; // 1024 samples ≈ 21ms

export interface CaptureHandle {
	/** SharedArrayBuffer containing captured audio (Int16 PCM) */
	ringBufferSAB: SharedArrayBuffer;
	/** MessagePort for receiving frame-ready notifications from the capture worklet */
	capturePort: MessagePort;
	/**
	 * Hot-swap the input device without touching the worklet, ring buffer
	 * or transport — the new source is connected before the old one is
	 * dropped, so the network stream never stalls. null = system default.
	 */
	switchDevice: (deviceId: string | null) => Promise<void>;
	/** Called when the live mic track ends (device unplugged) */
	onTrackEnded: (handler: () => void) => void;
	/** Stop capture and release resources */
	stop: () => void;
}

/**
 * Start capturing audio from the microphone.
 * Returns a handle with the ring buffer SAB and a stop function.
 */
export async function startCapture(deviceId: string | null = null): Promise<CaptureHandle> {
	const ctx = getAudioContext();

	// Register the capture worklet
	// Version query busts browser caches (see playback.ts)
	await ctx.audioWorklet.addModule('/worklets/capture-processor.js?v=2');

	// Get microphone stream
	let stream = await getMicrophoneStream(deviceId);

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
	let source = ctx.createMediaStreamSource(stream);
	source.connect(workletNode);

	let stopped = false;
	let endedHandler: (() => void) | null = null;
	const watchTrack = (s: MediaStream) => {
		const track = s.getAudioTracks()[0];
		if (!track) return;
		track.addEventListener('ended', () => {
			// Only the live stream's track matters; swapped-out tracks are
			// stopped by us and must not trigger a recovery
			if (!stopped && s === stream) endedHandler?.();
		});
	};
	watchTrack(stream);

	// Serialise swaps so rapid picks can't interleave getUserMedia calls
	let swapChain: Promise<void> = Promise.resolve();

	return {
		ringBufferSAB,
		capturePort: workletNode.port,
		switchDevice(nextId: string | null) {
			const run = async () => {
				if (stopped) return;
				const nextStream = await getMicrophoneStream(nextId);
				if (stopped) {
					nextStream.getTracks().forEach((t) => t.stop());
					return;
				}
				const nextSource = ctx.createMediaStreamSource(nextStream);
				nextSource.connect(workletNode);
				const oldStream = stream;
				source.disconnect();
				source = nextSource;
				stream = nextStream;
				oldStream.getTracks().forEach((t) => t.stop());
				watchTrack(nextStream);
			};
			const p = swapChain.then(run);
			swapChain = p.catch(() => {});
			return p;
		},
		onTrackEnded(handler: () => void) {
			endedHandler = handler;
		},
		stop() {
			stopped = true;
			source.disconnect();
			workletNode.disconnect();
			stream.getTracks().forEach((t) => t.stop());
		}
	};
}
