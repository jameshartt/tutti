/**
 * Abstract transport interface for network communication.
 *
 * Implemented by WebTransport (primary) and WebRTC DataChannel (Safari fallback).
 * Both provide:
 * - Unreliable datagrams for audio packets
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

	/** Send a reliable control message */
	sendReliable(message: string): void;

	/** Register callback for incoming datagrams */
	onDatagram(callback: DatagramCallback): void;

	/** Register callback for incoming reliable messages */
	onMessage(callback: MessageCallback): void;

	/** Register callback for state changes */
	onStateChange(callback: StateCallback): void;
}
