/**
 * Game datagram framing — mirrors server/src/transport/game_datagram.h.
 *
 * Above the transport layer a game datagram is always [kind u8][payload...].
 *
 * - WebTransport shares one datagram channel with audio, so game datagrams
 *   are prefixed with four 0xFF bytes. That is where an audio packet carries
 *   its little-endian u32 sequence number, and the audio sequence counters
 *   never emit 0xFFFFFFFF (see audioSequence).
 * - WebRTC sends game datagrams on their own "game" DataChannel, unframed.
 */

export const GAME_MAGIC_SIZE = 4;
export const RESERVED_AUDIO_SEQUENCE = 0xffffffff;

/** Largest [kind][payload] sent or accepted (matches the server). */
export const MAX_GAME_DATAGRAM_SIZE = 1100;

/** True if a raw WebTransport datagram is a game datagram. */
export function isGameDatagram(data: Uint8Array): boolean {
	return (
		data.byteLength > GAME_MAGIC_SIZE &&
		data[0] === 0xff &&
		data[1] === 0xff &&
		data[2] === 0xff &&
		data[3] === 0xff
	);
}

/** Prefix [kind][payload] with the magic for sending over WebTransport. */
export function frameGameDatagram(body: Uint8Array): Uint8Array {
	const framed = new Uint8Array(GAME_MAGIC_SIZE + body.byteLength);
	framed.fill(0xff, 0, GAME_MAGIC_SIZE);
	framed.set(body, GAME_MAGIC_SIZE);
	return framed;
}

/** Strip the magic from a received WebTransport game datagram (no copy). */
export function unframeGameDatagram(data: Uint8Array): Uint8Array {
	return data.subarray(GAME_MAGIC_SIZE);
}

/**
 * Wire sequence number for the `count`-th audio packet sent (0-based).
 * Wraps one short of 2^32, so it never produces the reserved value and an
 * audio packet can never be mistaken for a game datagram.
 */
export function audioSequence(count: number): number {
	return count % RESERVED_AUDIO_SEQUENCE;
}
