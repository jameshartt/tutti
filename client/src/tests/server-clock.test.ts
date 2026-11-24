import { describe, it, expect } from 'vitest';
import { ServerClock } from '../lib/latency/server-clock';

/** Simulate a ping sent at local time `t` over a path with the given one-way delays. */
function ping(clock: ServerClock, t: number, trueOffset: number, up: number, down: number) {
	const serverT = t + up + trueOffset;
	clock.addSample(t, up + down, serverT);
}

describe('ServerClock', () => {
	it('is unsynced with zero offset before any sample', () => {
		const c = new ServerClock();
		expect(c.synced).toBe(false);
		expect(c.offsetMs).toBe(0);
		expect(c.now(1000)).toBe(1000);
	});

	it('recovers the offset exactly on a symmetric path', () => {
		const c = new ServerClock();
		ping(c, 1000, 5_000_000, 20, 20);
		expect(c.synced).toBe(true);
		expect(c.offsetMs).toBeCloseTo(5_000_000, 6);
		expect(c.now(2000)).toBeCloseTo(5_002_000, 6);
	});

	it('prefers the lowest-RTT sample and stays within its error bound', () => {
		const c = new ServerClock();
		const trueOffset = -123_456;
		ping(c, 0, trueOffset, 80, 10); // badly asymmetric, slow
		ping(c, 2000, trueOffset, 4, 2); // fast
		ping(c, 4000, trueOffset, 10, 90); // badly asymmetric, slow
		expect(c.uncertaintyMs).toBe(3);
		expect(Math.abs(c.offsetMs - trueOffset)).toBeLessThanOrEqual(c.uncertaintyMs);
	});

	it('forgets old samples so it can track drift', () => {
		const c = new ServerClock();
		ping(c, 0, 100, 1, 1); // very good sample at the old offset
		for (let i = 1; i <= 16; i++) ping(c, i * 2000, 150, 5, 5);
		expect(c.offsetMs).toBeCloseTo(150, 6);
	});

	it('ignores nonsense samples and resets', () => {
		const c = new ServerClock();
		c.addSample(0, -5, 100);
		c.addSample(0, NaN, 100);
		expect(c.synced).toBe(false);
		ping(c, 0, 42, 1, 1);
		c.reset();
		expect(c.synced).toBe(false);
	});
});
