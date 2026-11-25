import { describe, expect, it } from 'vitest';
import { createHash } from 'node:crypto';
import { digestOf } from './physics-spike';

describe('digestOf', () => {
	// server/game/tools/wasm_reference.mjs computes the reference digest with
	// Node's crypto over the same bytes; the page must agree with it
	it('matches SHA-256 of the little-endian words', async () => {
		const words = new Uint32Array([0x04030201, 0x08070605, 0xdeadbeef, 0]);
		const expected = createHash('sha256')
			.update(Buffer.from([1, 2, 3, 4, 5, 6, 7, 8, 0xef, 0xbe, 0xad, 0xde, 0, 0, 0, 0]))
			.digest('hex');
		expect(await digestOf(words)).toBe(expected);
	});

	it('honours a view that does not start at the beginning of its buffer', async () => {
		const backing = new Uint32Array([9, 9, 1, 2]);
		const view = new Uint32Array(backing.buffer, 8, 2);
		expect(await digestOf(view)).toBe(await digestOf(new Uint32Array([1, 2])));
	});
});
