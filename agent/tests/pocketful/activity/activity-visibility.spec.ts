// tests/pocketful/activity/activity-visibility.spec.ts
// V3: public payment visible to third party
// V4: private payment NOT visible to third party
// V8: unauthenticated GET /activity → 401
import { test, expect } from '../test.base';

test.describe('Activity feed visibility', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 100 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 0 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 0 },
    ]);
  });

  test('V3 — public payment is visible to third party (Carol)', async ({ page }) => {
    // Alice logs in and sends a PUBLIC payment to Bob
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const payment = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'v3-public-payment',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'public' },
    });
    expect(payment.status()).toBe(201);
    const paymentBody = await payment.json();
    const paymentId = paymentBody.payment_id;

    // Carol logs in and checks activity
    const carolLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'c@test.local', password: 'Password1234!' },
    });
    const carolToken = (await carolLogin.json()).token;

    const activity = await page.request.get('http://localhost:8080/activity', {
      headers: { 'Authorization': `Bearer ${carolToken}` },
    });
    expect(activity.status()).toBe(200);
    const activityBody = await activity.json();

    // Payment record must be present in Carol's activity feed
    const found = activityBody.payments.find((p: any) => p.payment_id === paymentId);
    expect(found).toBeDefined();
    // Must include both handles and amount
    expect(found.from_handle).toBe('alice');
    expect(found.to_handle).toBe('bob');
    expect(BigInt(found.amount)).toBe(10n);
    expect(found.visibility).toBe('public');
  });

  test('V4 — private payment is NOT visible to third party (Carol)', async ({ page }) => {
    // Alice sends a PRIVATE payment to Bob
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const payment = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'v4-private-payment',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 10, visibility: 'private' },
    });
    expect(payment.status()).toBe(201);
    const paymentId = (await payment.json()).payment_id;

    // Carol checks activity — must NOT see the private payment
    const carolLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'c@test.local', password: 'Password1234!' },
    });
    const carolToken = (await carolLogin.json()).token;

    const activity = await page.request.get('http://localhost:8080/activity', {
      headers: { 'Authorization': `Bearer ${carolToken}` },
    });
    expect(activity.status()).toBe(200);
    const activityBody = await activity.json();

    // Payment must be absent — no inference possible
    const found = activityBody.payments.find((p: any) => p.payment_id === paymentId);
    expect(found).toBeUndefined();
  });

  test('V4b — private payment IS visible to sender and receiver', async ({ page }) => {
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const payment = await page.request.post('http://localhost:8080/payments', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'v4b-private-visible-parties',
        'Content-Type': 'application/json',
      },
      data: { to_handle: 'bob', amount: 5, visibility: 'private' },
    });
    const paymentId = (await payment.json()).payment_id;

    // Alice (sender) sees it
    const aliceActivity = await page.request.get('http://localhost:8080/activity', {
      headers: { 'Authorization': `Bearer ${aliceToken}` },
    }).then(r => r.json());
    expect(aliceActivity.payments.find((p: any) => p.payment_id === paymentId)).toBeDefined();

    // Bob (receiver) sees it
    const bobLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;
    const bobActivity = await page.request.get('http://localhost:8080/activity', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    expect(bobActivity.payments.find((p: any) => p.payment_id === paymentId)).toBeDefined();
  });

  test('V8 — unauthenticated GET /activity returns 401', async ({ page }) => {
    const resp = await page.request.get('http://localhost:8080/activity');
    expect(resp.status()).toBe(401);
    const body = await resp.json();
    expect(body.error.code).toBe('unauthenticated');
  });
});
