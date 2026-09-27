// tests/pocketful/browser/browser-pay-state-machine.spec.ts
// BP1: successful payment → pay-success; form clears; not pay-uncertain
// BP2: 409 insufficient_funds → pay-error (NOT pay-uncertain — definitive failure per spec §7)
// BP3: 409 idempotency_key_reuse → pay-error (not pay-uncertain)
// BP4: 422 validation_failed → pay-error
// BP5: in-flight request → pay-pending shown; form submit blocked
// BP6: pay-form has expected named inputs and submit control

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

test.describe('Pay form state machine', () => {
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

  test('BP1 — successful payment → pay-success; not pay-uncertain', async ({ page }) => {
    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');
    await page.selectOption('#pay-visibility', 'private');
    await page.click('#pay-form button[type="submit"]');

    await expect(page.locator('#pay-success')).toBeVisible();
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();
    await expect(page.locator('#pay-error')).not.toBeVisible();
  });

  test('BP2 — 409 insufficient_funds → pay-error, NOT pay-uncertain (spec §7)', async ({ page }) => {
    // 409 insufficient_funds is a definitive failure — no payment occurred.
    // The client must enter pay-error, not pay-uncertain. Entering pay-uncertain
    // here would be incorrect: it would retain the key and block the form even
    // though no payment was attempted. (spec §7: only lost-response triggers pay-uncertain)
    await page.route(`${API_BASE}/payments`, async (route) => {
      await route.fulfill({
        status: 409,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'insufficient_funds', message: 'Insufficient funds' } }),
      });
    });

    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '99999');
    await page.click('#pay-form button[type="submit"]');

    await expect(page.locator('#pay-error')).toBeVisible();
    // Critical: must NOT enter pay-uncertain on a definitive 4xx response
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();
    await expect(page.locator('#pay-success')).not.toBeVisible();

    await page.unroute(`${API_BASE}/payments`);
  });

  test('BP3 — 409 idempotency_key_reuse → pay-error, NOT pay-uncertain', async ({ page }) => {
    await page.route(`${API_BASE}/payments`, async (route) => {
      await route.fulfill({
        status: 409,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'idempotency_key_reuse', message: 'Key reuse conflict' } }),
      });
    });

    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');
    await page.click('#pay-form button[type="submit"]');

    await expect(page.locator('#pay-error')).toBeVisible();
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();

    await page.unroute(`${API_BASE}/payments`);
  });

  test('BP4 — 422 validation_failed → pay-error, NOT pay-uncertain', async ({ page }) => {
    await page.route(`${API_BASE}/payments`, async (route) => {
      await route.fulfill({
        status: 422,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'validation_failed', message: 'Invalid input' } }),
      });
    });

    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');
    await page.click('#pay-form button[type="submit"]');

    await expect(page.locator('#pay-error')).toBeVisible();
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();

    await page.unroute(`${API_BASE}/payments`);
  });

  test('BP5 — in-flight payment request → pay-pending shown; submit blocked', async ({ page }) => {
    let continueRequest!: () => void;
    const held = new Promise<void>(resolve => { continueRequest = resolve; });

    await page.route(`${API_BASE}/payments`, async (route) => {
      await held;
      await route.continue();
    });

    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');
    await page.click('#pay-form button[type="submit"]');

    // While in-flight: pay-pending must be visible; submit must be disabled
    await expect(page.locator('#pay-pending')).toBeVisible();
    await expect(page.locator('#pay-form button[type="submit"]')).toBeDisabled();
    await expect(page.locator('#pay-success')).not.toBeVisible();
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();
    await expect(page.locator('#pay-error')).not.toBeVisible();

    continueRequest();
    await page.unroute(`${API_BASE}/payments`);
  });

  test('BP6 — pay form has required named inputs and visibility select', async ({ page }) => {
    await expect(page.locator('#pay-handle')).toBeVisible();
    await expect(page.locator('#pay-amount')).toBeVisible();
    await expect(page.locator('#pay-note')).toBeVisible();
    await expect(page.locator('#pay-visibility')).toBeVisible();

    // visibility select must offer public and private options
    const options = await page.locator('#pay-visibility option').allTextContents();
    expect(options.map((o: string) => o.toLowerCase())).toEqual(
      expect.arrayContaining(['public', 'private'])
    );
  });
});
