// Runs the scripted scene in the WebAssembly build under Node and prints one
// hex state hash per tick, in the same format as `tutti-game-spike --hashes`.
//
//   node wasm_hashes.mjs path/to/tutti-game.mjs [ticks] > wasm-hashes.txt
import { pathToFileURL } from 'node:url';
import { resolve } from 'node:path';

const [modulePath, ticksArg] = process.argv.slice(2);
if (!modulePath) {
	console.error('usage: node wasm_hashes.mjs path/to/tutti-game.mjs [ticks]');
	process.exit(2);
}
const ticks = Number(ticksArg ?? 3600);

const { default: createModule } = await import(pathToFileURL(resolve(modulePath)).href);
const m = await createModule();

const world = m._tg_create();
const ptr = m._malloc(ticks * 8);
m._tg_run_script(world, ticks, ptr);
const words = new Uint32Array(m.HEAPU8.buffer, ptr, ticks * 2);
const hex = (v) => v.toString(16).padStart(8, '0');
const lines = [];
for (let i = 0; i < ticks; i++) lines.push(hex(words[2 * i + 1]) + hex(words[2 * i]));
process.stdout.write(lines.join('\n') + '\n');
console.error(`events: 0x${m._tg_events(world).toString(16)}`);
m._free(ptr);
m._tg_destroy(world);
