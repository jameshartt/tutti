<script lang="ts">
	import { runLoopbackTest, type LoopbackResult } from '../latency/loopback-test.js';

	let {
		capturePort,
		playbackPort,
		setLoopback
	}: { capturePort: MessagePort; playbackPort: MessagePort; setLoopback: (enabled: boolean) => void } = $props();

	let running = $state(false);
	let results: LoopbackResult[] = $state([]);
	let error = $state('');

	async function measure() {
		running = true;
		error = '';
		try {
			const result = await runLoopbackTest(capturePort, playbackPort, setLoopback);
			results = [result, ...results.slice(0, 4)];
		} catch (err) {
			error = err instanceof Error ? err.message : 'Test failed';
		} finally {
			running = false;
		}
	}
</script>

<div class="latency-tester">
	<div class="section-title">Loopback Test</div>
	<button class="test-btn" onclick={measure} disabled={running}>
		{#if running}
			Measuring...
		{:else}
			Measure Latency
		{/if}
	</button>

	{#if error}
		<div class="error">{error}</div>
	{/if}

	{#if results.length > 0}
		<table class="results">
			<tbody>
				{#each results as result, i}
					<tr class:latest={i === 0}>
						<td>Round-trip</td>
						<td>{result.roundTripMs.toFixed(1)} ms</td>
						<td class="sep">/</td>
						<td>One-way</td>
						<td>{result.oneWayMs.toFixed(1)} ms</td>
					</tr>
				{/each}
			</tbody>
		</table>
	{/if}
</div>

<style>
	.latency-tester {
		margin-top: 0.5rem;
	}

	.section-title {
		font-weight: 600;
		color: #ccc;
		margin-bottom: 0.35rem;
		font-size: 0.72rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
	}

	.test-btn {
		padding: 4px 12px;
		border: 1px solid #555;
		border-radius: 4px;
		background: transparent;
		color: #aaa;
		cursor: pointer;
		font-family: monospace;
		font-size: 0.7rem;
	}

	.test-btn:disabled {
		color: #666;
		cursor: wait;
	}

	.error {
		color: #f87171;
		font-size: 0.65rem;
		margin-top: 0.25rem;
	}

	.results {
		margin-top: 0.35rem;
		border-collapse: collapse;
		font-size: 0.65rem;
	}

	.results td {
		padding: 1px 4px;
		color: #888;
	}

	.results .sep {
		color: #555;
		padding: 1px 2px;
	}

	.results .latest td {
		color: #ccc;
		font-weight: 600;
	}

	.results td:nth-child(2),
	.results td:nth-child(5) {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}
</style>
