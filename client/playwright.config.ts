import { defineConfig, devices } from '@playwright/test';

// Ports are overridable so the suite can run beside a developer's own
// dev server and backend. TUTTI_SERVER_BIN, when set, starts a real
// tutti-server for the tests that need one (see e2e/game-transport.spec.ts).
const devPort = process.env.TUTTI_DEV_PORT ?? '3000';
const apiPort = process.env.TUTTI_API_PORT ?? '8080';
const wsPort = process.env.TUTTI_WS_PORT ?? '8081';
const serverBin = process.env.TUTTI_SERVER_BIN;
const baseURL = `https://localhost:${devPort}`;

export default defineConfig({
	testDir: 'e2e',
	fullyParallel: true,
	forbidOnly: !!process.env.CI,
	retries: process.env.CI ? 1 : 0,
	reporter: process.env.CI ? [['github'], ['html', { open: 'never' }]] : 'list',
	use: {
		baseURL,
		ignoreHTTPSErrors: true,
		trace: 'on-first-retry'
	},
	projects: [
		{ name: 'chromium', use: { ...devices['Desktop Chrome'] } },
		{ name: 'firefox', use: { ...devices['Desktop Firefox'] } },
		{ name: 'webkit', use: { ...devices['Desktop Safari'] } }
	],
	webServer: [
		{
			command: 'npm run dev -- --strictPort',
			url: baseURL,
			ignoreHTTPSErrors: true,
			reuseExistingServer: !process.env.CI,
			timeout: 120_000
		},
		...(serverBin
			? [
					{
						command: `${serverBin} --http-port ${apiPort} --ws-port ${wsPort} --wt-port ${process.env.TUTTI_WT_PORT ?? '14433'}`,
						url: `http://localhost:${apiPort}/api/health`,
						reuseExistingServer: !process.env.CI,
						timeout: 30_000
					}
				]
			: [])
	]
});
