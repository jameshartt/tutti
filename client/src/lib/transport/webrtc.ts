/**
 * WebRTC DataChannel transport implementation.
 * Fallback for Safari and iOS where WebTransport is not available.
 *
 * Uses an unreliable+unordered DataChannel for audio datagrams
 * and a reliable DataChannel for control messages.
 * SDP signaling is done over WebSocket.
 */

import type {
	Transport,
	TransportState,
	DatagramCallback,
	MessageCallback,
	StateCallback
} from './transport.js';
import { summarizeCandidate } from './diagnostics.js';

/** Connection-attempt trace for the diagnostics beacon */
export interface RtcDiagnostics {
	wsConnected: boolean;
	answerReceived: boolean;
	localCandidates: string[];
	remoteCandidates: string[];
	iceStates: string[];
	connStates: string[];
}

export class WebRTCTransport implements Transport {
	private pc: RTCPeerConnection | null = null;
	private audioDC: RTCDataChannel | null = null;
	private controlDC: RTCDataChannel | null = null;
	private signalingWs: WebSocket | null = null;
	private datagramCallbacks: DatagramCallback[] = [];
	private messageCallbacks: MessageCallback[] = [];
	private stateCallbacks: StateCallback[] = [];
	private _state: TransportState = 'disconnected';
	private diag: RtcDiagnostics = {
		wsConnected: false,
		answerReceived: false,
		localCandidates: [],
		remoteCandidates: [],
		iceStates: [],
		connStates: []
	};

	get state(): TransportState {
		return this._state;
	}

	getDiagnostics(): RtcDiagnostics {
		return this.diag;
	}

	/**
	 * Connect via WebRTC.
	 * @param url WebSocket signaling URL (ws://host:port)
	 */
	async connect(url: string): Promise<void> {
		this.setState('connecting');

		try {
			// Connect to signaling server
			await this.connectSignaling(url);

			// Promise that resolves when both DataChannels are open
			const channelsOpen = new Promise<void>((resolve, reject) => {
				const checkBothOpen = () => {
					if (
						this.audioDC?.readyState === 'open' &&
						this.controlDC?.readyState === 'open'
					) {
						this.setState('connected');
						resolve();
					}
				};

				// Create peer connection
				this.pc = new RTCPeerConnection({
					iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
				});

				// Create audio data channel (unreliable, unordered)
				this.audioDC = this.pc.createDataChannel('audio', {
					ordered: false,
					maxRetransmits: 0
				});
				this.audioDC.binaryType = 'arraybuffer';

				this.audioDC.onopen = checkBothOpen;

				this.audioDC.onmessage = (event) => {
					const data = new Uint8Array(event.data);
					for (const cb of this.datagramCallbacks) {
						cb(data);
					}
				};

				// Create control data channel (reliable, ordered)
				this.controlDC = this.pc.createDataChannel('control');

				this.controlDC.onopen = checkBothOpen;

				this.controlDC.onmessage = (event) => {
					for (const cb of this.messageCallbacks) {
						cb(event.data);
					}
				};

				// Handle ICE candidates
				this.pc.onicecandidate = (event) => {
					if (event.candidate && this.signalingWs) {
						this.diag.localCandidates.push(
							summarizeCandidate(event.candidate.candidate)
						);
						this.signalingWs.send(
							JSON.stringify({
								type: 'ice_candidate',
								candidate: event.candidate.candidate,
								mid: event.candidate.sdpMid
							})
						);
					}
				};

				this.pc.oniceconnectionstatechange = () => {
					if (this.pc) this.diag.iceStates.push(this.pc.iceConnectionState);
				};

				this.pc.onconnectionstatechange = () => {
					if (!this.pc) return;
					const pcState = this.pc.connectionState;
					this.diag.connStates.push(pcState);
					if (pcState === 'disconnected' || pcState === 'closed') {
						this.setState('disconnected');
					} else if (pcState === 'failed') {
						this.setState('failed');
						reject(new Error('WebRTC peer connection failed'));
					}
				};
			});

			// Create and send offer (pc is set synchronously above)
			const offer = await this.pc!.createOffer();
			await this.pc!.setLocalDescription(offer);

			this.signalingWs!.send(
				JSON.stringify({
					type: 'offer',
					sdp: offer.sdp
				})
			);

			// Wait for both DataChannels to open before resolving
			await channelsOpen;
		} catch {
			this.setState('failed');
			throw new Error('WebRTC connection failed');
		}
	}

	disconnect(): void {
		this.audioDC?.close();
		this.controlDC?.close();
		this.pc?.close();
		this.signalingWs?.close();
		this.audioDC = null;
		this.controlDC = null;
		this.pc = null;
		this.signalingWs = null;
		this.setState('disconnected');
	}

	sendDatagram(data: Uint8Array): void {
		if (!this.audioDC || this.audioDC.readyState !== 'open') return;
		this.audioDC.send(data);
	}

	sendReliable(message: string): void {
		if (!this.controlDC || this.controlDC.readyState !== 'open') return;
		this.controlDC.send(message);
	}

	onDatagram(callback: DatagramCallback): void {
		this.datagramCallbacks.push(callback);
	}

	onMessage(callback: MessageCallback): void {
		this.messageCallbacks.push(callback);
	}

	onStateChange(callback: StateCallback): void {
		this.stateCallbacks.push(callback);
	}

	private setState(state: TransportState): void {
		// No-op on unchanged state. Callbacks fire synchronously, and a
		// listener may respond to 'disconnected' by calling disconnect()
		// again — without this guard that recurses until stack overflow.
		if (this._state === state) return;
		this._state = state;
		for (const cb of this.stateCallbacks) {
			cb(state);
		}
	}

	/** Connect to signaling WebSocket */
	private connectSignaling(url: string): Promise<void> {
		return new Promise((resolve, reject) => {
			this.signalingWs = new WebSocket(url);

			this.signalingWs.onopen = () => {
				this.diag.wsConnected = true;
				resolve();
			};
			this.signalingWs.onerror = () => reject(new Error('Signaling connection failed'));

			this.signalingWs.onmessage = async (event) => {
				const msg = JSON.parse(event.data);

				if (msg.type === 'answer' && this.pc) {
					this.diag.answerReceived = true;
					await this.pc.setRemoteDescription({
						type: 'answer',
						sdp: msg.sdp
					});
				} else if (msg.type === 'ice_candidate' && this.pc) {
					this.diag.remoteCandidates.push(summarizeCandidate(msg.candidate));
					await this.pc.addIceCandidate({
						candidate: msg.candidate,
						sdpMid: msg.mid
					});
				}
			};

			this.signalingWs.onclose = () => {
				// Signaling socket closed - this is expected after connection
			};
		});
	}
}
