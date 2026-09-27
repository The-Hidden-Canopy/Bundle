// tests/pocketful/browser/browser-splits-ui.spec.ts
// BS1: split-preview shows all computed shares before submit (including caller's own share)
// BS2: split-preview share amounts use floor-division + remainder-to-first-N algorithm
// BS3: participant_handles body field (not `handles`) drives the split POST
// BS4: split-error shown on 422 validation_failed
// BS5: split-error shown on 404 not_found (unknown handle)
// BS6: caller-only split produces empty requests list; split succeeds

import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';
const UI_BASE  = 'http://localhost:3000';

async function signInAndGotoSplit(page: any, email: string, password: string) {
  await page.goto(`${UI_BASE}/login`);
  await page.fill('#login-email', email);
  await page.fill('#login-password', password);
  await page.click('#login-submit');
  await page.goto(`${UI_BASE}/split`);
  await expect(page.locator('#split-amount')).toBeVisible();
}

test.describe('Splits UI', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 1000 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signInAndGotoSplit(page, 'a@test.local', 'Password1234!');
  });

  test('BS1 — split-preview visible after entering amount and handles', async ({ page }) => {
    await page.fill('#split-amount', '10');
    await page.fill('#split-handles', 'bob,carol');

    // Preview must appear (either on input or on explicit trigger)
    // Allow either immediate update or a preview-update button/trigger
    const preview = page.locator('#split-preview');
    await expect(preview).toBeVisible({ timeout: 3000 });
  });

  test('BS2 — split-preview amounts use floor-division + remainder-to-first-N (server algorithm)', async ({ page }) => {
    // 10 split among alice, bob, carol (3 participants): floor(10/3)=3 each, +1 to first 1 → [4,3,3]
    await page.fill('#split-amount', '10');
    await page.fill('#split-handles', 'alice,bob,carol');

    await expect(page.locator('#split-preview')).toBeVisible({ timeout: 3000 });

    // share for first participant (alice) must be 4 (gets the remainder)
    // share for bob must be 3, carol must be 3
    const aliceShare = page.locator('#split-share-alice');
    const bobShare   = page.locator('#split-share-bob');
    const carolShare = page.locator('#split-share-carol');

    await expect(aliceShare).toBeVisible();
    await expect(bobShare).toBeVisible();
    await expect(carolShare).toBeVisible();

    const aliceText = await aliceShare.textContent();
    const bobText   = await bobShare.textContent();
    const carolText = await carolShare.textContent();

    // Amounts must reflect floor(10/3)=3 base; first N get +1
    // Exact format is implementation-specific; assert numeric content
    expect(aliceText).toMatch(/4/);
    expect(bobText).toMatch(/3/);
    expect(carolText).toMatch(/3/);
  });

  test('BS3 — POST /splits uses participant_handles body field (not `handles`)', async ({ page }) => {
    let capturedBody: any = null;

    await page.route(`${API_BASE}/splits`, async (route) => {
      const body = JSON.parse(route.request().postData() || '{}');
      capturedBody = body;
      await route.continue();
    });

    await page.fill('#split-amount', '6');
    await page.fill('#split-handles', 'alice,bob,carol');
    await page.click('#split-submit');

    // Wait for route intercept to fire
    await page.waitForTimeout(1000);
    await page.unroute(`${API_BASE}/splits`);

    expect(capturedBody).not.toBeNull();
    // Must use participant_handles, not handles
    expect(Array.isArray(capturedBody.participant_handles)).toBe(true);
    expect(capturedBody.handles).toBeUndefined();
  });

  test('BS4 — 422 validation_failed → split-error shown', async ({ page }) => {
    await page.route(`${API_BASE}/splits`, async (route) => {
      await route.fulfill({
        status: 422,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'validation_failed', message: 'Note too long' } }),
      });
    });

    await page.fill('#split-amount', '9');
    await page.fill('#split-handles', 'alice,bob,carol');
    await page.click('#split-submit');

    await expect(page.locator('#split-error')).toBeVisible();

    await page.unroute(`${API_BASE}/splits`);
  });

  test('BS5 — 404 not_found (unknown handle) → split-error shown', async ({ page }) => {
    await page.route(`${API_BASE}/splits`, async (route) => {
      await route.fulfill({
        status: 404,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'not_found', message: 'Handle not found' } }),
      });
    });

    await page.fill('#split-amount', '9');
    await page.fill('#split-handles', 'alice,ghost_user');
    await page.click('#split-submit');

    await expect(page.locator('#split-error')).toBeVisible();

    await page.unroute(`${API_BASE}/splits`);
  });

  test('BS6 — caller-only split succeeds; no requests generated (requests: [])', async ({ page }) => {
    // Caller (alice) is the only participant — valid split, no payment requests generated
    await page.route(`${API_BASE}/splits`, async (route, request) => {
      // Let it through; mock the success response for a caller-only split
      await route.fulfill({
        status: 201,
        contentType: 'application/json',
        body: JSON.stringify({
          split_id: 'sp_test',
          amount: 5,
          currency: 'USD',
          note: '',
          shares: [{ handle: 'alice', amount: 5 }],
          requests: [],
          created_at: new Date().toISOString(),
        }),
      });
    });

    await page.fill('#split-amount', '5');
    await page.fill('#split-handles', 'alice');
    await page.click('#split-submit');

    // Must not show split-error — empty requests is valid
    await expect(page.locator('#split-error')).not.toBeVisible();

    await page.unroute(`${API_BASE}/splits`);
  });
});
