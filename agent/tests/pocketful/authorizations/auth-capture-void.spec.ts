import { expect, test } from '../test.base';
import type { APIRequestContext } from '@playwright/test';

const API_BASE = 'http://localhost:8080';

const ALICE = { email: 'alice@test.local', password: 'Password1234!' };
const BOB   = { email: 'bob@test.local',   password: 'Password1234!' };

const AUTHORIZATION_AMOUNT = 1_500;

type Session = { token: string; headers: { Authorization: string } };

async function login(
  request: APIRequestContext,
  user: { email: string; password: string },
): Promise<Session> {
  const response = await request.post(`${API_BASE}/auth/login`, { data: user });
  expect(response.status()).toBe(200);
  const body = await response.json();
  expect(typeof body.token).toBe('string');
  return { token: body.token, headers: { Authorization: `Bearer ${body.token}` } };
}

async function balance(request: APIRequestContext, session: Session): Promise<bigint> {
  const response = await request.get(`${API_BASE}/me`, { headers: session.headers });
  expect(response.status()).toBe(200);
  return BigInt((await response.json()).balance);
}

test.describe('Payment authorizations', () => {
  test.beforeEach(async ({ pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'alice@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 5000 },
      { id: 'bob', email: 'bob@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
    ]);
  });

  test('Auth1 — Alice creates an authorization against Bob', async ({ request }) => {
    const alice = await login(request, ALICE);

    const response = await request.post(`${API_BASE}/authorizations`, {
      headers: { ...alice.headers, 'Idempotency-Key': 'auth1-create-alice-bob' },
      data: { to_handle: 'bob', amount: AUTHORIZATION_AMOUNT },
    });

    expect(response.status()).toBe(201);
    const body = await response.json();
    expect(body.authorization_id).toEqual(expect.any(String));
    expect(body.from_handle).toBe('alice');
    expect(body.to_handle).toBe('bob');
    expect(body.amount).toBe(AUTHORIZATION_AMOUNT);
    expect(body.status).toBe('open');
    expect(body.captured_amount).toBe(0);
    expect(body.payment_id).toBeNull();
  });

  test('Auth2 — Bob captures Alice\'s authorization', async ({ request }) => {
    const alice = await login(request, ALICE);
    const bob   = await login(request, BOB);
    const aliceBefore = await balance(request, alice);
    const bobBefore   = await balance(request, bob);

    const authResp = await request.post(`${API_BASE}/authorizations`, {
      headers: { ...alice.headers, 'Idempotency-Key': 'auth2-create-alice-bob' },
      data: { to_handle: 'bob', amount: AUTHORIZATION_AMOUNT },
    });
    expect(authResp.status()).toBe(201);
    const auth = await authResp.json();

    // Receiver (Bob) captures — spec-compliant; returns 201 + payment
    const captureResp = await request.post(
      `${API_BASE}/authorizations/${auth.authorization_id}/capture`,
      {
        headers: { ...bob.headers, 'Idempotency-Key': 'auth2-capture-alice-bob' },
        data: {},
      },
    );
    expect(captureResp.status()).toBe(201);
    const payment = await captureResp.json();
    expect(payment.authorization_id).toBe(auth.authorization_id);
    expect(payment.from_handle).toBe('alice');
    expect(payment.to_handle).toBe('bob');
    expect(BigInt(payment.amount)).toBe(BigInt(AUTHORIZATION_AMOUNT));

    expect(await balance(request, alice)).toBe(aliceBefore - BigInt(AUTHORIZATION_AMOUNT));
    expect(await balance(request, bob)).toBe(bobBefore   + BigInt(AUTHORIZATION_AMOUNT));
  });

  test('Auth3 — Alice voids a fresh authorization; Bob balance unchanged', async ({ request }) => {
    const alice = await login(request, ALICE);
    const bob   = await login(request, BOB);
    const bobBefore = await balance(request, bob);

    const authResp = await request.post(`${API_BASE}/authorizations`, {
      headers: { ...alice.headers, 'Idempotency-Key': 'auth3-create-alice-bob' },
      data: { to_handle: 'bob', amount: AUTHORIZATION_AMOUNT },
    });
    expect(authResp.status()).toBe(201);
    const auth = await authResp.json();

    const voidResp = await request.post(
      `${API_BASE}/authorizations/${auth.authorization_id}/void`,
      { headers: alice.headers },
    );
    expect(voidResp.status()).toBe(200);
    const voided = await voidResp.json();
    expect(voided.authorization_id).toBe(auth.authorization_id);
    expect(voided.status).toBe('voided');
    expect(await balance(request, bob)).toBe(bobBefore);
  });

  test('Auth4 — void is repeatable; capture after void → 409 authorization_not_open', async ({ request }) => {
    const alice = await login(request, ALICE);
    const bob   = await login(request, BOB);

    const authResp = await request.post(`${API_BASE}/authorizations`, {
      headers: { ...alice.headers, 'Idempotency-Key': 'auth4-create-alice-bob' },
      data: { to_handle: 'bob', amount: AUTHORIZATION_AMOUNT },
    });
    expect(authResp.status()).toBe(201);
    const auth = await authResp.json();

    const firstVoid = await request.post(
      `${API_BASE}/authorizations/${auth.authorization_id}/void`,
      { headers: alice.headers },
    );
    expect(firstVoid.status()).toBe(200);

    const secondVoid = await request.post(
      `${API_BASE}/authorizations/${auth.authorization_id}/void`,
      { headers: alice.headers },
    );
    expect(secondVoid.status()).toBe(200);
    expect((await secondVoid.json()).status).toBe('voided');

    const capture = await request.post(
      `${API_BASE}/authorizations/${auth.authorization_id}/capture`,
      {
        headers: { ...bob.headers, 'Idempotency-Key': 'auth4-capture-after-void' },
        data: {},
      },
    );
    expect(capture.status()).toBe(409);
    expect((await capture.json()).error.code).toBe('authorization_not_open');
  });
});
