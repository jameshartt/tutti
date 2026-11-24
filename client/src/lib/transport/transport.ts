/**
 * Abstract transport interface for network communication.
 *
 * Implemented by WebTransport (primary) and WebRTC DataChannel (Safari fallback).
 * Both provide:
 * - Unreliable datagrams for audio packets
 * - Unreliable datagrams for game traffic, kept apart from audio
 * - Reliable channel for control messages (JSON)
 */

export type DatagramCallback = (data: Uint8Array) => void;
export type MessageCallback = (message: string) => void;
export type StateCallback = (state: TransportState) => void;

export type TransportState = 'connecting' | 'connected' | 'disconnected' | 'failed';

export interface Transport {
	/** Current connection state */
	readonly state: TransportState;

	/** Connect to the server */
	connect(url: string, options?: { certHash?: string }): Promise<void>;

	/** Disconnect from the server */
	disconnect(): void;

	/** Send an unreliable audio datagram */
	sendDatagram(data: Uint8Array): void;

	/**
	 * Send an unreliable game datagram: [kind u8][payload...]. The transport
	 * adds whatever framing it needs to keep it apart from audio. Dropped
	 * silently if game datagrams are not ready.
	 */
	sendGameDatagram(data: Uint8Array): void;

	/** True when game datagrams can be sent */
	readonly gameDatagramsReady: boolean;

	/** Send a reliable control message */
	sendReliable(message: string): void;

	/** Register callback for incoming datagrams */
	onDatagram(callback: DatagramCallback): void;

	/**
	 * Register callback for incoming game datagrams ([kind u8][payload...],
	 * transport framing already removed). Never called for audio.
	 */
	onGameDatagram(callback: DatagramCallback): void;

	/** Register callback for incoming reliable messages */
	onMessage(callback: MessageCallback): void;

	/** Register callback for state changes */
	onStateChange(callback: StateCallback): void;
}
