<script lang="ts">
	import { micState, selectMic } from '../stores/microphone.js';
	import { settings } from '../stores/settings.js';

	let devices = $derived($micState.devices);
	let selectedId = $derived($micState.selectedId);
	let defaultLabel = $derived($micState.defaultLabel);
	let switching = $derived($micState.switching);
	let autoSwitch = $derived($settings.autoSwitchMic);

	// <select> can't hold null — use '' for "system default"
	let value = $derived(selectedId ?? '');

	function handleChange(e: Event) {
		const id = (e.target as HTMLSelectElement).value;
		void selectMic(id === '' ? null : id);
	}

	function toggleAuto(e: Event) {
		const on = (e.target as HTMLInputElement).checked;
		settings.update((s) => ({ ...s, autoSwitchMic: on }));
	}
</script>

<div class="picker-row">
	<span class="control-label">Input</span>
	<div class="select-wrap" class:switching>
		<select class="mic-select" {value} onchange={handleChange} aria-label="Microphone" disabled={switching}>
			<option value="">System default{defaultLabel ? ` · ${defaultLabel}` : ''}</option>
			{#each devices as d (d.deviceId)}
				<option value={d.deviceId}>{d.label}</option>
			{/each}
		</select>
		<span class="chevron" aria-hidden="true">&#9662;</span>
	</div>
</div>
<label class="auto-row" title="When a new microphone is plugged in, switch to it — until you pick one yourself">
	<input type="checkbox" checked={autoSwitch} onchange={toggleAuto} />
	<span>Switch to newly connected mics</span>
</label>

<style>
	.picker-row {
		display: flex;
		align-items: center;
		gap: 0.6rem;
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

	.select-wrap {
		position: relative;
		flex: 1;
		min-width: 0;
	}

	.mic-select {
		appearance: none;
		-webkit-appearance: none;
		width: 100%;
		padding: 6px 28px 6px 10px;
		font: inherit;
		font-size: 0.82rem;
		color: var(--text-1);
		background: var(--bg-2);
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		cursor: pointer;
		text-overflow: ellipsis;
		transition: border-color 0.15s, box-shadow 0.2s;
	}

	.mic-select:hover {
		border-color: var(--text-3);
	}

	.mic-select:focus-visible {
		outline: none;
		border-color: var(--accent);
		box-shadow: 0 0 0 2px var(--accent-dim);
	}

	.mic-select option {
		background: var(--bg-2);
		color: var(--text-1);
	}

	.chevron {
		position: absolute;
		right: 10px;
		top: 50%;
		transform: translateY(-50%);
		font-size: 0.7rem;
		color: var(--text-3);
		pointer-events: none;
	}

	.switching .mic-select {
		opacity: 0.6;
		cursor: progress;
	}

	.switching .chevron {
		color: var(--accent);
		animation: pulse-dot 1s ease-in-out infinite;
	}

	.auto-row {
		display: flex;
		align-items: center;
		gap: 0.45rem;
		margin: -0.25rem 0 0 calc(46px + 0.6rem);
		font-size: 0.72rem;
		color: var(--text-3);
		cursor: pointer;
		user-select: none;
	}

	.auto-row:hover {
		color: var(--text-2);
	}

	.auto-row input {
		accent-color: var(--accent);
		margin: 0;
	}
</style>
