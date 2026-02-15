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
 *
 * On Linux, baseLatency is near-zero and doesn't reflect real input device
 * latency. When that happens, we estimate input latency from outputLatency
 * since both directions go through the same audio stack (PipeWire/PulseAudio).
 * On macOS, baseLatency is a reasonable proxy (~3-5ms) so we use it directly.
 */
export function getHardwareLatency(): { inputMs: number; outputMs: number } {
	const ctx = getAudioContext();
	const baseMs = (ctx.baseLatency ?? 0) * 1000;
	const outputMs = ((ctx as AudioContext & { outputLatency?: number }).outputLatency ?? ctx.baseLatency ?? 0) * 1000;

	// If baseLatency is negligible (<1ms) but outputLatency is available,
	// the OS isn't reporting input latency (Linux). Use outputLatency as
	// the input estimate — input and output share the same audio stack.
	const inputMs = baseMs < 1 && outputMs > 1 ? outputMs : baseMs;

	return { inputMs, outputMs };
}

/**
 * Check if hardware output latency is high and return platform-specific
 * guidance for reducing it. Returns null when latency is acceptable.
 */
export function getLatencyWarning(): { message: string; instructions: string; command?: string } | null {
	const ctx = getAudioContext();
	const outputMs = ((ctx as AudioContext & { outputLatency?: number }).outputLatency ?? 0) * 1000;

	if (outputMs <= 15) return null;

	const isLinux = navigator.platform?.startsWith('Linux');

	if (isLinux) {
		return {
			message: `Hardware output latency is ${outputMs.toFixed(0)}ms — likely caused by PipeWire/PulseAudio defaulting to a large buffer (1024 samples).`,
			instructions: [
				'Try reducing the PipeWire quantum to 128 samples (~2.7ms at 48kHz):',
				'',
				'Immediate (until reboot):',
				'  pw-metadata -n settings 0 clock.force-quantum 128',
				'',
				'Persistent — create or edit ~/.config/pipewire/pipewire.conf.d/low-latency.conf:',
				'  context.properties = {',
				'    default.clock.quantum = 128',
				'    default.clock.min-quantum = 128',
				'  }'
			].join('\n'),
			command: 'pw-metadata -n settings 0 clock.force-quantum 128'
		};
	}

	return {
		message: `Hardware output latency is ${outputMs.toFixed(0)}ms — this is higher than expected.`,
		instructions: 'Check your OS audio settings and try reducing the audio buffer size. Using an ASIO or low-latency audio driver may help.'
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
