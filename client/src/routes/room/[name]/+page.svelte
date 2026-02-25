<script lang="ts">
	import { onMount } from 'svelte';
	import Room from '$lib/components/Room.svelte';
	import JoinDialog from '$lib/components/JoinDialog.svelte';
	import { roomState, joinRoom } from '$lib/stores/room.js';
	import { goto } from '$app/navigation';

	let { data } = $props();

	let showJoinDialog = $state(true);
	let joinError = $state('');
	let joined = $state(false);
	let requiresPassword = $state(false);

	roomState.subscribe((s) => {
		joined = s.currentRoom !== null;
	});

	// Navigate to lobby when room state resets (after handleLeave)
	$effect(() => {
		if (!joined && !showJoinDialog) {
			goto('/');
		}
	});

	async function handleJoin(alias: string, password: string) {
		const result = await joinRoom(data.roomName, alias, password);
		if (result.success) {
			showJoinDialog = false;
		} else {
			if (result.error === 'password_required') {
				requiresPassword = true;
				joinError = 'This room requires a password';
			} else if (result.error === 'password_incorrect') {
				joinError = 'Incorrect password';
			} else if (result.error === 'room_full') {
				joinError = 'Room is full';
			} else {
				joinError = 'Could not join room';
			}
		}
	}

	function handleCancel() {
		goto('/');
	}
</script>

<svelte:head>
	<title>{data.roomName} - Tutti</title>
</svelte:head>

<main>
	{#if showJoinDialog}
		<JoinDialog
			roomName={data.roomName}
			{requiresPassword}
			onJoin={handleJoin}
			onCancel={handleCancel}
		/>
	{/if}

	{#if joined}
		<Room roomName={data.roomName} />
	{/if}
</main>

<style>
	main {
		min-height: 100vh;
		background: #0a0a0a;
		color: #eee;
	}
</style>
