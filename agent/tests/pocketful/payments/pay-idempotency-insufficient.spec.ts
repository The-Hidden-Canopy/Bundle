// tests/pocketful/payments/pay-idempotency-insufficient.spec.ts
// I4f: idempotency key retained after 409 insufficient_funds; retry succeeds after funding
import { test, expect } from '../test.base';

test.describe('I4f — key retained after 409 insufficient_funds', () => {
  const IDEM_KEY = `test-key-i4f-${Date.now()}`;

  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 5 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'funder', email: 'f@test.local', password: 'Password1234!',
        display_name: 'Funder', handle: 'funder', balance: 1000 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
    await page.goto('http://localhost:3000');
    await page.fill('#login-email', 'a@test.local');
    await page.fill('#login-password', 'Password1234!');
    await page.click('#login-submit');
    await expect(page.locator('#wallet-balance')).toBeVisible();
  });

  test('same key succeeds after funding — 409 does not consume key', async ({ page }) => {
    const aliceToken = await page.evaluate(() => localStorage.getItem('auth_token'));

    // Step 1: Attempt payment of 10 with only 5 → 409 insufficient_funds
    const firstAttempt = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': IDEM_KEY,
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'private' },
    });
    expect(firstAttempt.status()).toBe(409);
    const firstBody = await firstAttempt.json();
    expect(firstBody.error.code).toBe('insufficient_funds');

    // Step 2: Fund Alice to 100 (funder sends 95 to bring total to 100)
    const funderResp = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'f@test.local', password: 'Password1234!' },
    });
    const funderToken = (await funderResp.json()).token;

    await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${funderToken}`,
        'Idempotency-Key': `fund-alice-${Date.now()}`,
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'alice', amount: 95, visibility: 'private' },
    });

    // Step 3: Replay same key with same body → 201 (key not consumed by 409)
    const retry = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': IDEM_KEY,
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'private' },
    });
    expect(retry.status()).toBe(201);

    // Step 4: Verify balances with BigInt (never Number() for money)
    const aliceMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${aliceToken}` },
    }).then(r => r.json());
    // Alice: 5 (initial) + 95 (funded) - 10 (paid) = 90
    expect(BigInt(aliceMe.balance)).toBe(90n);

    const bobResp = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobResp.json()).token;
    const bobMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(10n);

    // Step 5: Second replay of same key → 200 (idempotent, no double-pay)
    const secondReplay = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': IDEM_KEY,
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'private' },
    });
    expect(secondReplay.status()).toBe(200);

    // Balances unchanged after idempotent replay
    const aliceFinal = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${aliceToken}` },
    }).then(r => r.json());
    expect(BigInt(aliceFinal.balance)).toBe(90n);
  });
});
