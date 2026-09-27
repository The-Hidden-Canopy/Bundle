// tests/pocketful/payments/pay-concurrent.spec.ts
// C1: two concurrent payments from same wallet (Alice has 10; Bob gets 10, Carol gets 10)
// Exactly one 201, one 409 insufficient_funds; Alice never goes negative.
import { test, expect } from '../test.base';

test.describe('C1 — concurrent payments from same wallet', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 10 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
  });

  test('exactly one 201, one 409 insufficient_funds; balances conserved', async ({ page }) => {
    // Log in as Alice (reset created user; login not signup)
    const loginResp = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    expect(loginResp.status()).toBe(200);
    const aliceToken = (await loginResp.json()).token;

    // Log in as Bob and Carol to check their balances afterward
    const bobLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;

    const carolLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'c@test.local', password: 'Password1234!' },
    });
    const carolToken = (await carolLogin.json()).token;

    // Fire both payments concurrently with Promise.all
    const [toBob, toCarol] = await Promise.all([
      page.request.post('http://localhost:8080/payments', {
        headers: {
          'Authorization': `Bearer ${aliceToken}`,
          'Idempotency-Key': 'c1-alice-to-bob',
          'Content-Type': 'application/json',
        },
        data: { to_handle: 'bob', amount: 10, visibility: 'private' },
      }),
      page.request.post('http://localhost:8080/payments', {
        headers: {
          'Authorization': `Bearer ${aliceToken}`,
          'Idempotency-Key': 'c1-alice-to-carol',
          'Content-Type': 'application/json',
        },
        data: { to_handle: 'carol', amount: 10, visibility: 'private' },
      }),
    ]);

    const statuses = [toBob.status(), toCarol.status()].sort();

    // Exactly one 201, one 409
    expect(statuses).toEqual([201, 409]);

    // The 409 must be insufficient_funds specifically
    const failedResp = toBob.status() === 409 ? toBob : toCarol;
    const failedBody = await failedResp.json();
    expect(failedBody.error.code).toBe('insufficient_funds');

    // Alice balance: exactly 0 (she spent exactly 10; never negative)
    const aliceMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${aliceToken}` },
    }).then(r => r.json());
    expect(BigInt(aliceMe.balance)).toBe(0n);
    // Also assert not negative (belt-and-suspenders)
    expect(BigInt(aliceMe.balance)).toBeGreaterThanOrEqual(0n);

    // Balance conservation: exactly one recipient got 10, the other got 0
    const bobMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());

    const carolMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${carolToken}` },
    }).then(r => r.json());

    const bobBalance = BigInt(bobMe.balance);
    const carolBalance = BigInt(carolMe.balance);

    // One got 10, the other got 0; sum is exactly 10
    expect(bobBalance + carolBalance).toBe(10n);
    expect([bobBalance, carolBalance]).toContain(10n);
    expect([bobBalance, carolBalance]).toContain(0n);
  });
});
