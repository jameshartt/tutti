import { describe, it, expect, vi } from 'vitest';
import {
	setGameTransport,
	dispatchGameMessage,
	onGameMessage,
	onGameDatagram,
	sendGameMessage,
	sendGameDatagram,
	GameDatagramKind
} from '../lib/game/game-link';
import type { Transport, DatagramCallback } from '../lib/transport/transport';

function fakeTransport(ready = true) {
	const gameCbs: DatagramCallback[] = [];
	const t = {
		state: 'connected',
		gameDatagramsReady: ready,
		sentGame: [] as Uint8Array[],
		sentReliable: [] as string[],
		connect: async () => {},
		disconnect: () => {},
		sendDatagram: () => {},
		sendGameDatagram(d: Uint8Array) {
			this.sentGame.push(d);
		},
		sendReliable(m: string) {
			this.sentReliable.push(m);
		},
		onDatagram: () => {},
		onGameDatagram: (cb: DatagramCallback) => gameCbs.push(cb),
		onMessage: () => {},
		onStateChange: () => {},
		deliver: (d: Uint8Array) => gameCbs.forEach((cb) => cb(d))
	};
	return t as typeof t & Transport;
}

describe('game link', () => {
	it('routes reliable messages by type and supports unsubscribe', () => {
		const h = vi.fn();
		const off = onGameMessage('game_ping', h);
		dispatchGameMessage({ type: 'game_ping', n: 1 });
		dispatchGameMessage({ type: 'game_other' });
		expect(h).toHaveBeenCalledTimes(1);
		expect(h).toHaveBeenCalledWith({ type: 'game_ping', n: 1 });
		off();
		dispatchGameMessage({ type: 'game_ping', n: 2 });
		expect(h).toHaveBeenCalledTimes(1);
	});

	it('sends datagrams as [kind][payload] and routes replies by kind', () => {
		const t = fakeTransport();
		setGameTransport(t);
		const got: number[][] = [];
		const off = onGameDatagram(GameDatagramKind.Echo, (p) => got.push(Array.from(p)));

		expect(sendGameDatagram(GameDatagramKind.Echo, new Uint8Array([9, 8]))).toBe(true);
		expect(Array.from(t.sentGame[0])).toEqual([0x00, 9, 8]);

		t.deliver(new Uint8Array([0x00, 9, 8]));
		t.deliver(new Uint8Array([0x7f, 1])); // unknown kind: ignored
		t.deliver(new Uint8Array([])); // empty: ignored
		expect(got).toEqual([[9, 8]]);
		off();
		setGameTransport(null);
	});

	it('refuses to send when there is no transport, it is not ready, or the payload is too big', () => {
		setGameTransport(null);
		expect(sendGameDatagram(0, new Uint8Array(1))).toBe(false);
		expect(sendGameMessage({ type: 'game_ping' })).toBe(false);

		const notReady = fakeTransport(false);
		setGameTransport(notReady);
		expect(sendGameDatagram(0, new Uint8Array(1))).toBe(false);

		const t = fakeTransport();
		setGameTransport(t);
		expect(sendGameDatagram(0, new Uint8Array(1100))).toBe(false);
		expect(sendGameDatagram(0, new Uint8Array(1099))).toBe(true);
		expect(sendGameMessage({ type: 'game_ping', n: 3 })).toBe(true);
		expect(JSON.parse(t.sentReliable[0])).toEqual({ type: 'game_ping', n: 3 });
		setGameTransport(null);
	});

	it('ignores datagrams from a transport that has been replaced', () => {
		const oldT = fakeTransport();
		const newT = fakeTransport();
		const h = vi.fn();
		const off = onGameDatagram(0, h);
		setGameTransport(oldT);
		setGameTransport(newT);
		oldT.deliver(new Uint8Array([0, 1]));
		expect(h).not.toHaveBeenCalled();
		newT.deliver(new Uint8Array([0, 1]));
		expect(h).toHaveBeenCalledTimes(1);
		off();
		setGameTransport(null);
	});
});
