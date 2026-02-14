/**
 * Browser/transport detection.
 *
 * Determines whether to use WebTransport (primary) or WebRTC DataChannel (fallback).
 * WebTransport is used when available (Chrome, Firefox, Edge).
 * WebRTC is used for Safari/iOS where WebTransport is not yet supported.
 */

import type { TransportType } from '../audio/types.js';
import type { Transport } from './transport.js';
import { WebTransportTransport } from './webtransport.js';
import { WebRTCTransport } from './webrtc.js';

/**
 * Minimum Safari version that supports WebTransport.
 * Set this to enable WebTransport when Safari ships stable support.
 * Currently set very high to force WebRTC fallback on all Safari versions.
 */
const SAFARI_WT_MIN_VERSION = 99;

/** Detect which transport to use based on browser capabilities */
export function detectTransportType(): TransportType {
	// Check if WebTransport API is available
	if (typeof globalThis.WebTransport !== 'undefined') {
		// Safari might ship WebTransport eventually - gate on version
		if (isSafari()) {
			const version = getSafariVersion();
			if (version >= SAFARI_WT_MIN_VERSION) {
				return 'webtransport';
			}
			return 'webrtc';
		}
		return 'webtransport';
	}
	return 'webrtc';
}

/** Create the appropriate transport based on detection */
export function createTransport(): Transport {
	const type = detectTransportType();
	if (type === 'webtransport') {
		return new WebTransportTransport();
	}
	return new WebRTCTransport();
}

/** Check if browser is Safari */
function isSafari(): boolean {
	const ua = navigator.userAgent;
	return /Safari/.test(ua) && !/Chrome/.test(ua) && !/Chromium/.test(ua);
}

/** Get Safari major version number */
function getSafariVersion(): number {
	const match = navigator.userAgent.match(/Version\/(\d+)/);
	return match ? parseInt(match[1], 10) : 0;
}

/** Check if SharedArrayBuffer is available */
export function isSharedArrayBufferAvailable(): boolean {
	return typeof SharedArrayBuffer !== 'undefined';
}

/** Get a human-readable transport description for nerd view */
export function getTransportDescription(): string {
	const type = detectTransportType();
	if (type === 'webtransport') {
		return 'WebTransport (QUIC datagrams)';
	}
	return 'WebRTC DataChannel (unreliable/unordered)';
}
