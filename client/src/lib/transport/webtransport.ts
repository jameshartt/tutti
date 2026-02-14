/**
 * WebTransport implementation.
 * Primary transport for Chrome, Firefox, Edge, Android Chrome.
 *
 * Uses unreliable QUIC datagrams for audio packets and a bidirectional
 * stream for reliable control messages.
 */

import type {
	Transport,
	TransportState,
	DatagramCallback,
	MessageCallback,
	StateCallback
} from './transport.js';

export class WebTransportTransport implements Transport {
	private wt: WebTransport | null = null;
	private controlWriter: WritableStreamDefaultWriter<Uint8Array> | null = null;
	private datagramWriter: WritableStreamDefaultWriter<Uint8Array> | null = null;
	private datagramCallbacks: DatagramCallback[] = [];
	private messageCallbacks: MessageCallback[] = [];
	private stateCallbacks: StateCallback[] = [];
	private _state: TransportState = 'disconnected';

	get state(): TransportState {
		return this._state;
	}

	async connect(url: string, options?: { certHash?: string }): Promise<void> {
		this.setState('connecting');

		try {
			const wtOptions: WebTransportOptions = {};
			if (options?.certHash) {
				const binary = Uint8Array.from(atob(options.certHash), c => c.charCodeAt(0));
				wtOptions.serverCertificateHashes = [{
					algorithm: 'sha-256',
					value: binary.buffer
				}];
			}
			this.wt = new WebTransport(url, wtOptions);
			await this.wt.ready;
			this.setState('connected');

			// Acquire datagram writer (cached for lifetime of connection)
			this.datagramWriter = this.wt.datagrams.writable.getWriter();

			// Start reading datagrams
			this.readDatagrams();

			// Open a bidirectional stream for control messages
			await this.openControlStream();

			// Handle connection close
			this.wt.closed.then(() => {
				this.setState('disconnected');
			}).catch(() => {
				this.setState('failed');
			});
		} catch {
			this.setState('failed');
			throw new Error('WebTransport connection failed');
		}
	}

	disconnect(): void {
		this.datagramWriter?.releaseLock();
		this.datagramWriter = null;
		this.controlWriter = null;
		if (this.wt) {
			this.wt.close();
			this.wt = null;
		}
		this.setState('disconnected');
	}

	sendDatagram(data: Uint8Array): void {
		if (!this.datagramWriter || this._state !== 'connected') return;
		this.datagramWriter.write(data).catch(() => {
			// Connection closed
		});
	}

	sendReliable(message: string): void {
		if (!this.controlWriter) return;
		const encoded = new TextEncoder().encode(message + '\n');
		this.controlWriter.write(encoded).catch(() => {
			// Stream closed
		});
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
		this._state = state;
		for (const cb of this.stateCallbacks) {
			cb(state);
		}
	}

	/** Read datagrams in a loop */
	private async readDatagrams(): Promise<void> {
		if (!this.wt) return;
		const reader = this.wt.datagrams.readable.getReader();
		try {
			while (true) {
				const { value, done } = await reader.read();
				if (done) break;
				for (const cb of this.datagramCallbacks) {
					cb(value);
				}
			}
		} catch {
			// Connection closed
		} finally {
			reader.releaseLock();
		}
	}

	/** Open a bidirectional stream for reliable control messages */
	private async openControlStream(): Promise<void> {
		if (!this.wt) return;

		const stream = await this.wt.createBidirectionalStream();
		this.controlWriter = stream.writable.getWriter();

		// Read incoming control messages
		const reader = stream.readable.getReader();
		const decoder = new TextDecoder();
		let buffer = '';

		(async () => {
			try {
				while (true) {
					const { value, done } = await reader.read();
					if (done) break;
					buffer += decoder.decode(value, { stream: true });

					// Split on newlines (our message delimiter)
					const lines = buffer.split('\n');
					buffer = lines.pop() ?? '';
					for (const line of lines) {
						if (line.trim()) {
							for (const cb of this.messageCallbacks) {
								cb(line);
							}
						}
					}
				}
			} catch {
				// Stream closed
			} finally {
				reader.releaseLock();
			}
		})();
	}
}
