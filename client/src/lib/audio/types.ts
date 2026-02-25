/**
 * Audio pipeline types for Tutti.
 */

/** Audio parameters matching server protocol */
export const SAMPLE_RATE = 48_000;
export const SAMPLES_PER_FRAME = 128;
export const BYTES_PER_SAMPLE = 2; // int16
export const AUDIO_HEADER_SIZE = 8; // 4-byte sequence + 4-byte timestamp
export const AUDIO_PAYLOAD_SIZE = SAMPLES_PER_FRAME * BYTES_PER_SAMPLE; // 256
export const AUDIO_PACKET_SIZE = AUDIO_HEADER_SIZE + AUDIO_PAYLOAD_SIZE; // 264

/** Binary audio packet as sent over the wire */
export interface AudioPacket {
	sequence: number;
	timestamp: number;
	samples: Int16Array;
}

/** Serialize an AudioPacket to a Uint8Array for transmission */
export function serializePacket(packet: AudioPacket): Uint8Array {
	const buf = new ArrayBuffer(AUDIO_PACKET_SIZE);
	const view = new DataView(buf);
	view.setUint32(0, packet.sequence, true); // little-endian
	view.setUint32(4, packet.timestamp, true);
	const bytes = new Uint8Array(buf);
	bytes.set(new Uint8Array(packet.samples.buffer, packet.samples.byteOffset, AUDIO_PAYLOAD_SIZE), AUDIO_HEADER_SIZE);
	return bytes;
}

/** Deserialize a Uint8Array into an AudioPacket */
export function deserializePacket(data: Uint8Array): AudioPacket | null {
	if (data.byteLength < AUDIO_PACKET_SIZE) return null;
	const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
	return {
		sequence: view.getUint32(0, true),
		timestamp: view.getUint32(4, true),
		samples: new Int16Array(data.buffer, data.byteOffset + AUDIO_HEADER_SIZE, SAMPLES_PER_FRAME)
	};
}

/** Latency gradation based on acoustic distance */
export type LatencyGradation = 'side-by-side' | 'small-room' | 'large-room' | 'hall' | 'stadium';

/** Latency information for display */
export interface LatencyInfo {
	/** Total one-way latency in milliseconds */
	totalMs: number;
	/** Equivalent acoustic distance in metres */
	distanceMetres: number;
	/** Human-readable gradation */
	gradation: LatencyGradation;
	/** Gradation display label */
	label: string;
	/** Colour for UI indicator */
	colour: 'green' | 'amber' | 'red';
}

/** Detailed latency breakdown (nerd view) */
export interface LatencyBreakdown {
	hardwareInputMs: number;
	hardwareOutputMs: number;
	captureBufferMs: number;
	playbackBufferMs: number;
	networkOneWayMs: number;
	serverProcessingMs: number;
	fudgeFactorMs: number;
	totalMs: number;
}

/** Room participant info */
export interface Participant {
	id: string;
	name: string;
	gain: number;
	muted: boolean;
	latency?: LatencyInfo;
}

/** Room info from lobby listing */
export interface RoomInfo {
	name: string;
	participant_count: number;
	max_participants: number;
	claimed: boolean;
}

/** Audio pipeline state */
export type PipelineState = 'inactive' | 'initializing' | 'active' | 'error' | 'disconnected';

/** Transport type */
export type TransportType = 'webtransport' | 'webrtc';
