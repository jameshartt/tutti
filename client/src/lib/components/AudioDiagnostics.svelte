<script lang="ts">
	import { audioStats, type AudioStats } from '../stores/audio-stats.js';

	let { transportDesc, transportConnected }: { transportDesc: string; transportConnected: boolean } =
		$props();

	let stats: AudioStats = $state({
		playbackUnderruns: 0,
		playbackPartialFrames: 0,
		playbackTotalFrames: 0,
		playbackFillLevel: 0,
		playbackPrebuffering: false,
		captureDroppedFrames: 0,
		captureTotalFrames: 0,
		captureFillLevel: 0,
		packetsSent: 0,
		packetsReceived: 0,
		sampleRate: 0,
		contextState: 'suspended'
	});

	audioStats.subscribe((s) => (stats = s));

	// Ring buffer capacity (matches SAMPLES_PER_FRAME * 8 in playback.ts / capture.ts)
	const BUFFER_CAPACITY = 1024;

	function fillPercent(fillLevel: number): number {
		return Math.round((fillLevel / BUFFER_CAPACITY) * 100);
	}

	function fillColor(fillLevel: number): string {
		const pct = fillPercent(fillLevel);
		if (pct > 50) return '#4ade80';
		if (pct > 20) return '#facc15';
		return '#f87171';
	}

	function sampleRateWarning(rate: number): boolean {
		return rate > 0 && rate !== 48000;
	}
</script>

<div class="diagnostics">
	<div class="section">
		<div class="section-title">Buffer Health</div>
		<div class="buffer-row">
			<span class="label">Playback</span>
			<div class="bar-container">
				<div
					class="bar-fill"
					style="width: {fillPercent(stats.playbackFillLevel)}%; background: {fillColor(stats.playbackFillLevel)}"
				></div>
			</div>
			<span class="value">{fillPercent(stats.playbackFillLevel)}%</span>
		</div>
		<div class="stat-row">
			<span>Underruns: {stats.playbackUnderruns}</span>
			<span>Partial: {stats.playbackPartialFrames}</span>
			{#if stats.playbackPrebuffering}<span class="warn">PREBUFFERING</span>{/if}
		</div>
		<div class="buffer-row">
			<span class="label">Capture</span>
			<div class="bar-container">
				<div
					class="bar-fill"
					style="width: {fillPercent(stats.captureFillLevel)}%; background: {fillColor(stats.captureFillLevel)}"
				></div>
			</div>
			<span class="value">{fillPercent(stats.captureFillLevel)}%</span>
		</div>
		<div class="stat-row">
			<span>Dropped: {stats.captureDroppedFrames}</span>
		</div>
	</div>

	<div class="section">
		<div class="section-title">Transport</div>
		<table>
			<tbody>
				<tr>
					<td>Type</td>
					<td>{transportDesc || 'none'}</td>
				</tr>
				<tr>
					<td>Connected</td>
					<td>{transportConnected ? 'yes' : 'no'}</td>
				</tr>
				<tr>
					<td>Packets sent</td>
					<td>{stats.packetsSent}</td>
				</tr>
				<tr>
					<td>Packets received</td>
					<td>{stats.packetsReceived}</td>
				</tr>
			</tbody>
		</table>
	</div>

	<div class="section">
		<div class="section-title">Audio Context</div>
		<table>
			<tbody>
				<tr>
					<td>Sample rate</td>
					<td class:warn={sampleRateWarning(stats.sampleRate)}>
						{stats.sampleRate || '—'} Hz
						{#if sampleRateWarning(stats.sampleRate)}(resampling?){/if}
					</td>
				</tr>
				<tr>
					<td>State</td>
					<td>{stats.contextState}</td>
				</tr>
			</tbody>
		</table>
	</div>
</div>

<style>
	.diagnostics {
		margin-top: 1rem;
		padding: 0.75rem;
		border: 1px solid #333;
		border-radius: 6px;
		font-family: monospace;
		font-size: 0.7rem;
		color: #aaa;
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
	}

	.section-title {
		font-weight: 600;
		color: #ccc;
		margin-bottom: 0.35rem;
		font-size: 0.72rem;
		text-transform: uppercase;
		letter-spacing: 0.05em;
	}

	.buffer-row {
		display: flex;
		align-items: center;
		gap: 0.5rem;
		margin-bottom: 0.15rem;
	}

	.buffer-row .label {
		width: 60px;
		flex-shrink: 0;
	}

	.bar-container {
		flex: 1;
		height: 8px;
		background: #222;
		border-radius: 4px;
		overflow: hidden;
	}

	.bar-fill {
		height: 100%;
		border-radius: 4px;
		transition: width 0.2s ease;
	}

	.buffer-row .value {
		width: 32px;
		text-align: right;
		font-variant-numeric: tabular-nums;
	}

	.stat-row {
		display: flex;
		gap: 1rem;
		margin-bottom: 0.25rem;
		padding-left: 60px;
		font-size: 0.65rem;
		color: #888;
	}

	table {
		border-collapse: collapse;
		width: 100%;
	}

	td {
		padding: 1px 8px 1px 0;
	}

	td:last-child {
		text-align: right;
		font-variant-numeric: tabular-nums;
	}

	.warn {
		color: #facc15;
	}
</style>
