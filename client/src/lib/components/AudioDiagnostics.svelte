<script lang="ts">
	import { audioStats } from '../stores/audio-stats.js';
	import { getLatencyWarning } from '../audio/context.js';
	import { settings } from '../stores/settings.js';

	let { transportDesc, transportConnected }: { transportDesc: string; transportConnected: boolean } =
		$props();

	// $store auto-subscriptions — cleaned up automatically (no leaks)
	let stats = $derived($audioStats);
	let prebufferFrames = $derived($settings.prebufferFrames);

	let latencyWarning = $derived(stats.hardwareOutputMs > 15 ? getLatencyWarning() : null);
	let copied = $state(false);

	async function copyCommand() {
		if (latencyWarning?.command) {
			await navigator.clipboard.writeText(latencyWarning.command);
			copied = true;
			setTimeout(() => (copied = false), 2000);
		}
	}

	// Ring buffer capacities (match RING_BUFFER_CAPACITY in playback.ts / capture.ts)
	const PLAYBACK_CAPACITY = 2048;
	const CAPTURE_CAPACITY = 1024;

	function fillPercent(fillLevel: number, capacity: number): number {
		return Math.round((fillLevel / capacity) * 100);
	}

	// Playback health is fill relative to the configured prebuffer cushion,
	// not to raw capacity — a half-empty big buffer at target is healthy.
	function playbackFillClass(fillLevel: number): string {
		const target = Math.max(1, prebufferFrames) * 128;
		const ratio = fillLevel / target;
		if (ratio >= 0.75) return 'fill-good';
		if (ratio >= 0.35) return 'fill-warn';
		return 'fill-bad';
	}

	function captureFillClass(fillLevel: number): string {
		const pct = fillPercent(fillLevel, CAPTURE_CAPACITY);
		if (pct > 50) return 'fill-good';
		if (pct > 20) return 'fill-warn';
		return 'fill-bad';
	}

	function sampleRateWarning(rate: number): boolean {
		return rate > 0 && rate !== 48000;
	}

	const prebufferPresets = [
		{ label: 'None', value: 0, hint: '0ms' },
		{ label: 'Low', value: 2, hint: '~5ms' },
		{ label: 'Medium', value: 4, hint: '~11ms' },
		{ label: 'High', value: 8, hint: '~21ms' }
	] as const;

	function setPrebuffer(value: number) {
		settings.update((s) => ({ ...s, prebufferFrames: value }));
	}
</script>

<div class="diagnostics">
	<div class="panel-titlebar">
		<span class="titlebar-dot"></span>
		<span class="titlebar-label">diagnostics</span>
		<span class="titlebar-status" class:ok={transportConnected}>
			{transportConnected ? 'link up' : 'link down'}
		</span>
	</div>

	{#if latencyWarning}
		<div class="latency-warning">
			<div class="warning-title">&#9888; High hardware latency</div>
			<p class="warning-message">{latencyWarning.message}</p>
			<pre class="warning-instructions">{latencyWarning.instructions}</pre>
			{#if latencyWarning.command}
				<button class="copy-btn" onclick={copyCommand}>
					{copied ? 'Copied ✓' : 'Copy command'}
				</button>
			{/if}
		</div>
	{/if}

	<div class="section">
		<div class="section-title">Buffer health</div>
		<div class="buffer-row">
			<span class="label">Playback</span>
			<div class="bar-container">
				<div
					class="bar-fill {playbackFillClass(stats.playbackFillLevel)}"
					style="width: {fillPercent(stats.playbackFillLevel, PLAYBACK_CAPACITY)}%"
				></div>
			</div>
			<span class="value">{fillPercent(stats.playbackFillLevel, PLAYBACK_CAPACITY)}%</span>
		</div>
		<div class="stat-row">
			<span>underruns <b>{stats.playbackUnderruns}</b></span>
			<span>partial <b>{stats.playbackPartialFrames}</b></span>
			<span>skips <b>{stats.playbackSkipAheads}</b></span>
			{#if stats.playbackPrebuffering}<span class="warn">PREBUFFERING</span>{/if}
		</div>
		<div class="prebuffer-row">
			<span class="label">Pre-buffer</span>
			<div class="preset-group">
				{#each prebufferPresets as preset}
					<button
						class="preset-btn"
						class:active={prebufferFrames === preset.value}
						onclick={() => setPrebuffer(preset.value)}
					>
						{preset.label}<span class="preset-hint">{preset.hint}</span>
					</button>
				{/each}
			</div>
		</div>
		<p class="prebuffer-hint">Seeing underruns? Increase the pre-buffer — adds latency, improves stability.</p>
		<div class="buffer-row">
			<span class="label">Capture</span>
			<div class="bar-container">
				<div
					class="bar-fill {captureFillClass(stats.captureFillLevel)}"
					style="width: {fillPercent(stats.captureFillLevel, CAPTURE_CAPACITY)}%"
				></div>
			</div>
			<span class="value">{fillPercent(stats.captureFillLevel, CAPTURE_CAPACITY)}%</span>
		</div>
		<div class="stat-row">
			<span>dropped <b>{stats.captureDroppedFrames}</b></span>
		</div>
	</div>

	<div class="section-grid">
		<div class="section">
			<div class="section-title">Transport</div>
			<table>
				<tbody>
					<tr>
						<td>type</td>
						<td>{transportDesc || 'none'}</td>
					</tr>
					<tr>
						<td>connected</td>
						<td class:good={transportConnected} class:warn={!transportConnected}>
							{transportConnected ? 'yes' : 'no'}
						</td>
					</tr>
					<tr>
						<td>pkts sent</td>
						<td>{stats.packetsSent}</td>
					</tr>
					<tr>
						<td>pkts recv</td>
						<td>{stats.packetsReceived}</td>
					</tr>
					<tr>
						<td>seq gaps</td>
						<td class:warn={stats.seqGaps > 0}>{stats.seqGaps}</td>
					</tr>
					<tr>
						<td>reordered</td>
						<td>{stats.seqReordered}</td>
					</tr>
				</tbody>
			</table>
		</div>

		<div class="section">
			<div class="section-title">Audio context</div>
			<table>
				<tbody>
					<tr>
						<td>sample rate</td>
						<td class:warn={sampleRateWarning(stats.sampleRate)}>
							{stats.sampleRate || '—'} Hz
							{#if sampleRateWarning(stats.sampleRate)}(resampling?){/if}
						</td>
					</tr>
					<tr>
						<td>state</td>
						<td class:good={stats.contextState === 'running'}>{stats.contextState}</td>
					</tr>
					<tr>
						<td>hw output</td>
						<td class:warn={stats.hardwareOutputMs > 15}>{stats.hardwareOutputMs.toFixed(1)} ms</td>
					</tr>
				</tbody>
			</table>
		</div>
	</div>
</div>

<style>
	.diagnostics {
		border: 1px solid var(--line-1);
		border-radius: var(--radius-m);
		background: #080606;
		font-family: var(--font-mono);
		font-size: 0.7rem;
		color: var(--text-2);
		display: flex;
		flex-direction: column;
		overflow: hidden;
	}

	/* Terminal-style title bar */
	.panel-titlebar {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		padding: 0.45rem 0.75rem;
		background: var(--bg-1);
		border-bottom: 1px solid var(--line-1);
	}

	.titlebar-dot {
		width: 7px;
		height: 7px;
		border-radius: 50%;
		background: var(--accent);
		box-shadow: 0 0 6px var(--accent-glow);
	}

	.titlebar-label {
		text-transform: uppercase;
		letter-spacing: 0.14em;
		font-size: 0.62rem;
		color: var(--text-3);
	}

	.titlebar-status {
		margin-left: auto;
		font-size: 0.6rem;
		text-transform: uppercase;
		letter-spacing: 0.1em;
		color: var(--warn);
	}

	.titlebar-status.ok {
		color: var(--good);
	}

	.section {
		padding: 0.65rem 0.75rem;
	}

	.section + .section,
	.section-grid {
		border-top: 1px solid var(--line-1);
	}

	.section-grid {
		display: grid;
		grid-template-columns: 1fr 1fr;
	}

	.section-grid .section + .section {
		border-top: none;
		border-left: 1px solid var(--line-1);
	}

	@media (max-width: 480px) {
		.section-grid {
			grid-template-columns: 1fr;
		}

		.section-grid .section + .section {
			border-left: none;
			border-top: 1px solid var(--line-1);
		}
	}

	.section-title {
		font-weight: 600;
		color: var(--text-3);
		margin-bottom: 0.45rem;
		font-size: 0.6rem;
		text-transform: uppercase;
		letter-spacing: 0.14em;
	}

	.buffer-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		margin-bottom: 0.25rem;
	}

	.buffer-row .label {
		width: 64px;
		flex-shrink: 0;
		color: var(--text-3);
	}

	.bar-container {
		flex: 1;
		height: 8px;
		background: #121010;
		border-radius: 4px;
		border: 1px solid var(--line-1);
		overflow: hidden;
	}

	.bar-fill {
		height: 100%;
		border-radius: 3px;
		transition: width 0.2s ease, background 0.3s;
	}

	.fill-good {
		background: linear-gradient(90deg, rgba(94, 224, 138, 0.5), var(--good));
	}

	.fill-warn {
		background: linear-gradient(90deg, rgba(242, 201, 76, 0.5), var(--warn));
	}

	.fill-bad {
		background: linear-gradient(90deg, rgba(255, 107, 94, 0.5), var(--bad));
	}

	.buffer-row .value {
		width: 36px;
		text-align: right;
		font-variant-numeric: tabular-nums;
		color: var(--text-1);
	}

	.stat-row {
		display: flex;
		gap: 1rem;
		margin-bottom: 0.35rem;
		padding-left: 72px;
		font-size: 0.64rem;
		color: var(--text-3);
	}

	.stat-row b {
		color: var(--text-1);
		font-weight: 600;
	}

	.prebuffer-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		margin: 0.4rem 0 0.2rem;
	}

	.prebuffer-row .label {
		width: 64px;
		flex-shrink: 0;
		color: var(--text-3);
	}

	.preset-group {
		display: flex;
		gap: 0;
	}

	.preset-btn {
		padding: 3px 9px;
		border: 1px solid var(--line-2);
		background: transparent;
		color: var(--text-3);
		cursor: pointer;
		font-size: 0.62rem;
		font-family: var(--font-mono);
		line-height: 1.4;
		transition: all 0.15s;
	}

	.preset-btn:first-child {
		border-radius: 4px 0 0 4px;
	}

	.preset-btn:last-child {
		border-radius: 0 4px 4px 0;
	}

	.preset-btn + .preset-btn {
		border-left: none;
	}

	.preset-btn:hover {
		color: var(--text-1);
	}

	.preset-btn.active {
		background: var(--accent-dim);
		color: var(--accent);
		border-color: var(--accent);
	}

	.preset-btn.active + .preset-btn {
		border-left-color: var(--accent);
	}

	.preset-hint {
		margin-left: 3px;
		opacity: 0.6;
		font-size: 0.56rem;
	}

	.prebuffer-hint {
		margin: 0.25rem 0 0.5rem;
		padding-left: 72px;
		font-size: 0.6rem;
		color: var(--text-3);
		line-height: 1.4;
	}

	table {
		border-collapse: collapse;
		width: 100%;
	}

	td {
		padding: 2px 8px 2px 0;
		color: var(--text-3);
	}

	td:last-child {
		text-align: right;
		font-variant-numeric: tabular-nums;
		color: var(--text-1);
	}

	td.good {
		color: var(--good);
	}

	.warn,
	td.warn {
		color: var(--warn);
	}

	.latency-warning {
		background: var(--warn-dim);
		border-bottom: 1px solid rgba(242, 201, 76, 0.3);
		padding: 0.65rem 0.75rem;
	}

	.warning-title {
		font-weight: 600;
		color: var(--warn);
		font-size: 0.68rem;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		margin-bottom: 0.3rem;
	}

	.warning-message {
		color: var(--text-2);
		margin: 0 0 0.4rem;
		font-size: 0.66rem;
		line-height: 1.4;
	}

	.warning-instructions {
		background: #0a0808;
		border: 1px solid var(--line-1);
		border-radius: 4px;
		padding: 0.5rem;
		margin: 0 0 0.4rem;
		font-size: 0.62rem;
		line-height: 1.5;
		color: var(--text-2);
		white-space: pre-wrap;
		overflow-x: auto;
	}

	.copy-btn {
		padding: 3px 10px;
		border: 1px solid var(--warn);
		border-radius: 4px;
		background: transparent;
		color: var(--warn);
		cursor: pointer;
		font-size: 0.62rem;
		font-family: var(--font-mono);
		transition: background 0.15s;
	}

	.copy-btn:hover {
		background: var(--warn-dim);
	}
</style>
