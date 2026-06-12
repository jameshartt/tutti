<script lang="ts">
	import RoomList from '$lib/components/RoomList.svelte';

	// Animated hero waveform — staggered bar heights/delays, CSS does the rest
	const bars = Array.from({ length: 24 }, (_, i) => ({
		delay: (i * 73) % 900,
		duration: 900 + ((i * 137) % 600),
		height: 14 + ((i * 41) % 34)
	}));
</script>

<svelte:head>
	<title>Tutti — play together, apart</title>
</svelte:head>

<main>
	<div class="hero">
		<div class="waveform" aria-hidden="true">
			{#each bars as bar}
				<span
					class="wave-bar"
					style="height: {bar.height}px; animation-duration: {bar.duration}ms; animation-delay: -{bar.delay}ms"
				></span>
			{/each}
		</div>

		<h1><span class="wordmark">Tutti</span></h1>
		<p class="tagline">
			<em>It.&nbsp;“all together”</em> — rehearse over the internet with latency low enough to
			actually play.
		</p>

		<div class="hero-stats" aria-hidden="true">
			<span class="stat"><span class="stat-num">2.7</span>ms per frame</span>
			<span class="stat-dot"></span>
			<span class="stat"><span class="stat-num">48</span>kHz uncompressed</span>
			<span class="stat-dot"></span>
			<span class="stat"><span class="stat-num">0</span> codecs in the way</span>
		</div>
	</div>

	<RoomList />

	<footer>
		<p>Wired headphones recommended &middot; Chrome or Edge for the lowest latency</p>
	</footer>
</main>

<style>
	main {
		min-height: 100vh;
		display: flex;
		flex-direction: column;
	}

	.hero {
		text-align: center;
		padding: 4.5rem 1rem 2.5rem;
		animation: rise-in 0.6s var(--ease-snap) both;
	}

	.waveform {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 5px;
		height: 56px;
		margin-bottom: 1.5rem;
	}

	.wave-bar {
		width: 4px;
		border-radius: 2px;
		background: linear-gradient(180deg, var(--accent-bright), var(--accent-deep));
		opacity: 0.85;
		animation: wave 1s ease-in-out infinite;
		transform-origin: center;
	}

	.wave-bar:nth-child(odd) {
		opacity: 0.45;
	}

	h1 {
		margin: 0;
		line-height: 1;
	}

	.wordmark {
		font-family: var(--font-display);
		font-style: italic;
		font-weight: 900;
		font-size: clamp(4rem, 12vw, 6.5rem);
		letter-spacing: -0.03em;
		background: linear-gradient(180deg, var(--text-1) 30%, var(--accent-bright) 110%);
		-webkit-background-clip: text;
		background-clip: text;
		color: transparent;
		text-shadow: 0 0 80px var(--accent-glow);
	}

	.tagline {
		color: var(--text-2);
		font-size: 1.05rem;
		max-width: 34rem;
		margin: 1rem auto 0;
		line-height: 1.5;
	}

	.tagline em {
		font-family: var(--font-display);
		color: var(--accent);
	}

	.hero-stats {
		display: flex;
		align-items: center;
		justify-content: center;
		flex-wrap: wrap;
		gap: 0.75rem;
		margin-top: 1.75rem;
		font-family: var(--font-mono);
		font-size: 0.72rem;
		color: var(--text-3);
		letter-spacing: 0.04em;
		text-transform: uppercase;
	}

	.stat-num {
		color: var(--accent);
		font-weight: 600;
	}

	.stat-dot {
		width: 3px;
		height: 3px;
		border-radius: 50%;
		background: var(--line-2);
	}

	footer {
		margin-top: auto;
		padding: 2.5rem 1rem 1.5rem;
		text-align: center;
	}

	footer p {
		margin: 0;
		font-size: 0.78rem;
		color: var(--text-3);
	}
</style>
