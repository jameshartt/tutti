/**
 * Connection self-test: joins a room, connects a transport, and exercises
 * every piece of game plumbing against the real server.
 *
 * Used by the /dev/selftest page (for people, on real devices) and by the
 * end-to-end tests (for CI). It deliberately does not touch audio.
 */

import type { Transport } from '../transport/transport.js';
import { WebTransportTransport } from '../transport/webtransport.js';
import { WebRTCTransport } from '../transport/webrtc.js';
import { detectTransportType } from '../transport/detect.js';
import { ServerClock } from '../latency/server-clock.js';
import { MAX_GAME_DATAGRAM_SIZE } from '../transport/game-datagram.js';
import {
	setGameTransport,
	dispatchGameMessage,
	onGameMessage,
	onGameDatagram,
	sendGameMessage,
	sendGameDatagram,
	gameDatagramsReady,
	GameDatagramKind,
	type GameMessage
} from './game-link.js';

export type SelfTestTransport = 'auto' | 'webtransport' | 'webrtc';

export interface SelfTestStep {
	name: string;
	ok: boolean;
	detail: string;
}

export interface SelfTestResult {
	pass: boolean;
	transport: string;
	steps: SelfTestStep[];
	echo: {
		sent: number;
		received: number;
		medianRttMs: number | null;
		largest?: { bytes: number; sent: number; received: number };
	};
	clock: { synced: boolean; offsetMs: number; uncertaintyMs: number | null };
	userAgent: string;
}

const ECHO_COUNT = 50;
const ECHO_INTERVAL_MS = 16;

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));

function withTimeout<T>(p: Promise<T>, ms: number, what: string): Promise<T> {
	return new Promise((resolve, reject) => {
		const timer = setTimeout(() => reject(new Error(`${what} timed out after ${ms}ms`)), ms);
		p.then(
			(v) => {
				clearTimeout(timer);
				resolve(v);
			},
			(e) => {
				clearTimeout(timer);
				reject(e);
			}
		);
	});
}

export async function runSelfTest(opts: {
	/** Room to use. Omit to pick an empty one automatically. */
	room?: string;
	transport: SelfTestTransport;
	onStep?: (step: SelfTestStep) => void;
}): Promise<SelfTestResult> {
	const steps: SelfTestStep[] = [];
	const result: SelfTestResult = {
		pass: false,
		transport: opts.transport,
		steps,
		echo: { sent: 0, received: 0, medianRttMs: null },
		clock: { synced: false, offsetMs: 0, uncertaintyMs: null },
		userAgent: navigator.userAgent
	};
	const step = (name: string, ok: boolean, detail = '') => {
		const s = { name, ok, detail };
		steps.push(s);
		opts.onStep?.(s);
		return ok;
	};

	let transport: Transport | null = null;
	let participantId: string | null = null;
	let joinedRoom = '';
	const cleanups: Array<() => void> = [];

	try {
		// 0. Pick a room. Joining an occupied room would show up in someone's
		// rehearsal and push them off the two-person fast path, so unless a
		// room was named explicitly, only ever use an empty one.
		let room = opts.room ?? '';
		if (!room) {
			const list = await (await fetch('/api/rooms')).json();
			const empty = (list.rooms as Array<{ name: string; participant_count: number; claimed: boolean }>)
				.filter((r) => r.participant_count === 0 && !r.claimed)
				.map((r) => r.name);
			if (empty.length === 0) {
				step('join', false, 'no empty room available right now');
				return result;
			}
			// Last in the list: least likely to be picked by people
			room = empty[empty.length - 1];
		}

		// 1. Join over HTTP
		const joinRes = await fetch(`/api/rooms/${encodeURIComponent(room)}/join`, {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({ alias: 'selftest', password: '' })
		});
		if (!joinRes.ok) {
			step('join', false, `HTTP ${joinRes.status}`);
			return result;
		}
		participantId = (await joinRes.json()).participant_id as string;
		joinedRoom = room;
		step('join', true, room);

		// 2. Connect the transport
		const kind =
			opts.transport === 'auto' ? detectTransportType() : opts.transport;
		result.transport = kind;
		// Each incoming message goes to exactly one place: the first waiter
		// whose predicate accepts it, or else the backlog for a later waiter.
		type Msg = Record<string, unknown>;
		const backlog: Msg[] = [];
		const waiters: Array<{ match: (m: Msg) => boolean; resolve: (m: Msg) => void }> = [];
		const nextMessage = (match: (m: Msg) => boolean, ms: number, what: string) =>
			withTimeout(
				new Promise<Msg>((resolve) => {
					const i = backlog.findIndex(match);
					if (i >= 0) return resolve(backlog.splice(i, 1)[0]);
					waiters.push({ match, resolve });
				}),
				ms,
				`waiting for ${what}`
			);

		if (kind === 'webtransport') {
			const info = await (await fetch('/api/transport')).json();
			const url = (info.wt_url as string) ?? `https://${location.hostname}:4433/wt`;
			transport = new WebTransportTransport();
			transport.onMessage((raw) => onRaw(raw));
			await withTimeout(
				transport.connect(url, { certHash: info.cert_hash }),
				8000,
				'WebTransport connect'
			);
		} else {
			const wsProto = location.protocol === 'https:' ? 'wss:' : 'ws:';
			transport = new WebRTCTransport();
			transport.onMessage((raw) => onRaw(raw));
			await withTimeout(
				transport.connect(`${wsProto}//${location.host}/ws`),
				15000,
				'WebRTC connect'
			);
		}
		function onRaw(raw: string) {
			let m: Record<string, unknown>;
			try {
				m = JSON.parse(raw);
			} catch {
				return;
			}
			if (typeof m.type === 'string' && m.type.startsWith('game_')) {
				// Same path Room.svelte uses
				dispatchGameMessage(m as GameMessage);
			}
			const i = waiters.findIndex((w) => w.match(m));
			if (i >= 0) waiters.splice(i, 1)[0].resolve(m);
			else backlog.push(m);
		}
		step('connect', true, kind);
		setGameTransport(transport);

		// 3. Bind to the room
		transport.sendReliable(
			JSON.stringify({ type: 'bind', participant_id: participantId, room: joinedRoom })
		);
		await nextMessage((m) => m.type === 'room_state', 5000, 'room_state');
		step('bind', true, 'room_state received');

		// 4. Clock sync from three pings
		const clock = new ServerClock();
		for (let i = 0; i < 3; i++) {
			const t = performance.now();
			const id = 1000 + i;
			transport.sendReliable(JSON.stringify({ type: 'ping', id, t }));
			// Pair each pong with its own ping
			const pong = await nextMessage(
				(m) => m.type === 'pong' && m.id === id,
				5000,
				`pong ${id}`
			);
			if (typeof pong.server_t === 'number') {
				clock.addSample(t, performance.now() - t, pong.server_t);
			}
		}
		result.clock = {
			synced: clock.synced,
			offsetMs: clock.offsetMs,
			uncertaintyMs: clock.synced ? clock.uncertaintyMs : null
		};
		// A stale or mismatched pong would show up as an implausibly small
		// round trip, so sanity-check the estimate rather than just "synced"
		const plausible = clock.synced && clock.uncertaintyMs > 0 && clock.uncertaintyMs < 2500;
		step(
			'clock',
			plausible,
			clock.synced ? `±${clock.uncertaintyMs.toFixed(1)}ms` : 'no server_t in pong'
		);

		// 5. Reliable game message round trip
		const nonce = Math.floor(Math.random() * 1e9);
		const pingBack = withTimeout(
			new Promise<Record<string, unknown>>((resolve) => {
				cleanups.push(
					onGameMessage('game_ping', (m) => {
						if (m.n === nonce) resolve(m);
					})
				);
			}),
			5000,
			'game_ping broadcast'
		);
		sendGameMessage({ type: 'game_ping', n: nonce });
		const back = await pingBack;
		step('game message', back.from === participantId, `from=${String(back.from).slice(0, 8)}…`);

		// 5b. The largest reliable game message the server accepts (16 KB),
		// which on WebTransport arrives split across several stream reads
		const bigNonce = nonce + 1;
		const LARGEST_GAME_MESSAGE = 16 * 1024;
		const envelope = JSON.stringify({ type: 'game_ping', n: bigNonce, pad: '' });
		const pad = 'x'.repeat(LARGEST_GAME_MESSAGE - new TextEncoder().encode(envelope).length);
		const bigBack = withTimeout(
			new Promise<Record<string, unknown>>((resolve) => {
				cleanups.push(
					onGameMessage('game_ping', (m) => {
						if (m.n === bigNonce) resolve(m);
					})
				);
			}),
			5000,
			'16 KB game_ping broadcast'
		);
		sendGameMessage({ type: 'game_ping', n: bigNonce, pad });
		const big = await bigBack;
		step(
			'large message',
			big.pad === pad && big.from === participantId,
			`${LARGEST_GAME_MESSAGE} bytes ${big.pad === pad ? 'intact' : 'corrupted'}`
		);

		// 6. Unreliable game datagrams (the "game" channel may open a moment
		// after connect on WebRTC)
		for (let i = 0; i < 100 && !gameDatagramsReady(); i++) await sleep(50);
		if (!step('game channel', gameDatagramsReady(), gameDatagramsReady() ? 'open' : 'never opened')) {
			return result;
		}

		const sentAt = new Map<number, number>();
		const rtts: number[] = [];
		let corrupt = 0;
		cleanups.push(
			onGameDatagram(GameDatagramKind.Echo, (payload) => {
				const dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
				if (payload.byteLength !== 64) return;
				if (dv.getUint32(4, true) !== nonce) {
					corrupt++;
					return;
				}
				const t = sentAt.get(dv.getUint32(0, true));
				if (t !== undefined) rtts.push(performance.now() - t);
			})
		);
		for (let i = 0; i < ECHO_COUNT; i++) {
			const payload = new Uint8Array(64);
			const dv = new DataView(payload.buffer);
			dv.setUint32(0, i, true);
			dv.setUint32(4, nonce, true);
			sentAt.set(i, performance.now());
			if (sendGameDatagram(GameDatagramKind.Echo, payload)) result.echo.sent++;
			await sleep(ECHO_INTERVAL_MS);
		}
		await sleep(500);
		rtts.sort((a, b) => a - b);
		result.echo.received = rtts.length;
		result.echo.medianRttMs = rtts.length ? rtts[Math.floor(rtts.length / 2)] : null;
		// Unreliable channel: tolerate loss, but most must arrive, intact
		step(
			'game datagrams',
			corrupt === 0 && rtts.length >= ECHO_COUNT * 0.8,
			`${rtts.length}/${result.echo.sent} echoed` + (corrupt ? `, ${corrupt} corrupt` : '')
		);

		// 7. The largest datagram the protocol allows must survive a round
		// trip too (it is close to the QUIC datagram size limit)
		const bigPayload = new Uint8Array(MAX_GAME_DATAGRAM_SIZE - 1);
		for (let i = 0; i < bigPayload.length; i++) bigPayload[i] = (i * 31 + nonce) & 0xff;
		let bigOk = 0;
		cleanups.push(
			onGameDatagram(GameDatagramKind.Echo, (payload) => {
				if (payload.byteLength !== bigPayload.length) return;
				for (let i = 0; i < payload.length; i++) if (payload[i] !== bigPayload[i]) return;
				bigOk++;
			})
		);
		for (let i = 0; i < 10; i++) {
			sendGameDatagram(GameDatagramKind.Echo, bigPayload);
			await sleep(ECHO_INTERVAL_MS);
		}
		await sleep(500);
		result.echo.largest = { bytes: MAX_GAME_DATAGRAM_SIZE, sent: 10, received: bigOk };
		step('largest datagram', bigOk >= 8, `${bigOk}/10 of ${MAX_GAME_DATAGRAM_SIZE} bytes`);

		result.pass = steps.every((s) => s.ok);
		return result;
	} catch (e) {
		step('error', false, e instanceof Error ? e.message : String(e));
		return result;
	} finally {
		for (const c of cleanups) c();
		setGameTransport(null);
		transport?.disconnect();
		if (participantId) {
			fetch(`/api/rooms/${encodeURIComponent(joinedRoom)}/leave`, {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ participant_id: participantId }),
				keepalive: true
			}).catch(() => {});
		}
	}
}
