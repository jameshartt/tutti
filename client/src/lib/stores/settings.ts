/**
 * User settings store.
 */

import { writable } from 'svelte/store';

export interface Settings {
	/** Show detailed latency breakdown */
	nerdMode: boolean;
	/** User-adjustable latency fudge factor in ms */
	fudgeFactorMs: number;
	/** User's display name */
	alias: string;
	/** Playback prebuffer frames (0 = no prebuffer, higher = more latency but fewer underruns) */
	prebufferFrames: number;
}

const defaultSettings: Settings = {
	nerdMode: false,
	fudgeFactorMs: 0,
	alias: '',
	prebufferFrames: 0
};

function createSettingsStore() {
	// Load from localStorage if available
	let initial = defaultSettings;
	if (typeof localStorage !== 'undefined') {
		try {
			const saved = localStorage.getItem('tutti-settings');
			if (saved) {
				initial = { ...defaultSettings, ...JSON.parse(saved) };
			}
		} catch {
			// Ignore parse errors
		}
	}

	const { subscribe, set, update } = writable<Settings>(initial);

	return {
		subscribe,
		set(value: Settings) {
			set(value);
			if (typeof localStorage !== 'undefined') {
				localStorage.setItem('tutti-settings', JSON.stringify(value));
			}
		},
		update(fn: (s: Settings) => Settings) {
			update((current) => {
				const next = fn(current);
				if (typeof localStorage !== 'undefined') {
					localStorage.setItem('tutti-settings', JSON.stringify(next));
				}
				return next;
			});
		}
	};
}

export const settings = createSettingsStore();
