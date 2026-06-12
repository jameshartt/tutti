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

	// Segmented VU meter: 24 segments, green -> amber -> red
	const SEGMENTS = 24;

	function segmentClass(i: number): string {
		if (i >= SEGMENTS - 3) return 'seg-red';
		if (i >= SEGMENTS - 8) return 'seg-amber';
		return 'seg-green';
	}

	let litSegments = $derived(Math.round(Math.min(inputLevel, 1) * SEGMENTS));

	function initials(name: string): string {
		return name
			.split(/\s+/)
			.slice(0, 2)
			.map((w) => w[0]?.toUpperCase() ?? '')
			.join('');
	}
</script>

<div class="mixer">
	<!-- Self channel strip -->
	<div class="channel self-channel">
		<div class="channel-header">
			<span class="avatar self-avatar" aria-hidden="true">&#9835;</span>
			<span class="participant-name self-label">You</span>
			{#if micMuted}<span class="muted-tag">muted</span>{/if}
		</div>

		<div class="meter-row">
			<span class="control-label">Mic</span>
			<div class="vu-meter" class:dimmed={micMuted}>
				{#each Array(SEGMENTS) as _, i}
					<span class="seg {segmentClass(i)}" class:lit={i < litSegments && !micMuted}></span>
				{/each}
			</div>
			<button
				class="mute-btn"
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
				class="fader"
				aria-label="Microphone boost"
				oninput={(e) => {
					const target = e.target as HTMLInputElement;
					onMicBoostChange?.(parseInt(target.value) / 100);
				}}
			/>
			<span class="slider-value">{micBoost.toFixed(1)}&times;</span>
		</div>

		<div class="slider-row">
			<span class="control-label">Output</span>
			<input
				type="range"
				min="0"
				max="100"
				value={Math.round(masterVolume * 100)}
				class="fader"
				aria-label="Master output volume"
				oninput={(e) => {
					const target = e.target as HTMLInputElement;
					onMasterVolumeChange?.(parseInt(target.value) / 100);
				}}
			/>
			<span class="slider-value">{Math.round(masterVolume * 100)}%</span>
		</div>
	</div>

	<!-- Other musicians -->
	{#each participants as p (p.id)}
		<div class="channel" class:channel-muted={p.muted}>
			<div class="channel-header">
				<span class="avatar" aria-hidden="true">{initials(p.name)}</span>
				<span class="participant-name">{p.name}</span>
				<span class="header-spacer"></span>
				<LatencyDisplay latency={p.latency ?? null} breakdown={null} />
			</div>
			<div class="slider-row">
				<span class="control-label">Level</span>
				<input
					type="range"
					min="0"
					max="100"
					value={Math.round(p.gain * 100)}
					class="fader"
					aria-label="Volume for {p.name}"
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
		<div class="empty-message">
			<span class="empty-icon" aria-hidden="true">&#119070;</span>
			<p>Waiting for other musicians&hellip;</p>
			<p class="empty-sub">Share the room link to get the band in here.</p>
		</div>
	{/if}
</div>

<style>
	.mixer {
		display: flex;
		flex-direction: column;
		gap: 0.8rem;
	}

	.channel {
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
		padding: 0.9rem 1rem;
		border: 1px solid var(--line-1);
		border-radius: var(--radius-l);
		background: linear-gradient(180deg, var(--bg-1), var(--bg-0));
		box-shadow: var(--shadow-card);
		transition: opacity 0.2s, border-color 0.2s;
		animation: rise-in 0.4s var(--ease-snap) both;
	}

	.channel-muted {
		opacity: 0.6;
	}

	.self-channel {
		border-color: rgba(242, 169, 59, 0.35);
		background:
			linear-gradient(180deg, rgba(242, 169, 59, 0.05), transparent 60%),
			linear-gradient(180deg, var(--bg-1), var(--bg-0));
	}

	.channel-header {
		display: flex;
		align-items: center;
		gap: 0.6rem;
	}

	.header-spacer {
		flex: 1;
	}

	.avatar {
		display: inline-flex;
		align-items: center;
		justify-content: center;
		width: 30px;
		height: 30px;
		border-radius: 50%;
		background: var(--bg-3);
		border: 1px solid var(--line-2);
		font-size: 0.72rem;
		font-weight: 600;
		color: var(--text-2);
		letter-spacing: 0.03em;
		flex-shrink: 0;
	}

	.self-avatar {
		background: var(--accent-dim);
		border-color: rgba(242, 169, 59, 0.4);
		color: var(--accent);
		font-size: 0.95rem;
	}

	.participant-name {
		font-weight: 600;
		font-size: 1rem;
		letter-spacing: 0.01em;
	}

	.self-label {
		color: var(--accent);
	}

	.muted-tag {
		font-family: var(--font-mono);
		font-size: 0.6rem;
		text-transform: uppercase;
		letter-spacing: 0.1em;
		color: var(--bad);
		background: var(--bad-dim);
		padding: 2px 8px;
		border-radius: 999px;
	}

	.control-label {
		font-family: var(--font-mono);
		font-size: 0.62rem;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		color: var(--text-3);
		min-width: 46px;
		flex-shrink: 0;
	}

	/* --- VU meter -------------------------------------------- */

	.meter-row {
		display: flex;
		align-items: center;
		gap: 0.6rem;
	}

	.vu-meter {
		flex: 1;
		display: flex;
		gap: 2px;
		height: 14px;
		padding: 3px;
		background: #060504;
		border-radius: 4px;
		border: 1px solid var(--line-1);
		box-shadow: inset 0 1px 3px rgba(0, 0, 0, 0.8);
	}

	.vu-meter.dimmed {
		opacity: 0.5;
	}

	.seg {
		flex: 1;
		border-radius: 1px;
		transition: opacity 0.06s linear;
		opacity: 0.13;
	}

	.seg-green {
		background: var(--good);
	}

	.seg-amber {
		background: var(--warn);
	}

	.seg-red {
		background: var(--bad);
	}

	.seg.lit {
		opacity: 1;
		box-shadow: 0 0 6px currentColor;
	}

	.seg-green.lit {
		box-shadow: 0 0 6px rgba(94, 224, 138, 0.6);
	}

	.seg-amber.lit {
		box-shadow: 0 0 6px rgba(242, 201, 76, 0.6);
	}

	.seg-red.lit {
		box-shadow: 0 0 6px rgba(255, 107, 94, 0.6);
	}

	/* --- Sliders & buttons ------------------------------------ */

	.slider-row {
		display: flex;
		align-items: center;
		gap: 0.6rem;
	}

	.slider-value {
		font-family: var(--font-mono);
		font-size: 0.72rem;
		color: var(--text-2);
		min-width: 42px;
		text-align: right;
		font-variant-numeric: tabular-nums;
	}

	.mute-btn {
		padding: 4px 0;
		width: 64px;
		font-size: 0.72rem;
		font-family: var(--font-mono);
		text-transform: uppercase;
		letter-spacing: 0.06em;
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		background: var(--bg-2);
		color: var(--text-2);
		cursor: pointer;
		flex-shrink: 0;
		transition: all 0.15s;
	}

	.mute-btn:hover {
		border-color: var(--text-3);
		color: var(--text-1);
	}

	.mute-btn.muted {
		background: var(--bad-dim);
		border-color: var(--bad);
		color: var(--bad);
		box-shadow: 0 0 10px -4px rgba(255, 107, 94, 0.5);
	}

	/* --- Empty state ------------------------------------------ */

	.empty-message {
		text-align: center;
		padding: 2.5rem 1rem;
		border: 1px dashed var(--line-1);
		border-radius: var(--radius-l);
		color: var(--text-3);
	}

	.empty-icon {
		font-size: 1.8rem;
		display: block;
		margin-bottom: 0.5rem;
		opacity: 0.6;
	}

	.empty-message p {
		margin: 0.15rem 0;
		font-style: italic;
		font-size: 0.95rem;
		color: var(--text-2);
	}

	.empty-message .empty-sub {
		font-style: normal;
		font-size: 0.78rem;
		color: var(--text-3);
	}
</style>
