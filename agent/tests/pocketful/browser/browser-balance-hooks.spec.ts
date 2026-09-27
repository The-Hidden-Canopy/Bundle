// tests/pocketful/browser/browser-balance-hooks.spec.ts
// BH1: wallet-balance data-amount is an integer minor-units string (BigInt-safe, no float)
// BH2: wallet-held absent when no active holds; wallet-available always present
// BH3: wallet-refresh updates balance without clearing pay form inputs
// BH4: stale wallet-refresh response does not overwrite a more recent result (ordering guard)

import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';
const UI_BASE  = 'http://localhost:3000';

async function signInAs(page: any, email: string, password: string) {
  await page.goto(`${UI_BASE}/login`);
  await page.fill('#login-email', email);
  await page.fill('#login-password', password);
  await page.click('#login-submit');
  await expect(page.locator('#wallet-balance')).toBeVisible();
}

test.describe('Balance DOM hooks', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 1000 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signInAs(page, 'a@test.local', 'Password1234!');
  });

  test('BH1 — wallet-balance data-amount is integer minor-units (BigInt-safe)', async ({ page }) => {
    const el = page.locator('#wallet-balance');
    await expect(el).toBeVisible();
    const raw = await el.getAttribute('data-amount');
    expect(raw).toBeTruthy();
    // Must parse as BigInt without throwing — no decimal point, no float noise, no currency prefix
    expect(() => BigInt(raw!)).not.toThrow();
    expect(BigInt(raw!)).toBe(1000n);
  });

  test('BH2 — wallet-held absent when no holds; wallet-available equals full balance', async ({ page }) => {
    // No authorization holds active → wallet-held must not be rendered or visible
    await expect(page.locator('#wallet-held')).not.toBeVisible();

    // wallet-available is always present when signed in
    await expect(page.locator('#wallet-available')).toBeVisible();
    const available = await page.locator('#wallet-available').getAttribute('data-amount');
    expect(BigInt(available!)).toBe(1000n);
  });

  test('BH3 — wallet-refresh updates balance without clearing pay form inputs', async ({ page }) => {
    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');
    await page.fill('#pay-note', 'lunch');

    await page.click('#wallet-refresh');
    await expect(page.locator('#wallet-balance')).toBeVisible();

    // Pay form inputs must survive the refresh — wallet-refresh is not a page reload
    await expect(page.locator('#pay-handle')).toHaveValue('bob');
    await expect(page.locator('#pay-amount')).toHaveValue('5');
    await expect(page.locator('#pay-note')).toHaveValue('lunch');
  });

  test('BH4 — stale wallet-refresh response does not overwrite a more recent result', async ({ page }) => {
    // Inject two /me responses with known values so we can assert ordering.
    // First request is held; second is served immediately. Once second lands,
    // first is released. The UI must display the second (newer) value — not 500.
    let resolveHeld!: () => void;
    const held = new Promise<void>(resolve => { resolveHeld = resolve; });
    let requestIndex = 0;

    await page.route(`${API_BASE}/me`, async (route) => {
      const idx = ++requestIndex;
      if (idx === 1) {
        await held;
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            user_id: 'alice', display_name: 'Alice', handle: 'alice',
            balance: 500, total: 500, available: 500, held: 0,
            currency: 'USD', minor_units: 2,
          }),
        });
      } else {
        await route.fulfill({
          status: 200,
          contentType: 'application/json',
          body: JSON.stringify({
            user_id: 'alice', display_name: 'Alice', handle: 'alice',
            balance: 999, total: 999, available: 999, held: 0,
            currency: 'USD', minor_units: 2,
          }),
        });
        resolveHeld();
      }
    });

    // Fire two refreshes; the second is newer even though it returns first
    await page.click('#wallet-refresh');
    await page.click('#wallet-refresh');
    await page.waitForTimeout(500);
    await page.unroute(`${API_BASE}/me`);

    // Must display the second (newer) response value — stale first must not overwrite
    const amount = await page.locator('#wallet-balance').getAttribute('data-amount');
    expect(BigInt(amount!)).toBe(999n);
  });
});
