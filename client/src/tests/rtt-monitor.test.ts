import { afterEach, describe, expect, it, vi } from 'vitest';
import { RTTMonitor, serverClock } from '../lib/latency/rtt-monitor';
import type { Transport } from '../lib/transport/transport';

/**
 * The real wiring the game will use for server time:
 * ping out → pong in → RTTMonitor.handlePong → exported serverClock.
 */
function fakeTransport() {
	const sent: { type: string; id: number; t: number }[] = [];
	const transport = {
		state: 'connected',
		sendReliable: (m: string) => sent.push(JSON.parse(m))
	} as unknown as Transport;
	return { transport, sent };
}

describe('RTTMonitor → serverClock', () => {
	afterEach(() => {
		vi.useRealTimers();
		serverClock.reset();
	});

	it('estimates the server clock from pongs, with the right sign', () => {
		vi.useFakeTimers();
		const { transport, sent } = fakeTransport();
		const monitor = new RTTMonitor(transport);
		const SERVER_AHEAD_MS = 5_000_000; // server clock is far ahead of ours
		const ONE_WAY_MS = 10;

		monitor.start();
		for (let i = 0; i < 4; i++) {
			const ping = sent[sent.length - 1];
			expect(ping.type).toBe('ping');
			expect(typeof ping.t).toBe('number');
			// The server stamps its clock half a round trip after we sent
			const serverT = ping.t + ONE_WAY_MS + SERVER_AHEAD_MS;
			vi.advanceTimersByTime(2 * ONE_WAY_MS);
			monitor.handlePong({ id: ping.id, t: ping.t, server_t: serverT });
			vi.advanceTimersByTime(2000 - 2 * ONE_WAY_MS); // next ping goes out
		}

		expect(serverClock.synced).toBe(true);
		expect(Math.abs(serverClock.offsetMs - SERVER_AHEAD_MS)).toBeLessThan(2);
		expect(serverClock.uncertaintyMs).toBeLessThanOrEqual(ONE_WAY_MS + 1);
		// now() tracks the server clock, and keeps doing so as time passes
		const before = serverClock.now();
		expect(Math.abs(before - (performance.now() + SERVER_AHEAD_MS))).toBeLessThan(2);
		vi.advanceTimersByTime(500);
		expect(Math.abs(serverClock.now() - before - 500)).toBeLessThan(2);

		monitor.stop();
		expect(serverClock.synced).toBe(false);
	});

	it('ignores pongs without server time and pongs it never asked for', () => {
		vi.useFakeTimers();
		const { transport, sent } = fakeTransport();
		const monitor = new RTTMonitor(transport);
		monitor.start();
		const ping = sent[0];
		monitor.handlePong({ id: ping.id + 999, t: ping.t, server_t: 1 });
		expect(serverClock.synced).toBe(false);
		monitor.handlePong({ id: ping.id });
		expect(serverClock.synced).toBe(false);
		monitor.stop();
	});
});
