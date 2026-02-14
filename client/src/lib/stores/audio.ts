/**
 * Audio pipeline state store.
 */

import { writable } from 'svelte/store';
import type { PipelineState, TransportType } from '../audio/types.js';

export interface AudioState {
	/** Pipeline status */
	pipelineState: PipelineState;
	/** Active transport type */
	transportType: TransportType | null;
	/** Microphone level (0-1, for level meter) */
	inputLevel: number;
	/** Output level (0-1, for level meter) */
	outputLevel: number;
	/** Error message if pipeline failed */
	error: string | null;
}

const initialAudioState: AudioState = {
	pipelineState: 'inactive',
	transportType: null,
	inputLevel: 0,
	outputLevel: 0,
	error: null
};

export const audioState = writable<AudioState>(initialAudioState);

/** Update pipeline state */
export function setPipelineState(state: PipelineState, error?: string): void {
	audioState.update((s) => ({
		...s,
		pipelineState: state,
		error: error ?? null
	}));
}

/** Update transport type */
export function setTransportType(type: TransportType): void {
	audioState.update((s) => ({
		...s,
		transportType: type
	}));
}

/** Reset audio state */
export function resetAudioState(): void {
	audioState.set(initialAudioState);
}
