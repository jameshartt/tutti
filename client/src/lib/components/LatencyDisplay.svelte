<script lang="ts">
	import { settings } from '../stores/settings.js';
	import type { LatencyInfo, LatencyBreakdown } from '../audio/types.js';

	let { latency, breakdown }: { latency: LatencyInfo | null; breakdown: LatencyBreakdown | null } =
		$props();

	let nerdMode = $state(false);
	settings.subscribe((s) => (nerdMode = s.nerdMode));
</script>

{#if latency}
	<div class="latency-display">
		<div class="latency-label {latency.colour}">
			{latency.label}
		</div>
		<div class="latency-distance">
			~{latency.distanceMetres.toFixed(1)}m
		</div>

		{#if nerdMode && breakdown}
			<table class="latency-breakdown">
				<tbody>
					<tr>
						<td>Hardware input (est.)</td>
						<td>{breakdown.hardwareInputMs.toFixed(1)} ms</td>
					</tr>
					<tr>
						<td>Hardware output</td>
						<td>{breakdown.hardwareOutputMs.toFixed(1)} ms</td>
					</tr>
					<tr>
						<td>Capture buffer</td>
						<td>{breakdown.captureBufferMs.toFixed(1)} ms</td>
					</tr>
					<tr>
						<td>Playback buffer</td>
						<td>{breakdown.playbackBufferMs.toFixed(1)} ms</td>
					</tr>
					<tr>
						<td>Network (one-way)</td>
						<td>{breakdown.networkOneWayMs.toFixed(1)} ms</td>
					</tr>
					<tr>
						<td>Server processing</td>
						<td>{breakdown.serverProcessingMs.toFixed(2)} ms</td>
					</tr>
					{#if breakdown.fudgeFactorMs > 0}
						<tr>
							<td>Fudge factor</td>
							<td>{breakdown.fudgeFactorMs.toFixed(1)} ms</td>
						</tr>
					{/if}
					<tr class="total">
						<td>Total one-way</td>
						<td>{breakdown.totalMs.toFixed(1)} ms</td>
					</tr>
				</tbody>
			</table>
		{/if}
	</div>
{/if}

<style>
	.latency-display {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
	}

	.latency-label {
		font-size: 0.85rem;
		font-weight: 600;
		padding: 2px 6px;
		border-radius: 3px;
		display: inline-block;
		width: fit-content;
	}

	.latency-label.green {
		background: #1a3a1a;
		color: #4ade80;
	}

	.latency-label.amber {
		background: #3a3a1a;
		color: #facc15;
	}

	.latency-label.red {
		background: #3a1a1a;
		color: #f87171;
	}

	.latency-distance {
		font-size: 0.75rem;
		color: #999;
	}

	.latency-breakdown {
		font-size: 0.7rem;
		margin-top: 0.25rem;
		border-collapse: collapse;
	}

	.latency-breakdown td {
		padding: 1px 8px 1px 0;
		color: #aaa;
	}

	.latency-breakdown td:last-child {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}

	.latency-breakdown .total td {
		font-weight: 600;
		color: #ddd;
		border-top: 1px solid #444;
		padding-top: 2px;
	}
</style>
