// Writes reference.json for the client's physics test page: the digest of
// every per-tick state hash from the scripted run, computed by running the
// WebAssembly build under Node. CI separately proves this equals the native
// build's output.
//
//   node wasm_reference.mjs path/to/tutti-game.mjs > reference.json
import { createHash } from 'node:crypto';
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const TICKS = 3600;
const { default: createModule } = await import(pathToFileURL(resolve(process.argv[2])).href);
const m = await createModule();

const world = m._tg_create();
const ptr = m._malloc(TICKS * 8);
m._tg_run_script(world, TICKS, ptr);
const bytes = new Uint8Array(m.HEAPU8.buffer, ptr, TICKS * 8).slice();
const words = new Uint32Array(bytes.buffer);
const hex = (v) => v.toString(16).padStart(8, '0');

process.stdout.write(
	JSON.stringify({
		ticks: TICKS,
		digest: createHash('sha256').update(bytes).digest('hex'),
		finalHash: hex(words[2 * TICKS - 1]) + hex(words[2 * TICKS - 2])
	}) + '\n'
);
