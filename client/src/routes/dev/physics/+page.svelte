<script lang="ts">
	import { page } from '$app/state';
	import {
		ROLLBACK_BUDGET_MS,
		loadReference,
		runBench,
		runDeterminism,
		type BenchResult
	} from '$lib/game/physics-spike';

	interface Result {
		pass: boolean;
		userAgent: string;
		ticks: number;
		digest: string;
		finalHash: string;
		/** null when no reference file was built */
		matchesReference: boolean | null;
		events: number;
		simulateMs: number;
		bench: BenchResult;
		withinBudget: boolean;
	}

	let running = $state(false);
	let stage = $state('');
	let error = $state('');
	let result = $state<Result | null>(null);

	/** Short code a person can read out: verdict, hash match, rollback cost */
	let code = $derived(
		result
			? [
					result.pass ? 'PASS' : 'FAIL',
					result.matchesReference === null ? 'NOREF' : result.matchesReference ? 'SAME' : 'DIFF',
					`${result.bench.rollbackMs.toFixed(2)}ms`
				].join('-')
			: ''
	);

	const fmt = (ms: number) => (ms < 0.1 ? `${(ms * 1000).toFixed(0)} µs` : `${ms.toFixed(2)} ms`);
	/** Let the page paint before a long synchronous run */
	const paint = () => new Promise((r) => requestAnimationFrame(() => setTimeout(r, 0)));

	async function start() {
		running = true;
		error = '';
		result = null;
		try {
			stage = 'Simulating 60 seconds…';
			await paint();
			const reference = await loadReference();
			const run = await runDeterminism();
			// The end-to-end test compares every tick against the native build
			(window as unknown as { __physicsHashes?: string[] }).__physicsHashes = run.hashes;

			stage = 'Timing rollbacks…';
			await paint();
			const bench = await runBench();

			const matchesReference = reference ? reference.digest === run.digest : null;
			const withinBudget = bench.rollbackMs <= ROLLBACK_BUDGET_MS;
			result = {
				pass: matchesReference !== false && withinBudget,
				userAgent: navigator.userAgent,
				ticks: run.ticks,
				digest: run.digest,
				finalHash: run.finalHash,
				matchesReference,
				events: run.events,
				simulateMs: run.elapsedMs,
				bench,
				withinBudget
			};
			// Leave a copy in the server log so it can be read without the device
			fetch('/api/client-log', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ event: 'physics-spike', code, result })
			}).catch(() => {});
		} catch (e) {
			error = e instanceof Error ? e.message : String(e);
		}
		stage = '';
		running = false;
	}

	if (page.url.searchParams.has('autostart')) start();
</script>

<svelte:head><title>Physics test · Tutti</title></svelte:head>

<main>
	<h1>Physics test</h1>
	<p class="hint">
		Checks that this device computes the game's physics exactly as the server does, and how long
		a correction takes. About ten seconds.
	</p>

	<button class="btn btn-primary" onclick={start} disabled={running} data-testid="start">
		{running ? stage || 'Running…' : result ? 'Run again' : 'Start'}
	</button>

	{#if error}
		<p class="error" data-testid="error">Could not run: {error}</p>
	{/if}

	{#if result}
		<div class="verdict" class:pass={result.pass} class:fail={!result.pass} data-testid="verdict">
			{result.pass ? 'PASS' : 'FAIL'}
		</div>
		<p class="code" data-testid="code">{code}</p>

		<ul>
			<li class:bad={result.matchesReference === false}>
				<span class="name">Same as server</span>
				<span class="detail">
					{result.matchesReference === null
						? 'no reference built'
						: result.matchesReference
							? `yes, all ${result.ticks} ticks`
							: 'NO'}
				</span>
			</li>
			<li class:bad={!result.withinBudget}>
				<span class="name">Rollback</span>
				<span class="detail">{fmt(result.bench.rollbackMs)} (budget {ROLLBACK_BUDGET_MS} ms)</span>
			</li>
			<li>
				<span class="name">Slowest batch</span>
				<span class="detail">{fmt(result.bench.rollbackWorstBatchMs)}</span>
			</li>
			<li>
				<span class="name">One tick</span>
				<span class="detail">{fmt(result.bench.tickMs)}</span>
			</li>
			<li>
				<span class="name">Save / restore</span>
				<span class="detail">
					{fmt(result.bench.saveMs)} / {fmt(result.bench.restoreMs)} ({result.bench.snapshotBytes} bytes)
				</span>
			</li>
			<li>
				<span class="name">60 s of game</span>
				<span class="detail">{result.simulateMs.toFixed(0)} ms</span>
			</li>
			<li>
				<span class="name">Final hash</span>
				<span class="detail">{result.finalHash}</span>
			</li>
		</ul>

		<pre data-testid="result">{JSON.stringify(result, null, 2)}</pre>
	{/if}
</main>

<style>
	main {
		max-width: 34rem;
		margin: 0 auto;
		padding: 2rem 1.25rem 4rem;
		font-family: var(--font-body);
		color: var(--text-1);
	}
	h1 {
		font-family: var(--font-display);
		font-size: 1.75rem;
		margin: 0 0 0.25rem;
	}
	.hint {
		color: var(--text-2);
		margin: 0 0 1.5rem;
	}
	.error {
		color: var(--bad);
		margin-top: 1rem;
	}
	.verdict {
		margin-top: 1.5rem;
		padding: 1.25rem;
		border-radius: var(--radius-l);
		text-align: center;
		font-family: var(--font-display);
		font-size: 3.5rem;
		line-height: 1;
	}
	.verdict.pass {
		background: var(--good-dim);
		color: var(--good);
	}
	.verdict.fail {
		background: var(--bad-dim);
		color: var(--bad);
	}
	.code {
		text-align: center;
		font-family: var(--font-mono);
		font-size: 1.25rem;
		letter-spacing: 0.04em;
		margin: 0.75rem 0 0;
		user-select: all;
	}
	ul {
		list-style: none;
		padding: 0;
		margin: 1.5rem 0;
		border-top: 1px solid var(--line-1);
	}
	li {
		display: grid;
		grid-template-columns: 9rem 1fr;
		gap: 0.5rem;
		padding: 0.5rem 0;
		border-bottom: 1px solid var(--line-1);
		font-family: var(--font-mono);
		font-size: 0.85rem;
	}
	li.bad {
		color: var(--bad);
	}
	.detail {
		color: var(--text-2);
		overflow-wrap: anywhere;
	}
	li.bad .detail {
		color: inherit;
	}
	pre {
		font-family: var(--font-mono);
		font-size: 0.7rem;
		color: var(--text-3, var(--text-2));
		overflow-x: auto;
	}
</style>
