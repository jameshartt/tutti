import { describe, it, expect } from 'vitest';
import {
	isGameDatagram,
	frameGameDatagram,
	unframeGameDatagram,
	audioSequence,
	RESERVED_AUDIO_SEQUENCE
} from '../lib/transport/game-datagram';

describe('game datagram framing', () => {
	it('round-trips a body through frame/unframe', () => {
		const body = new Uint8Array([0x00, 1, 2, 3]);
		const framed = frameGameDatagram(body);
		expect(framed.byteLength).toBe(8);
		expect(isGameDatagram(framed)).toBe(true);
		expect(Array.from(unframeGameDatagram(framed))).toEqual([0x00, 1, 2, 3]);
	});

	it('does not treat the bare magic (no kind byte) as a game datagram', () => {
		expect(isGameDatagram(new Uint8Array([0xff, 0xff, 0xff, 0xff]))).toBe(false);
	});

	it('never classifies an audio packet as a game datagram', () => {
		// PCM-sized and Opus-sized packets with every sequence the counter
		// can produce around the wrap point
		for (let count = RESERVED_AUDIO_SEQUENCE - 8; count < RESERVED_AUDIO_SEQUENCE + 8; count++) {
			for (const size of [264, 60]) {
				const pkt = new Uint8Array(size);
				new DataView(pkt.buffer).setUint32(0, audioSequence(count), true);
				expect(isGameDatagram(pkt)).toBe(false);
			}
		}
	});
});

describe('audioSequence', () => {
	it('counts normally from zero', () => {
		expect(audioSequence(0)).toBe(0);
		expect(audioSequence(12345)).toBe(12345);
	});

	it('skips the reserved value and wraps to zero', () => {
		expect(audioSequence(RESERVED_AUDIO_SEQUENCE - 1)).toBe(0xfffffffe);
		expect(audioSequence(RESERVED_AUDIO_SEQUENCE)).toBe(0);
		expect(audioSequence(RESERVED_AUDIO_SEQUENCE + 1)).toBe(1);
	});
});
