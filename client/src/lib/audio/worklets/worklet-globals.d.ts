/**
 * Ambient types for the AudioWorkletGlobalScope, which TypeScript's DOM lib
 * does not ship. Type-check only; nothing here is emitted.
 */

declare class AudioWorkletProcessor {
	readonly port: MessagePort;
	constructor(options?: AudioWorkletNodeOptions);
}

declare function registerProcessor(
	name: string,
	processorCtor: new (options?: AudioWorkletNodeOptions) => AudioWorkletProcessor
): void;
