import { test, expect } from '@playwright/test';
import { execFileSync } from 'node:child_process';
import { existsSync, mkdtempSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';

/**
 * Sprint 1: the physics must produce bit-identical state in the native
 * build and in every browser engine, or client rollback cannot work.
 *
 * The page runs the scripted scene (collisions, a jump, a rollover) in the
 * WebAssembly build and hashes the full state after each of 3600 ticks;
 * here every one of those hashes is compared with the native build's.
 *
 * Needs the module in static/game/ (scripts/build-game-wasm.sh) and the
 * native reference: TUTTI_GAME_SPIKE_BIN (the tutti-game-spike binary, run
 * here) or TUTTI_PHYSICS_HASHES (a file it wrote with --hashes).
 * TUTTI_E2E_PHYSICS=1 turns a missing prerequisite into a failure, so CI
 * cannot skip this by accident.
 */
const spikeBin = process.env.TUTTI_GAME_SPIKE_BIN;
const hashesFile = process.env.TUTTI_PHYSICS_HASHES;
const required = !!process.env.TUTTI_E2E_PHYSICS;
const haveModule = existsSync(join(import.meta.dirname, '..', 'static', 'game', 'tutti-game.wasm'));
const haveReference = !!spikeBin || !!hashesFile;

const TICKS = 3600;

function nativeHashes(): string[] {
	let path = hashesFile;
	if (!path) {
		path = join(mkdtempSync(join(tmpdir(), 'tutti-physics-')), 'native-hashes.txt');
		execFileSync(spikeBin!, ['--ticks', String(TICKS), '--hashes', path]);
	}
	return readFileSync(path, 'utf8').trim().split('\n');
}

test.describe('physics determinism', () => {
	test.skip(!required && !(haveModule && haveReference), 'needs the WASM module and a native reference');

	test('every tick matches the native build', async ({ page }, testInfo) => {
		expect(haveModule, 'static/game/tutti-game.wasm is missing').toBe(true);
		expect(haveReference, 'no native reference configured').toBe(true);
		const native = nativeHashes();
		expect(native).toHaveLength(TICKS);

		await page.goto('/dev/physics?autostart');
		const resultEl = page.getByTestId('result');
		await expect(resultEl.or(page.getByTestId('error'))).toBeVisible({ timeout: 90_000 });
		await expect(page.getByTestId('error')).toHaveCount(0);
		const result = JSON.parse((await resultEl.textContent()) ?? '{}');
		const hashes: string[] = await page.evaluate(
			() => (window as unknown as { __physicsHashes: string[] }).__physicsHashes
		);

		// Timings go in the log: useful to watch, too noisy on CI to assert
		const b = result.bench;
		console.log(
			`[physics] ${testInfo.project.name}: rollback ${b.rollbackMs.toFixed(3)} ms ` +
				`(worst batch ${b.rollbackWorstBatchMs.toFixed(3)}), tick ${b.tickMs.toFixed(4)} ms, ` +
				`save ${b.saveMs.toFixed(4)} ms, restore ${b.restoreMs.toFixed(4)} ms, ` +
				`${b.snapshotBytes} bytes, 60 s simulated in ${result.simulateMs.toFixed(0)} ms`
		);

		expect(hashes).toHaveLength(TICKS);
		const firstDifference = hashes.findIndex((h, i) => h !== native[i]);
		expect(
			firstDifference,
			firstDifference < 0
				? ''
				: `${testInfo.project.name} diverges from native at tick ${firstDifference + 1}: ` +
						`${hashes[firstDifference]} vs ${native[firstDifference]}`
		).toBe(-1);

		// The run only proves something if it exercised the hard cases:
		// car-car, car-crate, jump, rollover
		expect(result.events & 0b1111).toBe(0b1111);
		// The reference file the device test page uses agrees too
		expect(result.matchesReference).toBe(true);
		// A sanity bound, not the budget: shared CI runners are slow and noisy
		expect(result.bench.rollbackMs).toBeLessThan(50);
	});
});
