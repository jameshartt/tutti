<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import { get } from 'svelte/store';
	import Mixer from './Mixer.svelte';
	import VacateNotice from './VacateNotice.svelte';
	import LatencyDisplay from './LatencyDisplay.svelte';
	import AudioDiagnostics from './AudioDiagnostics.svelte';
	import LatencyTester from './LatencyTester.svelte';
	import { roomState, leaveRoom, sendLeaveRequest, joinRoom } from '../stores/room.js';
	import { audioState, setPipelineState, setTransportType } from '../stores/audio.js';
	import { settings } from '../stores/settings.js';
	import { audioStats, updatePlaybackStats, updateCaptureStats, updateTransportStats, updateContextInfo, updateHardwareOutputMs, updateCodecMode } from '../stores/audio-stats.js';
	import { opusSupported } from '../audio/opus.js';
	import { startCapture, type CaptureHandle } from '../audio/capture.js';
	import { startPlayback, type PlaybackHandle } from '../audio/playback.js';
	import { TransportBridge } from '../audio/transport-bridge.js';
	import { createTransport, detectTransportType, getTransportDescription } from '../transport/detect.js';
	import { reportTransportEvent } from '../transport/diagnostics.js';
	import { resumeAudioContext, closeAudioContext, getAudioContext, getHardwareLatency } from '../audio/context.js';
	import { RTTMonitor } from '../latency/rtt-monitor.js';
	import { latencyBreakdown } from '../latency/breakdown.js';
	import type { LatencyBreakdown, LatencyInfo } from '../audio/types.js';

	let { roomName }: { roomName: string } = $props();

	// Auto-subscriptions ($store) are cleaned up on destroy — no leaked subscribers
	let participants = $derived($roomState.participants);
	let vacateNotice = $derived($roomState.vacateNotice);
	let participantId = $derived($roomState.participantId);
	let pipelineState = $derived($audioState.pipelineState);
	let nerdMode = $derived($settings.nerdMode);

	let currentBreakdown: LatencyBreakdown | null = $derived($latencyBreakdown?.breakdown ?? null);
	let currentLatencyInfo: LatencyInfo | null = $derived($latencyBreakdown?.info ?? null);

	let transportDesc = $state('');
	let prebufferFrames = $state(get(settings).prebufferFrames);

	// Push prebuffer changes from settings into the playback worklet
	$effect(() => {
		const newPrebuffer = $settings.prebufferFrames;
		if (newPrebuffer !== prebufferFrames) {
			prebufferFrames = newPrebuffer;
			playback?.sendConfig({ prebufferFrames: newPrebuffer });
		}
	});

	// Self-channel state
	let micMuted = $state(false);
	let micBoost = $state(1.0);
	let masterVolume = $state(1.0);
	let inputLevel = $state(0);

	// Reconnect state
	let reconnectAttempts = $state(0);
	let reconnecting = $state(false);
	let reconnectTimer: ReturnType<typeof setTimeout> | null = null;

	// $state so the nerd-zone template block reacts when the pipeline comes up
	let capture: CaptureHandle | null = $state(null);
	let playback: PlaybackHandle | null = $state(null);
	let bridge: TransportBridge | null = $state(null);
	let activeTransport: import('../transport/transport.js').Transport | null = null;
	let rttMonitor: RTTMonitor | null = null;
	let errorDetail = $state('');
	let transportConnected = $state(false);
	let statsTimer: ReturnType<typeof setInterval> | null = null;

	// Weak-link codec management
	let codecMode = $state<'pcm' | 'opus'>('pcm');
	let codecWarning = $state(false);
	let opusAvailable = false;
	const linkState = { prevRecv: 0, prevGaps: 0, badSince: 0, goodSince: 0, lastSwitch: 0 };

	// Connect retry loop
	let connectAttempt = $state(0);
	let retryingConnect = $state(false);
	let connectAborted = false;

	// beforeunload — leave promptly on tab close (keepalive fetch survives
	// unload; sendBeacon deliveries were observed getting lost)
	function handleBeforeUnload() {
		if (roomName && participantId) {
			sendLeaveRequest(roomName, participantId);
		}
	}

	onMount(() => {
		window.addEventListener('beforeunload', handleBeforeUnload);
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
			playback.sendConfig({ prebufferFrames });

			// Fetch transport config from server (URLs + optional cert hash)
			let certHash: string | undefined;
			const wsProtocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
			let wtUrl = `https://${window.location.hostname}:4433/wt`;
			let wsUrl = `${wsProtocol}//${window.location.host}/ws`;
			try {
				const transportInfo = await fetch('/api/transport').then(r => r.json());
				certHash = transportInfo.cert_hash;
				if (transportInfo.wt_url) wtUrl = transportInfo.wt_url;
				// wsUrl is NOT overridden from server — the client's default
				// (based on window.location.host) always routes through the
				// reverse proxy (Caddy in prod, Vite in dev).
			} catch {
				// Server may not have /api/transport — use defaults
			}

			// Helper: bound a transport connect attempt. Without this a
			// stalled QUIC handshake (Firefox, UDP-blocked networks) or a
			// failing ICE negotiation (iOS, strict NATs) hangs for minutes
			// before the fallback path ever runs.
			const connectWithTimeout = async (
				transport: import('../transport/transport.js').Transport,
				url: string,
				timeoutMs: number,
				connectOptions?: { certHash?: string }
			) => {
				let timer: ReturnType<typeof setTimeout> | undefined;
				try {
					await Promise.race([
						transport.connect(url, connectOptions),
						new Promise<never>((_, reject) => {
							timer = setTimeout(() => {
								transport.disconnect();
								reject(new Error(`Transport connect timed out after ${timeoutMs}ms`));
							}, timeoutMs);
						})
					]);
				} finally {
					clearTimeout(timer);
				}
			};

			// Helper: wire up a transport, connect, bind, and start bridge
			const wireTransport = async (
				transport: import('../transport/transport.js').Transport,
				url: string,
				timeoutMs: number,
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

				await connectWithTimeout(transport, url, timeoutMs, connectOptions);

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

				// Listen for transport disconnect. Ignore events during an
				// intentional leave — our own teardown fires 'disconnected'
				// synchronously, and treating that as a connection loss would
				// kick off the reconnect flow mid-departure.
				transport.onStateChange((state) => {
					if (leaving) return;
					if (state === 'disconnected' || state === 'failed') {
						handleTransportDisconnect();
					}
				});
			};

			// Try preferred transport, fall back if it fails — and retry the
			// whole sequence with backoff. On marginal links (1-bar 5G) the
			// ICE handshake is a coin flip; several flips usually land.
			const preferredType = detectTransportType();
			opusAvailable = await opusSupported();
			let connected = false;
			const maxAttempts = 8;
			retryingConnect = true;

			for (let attempt = 1; attempt <= maxAttempts && !connected && !connectAborted; attempt++) {
				connectAttempt = attempt;

				if (preferredType === 'webtransport') {
					try {
						setTransportType('webtransport');
						transportDesc = getTransportDescription();
						const transport = createTransport();
						await wireTransport(transport, wtUrl, 5000, { certHash });
						connected = true;
						reportTransportEvent({ stage: 'webtransport', ok: true, attempt });
					} catch (wtErr) {
						console.warn('[Tutti] WebTransport failed, falling back to WebRTC:', wtErr);
						reportTransportEvent({
							stage: 'webtransport',
							ok: false,
							attempt,
							error: String(wtErr).slice(0, 200)
						});
					}
				}

				if (!connected) {
					const { WebRTCTransport } = await import('../transport/webrtc.js');
					const rtcTransport = new WebRTCTransport();
					try {
						setTransportType('webrtc');
						transportDesc = 'WebRTC DataChannel' + (preferredType === 'webtransport' ? ' (fallback)' : '');
						await wireTransport(rtcTransport, wsUrl, 12000);
						connected = true;
						reportTransportEvent({
							stage: 'webrtc',
							ok: true,
							attempt,
							...rtcTransport.getDiagnostics()
						});
					} catch (rtcErr) {
						console.warn(`[Tutti] Transport attempt ${attempt}/${maxAttempts} failed:`, rtcErr);
						transportConnected = false;
						reportTransportEvent({
							stage: 'webrtc',
							ok: false,
							attempt,
							error: String(rtcErr).slice(0, 200),
							...rtcTransport.getDiagnostics()
						});
					}
				}

				if (!connected && attempt < maxAttempts && !connectAborted) {
					const delay = Math.min(15000, 1000 * 2 ** (attempt - 1));
					await new Promise((r) => setTimeout(r, delay));
				}
			}
			retryingConnect = false;

			// A cellular-class link cannot carry uncompressed PCM — start
			// compressed instead of letting the user discover the crackle
			if (connected && opusAvailable) {
				const nav = navigator as Navigator & { connection?: { effectiveType?: string } };
				const et = nav.connection?.effectiveType ?? '';
				if (et === 'slow-2g' || et === '2g' || et === '3g') {
					switchCodec('opus', `initial network hint: ${et}`);
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

	function switchCodec(mode: 'pcm' | 'opus', reason: string) {
		if (!bridge || !activeTransport || codecMode === mode) return;
		linkState.lastSwitch = Date.now();
		bridge.setCodec(mode);
		try {
			activeTransport.sendReliable(JSON.stringify({ type: 'codec', codec: mode }));
		} catch {
			// Transport gone; reconnect flow will reset codec state anyway
		}
		codecMode = mode;
		codecWarning = mode === 'opus';
		updateCodecMode(mode);
		reportTransportEvent({ stage: 'codec', ok: true, codec: mode, reason });
		console.log(`[Tutti] Codec → ${mode} (${reason})`);
	}

	// Evaluate link health every 2s; downgrade to Opus on sustained trouble,
	// upgrade back to PCM after a long clean stretch. Hysteresis + dwell
	// time prevent flapping.
	function evaluateLink() {
		if (!transportConnected || !bridge) return;
		const s = get(audioStats);
		const recvDelta = s.packetsReceived - linkState.prevRecv;
		const gapsDelta = s.seqGaps - linkState.prevGaps;
		linkState.prevRecv = s.packetsReceived;
		linkState.prevGaps = s.seqGaps;

		const rtt = s.networkRTT;
		const loss = recvDelta + gapsDelta > 0 ? gapsDelta / (recvDelta + gapsDelta) : 0;
		const now = Date.now();
		const bad = rtt > 250 || loss > 0.08;
		const good = rtt > 0 && rtt < 120 && loss < 0.01;

		if (bad) {
			linkState.goodSince = 0;
			if (!linkState.badSince) linkState.badSince = now;
			if (
				codecMode === 'pcm' &&
				opusAvailable &&
				now - linkState.badSince > 4000 &&
				now - linkState.lastSwitch > 15000
			) {
				switchCodec('opus', `link degraded: rtt=${Math.round(rtt)}ms loss=${(loss * 100).toFixed(1)}%`);
			}
		} else if (good) {
			linkState.badSince = 0;
			if (!linkState.goodSince) linkState.goodSince = now;
			if (
				codecMode === 'opus' &&
				now - linkState.goodSince > 30000 &&
				now - linkState.lastSwitch > 15000
			) {
				switchCodec('pcm', 'link recovered');
			}
		} else {
			// Middle ground: not bad enough to downgrade, not clean enough
			// to earn an upgrade
			linkState.badSince = 0;
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
					if (event.data.peakLevel !== undefined) {
						inputLevel = event.data.peakLevel;
					}
				} else if (originalHandler) {
					originalHandler.call(capture!.capturePort, event);
				}
			};
		}

		// Poll transport stats periodically
		let statsTicks = 0;
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

			statsTicks++;

			// Link-quality evaluation every 2s (4 ticks)
			if (statsTicks % 4 === 0) evaluateLink();

			// Every 10s, beacon client-side audio health to the server so
			// incidents are diagnosable from server logs after the fact
			if (statsTicks % 20 === 0 && activeTransport && transportConnected) {
				const s = get(audioStats);
				try {
					activeTransport.sendReliable(
						JSON.stringify({
							type: 'client_stats',
							underruns: s.playbackUnderruns,
							partial: s.playbackPartialFrames,
							skips: s.playbackSkipAheads,
							fill: s.playbackFillLevel,
							prebuffering: s.playbackPrebuffering,
							cap_dropped: s.captureDroppedFrames,
							sent: s.packetsSent,
							recv: s.packetsReceived,
							gaps: s.seqGaps,
							reordered: s.seqReordered,
							rtt: Math.round(s.networkRTT * 10) / 10,
							rate: s.sampleRate,
							codec: s.codec,
							mic_muted: micMuted
						})
					);
				} catch {
					// Transport went away between checks — ignore
				}
			}
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
					participants: (msg.participants as Array<{ id: string; name: string }>)
						.filter((p) => p.id !== s.participantId)
						.map((p) => ({
							id: p.id,
							name: p.name,
							gain: 1.0,
							muted: false
						}))
				}));
				break;
			case 'participant_joined':
				roomState.update((s) => {
					if ((msg.id as string) === s.participantId) return s;
					return {
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
					};
				});
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
			case 'error':
				// The server reaps participants that take too long to bind
				// (slow permission grants / transport fallback). Recover by
				// re-joining for a fresh ID and re-binding — previously this
				// error was ignored and the room sat in a zombie state.
				if (msg.error === 'participant_not_found') {
					rejoinAndRebind();
				}
				break;
		}
	}

	let rebinding = false;
	async function rejoinAndRebind() {
		if (rebinding || !activeTransport) return;
		rebinding = true;
		try {
			const alias = get(roomState).alias;
			if (!alias) return;
			const result = await joinRoom(roomName, alias);
			if (!result.success) {
				console.warn('[Tutti] Re-join after stale bind failed:', result.error);
				return;
			}
			const freshId = get(roomState).participantId;
			activeTransport.sendReliable(
				JSON.stringify({ type: 'bind', participant_id: freshId, room: roomName })
			);
		} finally {
			rebinding = false;
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

	function handleMicMute(muted: boolean) {
		micMuted = muted;
		bridge?.setMicMuted(muted);
	}

	function handleMicBoost(gain: number) {
		micBoost = gain;
		capture?.capturePort.postMessage({ type: 'input-gain', gain });
	}

	function handleMasterVolume(gain: number) {
		masterVolume = gain;
		playback?.playbackPort.postMessage({ type: 'volume', gain });
	}

	function handleTransportDisconnect() {
		if (pipelineState === 'disconnected') return; // guard double-fire

		// Tear down audio pipeline
		if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
		rttMonitor?.stop();
		rttMonitor = null;
		bridge?.stop();
		bridge = null;
		capture?.stop();
		capture = null;
		playback?.stop();
		playback = null;
		// Null before disconnect: disconnect() fires state callbacks
		// synchronously, and re-entry must find no transport to act on
		const transport = activeTransport;
		activeTransport = null;
		transport?.disconnect();
		transportConnected = false;
		codecMode = 'pcm';
		codecWarning = false;
		updateCodecMode('pcm');
		linkState.badSince = linkState.goodSince = linkState.lastSwitch = 0;
		closeAudioContext();

		setPipelineState('disconnected');

		// Auto-reconnect after 3s
		reconnectTimer = setTimeout(() => handleReconnect(), 3000);
	}

	async function handleReconnect() {
		reconnectAttempts++;
		reconnecting = true;

		try {
			// Read alias from store for rejoin
			const alias = get(roomState).alias;

			if (!alias) {
				reconnecting = false;
				return;
			}

			// Rejoin room (gets new participant ID)
			const result = await joinRoom(roomName, alias);
			if (!result.success) throw new Error(result.error ?? 'rejoin failed');

			// Restart the full audio pipeline
			await startAudio();

			// Success — reset reconnect state
			reconnectAttempts = 0;
			reconnecting = false;
		} catch (err) {
			console.warn('[Tutti] Reconnect attempt failed:', err);
			reconnecting = false;

			// Keep trying with capped backoff for as long as we're in the
			// room — transient radio drops recover on their own timetable.
			// (Manual buttons appear after 2 attempts as an escape hatch.)
			reconnectTimer = setTimeout(
				() => handleReconnect(),
				Math.min(15000, 3000 * (reconnectAttempts + 1))
			);
		}
	}

	// Leaving must FEEL instant: acknowledge the press synchronously, never
	// block on the network (leaveRoom is a fire-and-forget beacon), and let
	// the async audio-context close finish in the background. The room-state
	// reset triggers navigation back to the lobby immediately.
	let leaving = $state(false);

	function handleLeave() {
		if (leaving) return;
		leaving = true;
		connectAborted = true;
		if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
		if (statsTimer) { clearInterval(statsTimer); statsTimer = null; }
		rttMonitor?.stop();
		rttMonitor = null;
		bridge?.stop();
		activeTransport?.disconnect();
		capture?.stop();
		playback?.stop();
		activeTransport = null;
		void closeAudioContext();
		leaveRoom();
	}

	onDestroy(() => {
		connectAborted = true;
		window.removeEventListener('beforeunload', handleBeforeUnload);
		if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
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

<div class="room" class:leaving>
	<VacateNotice visible={vacateNotice} />

	<header class="room-header">
		<div class="header-title">
			<a class="back-link" href="/" onclick={(e) => { e.preventDefault(); handleLeave(); }} title="Back to lobby">&larr;</a>
			<h1>{roomName}</h1>
			{#if pipelineState === 'active'}
				<span class="live-pill" class:offline={!transportConnected}>
					<span class="live-pill-dot"></span>
					{transportConnected ? 'live' : 'local'}
				</span>
			{/if}
		</div>
		<div class="header-actions">
			<button class="nerd-btn" class:active={nerdMode} onclick={toggleNerdMode} title="Toggle diagnostics" aria-label="Toggle diagnostics">&#9881;</button>
			<button class="btn btn-ghost leave-btn" onclick={handleLeave} disabled={leaving}>
				{leaving ? 'Leaving…' : 'Leave'}
			</button>
		</div>
	</header>

	{#if pipelineState === 'inactive'}
		<div class="start-prompt">
			<div class="start-glow" aria-hidden="true"></div>
			<p class="start-eyebrow">The room is yours</p>
			<p class="start-title">Ready to rehearse?</p>
			<button class="btn btn-primary start-btn" onclick={startAudio}>
				<span class="start-icon" aria-hidden="true">&#9679;</span> Start audio
			</button>
			<p class="hint">Needs microphone access &middot; wired headphones strongly recommended</p>
		</div>
	{:else if pipelineState === 'initializing'}
		<div class="status-message">
			<span class="tuning-bars" aria-hidden="true"><span></span><span></span><span></span></span>
			Setting up audio&hellip;
		</div>
	{:else if pipelineState === 'error'}
		<div class="error-message">
			<p class="error-title">Audio setup failed</p>
			{#if errorDetail}
				<p class="error-detail">{errorDetail}</p>
			{/if}
			<p class="error-help">Check microphone permissions and that you're using wired headphones.</p>
			<button class="btn btn-danger-ghost" onclick={startAudio}>Retry</button>
		</div>
	{:else if pipelineState === 'disconnected'}
		<div class="disconnect-message">
			{#if reconnecting}
				<p>Reconnecting&hellip; (attempt {reconnectAttempts})</p>
			{:else if reconnectAttempts >= 2}
				<p>Connection lost.</p>
				<div class="disconnect-actions">
					<button class="btn reconnect-btn" onclick={() => handleReconnect()}>Reconnect</button>
					<button class="btn btn-ghost" onclick={handleLeave} disabled={leaving}>
					{leaving ? 'Leaving…' : 'Leave'}
				</button>
				</div>
			{:else}
				<p>Connection lost. Reconnecting shortly&hellip;</p>
			{/if}
		</div>
	{:else}
		{#if !transportConnected}
			<div class="transport-warning">
				{#if retryingConnect}
					Connecting to server &mdash; attempt {connectAttempt} of 8&hellip; Microphone is live locally.
				{:else}
					Microphone active (local only) &mdash; server transport not connected.
				{/if}
			</div>
		{/if}

		{#if codecWarning && transportConnected}
			<div class="codec-warning">
				Weak connection &mdash; switched to compressed audio (slightly more latency,
				lower fidelity). Full quality returns automatically when the link recovers.
			</div>
		{/if}

		<Mixer
			{participants}
			onGainChange={handleGainChange}
			onMuteToggle={handleMuteToggle}
			{inputLevel}
			{micMuted}
			{micBoost}
			{masterVolume}
			onMicMuteToggle={handleMicMute}
			onMicBoostChange={handleMicBoost}
			onMasterVolumeChange={handleMasterVolume}
		/>

		{#if nerdMode}
			<div class="nerd-zone">
				<LatencyDisplay latency={currentLatencyInfo} breakdown={currentBreakdown} />
				<AudioDiagnostics {transportDesc} {transportConnected} />
				{#if capture && playback && bridge}
					<LatencyTester capturePort={capture.capturePort} playbackPort={playback.playbackPort} setLoopback={(enabled) => bridge?.setLoopback(enabled)} />
				{/if}
			</div>
		{/if}
	{/if}
</div>

<style>
	.room {
		display: flex;
		flex-direction: column;
		max-width: 680px;
		margin: 0 auto;
		padding: 1rem 1.25rem 2rem;
		min-height: 100vh;
	}

	.room-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 1.25rem;
		padding-bottom: 0.85rem;
		border-bottom: 1px solid var(--line-1);
	}

	.header-title {
		display: flex;
		align-items: center;
		gap: 0.75rem;
		min-width: 0;
	}

	.back-link {
		color: var(--text-3);
		text-decoration: none;
		font-size: 1.1rem;
		transition: color 0.15s, transform 0.15s var(--ease-snap);
	}

	.back-link:hover {
		color: var(--text-1);
		transform: translateX(-2px);
	}

	h1 {
		font-family: var(--font-display);
		font-weight: 600;
		font-size: 1.6rem;
		letter-spacing: 0.01em;
		margin: 0;
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
	}

	.live-pill {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		padding: 3px 10px;
		border-radius: 999px;
		background: var(--good-dim);
		color: var(--good);
		font-family: var(--font-mono);
		font-size: 0.62rem;
		text-transform: uppercase;
		letter-spacing: 0.12em;
		flex-shrink: 0;
	}

	.live-pill.offline {
		background: var(--warn-dim);
		color: var(--warn);
	}

	.live-pill-dot {
		width: 5px;
		height: 5px;
		border-radius: 50%;
		background: currentColor;
		animation: pulse-dot 2s ease-in-out infinite;
	}

	.header-actions {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}

	.nerd-btn {
		padding: 6px 10px;
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		background: transparent;
		color: var(--text-3);
		cursor: pointer;
		font-size: 1rem;
		line-height: 1;
		transition: color 0.15s, border-color 0.15s, box-shadow 0.2s;
	}

	.nerd-btn:hover {
		color: var(--text-2);
	}

	.nerd-btn.active {
		color: var(--accent);
		border-color: var(--accent);
		box-shadow: 0 0 12px -4px var(--accent-glow);
	}

	.leave-btn {
		padding: 6px 16px;
	}

	/* --- Start screen --------------------------------------- */

	.start-prompt {
		position: relative;
		text-align: center;
		padding: 4.5rem 1rem 4rem;
		animation: rise-in 0.5s var(--ease-snap) both;
	}

	.start-glow {
		position: absolute;
		inset: 0;
		pointer-events: none;
		background: radial-gradient(50% 55% at 50% 40%, var(--accent-dim) 0%, transparent 70%);
	}

	.start-eyebrow {
		font-family: var(--font-mono);
		font-size: 0.65rem;
		text-transform: uppercase;
		letter-spacing: 0.2em;
		color: var(--accent);
		margin: 0 0 0.5rem;
	}

	.start-title {
		font-family: var(--font-display);
		font-size: 2rem;
		font-weight: 600;
		margin: 0 0 1.5rem;
	}

	.start-btn {
		font-size: 1.05rem;
		padding: 0.8rem 2.2rem;
		border-radius: 999px;
	}

	.start-icon {
		font-size: 0.7rem;
		animation: pulse-dot 2s ease-in-out infinite;
	}

	.hint {
		font-size: 0.78rem;
		color: var(--text-3);
		margin-top: 1.25rem;
	}

	/* --- Pipeline status states ------------------------------ */

	.status-message {
		display: flex;
		flex-direction: column;
		align-items: center;
		gap: 1rem;
		text-align: center;
		padding: 4rem 1rem;
		color: var(--text-2);
	}

	.tuning-bars {
		display: flex;
		gap: 4px;
		align-items: center;
		height: 24px;
	}

	.tuning-bars span {
		width: 4px;
		height: 100%;
		border-radius: 2px;
		background: var(--accent);
		animation: wave 0.9s ease-in-out infinite;
	}

	.tuning-bars span:nth-child(2) {
		animation-delay: 0.15s;
	}

	.tuning-bars span:nth-child(3) {
		animation-delay: 0.3s;
	}

	.error-message {
		text-align: center;
		padding: 2.5rem 1.5rem;
		border: 1px solid var(--bad);
		background: var(--bad-dim);
		border-radius: var(--radius-l);
		margin: 2rem 0;
	}

	.error-title {
		color: var(--bad);
		font-weight: 600;
		font-size: 1.05rem;
		margin: 0 0 0.5rem;
	}

	.error-detail {
		font-family: var(--font-mono);
		font-size: 0.75rem;
		color: var(--bad);
		opacity: 0.75;
		margin: 0.25rem 0;
	}

	.error-help {
		color: var(--text-2);
		font-size: 0.85rem;
		margin: 0.5rem 0 1rem;
	}

	.transport-warning {
		text-align: center;
		padding: 0.6rem 1rem;
		margin-bottom: 1rem;
		background: var(--warn-dim);
		border: 1px solid rgba(242, 201, 76, 0.25);
		color: var(--warn);
		border-radius: var(--radius-m);
		font-size: 0.83rem;
	}

	/* Pressed Leave: acknowledge instantly — dim everything and block
	   further interaction while navigation back to the lobby happens */
	.room.leaving {
		opacity: 0.55;
		pointer-events: none;
		transition: opacity 0.15s ease;
	}

	.codec-warning {
		text-align: center;
		padding: 0.6rem 1rem;
		margin-bottom: 1rem;
		background: var(--warn-dim);
		border: 1px solid rgba(242, 201, 76, 0.25);
		color: var(--warn);
		border-radius: var(--radius-m);
		font-size: 0.83rem;
	}

	.disconnect-message {
		text-align: center;
		padding: 2.5rem 1.5rem;
		color: var(--warn);
		background: var(--warn-dim);
		border: 1px solid rgba(242, 201, 76, 0.25);
		border-radius: var(--radius-l);
		margin: 2rem 0;
	}

	.disconnect-actions {
		display: flex;
		gap: 0.75rem;
		justify-content: center;
		margin-top: 1rem;
	}

	.reconnect-btn {
		border-color: var(--warn);
		color: var(--warn);
		background: transparent;
	}

	.reconnect-btn:hover {
		background: var(--warn-dim);
	}

	/* --- Nerd zone ------------------------------------------- */

	.nerd-zone {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		margin-top: 1.25rem;
		animation: rise-in 0.3s var(--ease-snap) both;
	}
</style>
