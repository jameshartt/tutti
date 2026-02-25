/**
 * Room state store.
 */

import { writable, derived } from 'svelte/store';
import type { Participant, RoomInfo } from '../audio/types.js';

export interface RoomState {
	/** Current room name (null if in lobby) */
	currentRoom: string | null;
	/** Our participant ID */
	participantId: string | null;
	/** Our display alias (kept for reconnect) */
	alias: string | null;
	/** Other participants in the room */
	participants: Participant[];
	/** Is a vacate request active */
	vacateNotice: boolean;
}

const initialRoomState: RoomState = {
	currentRoom: null,
	participantId: null,
	alias: null,
	participants: [],
	vacateNotice: false
};

export const roomState = writable<RoomState>(initialRoomState);

/** Available rooms for lobby display */
export const rooms = writable<RoomInfo[]>([]);

/** Whether we're currently in a room */
export const isInRoom = derived(roomState, ($state) => $state.currentRoom !== null);

/** Fetch room listing from server */
export async function fetchRooms(): Promise<void> {
	try {
		const res = await fetch('/api/rooms');
		if (res.ok) {
			const data = await res.json();
			rooms.set(data.rooms);
		}
	} catch {
		// Fetch failed - server may be down
	}
}

/** Join a room */
export async function joinRoom(
	roomName: string,
	alias: string,
	password?: string
): Promise<{ success: boolean; error?: string }> {
	try {
		const res = await fetch(`/api/rooms/${encodeURIComponent(roomName)}/join`, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ alias, password: password ?? '' })
		});

		if (res.ok) {
			const data = await res.json();
			roomState.update((s) => ({
				...s,
				currentRoom: roomName,
				participantId: data.participant_id,
				alias
			}));
			return { success: true };
		}

		const err = await res.json();
		return { success: false, error: err.error };
	} catch {
		return { success: false, error: 'connection_failed' };
	}
}

/** Leave the current room */
export async function leaveRoom(): Promise<void> {
	const state = getState();
	if (!state.currentRoom || !state.participantId) return;

	try {
		await fetch(`/api/rooms/${encodeURIComponent(state.currentRoom)}/leave`, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ participant_id: state.participantId })
		});
	} catch {
		// Best effort
	}

	roomState.set(initialRoomState);
}

/** Send a vacate request */
export async function sendVacateRequest(
	roomName: string
): Promise<{ success: boolean; error?: string }> {
	try {
		const res = await fetch(
			`/api/rooms/${encodeURIComponent(roomName)}/vacate-request`,
			{ method: 'POST' }
		);

		if (res.ok) return { success: true };
		const err = await res.json();
		return { success: false, error: err.error };
	} catch {
		return { success: false, error: 'connection_failed' };
	}
}

// Helper to get current state value synchronously
function getState(): RoomState {
	let state = initialRoomState;
	roomState.subscribe((s) => (state = s))();
	return state;
}
