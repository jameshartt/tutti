/**
 * Transport diagnostics beacon.
 *
 * Reports transport connect outcomes to the server over plain HTTPS —
 * the one channel guaranteed to work whenever the page loaded at all.
 * This is how we diagnose "works on WiFi, fails on 5G" class problems:
 * the server logs show exactly which stage failed and what ICE saw.
 */

export function reportTransportEvent(event: Record<string, unknown>): void {
	try {
		const nav = navigator as Navigator & {
			connection?: { type?: string; effectiveType?: string };
		};
		fetch('/api/client-log', {
			method: 'POST',
			headers: { 'Content-Type': 'application/json' },
			body: JSON.stringify({
				...event,
				ua: navigator.userAgent.slice(0, 160),
				netType: nav.connection?.type ?? null,
				netEffective: nav.connection?.effectiveType ?? null
			}),
			keepalive: true
		}).catch(() => {});
	} catch {
		// Telemetry must never break the app
	}
}

/**
 * Summarize an ICE candidate line without leaking full addresses:
 * "srflx/v4", "host/v6", "host/mdns", "relay/v4" …
 */
export function summarizeCandidate(candidate: string): string {
	const typeMatch = candidate.match(/ typ (\w+)/);
	const type = typeMatch ? typeMatch[1] : '?';
	// Candidate line format: foundation component protocol priority address port ...
	const parts = candidate.split(' ');
	const address = parts[4] ?? '';
	let family = 'v4';
	if (address.endsWith('.local')) family = 'mdns';
	else if (address.includes(':')) family = 'v6';
	return `${type}/${family}`;
}
