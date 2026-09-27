// tests/pocketful/auth/auth-flow.spec.ts
// A1: signup → 201, token returned, GET /me returns correct fields
// A2: login with correct credentials → 200, token works
// A3: login with wrong password → 401
// A4: GET /me with no token → 401
import { test, expect } from '../test.base';

const API_BASE = 'http://localhost:8080';

test.describe('Auth flow', () => {
  test('A1 — signup returns 201 with token; GET /me confirms user', async ({ page }) => {
    const signup = await page.request.post(`${API_BASE}/auth/signup`, {
      data: {
        email: 'alice@test.local',
        password: 'Password1234!',
        display_name: 'Alice',
        handle: 'alice',
      },
    });
    expect(signup.status()).toBe(201);
    const signupBody = await signup.json();
    expect(signupBody.token).toBeTruthy();

    const me = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${signupBody.token}` },
    });
    expect(me.status()).toBe(200);
    const meBody = await me.json();
    expect(meBody.handle).toBe('alice');
    expect(meBody.display_name).toBe('Alice');
    expect(meBody.balance).toBeDefined();
  });

  test('A2 — login with correct credentials returns 200, token authenticates', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
    ]);

    const login = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'Password1234!' },
    });
    expect(login.status()).toBe(200);
    const token = (await login.json()).token;
    expect(token).toBeTruthy();

    const me = await page.request.get(`${API_BASE}/me`, {
      headers: { Authorization: `Bearer ${token}` },
    });
    expect(me.status()).toBe(200);
    expect((await me.json()).handle).toBe('alice');
  });

  test('A3 — login with wrong password returns 401', async ({ page, pocketful }) => {
    await pocketful.reset([
      { id: 'alice', email: 'a@test.local', password: 'Password1234!',
        display_name: 'Alice', handle: 'alice', balance: 0 },
    ]);

    const login = await page.request.post(`${API_BASE}/auth/login`, {
      data: { email: 'a@test.local', password: 'WrongPassword!' },
    });
    expect(login.status()).toBe(401);
    const body = await login.json();
    expect(body.error.code).toBe('unauthenticated');
  });

  test('A4 — GET /me with no token returns 401', async ({ page }) => {
    const me = await page.request.get(`${API_BASE}/me`);
    expect(me.status()).toBe(401);
    const body = await me.json();
    expect(body.error.code).toBe('unauthenticated');
  });
});
