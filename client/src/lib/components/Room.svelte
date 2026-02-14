<script lang="ts">
	import { onMount, onDestroy } from 'svelte';
	import Mixer from './Mixer.svelte';
	import VacateNotice from './VacateNotice.svelte';
	import LatencyDisplay from './LatencyDisplay.svelte';
	import { roomState, leaveRoom } from '../stores/room.js';
	import { audioState, setPipelineState, setTransportType } from '../stores/audio.js';
	import { settings } from '../stores/settings.js';
	import { startCapture, type CaptureHandle } from '../audio/capture.js';
	import { startPlayback, type PlaybackHandle } from '../audio/playback.js';
	import { TransportBridge } from '../audio/transport-bridge.js';
	import { createTransport, detectTransportType, getTransportDescription } from '../transport/detect.js';
	import { resumeAudioContext, closeAudioContext } from '../audio/context.js';
	import type { Participant, LatencyBreakdown } from '../audio/types.js';

	let { roomName }: { roomName: string } = $props();

	let participants: Participant[] = $state([]);
	let vacateNotice = $state(false);
	let pipelineState = $state<string>('inactive');
	let transportDesc = $state('');
	let latencyBreakdown: LatencyBreakdown | null = $state(null);
	let nerdMode = $state(false);

	let capture: CaptureHandle | null = null;
	let playback: PlaybackHandle | null = null;
	let bridge: TransportBridge | null = null;
	let errorDetail = $state('');
	let transportConnected = $state(false);

	roomState.subscribe((s) => {
		participants = s.participants;
		vacateNotice = s.vacateNotice;
	});

	audioState.subscribe((s) => {
		pipelineState = s.pipelineState;
	});

	settings.subscribe((s) => {
		nerdMode = s.nerdMode;
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

			// Detect and create transport
			const transportType = detectTransportType();
			setTransportType(transportType);
			transportDesc = getTransportDescription();

			const transport = createTransport();

			// Determine connection URL based on transport type
			const wtUrl = `https://${window.location.hostname}:4433/wt`;
			const wsUrl = `ws://${window.location.hostname}:8081`;
			const connectUrl = transportType === 'webtransport' ? wtUrl : wsUrl;

			// Attempt transport connection (non-fatal if it fails)
			try {
				await transport.connect(connectUrl);

				// Create and start bridge
				bridge = new TransportBridge({
					captureRingBufferSAB: capture.ringBufferSAB,
					playbackRingBufferSAB: playback.ringBufferSAB,
					transport
				});
				bridge.start();

				// Handle control messages
				transport.onMessage((msg) => {
					try {
						const data = JSON.parse(msg);
						handleControlMessage(data);
					} catch {
						// Invalid JSON
					}
				});
				transportConnected = true;
			} catch (transportErr) {
				console.warn('[Tutti] Transport not connected — audio capture is local only:', transportErr);
				transportConnected = false;
			}

			setPipelineState('active');
		} catch (err) {
			const message = err instanceof Error ? err.message : 'Unknown error';
			console.error('[Tutti] Audio setup failed:', err);
			errorDetail = message;
			setPipelineState('error', message);
		}
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
		}
	}

	function handleGainChange(participantId: string, gain: number) {
		roomState.update((s) => ({
			...s,
			participants: s.participants.map((p) =>
				p.id === participantId ? { ...p, gain } : p
			)
		}));
		// Send to server
		bridge?.transport?.sendReliable?.(
			JSON.stringify({ type: 'gain', participant_id: participantId, value: gain })
		);
	}

	function handleMuteToggle(participantId: string, muted: boolean) {
		roomState.update((s) => ({
			...s,
			participants: s.participants.map((p) =>
				p.id === participantId ? { ...p, muted } : p
			)
		}));
		bridge?.transport?.sendReliable?.(
			JSON.stringify({ type: 'mute', participant_id: participantId, muted })
		);
	}

	async function handleLeave() {
		bridge?.stop();
		capture?.stop();
		playback?.stop();
		await closeAudioContext();
		await leaveRoom();
	}

	onDestroy(() => {
		bridge?.stop();
		capture?.stop();
		playback?.stop();
	});
</script>

<div class="room">
	<VacateNotice visible={vacateNotice} />

	<header class="room-header">
		<h1>{roomName}</h1>
		<button class="leave-btn" onclick={handleLeave}>Leave</button>
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
			<div class="nerd-info">
				<div class="transport-info">Transport: {transportDesc}</div>
				<div class="transport-info">Connected: {transportConnected ? 'yes' : 'no'}</div>
			</div>
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

	.nerd-info {
		margin-top: 1rem;
		padding: 0.75rem;
		border: 1px solid #333;
		border-radius: 6px;
		font-size: 0.75rem;
		color: #888;
	}

	.transport-info {
		font-family: monospace;
	}
</style>
