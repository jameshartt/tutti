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
	<div class="panel-titlebar">
		<span class="titlebar-dot" class:running></span>
		<span class="titlebar-label">loopback test</span>
	</div>

	<div class="body">
		<button class="test-btn" onclick={measure} disabled={running}>
			{#if running}
				<span class="spinner" aria-hidden="true"></span> Measuring&hellip;
			{:else}
				&#9658; Measure latency
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
							<td>round-trip</td>
							<td>{result.roundTripMs.toFixed(1)} ms</td>
							<td class="sep">/</td>
							<td>one-way</td>
							<td>{result.oneWayMs.toFixed(1)} ms</td>
						</tr>
					{/each}
				</tbody>
			</table>
		{/if}
	</div>
</div>

<style>
	.latency-tester {
		border: 1px solid var(--line-1);
		border-radius: var(--radius-m);
		background: #080606;
		font-family: var(--font-mono);
		overflow: hidden;
	}

	.panel-titlebar {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		padding: 0.45rem 0.75rem;
		background: var(--bg-1);
		border-bottom: 1px solid var(--line-1);
	}

	.titlebar-dot {
		width: 7px;
		height: 7px;
		border-radius: 50%;
		background: var(--text-3);
	}

	.titlebar-dot.running {
		background: var(--accent);
		box-shadow: 0 0 6px var(--accent-glow);
		animation: pulse-dot 0.8s ease-in-out infinite;
	}

	.titlebar-label {
		text-transform: uppercase;
		letter-spacing: 0.14em;
		font-size: 0.62rem;
		color: var(--text-3);
	}

	.body {
		padding: 0.65rem 0.75rem;
	}

	.test-btn {
		display: inline-flex;
		align-items: center;
		gap: 0.45rem;
		padding: 5px 14px;
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		background: var(--bg-2);
		color: var(--text-2);
		cursor: pointer;
		font-family: var(--font-mono);
		font-size: 0.68rem;
		transition: all 0.15s;
	}

	.test-btn:hover:not(:disabled) {
		border-color: var(--accent);
		color: var(--accent);
	}

	.test-btn:disabled {
		color: var(--text-3);
		cursor: wait;
	}

	.spinner {
		width: 9px;
		height: 9px;
		border: 1.5px solid var(--text-3);
		border-top-color: var(--accent);
		border-radius: 50%;
		animation: spin 0.7s linear infinite;
	}

	@keyframes spin {
		to {
			transform: rotate(360deg);
		}
	}

	.error {
		color: var(--bad);
		font-size: 0.65rem;
		margin-top: 0.35rem;
	}

	.results {
		margin-top: 0.45rem;
		border-collapse: collapse;
		font-size: 0.65rem;
	}

	.results td {
		padding: 1px 4px;
		color: var(--text-3);
	}

	.results .sep {
		color: var(--line-2);
		padding: 1px 2px;
	}

	.results .latest td {
		color: var(--accent);
		font-weight: 600;
	}

	.results td:nth-child(2),
	.results td:nth-child(5) {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}
</style>
