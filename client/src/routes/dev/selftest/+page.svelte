<script lang="ts">
	import { page } from '$app/state';
	import {
		runSelfTest,
		type SelfTestResult,
		type SelfTestStep,
		type SelfTestTransport
	} from '$lib/game/selftest';

	const params = page.url.searchParams;
	const transport = (params.get('transport') ?? 'auto') as SelfTestTransport;
	// No room named: the test picks an empty one so it never disturbs anyone
	const room = params.get('room') ?? undefined;

	let running = $state(false);
	let steps = $state<SelfTestStep[]>([]);
	let result = $state<SelfTestResult | null>(null);

	/** Short code a person can read out: verdict, transport, echoes, median RTT */
	let code = $derived(
		result
			? [
					result.pass ? 'PASS' : 'FAIL',
					result.transport === 'webtransport' ? 'WT' : 'RTC',
					`${result.echo.received}of${result.echo.sent}`,
					result.echo.medianRttMs === null ? 'x' : `${Math.round(result.echo.medianRttMs)}ms`
				].join('-')
			: ''
	);

	async function start() {
		running = true;
		steps = [];
		result = null;
		const r = await runSelfTest({ room, transport, onStep: (s) => (steps = [...steps, s]) });
		result = r;
		running = false;
		// Leave a copy in the server log so it can be read without the device
		fetch('/api/client-log', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ event: 'selftest', code, result: r })
		}).catch(() => {});
	}

	if (params.has('autostart')) start();
</script>

<svelte:head><title>Self-test · Tutti</title></svelte:head>

<main>
	<h1>Connection self-test</h1>
	<p class="hint">Checks that this device can carry game traffic. Takes about five seconds.</p>

	<button class="btn btn-primary" onclick={start} disabled={running} data-testid="start">
		{running ? 'Running…' : result ? 'Run again' : 'Start'}
	</button>

	{#if result}
		<div class="verdict" class:pass={result.pass} class:fail={!result.pass} data-testid="verdict">
			{result.pass ? 'PASS' : 'FAIL'}
		</div>
		<p class="code" data-testid="code">{code}</p>
	{/if}

	<ul>
		{#each steps as s (s.name)}
			<li class:bad={!s.ok}>
				<span class="mark">{s.ok ? '✓' : '✗'}</span>
				<span class="name">{s.name}</span>
				<span class="detail">{s.detail}</span>
			</li>
		{/each}
	</ul>

	{#if result}
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
		grid-template-columns: 1.5rem 9rem 1fr;
		gap: 0.5rem;
		padding: 0.5rem 0;
		border-bottom: 1px solid var(--line-1);
		font-family: var(--font-mono);
		font-size: 0.85rem;
	}
	.mark {
		color: var(--good);
	}
	li.bad .mark,
	li.bad .name {
		color: var(--bad);
	}
	.detail {
		color: var(--text-2);
		overflow-wrap: anywhere;
	}
	pre {
		font-family: var(--font-mono);
		font-size: 0.7rem;
		color: var(--text-3);
		background: var(--bg-1);
		border: 1px solid var(--line-1);
		border-radius: var(--radius-m);
		padding: 0.75rem;
		overflow-x: auto;
	}
</style>
