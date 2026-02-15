/**
 * RTT Monitor — measures network round-trip time via ping/pong messages.
 *
 * Sends periodic pings over the reliable transport channel and tracks
 * response times using the existing LatencyMeasurer EWMA smoothing.
 */

import { writable } from 'svelte/store';
import type { Transport } from '../transport/transport.js';
import { LatencyMeasurer } from '../audio/latency.js';
import { updateNetworkRTT } from '../stores/audio-stats.js';

export interface RTTStats {
	/** Most recent RTT sample in ms */
	currentRTT: number;
	/** EWMA-smoothed average RTT in ms */
	averageRTT: number;
	/** Estimated one-way latency (RTT/2) in ms */
	oneWayMs: number;
	/** Number of samples received */
	samples: number;
}

const PING_INTERVAL_MS = 2000;

export const rttStats = writable<RTTStats>({
	currentRTT: 0,
	averageRTT: 0,
	oneWayMs: 0,
	samples: 0
});

export class RTTMonitor {
	private measurer = new LatencyMeasurer();
	private timer: ReturnType<typeof setInterval> | null = null;
	private sampleCount = 0;

	constructor(private transport: Transport) {}

	start(): void {
		this.timer = setInterval(() => this.sendPing(), PING_INTERVAL_MS);
		// Send first ping immediately
		this.sendPing();
	}

	stop(): void {
		if (this.timer) {
			clearInterval(this.timer);
			this.timer = null;
		}
	}

	/** Call this when a pong message is received from the server */
	handlePong(msg: { id: number }): void {
		const rtt = this.measurer.handlePong(msg.id);
		if (rtt === null) return;

		this.sampleCount++;
		const avg = this.measurer.rttMs;
		const oneWay = avg / 2;

		rttStats.set({
			currentRTT: rtt,
			averageRTT: avg,
			oneWayMs: oneWay,
			samples: this.sampleCount
		});

		updateNetworkRTT(avg, oneWay);
	}

	private sendPing(): void {
		if (this.transport.state !== 'connected') return;

		const ping = this.measurer.createPing();
		this.transport.sendReliable(
			JSON.stringify({ type: 'ping', id: ping.id, t: ping.t })
		);
	}
}
