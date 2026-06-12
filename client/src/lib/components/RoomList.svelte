<script lang="ts">
	import { rooms, fetchRooms, sendVacateRequest } from '../stores/room.js';
	import type { RoomInfo } from '../audio/types.js';
	import { onMount } from 'svelte';

	let vacateCooldowns: Record<string, boolean> = $state({});

	// $rooms auto-subscription — cleaned up automatically (no leak)
	let roomList = $derived($rooms as RoomInfo[]);

	onMount(() => {
		fetchRooms();
		// Refresh every 5 seconds
		const interval = setInterval(fetchRooms, 5000);
		return () => clearInterval(interval);
	});

	function statusLabel(room: RoomInfo): string {
		if (room.participant_count >= room.max_participants) return 'Full';
		if (room.claimed) return 'In session';
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
		} else if (result.success) {
			vacateCooldowns[roomName] = true;
		}
	}
</script>

<section class="room-list">
	<div class="list-header">
		<h2>Rehearsal rooms</h2>
		<span class="live-hint"><span class="live-dot"></span>live</span>
	</div>

	<div class="rooms-grid">
		{#each roomList as room, i (room.name)}
			<a
				href="/room/{encodeURIComponent(room.name)}"
				class="room-card"
				style="animation-delay: {i * 45}ms"
			>
				<div class="card-top">
					<span class="room-name">{room.name}</span>
					<span class="status {statusClass(room)}">
						<span class="status-dot"></span>
						{statusLabel(room)}
					</span>
				</div>

				<div class="occupancy" aria-label="{room.participant_count} of {room.max_participants} musicians">
					{#each Array(room.max_participants) as _, slot}
						<span class="seat" class:filled={slot < room.participant_count}></span>
					{/each}
					<span class="occupancy-count">
						{room.participant_count}<span class="occupancy-max">/{room.max_participants}</span>
					</span>
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
						{vacateCooldowns[room.name] ? 'Request sent ✓' : 'Ask to practise'}
					</button>
				{/if}
			</a>
		{:else}
			<div class="empty-state">
				<p>Tuning up&hellip; no rooms reachable right now.</p>
			</div>
		{/each}
	</div>
</section>

<style>
	.room-list {
		max-width: 960px;
		width: 100%;
		margin: 0 auto;
		padding: 0 1.25rem;
	}

	.list-header {
		display: flex;
		align-items: baseline;
		justify-content: space-between;
		margin-bottom: 1rem;
	}

	h2 {
		font-family: var(--font-display);
		font-weight: 600;
		font-size: 1.35rem;
		letter-spacing: 0.01em;
		margin: 0;
	}

	.live-hint {
		display: inline-flex;
		align-items: center;
		gap: 0.4rem;
		font-family: var(--font-mono);
		font-size: 0.65rem;
		text-transform: uppercase;
		letter-spacing: 0.12em;
		color: var(--text-3);
	}

	.live-dot {
		width: 6px;
		height: 6px;
		border-radius: 50%;
		background: var(--good);
		animation: pulse-dot 2s ease-in-out infinite;
	}

	.rooms-grid {
		display: grid;
		grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
		gap: 0.9rem;
	}

	.room-card {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		padding: 1.1rem 1.15rem;
		border: 1px solid var(--line-1);
		border-radius: var(--radius-l);
		background: linear-gradient(180deg, var(--bg-1), var(--bg-0));
		box-shadow: var(--shadow-card);
		text-decoration: none;
		color: inherit;
		transition:
			border-color 0.2s,
			transform 0.2s var(--ease-snap),
			box-shadow 0.25s;
		animation: rise-in 0.5s var(--ease-snap) both;
	}

	.room-card:hover {
		border-color: var(--line-2);
		transform: translateY(-3px);
		box-shadow:
			var(--shadow-card),
			0 14px 40px -18px var(--accent-glow);
	}

	.card-top {
		display: flex;
		justify-content: space-between;
		align-items: center;
		gap: 0.5rem;
	}

	.room-name {
		font-family: var(--font-display);
		font-weight: 600;
		font-size: 1.15rem;
		letter-spacing: 0.01em;
	}

	.status {
		display: inline-flex;
		align-items: center;
		gap: 0.35rem;
		padding: 3px 9px;
		border-radius: 999px;
		font-family: var(--font-mono);
		font-size: 0.62rem;
		text-transform: uppercase;
		letter-spacing: 0.08em;
		white-space: nowrap;
	}

	.status-dot {
		width: 5px;
		height: 5px;
		border-radius: 50%;
		background: currentColor;
	}

	.status-open {
		background: var(--good-dim);
		color: var(--good);
	}

	.status-open .status-dot {
		animation: pulse-dot 2s ease-in-out infinite;
	}

	.status-claimed {
		background: var(--warn-dim);
		color: var(--warn);
	}

	.status-full {
		background: var(--bad-dim);
		color: var(--bad);
	}

	.occupancy {
		display: flex;
		align-items: center;
		gap: 5px;
	}

	.seat {
		width: 14px;
		height: 5px;
		border-radius: 3px;
		background: var(--line-1);
		transition: background 0.3s;
	}

	.seat.filled {
		background: linear-gradient(90deg, var(--accent-deep), var(--accent-bright));
		box-shadow: 0 0 8px var(--accent-glow);
	}

	.occupancy-count {
		margin-left: auto;
		font-family: var(--font-mono);
		font-size: 0.78rem;
		color: var(--text-2);
		font-variant-numeric: tabular-nums;
	}

	.occupancy-max {
		color: var(--text-3);
	}

	.vacate-btn {
		padding: 6px 10px;
		font-size: 0.75rem;
		background: transparent;
		border: 1px solid var(--line-2);
		border-radius: var(--radius-s);
		color: var(--text-2);
		cursor: pointer;
		transition: border-color 0.15s, color 0.15s;
	}

	.vacate-btn:hover:not(:disabled) {
		border-color: var(--accent);
		color: var(--accent);
	}

	.vacate-btn:disabled {
		opacity: 0.55;
		cursor: default;
	}

	.empty-state {
		grid-column: 1 / -1;
		text-align: center;
		padding: 2.5rem 1rem;
		border: 1px dashed var(--line-1);
		border-radius: var(--radius-l);
		color: var(--text-3);
		font-size: 0.9rem;
	}

	.empty-state p {
		margin: 0;
	}
</style>
