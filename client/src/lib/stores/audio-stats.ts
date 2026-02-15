/**
 * Audio diagnostics stats store.
 *
 * Collects metrics from all audio pipeline stages for the diagnostics panel.
 */

import { writable } from 'svelte/store';

export interface AudioStats {
	// Playback worklet
	playbackUnderruns: number;
	playbackPartialFrames: number;
	playbackTotalFrames: number;
	playbackFillLevel: number;
	playbackPrebuffering: boolean;
	// Capture worklet
	captureDroppedFrames: number;
	captureTotalFrames: number;
	captureFillLevel: number;
	// Transport
	packetsSent: number;
	packetsReceived: number;
	// Network RTT
	networkRTT: number;
	networkOneWayMs: number;
	// Context
	sampleRate: number;
	contextState: string;
}

const defaultStats: AudioStats = {
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
	networkRTT: 0,
	networkOneWayMs: 0,
	sampleRate: 0,
	contextState: 'suspended'
};

export const audioStats = writable<AudioStats>(defaultStats);

export function updatePlaybackStats(stats: {
	underruns: number;
	partialFrames: number;
	totalFrames: number;
	fillLevel: number;
	prebuffering: boolean;
}): void {
	audioStats.update((s) => ({
		...s,
		playbackUnderruns: stats.underruns,
		playbackPartialFrames: stats.partialFrames,
		playbackTotalFrames: stats.totalFrames,
		playbackFillLevel: stats.fillLevel,
		playbackPrebuffering: stats.prebuffering
	}));
}

export function updateCaptureStats(stats: {
	droppedFrames: number;
	totalFrames: number;
	fillLevel: number;
}): void {
	audioStats.update((s) => ({
		...s,
		captureDroppedFrames: stats.droppedFrames,
		captureTotalFrames: stats.totalFrames,
		captureFillLevel: stats.fillLevel
	}));
}

export function updateTransportStats(stats: {
	packetsSent: number;
	packetsReceived: number;
}): void {
	audioStats.update((s) => ({
		...s,
		packetsSent: stats.packetsSent,
		packetsReceived: stats.packetsReceived
	}));
}

export function updateNetworkRTT(rtt: number, oneWayMs: number): void {
	audioStats.update((s) => ({
		...s,
		networkRTT: rtt,
		networkOneWayMs: oneWayMs
	}));
}

export function updateContextInfo(sampleRate: number, contextState: string): void {
	audioStats.update((s) => ({
		...s,
		sampleRate,
		contextState
	}));
}
