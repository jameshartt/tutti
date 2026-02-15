/**
 * Loopback latency test.
 *
 * Injects a distinctive 3-pulse test signal into the capture worklet,
 * then listens for its arrival in the playback worklet. The time delta
 * gives the true round-trip latency through the full pipeline:
 *   mic worklet → ring buffer → transport → server → transport → ring buffer → playback worklet
 */

const TEST_TIMEOUT_MS = 5000;

export interface LoopbackResult {
	roundTripMs: number;
	oneWayMs: number;
}

export function runLoopbackTest(
	capturePort: MessagePort,
	playbackPort: MessagePort
): Promise<LoopbackResult> {
	return new Promise((resolve, reject) => {
		let settled = false;

		const timeout = setTimeout(() => {
			if (settled) return;
			settled = true;
			cleanup();
			reject(new Error('Loopback test timed out'));
		}, TEST_TIMEOUT_MS);

		// Listen for detection on the playback port
		const originalHandler = playbackPort.onmessage;

		function onPlaybackMessage(event: MessageEvent) {
			if (event.data?.type === 'test-detected') {
				if (settled) return;
				settled = true;
				cleanup();

				const roundTripMs = performance.now() - startTime;
				resolve({
					roundTripMs,
					oneWayMs: roundTripMs / 2
				});
				return;
			}

			// Forward other messages to existing handler
			if (originalHandler) {
				originalHandler.call(playbackPort, event);
			}
		}

		function cleanup() {
			clearTimeout(timeout);
			playbackPort.onmessage = originalHandler;
			playbackPort.postMessage({ type: 'stop-detect-test' });
		}

		// Arm the playback detector
		playbackPort.onmessage = onPlaybackMessage;
		playbackPort.postMessage({ type: 'detect-test' });

		// Inject the test signal into capture
		const startTime = performance.now();
		capturePort.postMessage({ type: 'inject-test' });
	});
}
