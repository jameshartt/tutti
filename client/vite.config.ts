import { sveltekit } from '@sveltejs/kit/vite';
import basicSsl from '@vitejs/plugin-basic-ssl';
import { defineConfig } from 'vite';

// Backend ports are overridable so tests can run a server next to
// whatever else is using the defaults on a developer machine.
const apiPort = process.env.TUTTI_API_PORT ?? '8080';
const wsPort = process.env.TUTTI_WS_PORT ?? '8081';
const devPort = Number(process.env.TUTTI_DEV_PORT ?? 3000);

export default defineConfig({
	plugins: [sveltekit(), basicSsl()],

	server: {
		port: devPort,
		host: true,
		proxy: {
			'/api': {
				target: `http://localhost:${apiPort}`,
				changeOrigin: true
			},
			'/ws': {
				target: `ws://localhost:${wsPort}`,
				ws: true
			}
		}
	}
});
