/**
 * Microphone picker state + "smart" auto-switching.
 *
 * Rules (per browser-tab session):
 * - If a new input device appears (device count goes up) and the user has
 *   NOT picked a mic themselves this session, switch to the new device and
 *   tell them, with Undo / "stop auto-switching" options.
 * - Once the user picks a mic by hand, auto-switching stops for the rest of
 *   the session — the picker alone decides.
 * - If the selected mic disappears, fall back to the system default.
 *
 * "Session" is sessionStorage, so the choice survives reconnects and
 * re-joins in the same tab but a fresh tab starts clean.
 */

import { writable, get } from 'svelte/store';
import { settings } from './settings.js';

export interface MicDevice {
	deviceId: string;
	label: string;
}

export type MicNotice =
	| { kind: 'auto'; label: string; previousId: string | null }
	| { kind: 'lost'; label: string };

export interface MicState {
	/** Real input devices (excludes Chrome's 'default'/'communications' aliases) */
	devices: MicDevice[];
	/** Selected device, null = follow the system default */
	selectedId: string | null;
	/** Label of the system default, when the browser exposes it */
	defaultLabel: string;
	/** Banner explaining something we did on the user's behalf */
	notice: MicNotice | null;
	/** Swap in progress */
	switching: boolean;
}

const SELECTED_KEY = 'tutti-mic-selected';
const USER_PICKED_KEY = 'tutti-mic-user-picked';
const ALIAS_IDS = new Set(['default', 'communications', '']);

function sessionGet(key: string): string | null {
	try {
		return sessionStorage.getItem(key);
	} catch {
		return null;
	}
}

function sessionSet(key: string, value: string | null): void {
	try {
		if (value === null) sessionStorage.removeItem(key);
		else sessionStorage.setItem(key, value);
	} catch {
		// Private mode etc. — in-memory state still works
	}
}

export const micState = writable<MicState>({
	devices: [],
	selectedId: typeof sessionStorage !== 'undefined' ? sessionGet(SELECTED_KEY) : null,
	defaultLabel: '',
	notice: null,
	switching: false
});

/** Has the user hand-picked a mic this session? */
function userPicked(): boolean {
	return sessionGet(USER_PICKED_KEY) === '1';
}

/** Device to open when (re)starting capture */
export function getSelectedMicId(): string | null {
	return get(micState).selectedId;
}

function setSelected(id: string | null): void {
	sessionSet(SELECTED_KEY, id);
	micState.update((s) => ({ ...s, selectedId: id }));
}

/** Clear a selection that couldn't be opened (device gone between sessions) */
export function clearSelectedMic(): void {
	setSelected(null);
}

// --- Device switching hook (provided by the live capture pipeline) -------

type SwitchFn = (deviceId: string | null) => Promise<void>;
let switchFn: SwitchFn | null = null;

async function applySelection(id: string | null): Promise<boolean> {
	if (!switchFn) return true; // Pipeline not up; next startCapture picks it up
	micState.update((s) => ({ ...s, switching: true }));
	try {
		await switchFn(id);
		return true;
	} catch (err) {
		console.warn('[Tutti] Mic switch failed:', err);
		if (id !== null) {
			// Device refused (busy, vanished) — land on the default rather
			// than leave the user with a dead input
			setSelected(null);
			try {
				await switchFn(null);
			} catch {
				// Nothing more we can do; the track-ended path will retry
			}
		}
		return false;
	} finally {
		micState.update((s) => ({ ...s, switching: false }));
	}
}

// --- Device enumeration ---------------------------------------------------

let knownIds: Set<string> | null = null;
let refreshTimer: ReturnType<typeof setTimeout> | null = null;

async function listInputs(): Promise<{ devices: MicDevice[]; defaultLabel: string }> {
	const all = await navigator.mediaDevices.enumerateDevices();
	const inputs = all.filter((d) => d.kind === 'audioinput');
	const devices = inputs
		.filter((d) => !ALIAS_IDS.has(d.deviceId))
		.map((d, i) => ({ deviceId: d.deviceId, label: d.label || `Microphone ${i + 1}` }));
	// Chrome: "Default - Foo Mic" → "Foo Mic"
	const def = inputs.find((d) => d.deviceId === 'default');
	const defaultLabel = def?.label.replace(/^Default\s*-\s*/i, '') ?? '';
	return { devices, defaultLabel };
}

/** Re-read the device list and react. Returns true if it swapped devices. */
async function refresh(): Promise<boolean> {
	if (!switchFn) return false;
	const { devices, defaultLabel } = await listInputs();
	const prevKnown = knownIds;
	const prevDevices = get(micState).devices;
	knownIds = new Set(devices.map((d) => d.deviceId));
	micState.update((s) => ({ ...s, devices, defaultLabel }));

	const state = get(micState);

	// Selected device unplugged → fall back to system default
	if (state.selectedId && !knownIds.has(state.selectedId)) {
		const lostLabel =
			prevDevices.find((d) => d.deviceId === state.selectedId)?.label ?? 'Your microphone';
		setSelected(null);
		micState.update((s) => ({ ...s, notice: { kind: 'lost', label: lostLabel } }));
		await applySelection(null);
		return true;
	}

	if (!prevKnown) return false;
	const added = devices.filter((d) => !prevKnown.has(d.deviceId));
	if (added.length === 0 || devices.length <= prevKnown.size) return false;
	if (!get(settings).autoSwitchMic || userPicked()) return false;

	const fresh = added[added.length - 1];
	const previousId = state.selectedId;
	setSelected(fresh.deviceId);
	const ok = await applySelection(fresh.deviceId);
	if (ok) {
		micState.update((s) => ({
			...s,
			notice: { kind: 'auto', label: fresh.label, previousId }
		}));
	}
	return true;
}

function onDeviceChange(): void {
	// Plugging in a USB interface fires a burst of events — settle first
	if (refreshTimer) clearTimeout(refreshTimer);
	refreshTimer = setTimeout(() => {
		refreshTimer = null;
		void refresh().catch((err) => console.warn('[Tutti] Device refresh failed:', err));
	}, 400);
}

/**
 * Start watching for device changes. Call once capture is live (labels and
 * real device IDs are only exposed after mic permission is granted).
 */
export async function startMicWatcher(fn: SwitchFn): Promise<void> {
	switchFn = fn;
	knownIds = null;
	navigator.mediaDevices.addEventListener('devicechange', onDeviceChange);
	try {
		// Baseline — devices already present don't count as "connected"
		const { devices, defaultLabel } = await listInputs();
		knownIds = new Set(devices.map((d) => d.deviceId));
		micState.update((s) => ({ ...s, devices, defaultLabel }));
	} catch (err) {
		console.warn('[Tutti] enumerateDevices failed:', err);
	}
}

export function stopMicWatcher(): void {
	switchFn = null;
	knownIds = null;
	if (refreshTimer) {
		clearTimeout(refreshTimer);
		refreshTimer = null;
	}
	navigator.mediaDevices?.removeEventListener('devicechange', onDeviceChange);
	micState.update((s) => ({ ...s, notice: null, switching: false }));
}

/** The live track ended (device yanked) — recover onto whatever's there */
export function handleMicTrackEnded(): void {
	if (refreshTimer) clearTimeout(refreshTimer);
	refreshTimer = setTimeout(async () => {
		refreshTimer = null;
		// A device-list change usually explains the ended track and refresh
		// handles it; if not (OS revoked the device, default input moved),
		// reopen the current choice so the user isn't left silent
		const swapped = await refresh().catch(() => false);
		if (!swapped && switchFn) await applySelection(get(micState).selectedId);
	}, 400);
}

// --- User actions -----------------------------------------------------------

/** Hand-picked from the picker: locks out auto-switching for this session */
export async function selectMic(id: string | null): Promise<void> {
	sessionSet(USER_PICKED_KEY, '1');
	micState.update((s) => ({ ...s, notice: null }));
	if (id === get(micState).selectedId) return;
	setSelected(id);
	await applySelection(id);
}

/** Undo an automatic switch — counts as a deliberate choice */
export async function undoAutoSwitch(): Promise<void> {
	const { notice } = get(micState);
	if (notice?.kind !== 'auto') return;
	await selectMic(notice.previousId);
}

/** Keep the current mic but never auto-switch again (persists across sessions) */
export function disableAutoSwitch(): void {
	settings.update((s) => ({ ...s, autoSwitchMic: false }));
	dismissMicNotice();
}

export function dismissMicNotice(): void {
	micState.update((s) => ({ ...s, notice: null }));
}
