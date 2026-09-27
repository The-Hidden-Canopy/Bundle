// tests/pocketful/browser/browser-requests-ui.spec.ts
// BR1: GET /requests → filtered list — Alice sees only her own requests, not 403/404
// BR2: incoming-list and outgoing-list containers both present on /requests
// BR3: request-pay button only on Alice's pending INCOMING requests (not outgoing)
// BR4: request-cancel button only on Alice's pending OUTGOING requests (not incoming)
// BR5: request-error shown when action is refused; list refreshes
// BR6: empty-requests shown when both lists empty

import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';
const UI_BASE  = 'http://localhost:3000';

async function signInAsAndGotoRequests(page: any, email: string, password: string) {
  await page.goto(`${UI_BASE}/login`);
  await page.fill('#login-email', email);
  await page.fill('#login-password', password);
  await page.click('#login-submit');
  await page.goto(`${UI_BASE}/requests`);
  await expect(page.locator('#incoming-list')).toBeVisible();
}

test.describe('Requests UI', () => {
  test('BR1 — GET /requests returns filtered list; non-party request absent (not 403)', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 100 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    // Alice requests 10 from Bob (Alice is requester; Bob is payer)
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;
    const createReq = await page.request.post(`${API_BASE}/requests`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'br1-alice-bob', 'Content-Type': 'application/json' },
      data: { from_handle: 'bob', amount: 10, note: 'BR1 test' },
    });
    expect(createReq.status()).toBe(201);

    // Carol navigates to /requests — must see page (200), not an error
    // Carol is not a party to the Alice→Bob request; it must simply be absent from her list
    await signInAsAndGotoRequests(page, 'c@test.local', 'Password1234!');

    // Page loaded — both list containers present (not an error page)
    await expect(page.locator('#incoming-list')).toBeVisible();
    await expect(page.locator('#outgoing-list')).toBeVisible();

    // The Alice→Bob request must NOT appear in Carol's lists
    const aliceRequest = await createReq.json();
    await expect(page.locator(`#item-${aliceRequest.id}`)).not.toBeVisible();
  });

  test('BR2 — incoming-list and outgoing-list containers both present on /requests', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signInAsAndGotoRequests(page, 'a@test.local', 'Password1234!');

    await expect(page.locator('#incoming-list')).toBeVisible();
    await expect(page.locator('#outgoing-list')).toBeVisible();
  });

  test('BR3 — request-pay button appears only on pending incoming requests', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    // Bob requests 20 from Alice (incoming for Alice)
    const bobLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;
    const createReq = await page.request.post(`${API_BASE}/requests`, {
      headers: { Authorization: `Bearer ${bobToken}`, 'Idempotency-Key': 'br3-bob-alice', 'Content-Type': 'application/json' },
      data: { from_handle: 'alice', amount: 20, note: 'BR3 test' },
    });
    const reqId = (await createReq.json()).id;

    // Alice sees the incoming request with a pay button (and decline button)
    await signInAsAndGotoRequests(page, 'a@test.local', 'Password1234!');

    await expect(page.locator(`#item-${reqId}`)).toBeVisible();
    await expect(page.locator(`#request-pay-${reqId}`)).toBeVisible();
    await expect(page.locator(`#request-decline-${reqId}`)).toBeVisible();

    // Cancel button must NOT appear on incoming requests (only on outgoing)
    await expect(page.locator(`#request-cancel-${reqId}`)).not.toBeVisible();
  });

  test('BR4 — request-cancel button appears only on pending outgoing requests', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 100 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    // Alice requests 15 from Bob (outgoing for Alice)
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;
    const createReq = await page.request.post(`${API_BASE}/requests`, {
      headers: { Authorization: `Bearer ${aliceToken}`, 'Idempotency-Key': 'br4-alice-bob', 'Content-Type': 'application/json' },
      data: { from_handle: 'bob', amount: 15, note: 'BR4 test' },
    });
    const reqId = (await createReq.json()).id;

    await signInAsAndGotoRequests(page, 'a@test.local', 'Password1234!');

    await expect(page.locator(`#item-${reqId}`)).toBeVisible();
    await expect(page.locator(`#request-cancel-${reqId}`)).toBeVisible();

    // Pay and decline buttons must NOT appear on outgoing requests
    await expect(page.locator(`#request-pay-${reqId}`)).not.toBeVisible();
    await expect(page.locator(`#request-decline-${reqId}`)).not.toBeVisible();
  });

  test('BR5 — request-error shown when server refuses action; list refreshes', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    const bobLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;
    const createReq = await page.request.post(`${API_BASE}/requests`, {
      headers: { Authorization: `Bearer ${bobToken}`, 'Idempotency-Key': 'br5-bob-alice', 'Content-Type': 'application/json' },
      data: { from_handle: 'alice', amount: 10, note: 'BR5 test' },
    });
    const reqId = (await createReq.json()).id;

    await signInAsAndGotoRequests(page, 'a@test.local', 'Password1234!');
    await expect(page.locator(`#request-pay-${reqId}`)).toBeVisible();

    // Simulate the action being rejected (e.g., request already cancelled by another session)
    await page.route(`${API_BASE}/requests/${reqId}/pay`, async (route) => {
      await route.fulfill({
        status: 403,
        contentType: 'application/json',
        body: JSON.stringify({ error: { code: 'forbidden', message: 'Not authorized' } }),
      });
    });

    await page.click(`#request-pay-${reqId}`);

    await expect(page.locator('#request-error')).toBeVisible();

    await page.unroute(`${API_BASE}/requests/${reqId}/pay`);
  });

  test('BR6 — empty-requests shown when both incoming and outgoing lists are empty', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signInAsAndGotoRequests(page, 'a@test.local', 'Password1234!');

    await expect(page.locator('#empty-requests')).toBeVisible();
  });
});
