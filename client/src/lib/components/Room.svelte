<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import Mixer from './Mixer.svelte';
	import VacateNotice from './VacateNotice.svelte';
	import LatencyDisplay from './LatencyDisplay.svelte';
	import AudioDiagnostics from './AudioDiagnostics.svelte';
	import LatencyTester from './LatencyTester.svelte';
	import { roomState, leaveRoom } from '../stores/room.js';
	import { audioState, setPipelineState, setTransportType } from '../stores/audio.js';
	import { settings } from '../stores/settings.js';
	import { updatePlaybackStats, updateCaptureStats, updateTransportStats, updateContextInfo, updateHardwareOutputMs } from '../stores/audio-stats.js';
	import { startCapture, type CaptureHandle } from '../audio/capture.js';
	import { startPlayback, type PlaybackHandle } from '../audio/playback.js';
	import { TransportBridge } from '../audio/transport-bridge.js';
	import { createTransport, detectTransportType, getTransportDescription } from '../transport/detect.js';
	import { resumeAudioContext, closeAudioContext, getAudioContext, getHardwareLatency } from '../audio/context.js';
	import { RTTMonitor } from '../latency/rtt-monitor.js';
	import { latencyBreakdown } from '../latency/breakdown.js';
	import type { Participant, LatencyBreakdown, LatencyInfo } from '../audio/types.js';

	let { roomName }: { roomName: string } = $props();

	let participants: Participant[] = $state([]);
	let vacateNotice = $state(false);
	let pipelineState = $state<string>('inactive');
	let transportDesc = $state('');
	let currentBreakdown: LatencyBreakdown | null = $state(null);
	let currentLatencyInfo: LatencyInfo | null = $state(null);
	let nerdMode = $state(false);

	let capture: CaptureHandle | null = null;
	let playback: PlaybackHandle | null = null;
	let bridge: TransportBridge | null = null;
	let activeTransport: import('../transport/transport.js').Transport | null = null;
	let rttMonitor: RTTMonitor | null = null;
	let errorDetail = $state('');
	let transportConnected = $state(false);
	let participantId: string | null = null;
	let statsTimer: ReturnType<typeof setInterval> | null = null;

	roomState.subscribe((s) => {
		participants = s.participants;
		vacateNotice = s.vacateNotice;
		participantId = s.participantId;
	});

	audioState.subscribe((s) => {
		pipelineState = s.pipelineState;
	});

	settings.subscribe((s) => {
		nerdMode = s.nerdMode;
	});

	latencyBreakdown.subscribe((lb) => {
		if (lb) {
			currentBreakdown = lb.breakdown;
			currentLatencyInfo = lb.info;
		} else {
			currentBreakdown = null;
			currentLatencyInfo = null;
		}
	});

	async function startAudio() {
		try {
			setPipelineState('initializing');
			errorDetail = '';

			// Must be triggered by user gesture
			await resumeAudioContext();

			// Start capture and playback first (these work independently)
			capture = await startCapture();
			playback = await startPlayback();

			// Fetch transport config (cert hash for WebTransport with self-signed certs)
			let certHash: string | undefined;
			try {
				const transportInfo = await fetch('/api/transport').then(r => r.json());
				certHash = transportInfo.cert_hash;
			} catch {
				// Server may not have /api/transport — continue without cert hash
			}

			// Determine URLs
			const wtUrl = `https://${window.location.hostname}:4433/wt`;
			const isDev = window.location.port === '3000';
			const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
			const wsUrl = isDev
				? `${wsProtocol}//${window.location.host}/ws`
				: `${wsProtocol}//${window.location.hostname}:8081`;

			// Helper: wire up a transport, connect, bind, and start bridge
			const wireTransport = async (
				transport: import('../transport/transport.js').Transport,
				url: string,
				connectOptions?: { certHash?: string }
			) => {
				transport.onMessage((msg) => {
					try {
						const data = JSON.parse(msg);
						handleControlMessage(data);
					} catch {
						// Invalid JSON
					}
				});

				await transport.connect(url, connectOptions);

				if (participantId) {
					transport.sendReliable(
						JSON.stringify({
							type: 'bind',
							participant_id: participantId,
							room: roomName
						})
					);
				}

				bridge = new TransportBridge({
					captureRingBufferSAB: capture!.ringBufferSAB,
					playbackRingBufferSAB: playback!.ringBufferSAB,
					transport,
					capturePort: capture!.capturePort
				});
				bridge.start();
				activeTransport = transport;
				transportConnected = true;

				// Start RTT monitoring
				rttMonitor = new RTTMonitor(transport);
				rttMonitor.start();
			};

			// Try preferred transport, fall back if it fails
			const preferredType = detectTransportType();
			let connected = false;

			if (preferredType === 'webtransport') {
				try {
					setTransportType('webtransport');
					transportDesc = getTransportDescription();
					const transport = createTransport();
					await wireTransport(transport, wtUrl, { certHash });
					connected = true;
				} catch (wtErr) {
					console.warn('[Tutti] WebTransport failed, falling back to WebRTC:', wtErr);
				}
			}

			if (!connected) {
				try {
					setTransportType('webrtc');
					transportDesc = 'WebRTC DataChannel' + (preferredType === 'webtransport' ? ' (fallback)' : '');
					const { WebRTCTransport } = await import('../transport/webrtc.js');
					const rtcTransport = new WebRTCTransport();
					await wireTransport(rtcTransport, wsUrl);
				} catch (rtcErr) {
					console.warn('[Tutti] Transport not connected — audio capture is local only:', rtcErr);
					transportConnected = false;
				}
			}

			// Wire up diagnostics stats listeners
			wireStats();

			setPipelineState('active');
		} catch (err) {
			const message = err instanceof Error ? err.message : 'Unknown error';
			console.error('[Tutti] Audio setup failed:', err);
			errorDetail = message;
			setPipelineState('error', message);
		}
	}

	function wireStats() {
		// Update audio context info
		const ctx = getAudioContext();
		updateContextInfo(ctx.sampleRate, ctx.state);

		// Listen for playback worklet stats
		if (playback) {
			playback.playbackPort.onmessage = (event: MessageEvent) => {
				if (event.data?.type === 'stats') {
					updatePlaybackStats(event.data);
				}
			};
		}

		// Listen for capture worklet stats (port already handles frame-ready via TransportBridge)
		if (capture) {
			const originalHandler = capture.capturePort.onmessage;
			capture.capturePort.onmessage = (event: MessageEvent) => {
				if (event.data?.type === 'stats') {
					updateCaptureStats(event.data);
				} else if (originalHandler) {
					originalHandler.call(capture!.capturePort, event);
				}
			};
		}

		// Poll transport stats periodically
		statsTimer = setInterval(() => {
			if (bridge) {
				updateTransportStats(bridge.getStats());
			}
			// Also update context state in case it changes
			const ctx = getAudioContext();
			updateContextInfo(ctx.sampleRate, ctx.state);
			// Push hardware output latency for diagnostics panel
			const hw = getHardwareLatency();
			updateHardwareOutputMs(hw.outputMs);
		}, 500);
	}

	function toggleNerdMode() {
		settings.update((s) => ({ ...s, nerdMode: !s.nerdMode }));
	}

	function handleControlMessage(msg: Record<string, unknown>) {
		switch (msg.type) {
			case 'room_state':
				roomState.update((s) => ({
					...s,
					participants: (msg.participants as Array<{ id: string; name: string }>).map(
						(p) => ({
							id: p.id,
							name: p.name,
							gain: 1.0,
							muted: false
						})
					)
				}));
				break;
			case 'participant_joined':
				roomState.update((s) => ({
					...s,
					participants: [
						...s.participants,
						{
							id: msg.id as string,
							name: msg.name as string,
							gain: 1.0,
							muted: false
						}
					]
				}));
				break;
			case 'participant_left':
				roomState.update((s) => ({
					...s,
					participants: s.participants.filter((p) => p.id !== msg.id)
				}));
				break;
			case 'vacate_request':
				roomState.update((s) => ({ ...s, vacateNotice: true }));
				break;
			case 'pong':
				rttMonitor?.handlePong(msg as unknown as { id: number });
				break;
		}
	}

	function handleGainChange(pid: string, gain: number) {
		roomState.update((s) => ({
			...s,
			participants: s.participants.map((p) =>
				p.id === pid ? { ...p, gain } : p
			)
		}));
		activeTransport?.sendReliable(
			JSON.stringify({ type: 'gain', participant_id: pid, value: gain })
		);
	}

	function handleMuteToggle(pid: string, muted: boolean) {
		roomState.update((s) => ({
			...s,
			participants: s.participants.map((p) =>
				p.id === pid ? { ...p, muted } : p
			)
		}));
		activeTransport?.sendReliable(
			JSON.stringify({ type: 'mute', participant_id: pid, muted })
		);
	}

	async function handleLeave() {
		if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
		rttMonitor?.stop();
		rttMonitor = null;
		bridge?.stop();
		activeTransport?.disconnect();
		capture?.stop();
		playback?.stop();
		activeTransport = null;
		await closeAudioContext();
		await leaveRoom();
	}

	onDestroy(() => {
		if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
		rttMonitor?.stop();
		rttMonitor = null;
		bridge?.stop();
		activeTransport?.disconnect();
		capture?.stop();
		playback?.stop();
		activeTransport = null;
	});
</script>

<div class="room">
	<VacateNotice visible={vacateNotice} />

	<header class="room-header">
		<h1>{roomName}</h1>
		<div class="header-actions">
			<button class="nerd-btn" class:active={nerdMode} onclick={toggleNerdMode} title="Toggle diagnostics">&#9881;</button>
			<button class="leave-btn" onclick={handleLeave}>Leave</button>
		</div>
	</header>

	{#if pipelineState === 'inactive'}
		<div class="start-prompt">
			<p>Ready to rehearse?</p>
			<button class="start-btn" onclick={startAudio}>Start Audio</button>
			<p class="hint">Requires microphone access. Use wired headphones for best results.</p>
		</div>
	{:else if pipelineState === 'initializing'}
		<div class="status-message">Setting up audio...</div>
	{:else if pipelineState === 'error'}
		<div class="error-message">
			<p>Audio setup failed.</p>
			{#if errorDetail}
				<p class="error-detail">{errorDetail}</p>
			{/if}
			<p>Check microphone permissions and that you're using wired headphones.</p>
			<button onclick={startAudio}>Retry</button>
		</div>
	{:else}
		{#if !transportConnected}
			<div class="transport-warning">
				Microphone active (local only). Server transport not connected.
			</div>
		{/if}

		<Mixer {participants} onGainChange={handleGainChange} onMuteToggle={handleMuteToggle} />

		{#if nerdMode}
			<LatencyDisplay latency={currentLatencyInfo} breakdown={currentBreakdown} />
			<AudioDiagnostics {transportDesc} {transportConnected} />
			{#if capture && playback && bridge}
				<LatencyTester capturePort={capture.capturePort} playbackPort={playback.playbackPort} setLoopback={(enabled) => bridge?.setLoopback(enabled)} />
			{/if}
		{/if}
	{/if}
</div>

<style>
	.room {
		display: flex;
		flex-direction: column;
		max-width: 640px;
		margin: 0 auto;
		padding: 1rem;
		min-height: 100vh;
	}

	.room-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 1rem;
		padding-bottom: 0.75rem;
		border-bottom: 1px solid #333;
	}

	h1 {
		font-size: 1.5rem;
		margin: 0;
	}

	.header-actions {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}

	.nerd-btn {
		padding: 4px 8px;
		border: 1px solid #555;
		border-radius: 6px;
		background: transparent;
		color: #888;
		cursor: pointer;
		font-size: 1rem;
		line-height: 1;
	}

	.nerd-btn.active {
		color: #4ade80;
		border-color: #4ade80;
	}

	.leave-btn {
		padding: 6px 16px;
		border: 1px solid #555;
		border-radius: 6px;
		background: transparent;
		color: #ccc;
		cursor: pointer;
	}

	.start-prompt {
		text-align: center;
		padding: 3rem 1rem;
	}

	.start-btn {
		padding: 12px 32px;
		font-size: 1.1rem;
		border: none;
		border-radius: 8px;
		background: #4ade80;
		color: #000;
		font-weight: 600;
		cursor: pointer;
		margin: 1rem 0;
	}

	.hint {
		font-size: 0.8rem;
		color: #666;
	}

	.status-message {
		text-align: center;
		padding: 3rem;
		color: #999;
	}

	.error-message {
		text-align: center;
		padding: 2rem;
		color: #f87171;
	}

	.error-detail {
		font-family: monospace;
		font-size: 0.75rem;
		color: #f8717199;
		margin: 0.25rem 0;
	}

	.error-message button {
		margin-top: 0.5rem;
		padding: 6px 16px;
		border: 1px solid #f87171;
		border-radius: 6px;
		background: transparent;
		color: #f87171;
		cursor: pointer;
	}

	.transport-warning {
		text-align: center;
		padding: 0.5rem 1rem;
		margin-bottom: 1rem;
		background: #3a3a1a;
		color: #facc15;
		border-radius: 6px;
		font-size: 0.85rem;
	}

</style>
