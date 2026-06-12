<script lang="ts">
	import { settings } from '../stores/settings.js';
	import type { LatencyInfo, LatencyBreakdown } from '../audio/types.js';

	let { latency, breakdown }: { latency: LatencyInfo | null; breakdown: LatencyBreakdown | null } =
		$props();

	// $settings auto-subscription — cleaned up automatically (no leak)
	let nerdMode = $derived($settings.nerdMode);
</script>

{#if latency}
	<div class="latency-display">
		<div class="latency-pill {latency.colour}">
			<span class="pill-dot"></span>
			{latency.label}
			<span class="latency-distance">~{latency.distanceMetres.toFixed(1)}m</span>
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
		gap: 0.35rem;
	}

	.latency-pill {
		display: inline-flex;
		align-items: center;
		gap: 0.4rem;
		font-family: var(--font-mono);
		font-size: 0.68rem;
		font-weight: 600;
		text-transform: uppercase;
		letter-spacing: 0.06em;
		padding: 3px 10px;
		border-radius: 999px;
		width: fit-content;
	}

	.pill-dot {
		width: 5px;
		height: 5px;
		border-radius: 50%;
		background: currentColor;
		animation: pulse-dot 2s ease-in-out infinite;
	}

	.latency-pill.green {
		background: var(--good-dim);
		color: var(--good);
	}

	.latency-pill.amber {
		background: var(--warn-dim);
		color: var(--warn);
	}

	.latency-pill.red {
		background: var(--bad-dim);
		color: var(--bad);
	}

	.latency-distance {
		opacity: 0.65;
		font-weight: 400;
	}

	.latency-breakdown {
		font-family: var(--font-mono);
		font-size: 0.7rem;
		margin-top: 0.25rem;
		border-collapse: collapse;
	}

	.latency-breakdown td {
		padding: 2px 10px 2px 0;
		color: var(--text-2);
	}

	.latency-breakdown td:last-child {
		text-align: right;
		font-variant-numeric: tabular-nums;
		color: var(--text-1);
	}

	.latency-breakdown .total td {
		font-weight: 600;
		color: var(--accent);
		border-top: 1px solid var(--line-2);
		padding-top: 4px;
	}
</style>
