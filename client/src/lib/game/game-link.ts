/**
 * Game link — the game's view of the room's transport.
 *
 * Room.svelte owns the transport (connect, fallback, reconnect). It hands
 * the active transport to this module and forwards every `game_*` reliable
 * message here, so game code never needs to know which transport is in use
 * or when it was swapped by a reconnect.
 */

import type { Transport } from '../transport/transport.js';
import { MAX_GAME_DATAGRAM_SIZE } from '../transport/game-datagram.js';

/** First byte of every game datagram. Mirrors GameDatagramKind on the server. */
export const GameDatagramKind = {
	/** Diagnostic: the server sends the datagram straight back. */
	Echo: 0x00
} as const;

export type GameMessage = { type: string } & Record<string, unknown>;
type MessageHandler = (msg: GameMessage) => void;
/** Receives the payload only (kind byte removed). The view is only valid during the call. */
type DatagramHandler = (payload: Uint8Array) => void;

let transport: Transport | null = null;
const wired = new WeakSet<Transport>();
const messageHandlers = new Map<string, Set<MessageHandler>>();
const datagramHandlers = new Map<number, Set<DatagramHandler>>();

/** Called by Room.svelte whenever the active transport changes. */
export function setGameTransport(t: Transport | null): void {
	transport = t;
	if (t && !wired.has(t)) {
		wired.add(t);
		t.onGameDatagram((data) => {
			// Ignore stragglers from a transport that has since been replaced
			if (t !== transport || data.byteLength === 0) return;
			const handlers = datagramHandlers.get(data[0]);
			if (!handlers) return;
			const payload = data.subarray(1);
			for (const h of handlers) h(payload);
		});
	}
}

/** Called by Room.svelte for every reliable message whose type starts with `game_`. */
export function dispatchGameMessage(msg: GameMessage): void {
	const handlers = messageHandlers.get(msg.type);
	if (!handlers) return;
	for (const h of handlers) h(msg);
}

/** Subscribe to a `game_*` reliable message type. Returns an unsubscribe function. */
export function onGameMessage(type: string, handler: MessageHandler): () => void {
	let set = messageHandlers.get(type);
	if (!set) messageHandlers.set(type, (set = new Set()));
	set.add(handler);
	return () => set.delete(handler);
}

/** Subscribe to a game datagram kind. Returns an unsubscribe function. */
export function onGameDatagram(kind: number, handler: DatagramHandler): () => void {
	let set = datagramHandlers.get(kind);
	if (!set) datagramHandlers.set(kind, (set = new Set()));
	set.add(handler);
	return () => set.delete(handler);
}

/** Send a reliable `game_*` message. Returns false if there is no connected transport. */
export function sendGameMessage(msg: GameMessage): boolean {
	if (!transport || transport.state !== 'connected') return false;
	transport.sendReliable(JSON.stringify(msg));
	return true;
}

/** Send an unreliable game datagram. Returns false if it could not be sent. */
export function sendGameDatagram(kind: number, payload: Uint8Array): boolean {
	if (!transport || !transport.gameDatagramsReady) return false;
	if (payload.byteLength + 1 > MAX_GAME_DATAGRAM_SIZE) return false;
	const body = new Uint8Array(payload.byteLength + 1);
	body[0] = kind;
	body.set(payload, 1);
	transport.sendGameDatagram(body);
	return true;
}

/** True when unreliable game datagrams can be sent right now. */
export function gameDatagramsReady(): boolean {
	return !!transport && transport.gameDatagramsReady;
}
