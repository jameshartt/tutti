<script lang="ts">
	import { settings } from '../stores/settings.js';

	let {
		roomName,
		requiresPassword,
		onJoin,
		onCancel
	}: {
		roomName: string;
		requiresPassword: boolean;
		onJoin: (alias: string, password: string) => void;
		onCancel: () => void;
	} = $props();

	let alias = $state('');
	let password = $state('');
	let error = $state('');

	// Pre-fill alias from settings
	settings.subscribe((s) => {
		if (s.alias && !alias) alias = s.alias;
	});

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
		// Save alias to settings
		settings.update((s) => ({ ...s, alias: trimmed }));
		onJoin(trimmed, password);
	}
</script>

<div class="dialog-backdrop" role="presentation" onclick={onCancel}>
	<!-- svelte-ignore a11y_click_events_have_key_events -->
	<!-- svelte-ignore a11y_no_static_element_interactions -->
	<div class="dialog" onclick={(e) => e.stopPropagation()}>
		<h3>Join {roomName}</h3>
		<form onsubmit={(e) => { e.preventDefault(); handleSubmit(); }}>
			<label>
				<span>Your name</span>
				<input
					type="text"
					bind:value={alias}
					placeholder="e.g. Alice"
					maxlength={32}
					autofocus
				/>
			</label>

			{#if requiresPassword}
				<label>
					<span>Room password</span>
					<input type="password" bind:value={password} placeholder="Password" />
				</label>
			{/if}

			{#if error}
				<p class="error">{error}</p>
			{/if}

			<div class="actions">
				<button type="button" class="btn-cancel" onclick={onCancel}>Cancel</button>
				<button type="submit" class="btn-join">Join</button>
			</div>
		</form>
	</div>
</div>

<style>
	.dialog-backdrop {
		position: fixed;
		inset: 0;
		background: rgba(0, 0, 0, 0.6);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 100;
	}

	.dialog {
		background: #1a1a1a;
		border: 1px solid #333;
		border-radius: 12px;
		padding: 1.5rem;
		width: 100%;
		max-width: 360px;
	}

	h3 {
		margin: 0 0 1rem;
		font-size: 1.2rem;
	}

	label {
		display: flex;
		flex-direction: column;
		gap: 0.25rem;
		margin-bottom: 0.75rem;
	}

	label span {
		font-size: 0.85rem;
		color: #999;
	}

	input {
		padding: 0.5rem;
		border: 1px solid #444;
		border-radius: 6px;
		background: #111;
		color: #eee;
		font-size: 1rem;
	}

	input:focus {
		outline: none;
		border-color: #4ade80;
	}

	.error {
		color: #f87171;
		font-size: 0.85rem;
		margin: 0 0 0.5rem;
	}

	.actions {
		display: flex;
		gap: 0.5rem;
		justify-content: flex-end;
		margin-top: 1rem;
	}

	.btn-cancel {
		padding: 0.5rem 1rem;
		border: 1px solid #444;
		border-radius: 6px;
		background: transparent;
		color: #999;
		cursor: pointer;
	}

	.btn-join {
		padding: 0.5rem 1.5rem;
		border: none;
		border-radius: 6px;
		background: #4ade80;
		color: #000;
		font-weight: 600;
		cursor: pointer;
	}
</style>
