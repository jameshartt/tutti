<script lang="ts">
	import '@fontsource/fraunces/latin-500.css';
	import '@fontsource/fraunces/latin-600.css';
	import '@fontsource/fraunces/latin-900.css';
	import '@fontsource/fraunces/latin-500-italic.css';
	import '@fontsource-variable/space-grotesk';
	import '@fontsource/jetbrains-mono/400.css';
	import '@fontsource/jetbrains-mono/600.css';
	import '../app.css';
	import type { Snippet } from 'svelte';

	let { children }: { children: Snippet } = $props();
</script>

<div id="tutti-app">
	<div class="stage-light" aria-hidden="true"></div>
	<div class="stage-grain" aria-hidden="true"></div>
	{@render children()}
</div>

<style>
	#tutti-app {
		position: relative;
		min-height: 100vh;
		isolation: isolate;
	}

	/* Warm spotlight falling from above the fold */
	.stage-light {
		position: fixed;
		inset: 0;
		z-index: -2;
		pointer-events: none;
		background:
			radial-gradient(120% 60% at 50% -10%, rgba(242, 169, 59, 0.13) 0%, transparent 60%),
			radial-gradient(80% 50% at 85% 110%, rgba(120, 80, 200, 0.07) 0%, transparent 60%),
			var(--bg-0);
	}

	/* Subtle film grain so the dark field isn't flat */
	.stage-grain {
		position: fixed;
		inset: 0;
		z-index: -1;
		pointer-events: none;
		opacity: 0.35;
		background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='160' height='160'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.9' numOctaves='2' stitchTiles='stitch'/%3E%3CfeColorMatrix type='matrix' values='0 0 0 0 0  0 0 0 0 0  0 0 0 0 0  0 0 0 0.04 0'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23n)'/%3E%3C/svg%3E");
	}
</style>
