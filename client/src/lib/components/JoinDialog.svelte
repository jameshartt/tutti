<script lang="ts">
	import { get } from 'svelte/store';
	import { settings } from '../stores/settings.js';

	let {
		roomName,
		requiresPassword,
		externalError = '',
		onJoin,
		onCancel
	}: {
		roomName: string;
		requiresPassword: boolean;
		externalError?: string;
		onJoin: (alias: string, password: string) => void;
		onCancel: () => void;
	} = $props();

	// One-shot read — no leaked subscription
	let alias = $state(get(settings).alias ?? '');
	let password = $state('');
	let error = $state('');

	let shownError = $derived(error || externalError);

	function handleSubmit() {
		const trimmed = alias.trim();
		if (!trimmed) {
			error = 'Please enter your name';
			return;
		}
		if (trimmed.length > 32) {
			error = 'Name must be 32 characters or fewer';
			return;
		}
		error = '';
		// Save alias to settings
		settings.update((s) => ({ ...s, alias: trimmed }));
		onJoin(trimmed, password);
	}

	function handleKeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') onCancel();
	}
</script>

<svelte:window onkeydown={handleKeydown} />

<div class="dialog-backdrop" role="presentation" onclick={onCancel}>
	<!-- svelte-ignore a11y_click_events_have_key_events -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="dialog" role="dialog" aria-modal="true" aria-label="Join {roomName}" tabindex="-1" onclick={(e) => e.stopPropagation()}>
		<p class="eyebrow">Stage door</p>
		<h3>Join <em>{roomName}</em></h3>

		<form onsubmit={(e) => { e.preventDefault(); handleSubmit(); }}>
			<label>
				<span>Your name</span>
				<input
					type="text"
					bind:value={alias}
					placeholder="e.g. Alice"
					maxlength={32}
					autocomplete="off"
					autofocus
				/>
			</label>

			{#if requiresPassword}
				<label>
					<span>Room password</span>
					<input type="password" bind:value={password} placeholder="Password" />
				</label>
			{/if}

			{#if shownError}
				<p class="error" role="alert">{shownError}</p>
			{/if}

			<div class="actions">
				<button type="button" class="btn btn-ghost" onclick={onCancel}>Cancel</button>
				<button type="submit" class="btn btn-primary">Step in</button>
			</div>
		</form>
	</div>
</div>

<style>
	.dialog-backdrop {
		position: fixed;
		inset: 0;
		background: rgba(8, 6, 4, 0.7);
		backdrop-filter: blur(8px);
		-webkit-backdrop-filter: blur(8px);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 100;
		padding: 1rem;
	}

	.dialog {
		background: linear-gradient(180deg, var(--bg-2), var(--bg-1));
		border: 1px solid var(--line-2);
		border-radius: var(--radius-l);
		box-shadow:
			0 0 0 1px rgba(255, 255, 255, 0.02) inset,
			0 30px 80px -20px rgba(0, 0, 0, 0.9),
			0 0 60px -30px var(--accent-glow);
		padding: 1.75rem;
		width: 100%;
		max-width: 380px;
		animation: scale-in 0.25s var(--ease-snap) both;
	}

	.eyebrow {
		margin: 0 0 0.25rem;
		font-family: var(--font-mono);
		font-size: 0.62rem;
		text-transform: uppercase;
		letter-spacing: 0.18em;
		color: var(--accent);
	}

	h3 {
		margin: 0 0 1.25rem;
		font-family: var(--font-display);
		font-weight: 600;
		font-size: 1.5rem;
		letter-spacing: 0.01em;
	}

	h3 em {
		font-style: italic;
		color: var(--accent-bright);
	}

	label {
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
		margin-bottom: 0.85rem;
	}

	label span {
		font-size: 0.8rem;
		color: var(--text-2);
		letter-spacing: 0.02em;
	}

	input {
		padding: 0.6rem 0.7rem;
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		background: var(--bg-0);
		color: var(--text-1);
		font-size: 1rem;
		transition: border-color 0.15s, box-shadow 0.15s;
	}

	input:focus {
		outline: none;
		border-color: var(--accent);
		box-shadow: 0 0 0 3px var(--accent-dim);
	}

	.error {
		color: var(--bad);
		font-size: 0.83rem;
		margin: 0 0 0.5rem;
		padding: 0.45rem 0.6rem;
		background: var(--bad-dim);
		border-radius: var(--radius-s);
	}

	.actions {
		display: flex;
		gap: 0.6rem;
		justify-content: flex-end;
		margin-top: 1.25rem;
	}
</style>
