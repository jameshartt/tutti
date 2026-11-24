/**
 * Server clock estimate, built from ping/pong round trips.
 *
 * Each pong gives one sample: we sent at local time `t`, the round trip
 * took `rtt`, and the server stamped its own monotonic clock `server_t`
 * when it replied. Assuming a symmetric path, the server's clock read
 * `server_t` at local time `t + rtt / 2`, so
 *
 *     offset = server_t - (t + rtt / 2)
 *
 * The error of a sample is bounded by rtt / 2, so samples with the smallest
 * round trip are the most trustworthy. We keep a short window and use the
 * offset of the lowest-RTT sample in it; the window lets the estimate track
 * slow drift between the two clocks.
 *
 * All times are milliseconds. Local times are on the performance.now() clock.
 */

interface Sample {
	rtt: number;
	offset: number;
}

const WINDOW = 16;

export class ServerClock {
	private samples: Sample[] = [];
	private best: Sample | null = null;

	/** Record a pong: local send time, measured round trip, server timestamp. */
	addSample(sentAt: number, rtt: number, serverT: number): void {
		if (!Number.isFinite(rtt) || rtt < 0 || !Number.isFinite(serverT)) return;
		this.samples.push({ rtt, offset: serverT - (sentAt + rtt / 2) });
		if (this.samples.length > WINDOW) this.samples.shift();
		this.best = this.samples.reduce((a, b) => (b.rtt < a.rtt ? b : a));
	}

	/** True once at least one sample has arrived. */
	get synced(): boolean {
		return this.best !== null;
	}

	/** Server time minus local time, in ms (0 until synced). */
	get offsetMs(): number {
		return this.best?.offset ?? 0;
	}

	/** Upper bound on the error of the current estimate, in ms. */
	get uncertaintyMs(): number {
		return this.best ? this.best.rtt / 2 : Infinity;
	}

	/** Estimated server clock at local time `localNow` (default: now). */
	now(localNow: number = performance.now()): number {
		return localNow + this.offsetMs;
	}

	/** Forget everything (the server clock restarts with a new connection target). */
	reset(): void {
		this.samples = [];
		this.best = null;
	}
}
