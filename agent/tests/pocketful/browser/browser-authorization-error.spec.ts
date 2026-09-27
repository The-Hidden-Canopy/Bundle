// tests/pocketful/browser/browser-authorization-error.spec.ts
// BA1: capture failure → authorization-error shown (authorization_not_open)
// BA2: void failure → authorization-error shown (authorization_not_open — same hook as capture)
// BA3: no capture_failed or void_failed hooks exist — authorization-error is the single hook
// BA4: void on already-voided → 200; authorization-error must NOT appear (idempotent no-op)
// BA5: capture exceeds hold → 422 capture_exceeds_authorization; authorization-error shown

import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';
const UI_BASE  = 'http://localhost:3000';

async function signInAndGotoAuthorizations(page: any, email: string, password: string) {
  await page.goto(`${UI_BASE}/login`);
  await page.fill('#login-email', email);
  await page.fill('#login-password', password);
  await page.click('#login-submit');
  await page.goto(`${UI_BASE}/authorizations`);
  await expect(page.locator('#authorization-list')).toBeVisible();
}

test.describe('Authorization error DOM hook', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 5000 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
  });

  test('BA1 — capture failure → authorization-error shown; no capture_failed hook', async ({ page }) => {
    // Create an authorization via API, then navigate to the UI
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const authResp = await page.request.post(`${API_BASE}/authorizations`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'ba1-create', 'Content-Type': 'application/json' },
      data: { to_handle: 'bob', amount: 500 },
    });
    expect(authResp.status()).toBe(201);
    const authId = (await authResp.json()).authorization_id;

    await signInAndGotoAuthorizations(page, 'a@test.local', 'Password1234!');
    await expect(page.locator(`#authorization-item-${authId}`)).toBeVisible();

    // Intercept the capture endpoint and return authorization_not_open
    await page.route(`${API_BASE}/authorizations/${authId}/capture`, async (route) => {
      await route.fulfill({
        status: 409,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'authorization_not_open', message: 'Authorization is not open' } }),
      });
    });

    await page.click(`#authorization-capture-${authId}`);

    // authorization-error must appear — this is the single hook for all capture/void failures
    await expect(page.locator('#authorization-error')).toBeVisible();

    // These hooks must NOT exist (they are not part of the DOM contract)
    await expect(page.locator('#capture_failed')).not.toBeAttached();
    await expect(page.locator('#capture-failed')).not.toBeAttached();

    await page.unroute(`${API_BASE}/authorizations/${authId}/capture`);
  });

  test('BA2 — void failure → authorization-error shown (same hook as capture failure)', async ({ page }) => {
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const authResp = await page.request.post(`${API_BASE}/authorizations`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'ba2-create', 'Content-Type': 'application/json' },
      data: { to_handle: 'bob', amount: 500 },
    });
    const authId = (await authResp.json()).authorization_id;

    await signInAndGotoAuthorizations(page, 'a@test.local', 'Password1234!');
    await expect(page.locator(`#authorization-item-${authId}`)).toBeVisible();

    // Intercept void and return authorization_not_open (covers captured, voided, expired)
    await page.route(`${API_BASE}/authorizations/${authId}/void`, async (route) => {
      await route.fulfill({
        status: 409,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'authorization_not_open', message: 'Authorization is not open' } }),
      });
    });

    await page.click(`#authorization-void-${authId}`);

    // Same authorization-error hook — no separate void_failed hook
    await expect(page.locator('#authorization-error')).toBeVisible();

    await expect(page.locator('#void_failed')).not.toBeAttached();
    await expect(page.locator('#void-failed')).not.toBeAttached();

    await page.unroute(`${API_BASE}/authorizations/${authId}/void`);
  });

  test('BA3 — no capture_failed or void_failed elements exist on the page at any time', async ({ page }) => {
    // These hooks do not exist in the DOM contract — authorization-error covers both
    await signInAndGotoAuthorizations(page, 'a@test.local', 'Password1234!');

    await expect(page.locator('#capture_failed')).not.toBeAttached();
    await expect(page.locator('#capture-failed')).not.toBeAttached();
    await expect(page.locator('#void_failed')).not.toBeAttached();
    await expect(page.locator('#void-failed')).not.toBeAttached();
  });

  test('BA4 — void on already-voided → 200; authorization-error must NOT appear', async ({ page }) => {
    // Void is idempotent: already-voided returns 200, not an error.
    // The UI must not show authorization-error on a 200 void response.
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const authResp = await page.request.post(`${API_BASE}/authorizations`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'ba4-create', 'Content-Type': 'application/json' },
      data: { to_handle: 'bob', amount: 300 },
    });
    const authId = (await authResp.json()).authorization_id;

    // First void via API — authorization is now voided
    await page.request.post(`${API_BASE}/authorizations/${authId}/void`, {
      headers: { Authorization: `Bearer ${aliceToken}` },
    });

    await signInAndGotoAuthorizations(page, 'a@test.local', 'Password1234!');

    // Intercept a second void and return 200 (idempotent success) — this is what the server returns
    await page.route(`${API_BASE}/authorizations/${authId}/void`, async (route) => {
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify({ authorization_id: authId, status: 'voided' }),
      });
    });

    // If the void button is visible (implementation may hide it post-void), click it
    const voidBtn = page.locator(`#authorization-void-${authId}`);
    if (await voidBtn.isVisible()) {
      await voidBtn.click();
      // 200 response → no error
      await expect(page.locator('#authorization-error')).not.toBeVisible();
    }

    await page.unroute(`${API_BASE}/authorizations/${authId}/void`);
  });

  test('BA5 — capture exceeds hold → 422 capture_exceeds_authorization; authorization-error shown', async ({ page }) => {
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const authResp = await page.request.post(`${API_BASE}/authorizations`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'ba5-create', 'Content-Type': 'application/json' },
      data: { to_handle: 'bob', amount: 100 },
    });
    const authId = (await authResp.json()).authorization_id;

    await signInAndGotoAuthorizations(page, 'a@test.local', 'Password1234!');
    await expect(page.locator(`#authorization-item-${authId}`)).toBeVisible();

    // Intercept capture and return 422 capture_exceeds_authorization
    await page.route(`${API_BASE}/authorizations/${authId}/capture`, async (route) => {
      await route.fulfill({
        status: 422,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'capture_exceeds_authorization', message: 'Amount exceeds remaining hold' } }),
      });
    });

    // Set capture amount greater than hold, then capture
    const captureAmountInput = page.locator(`#authorization-capture-amount-${authId}`);
    if (await captureAmountInput.isVisible()) {
      await captureAmountInput.fill('9999');
    }
    await page.click(`#authorization-capture-${authId}`);

    await expect(page.locator('#authorization-error')).toBeVisible();

    await page.unroute(`${API_BASE}/authorizations/${authId}/capture`);
  });
});
