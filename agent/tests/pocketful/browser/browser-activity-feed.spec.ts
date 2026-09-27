// tests/pocketful/browser/browser-activity-feed.spec.ts
// BA1: activity-list present after login; activity-item-{payment_id} per payment
// BA2: public payment visible to third party (Carol) in UI
// BA3: private payment absent from third party's activity-list
// BA4: empty-activity shown when feed is empty
// BA5: activity-item carries correct data-visibility attribute

import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';
const UI_BASE  = 'http://localhost:3000';

async function signIn(page: any, email: string, password: string) {
  await page.goto(`${UI_BASE}/login`);
  await page.fill('#login-email', email);
  await page.fill('#login-password', password);
  await page.click('#login-submit');
  await expect(page.locator('#wallet-balance')).toBeVisible();
}

test.describe('Activity feed UI', () => {
  test('BA1 — activity-list present; activity-item-{payment_id} rendered per payment', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signIn(page, 'a@test.local', 'Password1234!');

    const aliceToken = await page.evaluate(() => localStorage.getItem('auth_token'));
    const payResp = await page.request.post(`${API_BASE}/payments`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'ba1-alice-bob',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'public' },
    });
    expect(payResp.status()).toBe(201);
    const paymentId = (await payResp.json()).payment_id;

    await page.click('#wallet-refresh');

    await expect(page.locator('#activity-list')).toBeVisible();
    await expect(page.locator(`#activity-item-${paymentId}`)).toBeVisible();
  });

  test('BA2 — public payment visible to third party (Carol) in UI activity list', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    // Alice sends public payment to Bob
    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;
    const payResp = await page.request.post(`${API_BASE}/payments`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'ba2-alice-bob-public',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 15, visibility: 'public' },
    });
    const paymentId = (await payResp.json()).payment_id;

    // Carol signs in and views activity
    await signIn(page, 'c@test.local', 'Password1234!');
    await page.click('#wallet-refresh');

    // Public payment must appear in Carol's activity list
    await expect(page.locator(`#activity-item-${paymentId}`)).toBeVisible();

    // Both parties must appear in the parties element
    const partiesText = await page.locator(`#activity-parties-${paymentId}`).textContent();
    expect(partiesText).toContain('alice');
    expect(partiesText).toContain('bob');
  });

  test('BA3 — private payment absent from third-party activity list in UI', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());

    const aliceLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;
    const payResp = await page.request.post(`${API_BASE}/payments`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'ba3-alice-bob-private',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'private' },
    });
    const paymentId = (await payResp.json()).payment_id;

    // Carol signs in — private Alice→Bob payment must NOT appear
    await signIn(page, 'c@test.local', 'Password1234!');
    await page.click('#wallet-refresh');

    await expect(page.locator(`#activity-item-${paymentId}`)).not.toBeVisible();
  });

  test('BA4 — empty-activity shown when feed is empty', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signIn(page, 'a@test.local', 'Password1234!');

    await expect(page.locator('#empty-activity')).toBeVisible();
  });

  test('BA5 — activity-item data-visibility reflects payment visibility', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await signIn(page, 'a@test.local', 'Password1234!');

    const aliceToken = await page.evaluate(() => localStorage.getItem('auth_token'));

    const pubPay = await page.request.post(`${API_BASE}/payments`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'ba5-public',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 5, visibility: 'public' },
    });
    const privPay = await page.request.post(`${API_BASE}/payments`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'ba5-private',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 5, visibility: 'private' },
    });

    const pubId  = (await pubPay.json()).payment_id;
    const privId = (await privPay.json()).payment_id;

    await page.click('#wallet-refresh');

    const pubVisibility  = await page.locator(`#activity-item-${pubId}`).getAttribute('data-visibility');
    const privVisibility = await page.locator(`#activity-item-${privId}`).getAttribute('data-visibility');

    expect(pubVisibility).toBe('public');
    expect(privVisibility).toBe('private');
  });
});
