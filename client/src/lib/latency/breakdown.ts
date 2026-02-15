/**
 * Derived latency breakdown store.
 *
 * Recomputes the full latency breakdown whenever audio stats change,
 * combining hardware latency, buffer latency, network RTT, and settings.
 */

import { derived } from 'svelte/store';
import { audioStats } from '../stores/audio-stats.js';
import { settings } from '../stores/settings.js';
import { computeLatencyBreakdown, computeLatencyInfo } from '../audio/latency.js';
import type { LatencyBreakdown, LatencyInfo } from '../audio/types.js';

/** Server processing estimate for 2-participant fast path */
const SERVER_PROCESSING_MS = 0.1;

export const latencyBreakdown = derived(
	[audioStats, settings],
	([$stats, $settings]): { breakdown: LatencyBreakdown; info: LatencyInfo } | null => {
		// Don't compute until we have at least one RTT sample
		if ($stats.networkRTT === 0) return null;

		const breakdown = computeLatencyBreakdown(
			$stats.networkRTT,
			SERVER_PROCESSING_MS,
			$settings.fudgeFactorMs
		);

		const info = computeLatencyInfo(breakdown.totalMs);

		return { breakdown, info };
	}
);
