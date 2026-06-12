<script lang="ts">
	let { visible }: { visible: boolean } = $props();
</script>

{#if visible}
	<div class="vacate-notice" role="alert">
		<span class="notice-icon" aria-hidden="true">&#9834;</span>
		A musician is waiting to practise — wrap up when you can
	</div>
{/if}

<style>
	.vacate-notice {
		display: flex;
		align-items: center;
		justify-content: center;
		gap: 0.5rem;
		background: var(--warn-dim);
		border: 1px solid rgba(242, 201, 76, 0.3);
		color: var(--warn);
		padding: 0.6rem 1rem;
		margin-bottom: 0.75rem;
		text-align: center;
		font-size: 0.85rem;
		border-radius: var(--radius-m);
		animation: slide-down 0.35s var(--ease-snap) both;
	}

	.notice-icon {
		animation: pulse-dot 1.6s ease-in-out infinite;
	}
</style>
