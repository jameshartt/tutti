import { expect, test } from '@playwright/test';

test('lobby lists rooms from the API', async ({ page }) => {
	await page.route('**/api/rooms', (route) =>
		route.fulfill({
			json: {
				rooms: [{ name: 'Allegro', participant_count: 0, max_participants: 4, claimed: false }]
			}
		})
	);

	await page.goto('/');

	await expect(page.getByRole('heading', { name: 'Rehearsal rooms' })).toBeVisible();
	const card = page.getByRole('link', { name: /Allegro/ });
	await expect(card).toBeVisible();
	await expect(card).toContainText('Open');
	await expect(card).toHaveAttribute('href', '/room/Allegro');
});
