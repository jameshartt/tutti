/**
 * Latency measurement and acoustic distance calculation.
 *
 * Converts total one-way latency to equivalent sound-travel distance
 * using speed of sound = 343 m/s.
 */

import { SAMPLES_PER_FRAME, SAMPLE_RATE } from './types.js';
import type { LatencyInfo, LatencyBreakdown, LatencyGradation } from './types.js';
import { getHardwareLatency } from './context.js';

/** Speed of sound in m/s */
const SPEED_OF_SOUND = 343;

/** One render quantum duration in ms */
const QUANTUM_MS = (SAMPLES_PER_FRAME / SAMPLE_RATE) * 1000; // ~2.67ms

/** Acoustic distance gradation thresholds */
const GRADATIONS: Array<{
	maxMs: number;
	gradation: LatencyGradation;
	label: string;
	colour: 'green' | 'amber' | 'red';
}> = [
	{ maxMs: 6, gradation: 'side-by-side', label: 'Side by Side', colour: 'green' },
	{ maxMs: 20, gradation: 'small-room', label: 'Small Room', colour: 'green' },
	{ maxMs: 44, gradation: 'large-room', label: 'Large Room', colour: 'amber' },
	{ maxMs: 117, gradation: 'hall', label: 'Hall', colour: 'red' },
	{ maxMs: Infinity, gradation: 'stadium', label: 'Stadium', colour: 'red' }
];

/** Convert total one-way latency to LatencyInfo */
export function computeLatencyInfo(totalMs: number): LatencyInfo {
	const distanceMetres = (totalMs / 1000) * SPEED_OF_SOUND;
	const grad = GRADATIONS.find((g) => totalMs < g.maxMs) ?? GRADATIONS[GRADATIONS.length - 1];

	return {
		totalMs,
		distanceMetres,
		gradation: grad.gradation,
		label: grad.label,
		colour: grad.colour
	};
}

/** Compute full latency breakdown for nerd view */
export function computeLatencyBreakdown(
	networkRttMs: number,
	serverProcessingMs: number,
	fudgeFactorMs: number = 0
): LatencyBreakdown {
	const hw = getHardwareLatency();

	const breakdown: LatencyBreakdown = {
		hardwareInputMs: hw.inputMs,
		hardwareOutputMs: hw.outputMs,
		captureBufferMs: QUANTUM_MS,
		playbackBufferMs: QUANTUM_MS,
		networkOneWayMs: networkRttMs / 2,
		serverProcessingMs,
		fudgeFactorMs,
		totalMs: 0
	};

	breakdown.totalMs =
		breakdown.hardwareInputMs +
		breakdown.hardwareOutputMs +
		breakdown.captureBufferMs +
		breakdown.playbackBufferMs +
		breakdown.networkOneWayMs +
		breakdown.serverProcessingMs +
		breakdown.fudgeFactorMs;

	return breakdown;
}

/** Latency ping/pong tracker for measuring RTT */
export class LatencyMeasurer {
	private pendingPings = new Map<number, number>();
	private nextPingId = 0;
	private rttEwma = 0;
	private jitterEwma = 0;
	private readonly alpha = 0.125;

	/** Create a ping message. Returns [pingId, timestamp]. */
	createPing(): { id: number; t: number } {
		const id = this.nextPingId++;
		const t = performance.now();
		this.pendingPings.set(id, t);
		return { id, t };
	}

	/** Handle a pong response. Returns measured RTT in ms, or null. */
	handlePong(pingId: number): number | null {
		const sentAt = this.pendingPings.get(pingId);
		if (sentAt === undefined) return null;

		this.pendingPings.delete(pingId);
		const rtt = performance.now() - sentAt;

		// Update EWMA
		if (this.rttEwma === 0) {
			this.rttEwma = rtt;
		} else {
			const diff = Math.abs(rtt - this.rttEwma);
			this.jitterEwma = (1 - this.alpha) * this.jitterEwma + this.alpha * diff;
			this.rttEwma = (1 - this.alpha) * this.rttEwma + this.alpha * rtt;
		}

		// Clean up old pings (> 5 seconds)
		const now = performance.now();
		for (const [id, t] of this.pendingPings) {
			if (now - t > 5000) {
				this.pendingPings.delete(id);
			}
		}

		return rtt;
	}

	/** Get smoothed RTT in ms */
	get rttMs(): number {
		return this.rttEwma;
	}

	/** Get RTT jitter in ms */
	get jitterMs(): number {
		return this.jitterEwma;
	}
}
