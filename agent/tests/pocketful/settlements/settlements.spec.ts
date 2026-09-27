// tests/pocketful/settlements/settlements.spec.ts
// S1: operator creates settlement → 201, balances updated
// S2: non-operator attempts settlement → 403
// S3: operator settlement with insufficient funds → 409 insufficient_funds
// S4: multi-entry batch fails mid-batch → all-or-nothing rollback, no partial debit
import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';

async function resetWithOperator(page: any, operatorIds: string[]) {
  const resp = await page.request.post(`${API_BASE}/_test/reset`, {
    data: {
      currency: 'USD',
      minor_units: 2,
      settlement_operator_ids: operatorIds,
      users: [
        { id: 'alice', email: 'a@test.local', password: 'Password1234!',
          display_name: 'Alice', handle: 'alice', balance: 100 },
        { id: 'bob', email: 'b@test.local', password: 'Password1234!',
          display_name: 'Bob', handle: 'bob', balance: 50 },
        { id: 'carol', email: 'c@test.local', password: 'Password1234!',
          display_name: 'Carol', handle: 'carol', balance: 0 },
      ],
    },
  });
  if (!resp.ok()) throw new Error(`Reset failed: ${resp.status()}`);
}

test.describe('Settlements', () => {
  test('S1 — operator creates settlement; balances updated', async ({ page }) => {
    await resetWithOperator(page, ['alice']);

    const loginResp = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await loginResp.json()).token;

    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 's1-operator-settlement',
        'Content-Type': 'application/json',
      },
      data: {
        transfers: [
          { from_handle: 'bob', to_handle: 'carol', amount: 30 },
        ],
      },
    });
    expect(settlement.status()).toBe(201);

    const bobLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const carolLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'c@test.local', password: 'Password1234!' },
    });

    const bobMe = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${(await bobLogin.json()).token}` },
    }).then(r => r.json());
    const carolMe = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${(await carolLogin.json()).token}` },
    }).then(r => r.json());

    expect(BigInt(bobMe.balance)).toBe(20n);
    expect(BigInt(carolMe.balance)).toBe(30n);
  });

  test('S2 — non-operator gets 403', async ({ page }) => {
    // No operator IDs — nobody is an operator
    await resetWithOperator(page, []);

    const loginResp = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await loginResp.json()).token;

    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 's2-nonoperator-settlement',
        'Content-Type': 'application/json',
      },
      data: {
        transfers: [
          { from_handle: 'bob', to_handle: 'carol', amount: 10 },
        ],
      },
    });
    expect(settlement.status()).toBe(403);

    // Balances must be unchanged
    const bobLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobMe = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${(await bobLogin.json()).token}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(50n);
  });

  test('S3 — operator settlement with insufficient funds → 409', async ({ page }) => {
    await resetWithOperator(page, ['alice']);

    const loginResp = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await loginResp.json()).token;

    // Bob only has 50; try to transfer 200
    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 's3-insufficient-funds',
        'Content-Type': 'application/json',
      },
      data: {
        transfers: [
          { from_handle: 'bob', to_handle: 'carol', amount: 200 },
        ],
      },
    });
    expect(settlement.status()).toBe(409);
    const body = await settlement.json();
    expect(body.error.code).toBe('insufficient_funds');

    // Bob's balance must be unchanged — settlement is all-or-nothing
    const bobLogin = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobMe = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${(await bobLogin.json()).token}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(50n);
  });

  test('S4 — multi-entry batch: entry 3 fails → all-or-nothing rollback, entries 1 and 2 not applied', async ({ page }) => {
    const resp = await page.request.post(`${API_BASE}/_test/reset`, {
      data: {
        currency: 'USD',
        minor_units: 2,
        settlement_operator_ids: ['alice'],
        users: [
          { id: 'alice', email: 'a@test.local', password: 'Password1234!',
            display_name: 'Alice', handle: 'alice', balance: 0 },
          { id: 'bob', email: 'b@test.local', password: 'Password1234!',
            display_name: 'Bob', handle: 'bob', balance: 10 },
          { id: 'carol', email: 'c@test.local', password: 'Password1234!',
            display_name: 'Carol', handle: 'carol', balance: 0 },
          { id: 'dave', email: 'd@test.local', password: 'Password1234!',
            display_name: 'Dave', handle: 'dave', balance: 0 },
        ],
      },
    });
    if (!resp.ok()) throw new Error(`Reset failed: ${resp.status()}`);

    const aliceToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const bobToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'b@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const carolToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'c@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;
    const daveToken = (await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'd@test.local', password: 'Password1234!' },
    }).then(r => r.json())).token;

    // Entries 1-2 are individually valid, entry 3 exceeds Bob's balance
    const settlement = await page.request.post(`${API_BASE}/settlements`, {
      headers: {
        Authorization: `Bearer ${aliceToken}`,
        'Idempotency-Key': 's4-all-or-nothing',
        'Content-Type': 'application/json',
      },
      data: {
        transfers: [
          { from_handle: 'bob', to_handle: 'carol', amount: 5 },
          { from_handle: 'bob', to_handle: 'dave', amount: 5 },
          { from_handle: 'bob', to_handle: 'carol', amount: 20 },
        ],
      },
    });
    expect(settlement.status()).toBe(409);
    expect((await settlement.json()).error.code).toBe('insufficient_funds');

    // All three balances must be exactly as before — no partial debit
    const bobMe   = await page.request.get(`${API_BASE}/me`, { headers: { Authorization: `Bearer ${bobToken}` } }).then(r => r.json());
    const carolMe = await page.request.get(`${API_BASE}/me`, { headers: { Authorization: `Bearer ${carolToken}` } }).then(r => r.json());
    const daveMe  = await page.request.get(`${API_BASE}/me`, { headers: { Authorization: `Bearer ${daveToken}` } }).then(r => r.json());

    expect(BigInt(bobMe.balance)).toBe(10n);
    expect(BigInt(carolMe.balance)).toBe(0n);
    expect(BigInt(daveMe.balance)).toBe(0n);
  });
});
