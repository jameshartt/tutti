<script lang="ts">
	let { visible }: { visible: boolean } = $props();
</script>

{#if visible}
	<div class="vacate-notice" role="alert">
		A musician is waiting to practise
	</div>
{/if}

<style>
	.vacate-notice {
		background: #3a3a1a;
		color: #facc15;
		padding: 0.5rem 1rem;
		text-align: center;
		font-size: 0.85rem;
		border-bottom: 1px solid #555;
	}
</style>
