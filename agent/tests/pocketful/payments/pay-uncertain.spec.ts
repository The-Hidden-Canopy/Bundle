// tests/pocketful/payments/pay-uncertain.spec.ts
import { test, expect } from '../test.base';

test.describe('pay-uncertain strict recovery', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await page.goto('http://localhost:3000');
    await page.fill('#login-email', 'a@test.local');
    await page.fill('#login-password', 'Password1234!');
    await page.click('#login-submit');
    await expect(page.locator('#wallet-balance')).toBeVisible();
  });

  test('network drop → pay-uncertain; stays uncertain until both sources agree', async ({ page }) => {
    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');

    // INTERCEPT BEFORE CLICK — must be registered before the request fires
    await page.route('**/payments', route => route.abort('failed'));

    await page.click('#pay-form button[type="submit"]');

    // pay-uncertain must appear; no error or success
    await expect(page.locator('#pay-uncertain')).toBeVisible();
    await expect(page.locator('#pay-error')).not.toBeVisible();
    await expect(page.locator('#pay-success')).not.toBeVisible();

    // pay-form must be disabled — no new payment attempt allowed
    await expect(page.locator('#pay-form button[type="submit"]')).toBeDisabled();

    // idempotency key must still be in localStorage
    const keys = await page.evaluate(() =>
      Object.keys(localStorage).filter(k => k.startsWith('pay_intent_'))
    );
    expect(keys.length).toBeGreaterThan(0);

    // Restore network
    await page.unroute('**/payments');

    // Send the actual payment via API (simulating background completion)
    const aliceToken = await page.evaluate(() => localStorage.getItem('auth_token'));
    const intentKey = await page.evaluate(() => {
      const k = Object.keys(localStorage).find(k => k.startsWith('pay_intent_'));
      return k ? localStorage.getItem(k) : null;
    });

    await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': intentKey!,
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 5, visibility: 'private' },
    });

    // Step: wallet-refresh — balance debited but activity may lag
    await page.click('#wallet-refresh');
    // Must still be uncertain if activity hasn't confirmed yet
    await expect(page.locator('#pay-uncertain')).toBeVisible();
    await expect(page.locator('#pay-success')).not.toBeVisible();

    // Step: wallet-refresh again — activity now shows the payment
    // Both sources agree → transition to pay-success
    await page.click('#wallet-refresh');
    await expect(page.locator('#pay-success')).toBeVisible();
    await expect(page.locator('#pay-uncertain')).not.toBeVisible();

    // localStorage key must be cleared on confirmed exit
    const keysAfter = await page.evaluate(() =>
      Object.keys(localStorage).filter(k => k.startsWith('pay_intent_'))
    );
    expect(keysAfter.length).toBe(0);
  });

  test('401 mid-session redirects to /login, not pay-error', async ({ page }) => {
    await page.fill('#pay-handle', 'bob');
    await page.fill('#pay-amount', '5');

    // Intercept BEFORE click and return 401
    await page.route('**/payments', route =>
      route.fulfill({
        status: 401,
        body: JSON.stringify({ error: { code: 'unauthenticated', message: 'Token expired' } }),
      })
    );

    await page.click('#pay-form button[type="submit"]');

    await expect(page.locator('#pay-error')).not.toBeVisible();
    await expect(page).toHaveURL(/\/login/);
  });
});
