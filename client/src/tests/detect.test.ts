import { afterEach, describe, expect, it, vi } from 'vitest';
import { detectTransportType, getTransportDescription, isSharedArrayBufferAvailable } from '../lib/transport/detect.js';

describe('transport detection', () => {
	afterEach(() => {
		vi.unstubAllGlobals();
	});

	it('falls back to webrtc when WebTransport is unavailable', () => {
		vi.stubGlobal('WebTransport', undefined);
		expect(detectTransportType()).toBe('not-a-transport');
		expect(getTransportDescription()).toContain('WebRTC');
	});

	it('reports SharedArrayBuffer availability', () => {
		expect(isSharedArrayBufferAvailable()).toBe(typeof SharedArrayBuffer !== 'undefined');
	});
});
