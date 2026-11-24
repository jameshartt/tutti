import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { WebTransportTransport } from '../lib/transport/webtransport';
import { frameGameDatagram } from '../lib/transport/game-datagram';

/**
 * Drives the real WebTransportTransport read loops with a fake WebTransport,
 * so the audio/game demux and the control-line reassembly are tested as
 * wired, not just the framing helpers.
 */
class FakeWebTransport {
	static last: FakeWebTransport;
	ready = Promise.resolve();
	closed = new Promise<void>(() => {});
	datagramsIn!: ReadableStreamDefaultController<Uint8Array>;
	controlIn!: ReadableStreamDefaultController<Uint8Array>;
	sentDatagrams: Uint8Array[] = [];
	sentControl: Uint8Array[] = [];
	datagrams: {
		readable: ReadableStream<Uint8Array>;
		writable: WritableStream<Uint8Array>;
		incomingHighWaterMark: number;
		outgoingHighWaterMark: number;
	};

	constructor() {
		FakeWebTransport.last = this;
		this.datagrams = {
			readable: new ReadableStream<Uint8Array>({
				start: (c) => {
					this.datagramsIn = c;
				}
			}),
			writable: new WritableStream<Uint8Array>({
				write: (chunk) => {
					this.sentDatagrams.push(chunk);
				}
			}),
			incomingHighWaterMark: 0,
			outgoingHighWaterMark: 0
		};
	}

	async createBidirectionalStream() {
		return {
			readable: new ReadableStream<Uint8Array>({
				start: (c) => {
					this.controlIn = c;
				}
			}),
			writable: new WritableStream<Uint8Array>({
				write: (chunk) => {
					this.sentControl.push(chunk);
				}
			})
		};
	}

	close() {}
}

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function pcmPacket(sequence: number): Uint8Array {
	const packet = new Uint8Array(264);
	new DataView(packet.buffer).setUint32(0, sequence, true);
	for (let i = 8; i < packet.length; i++) packet[i] = (i * 7) & 0xff;
	return packet;
}

describe('WebTransportTransport demux', () => {
	const original = (globalThis as Record<string, unknown>).WebTransport;
	let transport: WebTransportTransport;
	let audio: Uint8Array[];
	let game: Uint8Array[];
	let messages: string[];

	beforeEach(async () => {
		(globalThis as Record<string, unknown>).WebTransport = FakeWebTransport;
		transport = new WebTransportTransport();
		audio = [];
		game = [];
		messages = [];
		transport.onDatagram((d) => audio.push(new Uint8Array(d)));
		transport.onGameDatagram((d) => game.push(new Uint8Array(d)));
		transport.onMessage((m) => messages.push(m));
		await transport.connect('https://example.invalid/wt');
	});

	afterEach(() => {
		(globalThis as Record<string, unknown>).WebTransport = original;
	});

	it('delivers a game datagram to game handlers only, with the magic removed', async () => {
		const body = Uint8Array.of(0x00, 1, 2, 3);
		FakeWebTransport.last.datagramsIn.enqueue(frameGameDatagram(body));
		await flush();
		expect(audio).toHaveLength(0);
		expect(game).toEqual([body]);
	});

	it('never passes audio-sized game datagrams to audio handlers', async () => {
		// Framed lengths equal to a PCM packet (264) and an Opus packet (100)
		for (const framedLength of [264, 100]) {
			FakeWebTransport.last.datagramsIn.enqueue(
				frameGameDatagram(new Uint8Array(framedLength - 4).fill(0x11))
			);
		}
		await flush();
		expect(audio).toHaveLength(0);
		expect(game).toHaveLength(2);
	});

	it('delivers audio packets to audio handlers only, byte for byte', async () => {
		const packets = [pcmPacket(0), pcmPacket(1), pcmPacket(0xfffffffe)];
		for (const p of packets) FakeWebTransport.last.datagramsIn.enqueue(p);
		await flush();
		expect(game).toHaveLength(0);
		expect(audio).toEqual(packets);
	});

	it('keeps audio and game apart when they are interleaved', async () => {
		const a = pcmPacket(5);
		const g = Uint8Array.of(0x00, 9);
		const inbound = FakeWebTransport.last.datagramsIn;
		inbound.enqueue(a);
		inbound.enqueue(frameGameDatagram(g));
		inbound.enqueue(a);
		await flush();
		expect(audio).toEqual([a, a]);
		expect(game).toEqual([g]);
	});

	it('sends audio unchanged and game datagrams with the magic prefix', async () => {
		const a = pcmPacket(7);
		transport.sendDatagram(a);
		transport.sendGameDatagram(Uint8Array.of(0x00, 42));
		await flush();
		const sent = FakeWebTransport.last.sentDatagrams;
		expect(sent).toHaveLength(2);
		expect(sent[0]).toEqual(a);
		expect(Array.from(sent[1])).toEqual([0xff, 0xff, 0xff, 0xff, 0x00, 42]);
	});

	it('reassembles a control message split across stream reads', async () => {
		const encoder = new TextEncoder();
		const big = JSON.stringify({ type: 'game_big', data: 'x'.repeat(16 * 1024) });
		const bytes = encoder.encode(big + '\n{"type":"pong"}\n');
		const inbound = FakeWebTransport.last.controlIn;
		for (let i = 0; i < bytes.length; i += 1000) {
			inbound.enqueue(bytes.slice(i, i + 1000));
		}
		await flush();
		expect(messages).toEqual([big, '{"type":"pong"}']);
	});
});
