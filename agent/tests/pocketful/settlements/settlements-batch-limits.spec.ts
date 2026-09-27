// tests/pocketful/settlements/settlements-batch-limits.spec.ts
// SB1: batch of 33 transfers → 422 (max is 32)
// SB2: empty transfers array → 422 or 400
// SB3: bad handle mid-batch → all-or-nothing rollback, prior entries not applied
import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';

function makeUsers() {
  return [
    { id: 'alice', email: 'a@test.local', password: 'Password1234!',
      display_name: 'Alice', handle: 'alice', balance: 0 },
    { id: 'bob', email: 'b@test.local', password: 'Password1234!',
      display_name: 'Bob', handle: 'bob', balance: 1000 },
    { id: 'carol', email: 'c@test.local', password: 'Password1234!',
      display_name: 'Carol', handle: 'carol', balance: 0 },
  ];
}

test.describe('Settlement batch limits', () => {
  test('SB1 — 33-entry batch → 422 (max is 32)', async ({ page }) => {
    // 33 distinct recipient users so the rejection is batch-size, not unknown-handle
    const recipients = Array.from({ length: 33 }, (_, i) => ({
      id: `r${i}`, email: `r${i}@test.local`, password: 'Password1234!',
      display_name: `R${i}`, handle: `r${i}`, balance: 0,
    }));
    const reset = await page.request.post(`${API_BASE}/_test/reset`, {
      data: {
        currency: 'USD',
        minor_units: 2,
        settlement_operator_ids: ['alice'],
        users: [
          { id: 'alice', email: 'a@test.local', password: 'Password1234!',
            display_name: 'Alice', handle: 'alice', balance: 0 },
          { id: 'bob', email: 'b@test.local', password: 'Password1234!',
            display_name: 'Bob', handle: 'bob', balance: 1000 },
          ...recipients,
        ],
      },
    });
    if (!reset.ok()) throw new Error(`Reset failed: ${reset.status()}`);

    const aliceToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;

    // 33 distinct recipients — isolates batch-size rejection from other validation
    const transfers = Array.from({ length: 33 }, (_, i) => ({
      from_handle: 'bob', to_handle: `r${i}`, amount: 1,
    }));

    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'sb1-33-transfers',
        'Content-Type': 'application/json',
      },
      data: { transfers },
    });
    expect(settlement.status()).toBe(422);

    // Bob's balance must be unchanged — rejected batch applied nothing
    const bobToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const bobMe = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${bobToken}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(1000n);
  });

  test('SB2 — empty transfers array → 422 or 400', async ({ page }) => {
    const reset = await page.request.post(`${API_BASE}/_test/reset`, {
      data: {
        currency: 'USD',
        minor_units: 2,
        settlement_operator_ids: ['alice'],
        users: makeUsers(),
      },
    });
    if (!reset.ok()) throw new Error(`Reset failed: ${reset.status()}`);

    const aliceToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;

    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'sb2-empty-batch',
        'Content-Type': 'application/json',
      },
      data: { transfers: [] },
    });
    expect([400, 422]).toContain(settlement.status());
  });

  test('SB3 — bad handle in entry 2 → 422 or 409; entry 1 must not apply', async ({ page }) => {
    const reset = await page.request.post(`${API_BASE}/_test/reset`, {
      data: {
        currency: 'USD',
        minor_units: 2,
        settlement_operator_ids: ['alice'],
        users: makeUsers(),
      },
    });
    if (!reset.ok()) throw new Error(`Reset failed: ${reset.status()}`);

    const aliceToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const bobToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const carolToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'c@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;

    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 'sb3-bad-handle',
        'Content-Type': 'application/json',
      },
      data: {
        transfers: [
          { from_handle: 'bob', to_handle: 'carol', amount: 50 },
          { from_handle: 'nonexistent_user', to_handle: 'carol', amount: 10 },
        ],
      },
    });
    expect([409, 422]).toContain(settlement.status());

    // All-or-nothing: entry 1 must NOT have applied
    const bobMe   = await page.request.get(`${API_BASE}/me`, { headers: { Authorization: `Bearer ${bobToken}` } }).then(r => r.json());
    const carolMe = await page.request.get(`${API_BASE}/me`, { headers: { Authorization: `Bearer ${carolToken}` } }).then(r => r.json());

    expect(BigInt(bobMe.balance)).toBe(1000n);
    expect(BigInt(carolMe.balance)).toBe(0n);
  });
});
