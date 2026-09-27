// tests/pocketful/requests/request-actions.spec.ts
// B1: Bob pays Alice's request → 201, status paid
// B2: Carol (non-party) attempts to pay → 403 or 404 (not pay-uncertain)
// B3: Bob declines a request → 200, status declined
import { test, expect } from '../test.base';

test.describe('Request actions', () => {
  test.beforeEach(async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
      { id: 'bob', email: 'b@test.local', password: 'Password1234!',
        display_name: 'Bob', handle: 'bob', balance: 100 },
      { id: 'carol', email: 'c@test.local', password: 'Password1234!',
        display_name: 'Carol', handle: 'carol', balance: 100 },
    ]);
    await page.addInitScript(() => window.localStorage.clear());
  });

  test('B1 — Bob pays Alice request → 201, request marked paid', async ({ page }) => {
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const bobLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;

    // Alice creates a request for Bob to pay 20
    const createReq = await page.request.post('http://localhost:8080/requests', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'b1-alice-requests-bob',
        'Content-Type': 'application/json',
      },
      data: { payer_handle: 'bob', amount: 20, note: 'Test B1' },
    });
    expect(createReq.status()).toBe(201);
    const requestId = (await createReq.json()).request_id;

    // Bob pays the request
    const payReq = await page.request.post(`http://localhost:8080/requests/${requestId}/pay`, {
      headers: {
        'Authorization': `Bearer ${bobToken}`,
        'Idempotency-Key': 'b1-bob-pays-request',
        'Content-Type': 'application/json',
      },
    });
    expect(payReq.status()).toBe(201);

    // Verify balances
    const aliceMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${aliceToken}` },
    }).then(r => r.json());
    expect(BigInt(aliceMe.balance)).toBe(20n);

    const bobMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(80n);

    // Verify request status via GET /requests
    const bobRequests = await page.request.get('http://localhost:8080/requests', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    const req = bobRequests.requests.find((r: any) => r.request_id === requestId);
    expect(req).toBeDefined();
    expect(req.status).toBe('paid');
  });

  test('B2 — Carol (non-party) cannot pay Alice→Bob request', async ({ page }) => {
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const carolLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'c@test.local', password: 'Password1234!' },
    });
    const carolToken = (await carolLogin.json()).token;

    // Alice requests payment from Bob
    const createReq = await page.request.post('http://localhost:8080/requests', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'b2-alice-requests-bob',
        'Content-Type': 'application/json',
      },
      data: { payer_handle: 'bob', amount: 10, note: 'Test B2' },
    });
    const requestId = (await createReq.json()).request_id;

    // Carol tries to pay it — must get 403 or 404 (spec allows either)
    const carolPay = await page.request.post(`http://localhost:8080/requests/${requestId}/pay`, {
      headers: {
        'Authorization': `Bearer ${carolToken}`,
        'Idempotency-Key': 'b2-carol-tries-to-pay',
        'Content-Type': 'application/json',
      },
    });
    expect([403, 404]).toContain(carolPay.status());

    // Balances must be unchanged — Carol's money untouched
    const carolMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${carolToken}` },
    }).then(r => r.json());
    expect(BigInt(carolMe.balance)).toBe(100n);
  });

  test('B3 — Bob declines a request → 200, status declined', async ({ page }) => {
    const aliceLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    const aliceToken = (await aliceLogin.json()).token;

    const bobLogin = await page.request.post('http://localhost:8080/auth/login', {
      data: { email: 'b@test.local', password: 'Password1234!' },
    });
    const bobToken = (await bobLogin.json()).token;

    // Alice requests 15 from Bob
    const createReq = await page.request.post('http://localhost:8080/requests', {
      headers: {
        'Authorization': `Bearer ${aliceToken}`,
        'Idempotency-Key': 'b3-alice-requests-bob',
        'Content-Type': 'application/json',
      },
      data: { payer_handle: 'bob', amount: 15, note: 'Test B3' },
    });
    const requestId = (await createReq.json()).request_id;

    // Bob declines
    const declineReq = await page.request.post(`http://localhost:8080/requests/${requestId}/decline`, {
      headers: {
        'Authorization': `Bearer ${bobToken}`,
        'Idempotency-Key': 'b3-bob-declines',
        'Content-Type': 'application/json',
      },
    });
    expect(declineReq.status()).toBe(200);

    // Verify status is declined
    const bobRequests = await page.request.get('http://localhost:8080/requests', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    const req = bobRequests.requests.find((r: any) => r.request_id === requestId);
    expect(req?.status).toBe('declined');

    // Bob's balance unchanged
    const bobMe = await page.request.get('http://localhost:8080/me', {
      headers: { 'Authorization': `Bearer ${bobToken}` },
    }).then(r => r.json());
    expect(BigInt(bobMe.balance)).toBe(100n);
  });
});
