import { test, expect } from '@playwright/test';

/**
 * Game plumbing against a real server, in a real browser: join, connect,
 * bind, clock sync, a reliable game message round trip, and unreliable game
 * datagrams echoed back.
 *
 * Needs a running tutti-server behind the dev proxy. Set TUTTI_SERVER_BIN
 * (Playwright starts it) or TUTTI_E2E_BACKEND=1 (you started one yourself).
 * Set TUTTI_E2E_WEBTRANSPORT=1 when that server was built with WebTransport.
 */
const haveBackend = !!process.env.TUTTI_SERVER_BIN || !!process.env.TUTTI_E2E_BACKEND;
const haveWebTransport = !!process.env.TUTTI_E2E_WEBTRANSPORT;

// Each engine gets its own room so parallel projects never fill one up
const ROOM_BY_PROJECT: Record<string, string> = {
	chromium: 'Legato',
	firefox: 'Ostinato',
	webkit: 'Pizzicato'
};

async function runSelfTest(
	page: import('@playwright/test').Page,
	transport: string,
	room: string
) {
	await page.goto(`/dev/selftest?transport=${transport}&room=${room}&autostart`);
	const resultEl = page.getByTestId('result');
	await expect(resultEl).toBeVisible({ timeout: 45_000 });
	return JSON.parse((await resultEl.textContent()) ?? '{}');
}

/**
 * Against a server on the same machine the clock estimate must be tight.
 * Its uncertainty is half the best round trip; the self-test's own limit
 * is loose because it also runs on real devices over the internet.
 */
function expectLocalClockAccuracy(result: { steps: { name: string; detail?: string }[] }) {
	const clock = result.steps.find((s) => s.name === 'clock');
	const match = /±([\d.]+)ms/.exec(clock?.detail ?? '');
	expect(match, `clock step detail: ${clock?.detail}`).not.toBeNull();
	expect(Number(match![1])).toBeLessThan(25);
}

test.describe('game transport', () => {
	test.skip(!haveBackend, 'needs a real tutti-server (set TUTTI_SERVER_BIN)');

	test('WebRTC carries game messages and datagrams', async ({ page }, testInfo) => {
		const room = ROOM_BY_PROJECT[testInfo.project.name] ?? 'Pizzicato';
		const result = await runSelfTest(page, 'webrtc', room);
		expect(result.steps, JSON.stringify(result.steps)).toEqual(
			expect.arrayContaining([
				expect.objectContaining({ name: 'bind', ok: true }),
				expect.objectContaining({ name: 'clock', ok: true }),
				expect.objectContaining({ name: 'game message', ok: true }),
				expect.objectContaining({ name: 'large message', ok: true }),
				expect.objectContaining({ name: 'game channel', ok: true }),
				expect.objectContaining({ name: 'game datagrams', ok: true })
			])
		);
		expect(result.pass).toBe(true);
		expect(result.transport).toBe('webrtc');
		expectLocalClockAccuracy(result);
		await expect(page.getByTestId('verdict')).toHaveText('PASS');
	});

	test('WebTransport carries game messages and datagrams', async ({ page }, testInfo) => {
		test.skip(!haveWebTransport, 'server not built with WebTransport');
		test.skip(testInfo.project.name === 'webkit', 'WebKit has no WebTransport');
		const room = ROOM_BY_PROJECT[testInfo.project.name] ?? 'Pizzicato';
		const result = await runSelfTest(page, 'webtransport', room);
		expect(result.pass, JSON.stringify(result.steps)).toBe(true);
		expect(result.transport).toBe('webtransport');
		expectLocalClockAccuracy(result);
	});
});
