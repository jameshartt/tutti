<script lang="ts">
	import type { Participant } from '../audio/types.js';
	import LatencyDisplay from './LatencyDisplay.svelte';

	let {
		participants,
		onGainChange,
		onMuteToggle
	}: {
		participants: Participant[];
		onGainChange: (id: string, gain: number) => void;
		onMuteToggle: (id: string, muted: boolean) => void;
	} = $props();
</script>

<div class="mixer">
	{#each participants as p (p.id)}
		<div class="mixer-channel">
			<div class="channel-header">
				<span class="participant-name">{p.name}</span>
				<LatencyDisplay latency={p.latency ?? null} breakdown={null} />
			</div>
			<div class="channel-controls">
				<input
					type="range"
					min="0"
					max="100"
					value={Math.round(p.gain * 100)}
					class="gain-slider"
					oninput={(e) => {
						const target = e.target as HTMLInputElement;
						onGainChange(p.id, parseInt(target.value) / 100);
					}}
				/>
				<button
					class="mute-btn"
					class:muted={p.muted}
					onclick={() => onMuteToggle(p.id, !p.muted)}
				>
					{p.muted ? 'Muted' : 'Mute'}
				</button>
			</div>
		</div>
	{/each}

	{#if participants.length === 0}
		<p class="empty-message">Waiting for other musicians to join...</p>
	{/if}
</div>

<style>
	.mixer {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
	}

	.mixer-channel {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
		padding: 0.75rem;
		border: 1px solid #333;
		border-radius: 6px;
	}

	.channel-header {
		display: flex;
		justify-content: space-between;
		align-items: flex-start;
	}

	.participant-name {
		font-weight: 600;
	}

	.channel-controls {
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}

	.gain-slider {
		flex: 1;
		height: 4px;
		accent-color: #4ade80;
	}

	.mute-btn {
		padding: 4px 12px;
		font-size: 0.8rem;
		border: 1px solid #555;
		border-radius: 4px;
		background: transparent;
		color: #ccc;
		cursor: pointer;
		min-width: 60px;
	}

	.mute-btn.muted {
		background: #3a1a1a;
		border-color: #f87171;
		color: #f87171;
	}

	.empty-message {
		color: #666;
		font-style: italic;
		text-align: center;
		padding: 2rem 0;
	}
</style>
