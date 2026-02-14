/**
 * AudioContext creation and microphone access.
 *
 * Creates a single AudioContext configured for minimum latency:
 * - 48kHz sample rate (or native rate on iOS to avoid resampling)
 * - Interactive latency hint
 * - All processing disabled (AEC, AGC, NS)
 */

import { SAMPLE_RATE } from './types.js';

let audioContext: AudioContext | null = null;

/** Get or create the shared AudioContext */
export function getAudioContext(): AudioContext {
	if (audioContext && audioContext.state !== 'closed') {
		return audioContext;
	}

	audioContext = new AudioContext({
		sampleRate: detectOptimalSampleRate(),
		latencyHint: 'interactive'
	});

	return audioContext;
}

/** Resume AudioContext (must be called from user gesture on iOS) */
export async function resumeAudioContext(): Promise<void> {
	const ctx = getAudioContext();
	if (ctx.state === 'suspended') {
		await ctx.resume();
	}
}

/** Close the AudioContext */
export async function closeAudioContext(): Promise<void> {
	if (audioContext) {
		await audioContext.close();
		audioContext = null;
	}
}

/**
 * Request microphone access with all processing disabled.
 * Returns a MediaStream for mono audio capture.
 */
export async function getMicrophoneStream(): Promise<MediaStream> {
	return navigator.mediaDevices.getUserMedia({
		audio: {
			echoCancellation: false,
			autoGainControl: false,
			noiseSuppression: false,
			channelCount: 1,
			sampleRate: detectOptimalSampleRate()
		},
		video: false
	});
}

/**
 * Get hardware latency info from the AudioContext.
 */
export function getHardwareLatency(): { inputMs: number; outputMs: number } {
	const ctx = getAudioContext();
	return {
		inputMs: (ctx.baseLatency ?? 0) * 1000,
		outputMs: ((ctx as AudioContext & { outputLatency?: number }).outputLatency ?? ctx.baseLatency ?? 0) * 1000
	};
}

/**
 * Detect optimal sample rate.
 * iOS devices often use 44.1kHz natively; forcing 48kHz causes resampling latency.
 */
function detectOptimalSampleRate(): number {
	if (isIOS()) {
		// Create a temporary context to check native rate
		try {
			const temp = new AudioContext();
			const nativeRate = temp.sampleRate;
			temp.close();
			if (nativeRate === 44100) {
				return 44100;
			}
		} catch {
			// Fall through to default
		}
	}
	return SAMPLE_RATE;
}

/** Detect iOS/iPadOS */
function isIOS(): boolean {
	return /iPad|iPhone|iPod/.test(navigator.userAgent) ||
		(navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
}
