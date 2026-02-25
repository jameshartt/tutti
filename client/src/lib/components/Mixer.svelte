<script lang="ts">
	import type { Participant } from '../audio/types.js';
	import LatencyDisplay from './LatencyDisplay.svelte';

	let {
		participants,
		onGainChange,
		onMuteToggle,
		inputLevel = 0,
		micMuted = false,
		micBoost = 1.0,
		masterVolume = 1.0,
		onMicMuteToggle,
		onMicBoostChange,
		onMasterVolumeChange
	}: {
		participants: Participant[];
		onGainChange: (id: string, gain: number) => void;
		onMuteToggle: (id: string, muted: boolean) => void;
		inputLevel?: number;
		micMuted?: boolean;
		micBoost?: number;
		masterVolume?: number;
		onMicMuteToggle?: (muted: boolean) => void;
		onMicBoostChange?: (gain: number) => void;
		onMasterVolumeChange?: (gain: number) => void;
	} = $props();
</script>

<div class="mixer">
	<div class="self-channel">
		<div class="channel-header">
			<span class="participant-name self-label">You</span>
		</div>

		<div class="level-meter-row">
			<span class="control-label">Mic</span>
			<div class="level-meter-track">
				<div
					class="level-meter-fill"
					class:muted={micMuted}
					style="width: {Math.min(inputLevel * 100, 100)}%"
				></div>
			</div>
			<button
				class="mic-mute-btn"
				class:muted={micMuted}
				onclick={() => onMicMuteToggle?.(!micMuted)}
			>
				{micMuted ? 'Muted' : 'Mute'}
			</button>
		</div>

		<div class="slider-row">
			<span class="control-label">Boost</span>
			<input
				type="range"
				min="100"
				max="400"
				value={Math.round(micBoost * 100)}
				class="gain-slider boost-slider"
				oninput={(e) => {
					const target = e.target as HTMLInputElement;
					onMicBoostChange?.(parseInt(target.value) / 100);
				}}
			/>
			<span class="slider-value">{micBoost.toFixed(1)}x</span>
		</div>

		<div class="slider-row">
			<span class="control-label">Output</span>
			<input
				type="range"
				min="0"
				max="100"
				value={Math.round(masterVolume * 100)}
				class="gain-slider"
				oninput={(e) => {
					const target = e.target as HTMLInputElement;
					onMasterVolumeChange?.(parseInt(target.value) / 100);
				}}
			/>
			<span class="slider-value">{Math.round(masterVolume * 100)}%</span>
		</div>
	</div>

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

	.self-channel {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
		padding: 0.75rem;
		border: 1px solid #4ade80;
		border-radius: 6px;
		background: #0a1a0a;
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

	.self-label {
		color: #4ade80;
	}

	.control-label {
		font-size: 0.75rem;
		color: #888;
		min-width: 40px;
	}

	.level-meter-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}

	.level-meter-track {
		flex: 1;
		height: 6px;
		background: #222;
		border-radius: 3px;
		overflow: hidden;
	}

	.level-meter-fill {
		height: 100%;
		background: #4ade80;
		border-radius: 3px;
		transition: width 0.1s linear;
	}

	.level-meter-fill.muted {
		background: #555;
	}

	.mic-mute-btn {
		padding: 4px 12px;
		font-size: 0.8rem;
		border: 1px solid #555;
		border-radius: 4px;
		background: transparent;
		color: #ccc;
		cursor: pointer;
		min-width: 60px;
	}

	.mic-mute-btn.muted {
		background: #3a1a1a;
		border-color: #f87171;
		color: #f87171;
	}

	.slider-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}

	.slider-value {
		font-size: 0.75rem;
		color: #888;
		min-width: 36px;
		text-align: right;
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
