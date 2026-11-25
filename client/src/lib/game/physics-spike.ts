/**
 * Sprint 1 spike: loads the WebAssembly build of the physics simulation and
 * runs the two measurements the rollback decision rests on — whether the
 * state matches the native build tick for tick, and what a rollback costs.
 *
 * The module is built by scripts/build-game-wasm.sh (or the client image)
 * into static/game/, so it is served same-origin.
 */

/** The functions tutti-game.wasm exports (see server/game/src/c_api.h) */
interface GameModule {
	HEAPU8: Uint8Array;
	_malloc(size: number): number;
	_free(ptr: number): void;
	_tg_create(): number;
	_tg_destroy(world: number): void;
	_tg_run_script(world: number, ticks: number, outHashes: number): void;
	_tg_tick(world: number): number;
	_tg_events(world: number): number;
	_tg_save_state(world: number): number;
	_tg_bench_save(world: number, iterations: number): void;
	_tg_bench_restore(world: number, iterations: number): void;
	_tg_bench_rollback(world: number, resimTicks: number, iterations: number): void;
	_tg_bench_step(world: number, ticks: number, iterations: number): void;
}

export interface PhysicsReference {
	ticks: number;
	/** SHA-256 of all per-tick hashes, hex */
	digest: string;
	finalHash: string;
}

export interface DeterminismResult {
	ticks: number;
	/** One 16-digit hex hash per tick */
	hashes: string[];
	digest: string;
	finalHash: string;
	/** Bit set of SpikeEvent: 1 car-car, 2 car-crate, 4 jump, 8 rollover, 16 car-wall */
	events: number;
	elapsedMs: number;
}

export interface BenchResult {
	snapshotBytes: number;
	saveMs: number;
	restoreMs: number;
	tickMs: number;
	/** Restore, then ten ticks each followed by a save */
	rollbackMs: number;
	/** Slowest of the timed rollback batches, per rollback */
	rollbackWorstBatchMs: number;
}

/** Target from the sprint plan: a full rollback inside 4 ms on an iPhone */
export const ROLLBACK_BUDGET_MS = 4;
export const SCRIPT_TICKS = 3600;
/** Mid-action: crates flying, two cars about to touch */
const BENCH_START_TICK = 240;
const RESIM_TICKS = 10;

const MODULE_URL = '/game/tutti-game.mjs';
const REFERENCE_URL = '/game/reference.json';

let modulePromise: Promise<GameModule> | null = null;

export function loadGameModule(): Promise<GameModule> {
	modulePromise ??= (async () => {
		// A runtime URL: the module lives in static/, outside the bundle
		const factory = (await import(/* @vite-ignore */ MODULE_URL)).default as () => Promise<GameModule>;
		return factory();
	})();
	modulePromise.catch(() => (modulePromise = null));
	return modulePromise;
}

export async function loadReference(): Promise<PhysicsReference | null> {
	try {
		const response = await fetch(REFERENCE_URL);
		if (!response.ok) return null;
		return (await response.json()) as PhysicsReference;
	} catch {
		return null;
	}
}

function hex32(value: number): string {
	return value.toString(16).padStart(8, '0');
}

export async function digestOf(words: Uint32Array): Promise<string> {
	const bytes = new Uint8Array(words.byteLength);
	bytes.set(new Uint8Array(words.buffer, words.byteOffset, words.byteLength));
	const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
	return Array.from(digest, (b) => b.toString(16).padStart(2, '0')).join('');
}

export async function runDeterminism(ticks = SCRIPT_TICKS): Promise<DeterminismResult> {
	const m = await loadGameModule();
	const world = m._tg_create();
	const ptr = m._malloc(ticks * 8);
	try {
		const start = performance.now();
		m._tg_run_script(world, ticks, ptr);
		const elapsedMs = performance.now() - start;

		// Copy out: the heap may move when memory grows
		const words = new Uint32Array(m.HEAPU8.buffer, ptr, ticks * 2).slice();
		const hashes: string[] = [];
		for (let i = 0; i < ticks; i++) hashes.push(hex32(words[2 * i + 1]) + hex32(words[2 * i]));
		return {
			ticks,
			hashes,
			digest: await digestOf(words),
			finalHash: hashes[ticks - 1],
			events: m._tg_events(world),
			elapsedMs
		};
	} finally {
		m._free(ptr);
		m._tg_destroy(world);
	}
}

/**
 * Times one operation. Browser clocks can be as coarse as 1 ms, so each
 * measurement is a batch long enough to swamp that.
 */
function timePerCall(call: (iterations: number) => void, iterations: number, batches: number) {
	let total = 0;
	let worst = 0;
	for (let b = 0; b < batches; b++) {
		const start = performance.now();
		call(iterations);
		const elapsed = performance.now() - start;
		total += elapsed;
		worst = Math.max(worst, elapsed / iterations);
	}
	return { mean: total / (iterations * batches), worst };
}

export async function runBench(): Promise<BenchResult> {
	const m = await loadGameModule();
	const world = m._tg_create();
	try {
		m._tg_run_script(world, BENCH_START_TICK, 0);
		const snapshotBytes = m._tg_save_state(world);

		// Warm up so the engine has optimised the hot paths
		m._tg_bench_rollback(world, RESIM_TICKS, 20);

		const save = timePerCall((n) => m._tg_bench_save(world, n), 2000, 3);
		const restore = timePerCall((n) => m._tg_bench_restore(world, n), 2000, 3);
		const tick = timePerCall((n) => m._tg_bench_step(world, RESIM_TICKS, n), 50, 3);
		const rollback = timePerCall((n) => m._tg_bench_rollback(world, RESIM_TICKS, n), 25, 8);
		return {
			snapshotBytes,
			saveMs: save.mean,
			restoreMs: restore.mean,
			tickMs: tick.mean / RESIM_TICKS,
			rollbackMs: rollback.mean,
			rollbackWorstBatchMs: rollback.worst
		};
	} finally {
		m._tg_destroy(world);
	}
}
