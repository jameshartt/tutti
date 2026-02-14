<script lang="ts">
	import RoomList from '$lib/components/RoomList.svelte';
</script>

<svelte:head>
	<title>Tutti - All Together</title>
</svelte:head>

<main>
	<div class="hero">
		<h1>Tutti</h1>
		<p class="tagline">Low-latency music rehearsal. All together.</p>
	</div>

	<RoomList />
</main>

<style>
	main {
		min-height: 100vh;
		background: #0a0a0a;
		color: #eee;
	}

	.hero {
		text-align: center;
		padding: 3rem 1rem 1rem;
	}

	h1 {
		font-size: 2.5rem;
		margin: 0;
		letter-spacing: -0.02em;
	}

	.tagline {
		color: #999;
		margin-top: 0.5rem;
	}
</style>
