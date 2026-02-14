import type { Handle } from '@sveltejs/kit';

/**
 * Set Cross-Origin isolation headers on every response.
 *
 * SharedArrayBuffer (required by AudioWorklet ring-buffers and
 * potentially WASM-based codecs) is only available in "cross-origin
 * isolated" contexts.  The two headers below opt the page into that
 * mode.
 *
 *   COEP: require-corp  - blocks cross-origin resources that don't
 *                          explicitly grant permission (via CORS or
 *                          Cross-Origin-Resource-Policy).
 *   COOP: same-origin   - isolates the browsing context group so
 *                          SharedArrayBuffer can be used.
 */
export const handle: Handle = async ({ event, resolve }) => {
	const response = await resolve(event);

	response.headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
	response.headers.set('Cross-Origin-Opener-Policy', 'same-origin');

	return response;
};
