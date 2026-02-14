<script lang="ts">
	import { rooms, fetchRooms, sendVacateRequest } from '../stores/room.js';
	import type { RoomInfo } from '../audio/types.js';
	import { onMount } from 'svelte';

	let roomList: RoomInfo[] = $state([]);
	let vacateCooldowns: Record<string, boolean> = $state({});

	rooms.subscribe((r) => (roomList = r));

	onMount(() => {
		fetchRooms();
		// Refresh every 5 seconds
		const interval = setInterval(fetchRooms, 5000);
		return () => clearInterval(interval);
	});

	function statusLabel(room: RoomInfo): string {
		if (room.participant_count >= room.max_participants) return 'Full';
		if (room.claimed) return 'Private';
		return 'Open';
	}

	function statusClass(room: RoomInfo): string {
		if (room.participant_count >= room.max_participants) return 'status-full';
		if (room.claimed) return 'status-claimed';
		return 'status-open';
	}

	async function handleVacate(roomName: string) {
		const result = await sendVacateRequest(roomName);
		if (!result.success && result.error === 'cooldown_active') {
			vacateCooldowns[roomName] = true;
		}
	}
</script>

<div class="room-list">
	<h2>Rehearsal Rooms</h2>
	<div class="rooms-grid">
		{#each roomList as room (room.name)}
			<a href="/room/{encodeURIComponent(room.name)}" class="room-card">
				<div class="room-name">{room.name}</div>
				<div class="room-meta">
					<span class="participant-count">
						{room.participant_count}/{room.max_participants}
					</span>
					<span class="status {statusClass(room)}">{statusLabel(room)}</span>
				</div>
				{#if room.participant_count >= room.max_participants}
					<button
						class="vacate-btn"
						disabled={vacateCooldowns[room.name]}
						onclick={(e) => {
							e.preventDefault();
							handleVacate(room.name);
						}}
					>
						{vacateCooldowns[room.name] ? 'Request sent' : 'Request to practise'}
					</button>
				{/if}
			</a>
		{/each}
	</div>
</div>

<style>
	.room-list {
		max-width: 960px;
		margin: 0 auto;
		padding: 1rem;
	}

	h2 {
		font-size: 1.5rem;
		margin-bottom: 1rem;
	}

	.rooms-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
		gap: 1rem;
	}

	.room-card {
		display: flex;
		flex-direction: column;
		padding: 1rem;
		border: 1px solid #333;
		border-radius: 8px;
		text-decoration: none;
		color: inherit;
		transition: border-color 0.15s;
	}

	.room-card:hover {
		border-color: #666;
	}

	.room-name {
		font-size: 1.1rem;
		font-weight: 600;
		margin-bottom: 0.5rem;
	}

	.room-meta {
		display: flex;
		justify-content: space-between;
		align-items: center;
		font-size: 0.85rem;
	}

	.participant-count {
		color: #999;
	}

	.status {
		padding: 2px 8px;
		border-radius: 4px;
		font-size: 0.75rem;
		text-transform: uppercase;
	}

	.status-open {
		background: #1a3a1a;
		color: #4ade80;
	}

	.status-claimed {
		background: #3a3a1a;
		color: #facc15;
	}

	.status-full {
		background: #3a1a1a;
		color: #f87171;
	}

	.vacate-btn {
		margin-top: 0.5rem;
		padding: 4px 8px;
		font-size: 0.75rem;
		background: transparent;
		border: 1px solid #555;
		border-radius: 4px;
		color: #ccc;
		cursor: pointer;
	}

	.vacate-btn:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}
</style>
