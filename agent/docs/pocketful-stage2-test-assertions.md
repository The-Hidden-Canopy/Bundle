# Pocketful Stage 2 Cross-Layer Test Assertion Matrix

*Maps every HTTP response code from Stage 1 to the exact UI state per the Stage 2 brief v5.*
*DOM hooks use `#id` notation (id attribute selectors). Underscores per spec: `authorization_error`.*

---

## §A. `POST /payments` → DOM State

| HTTP | Prior State | Must Exist | Text Must | Must NOT Exist | Storage |
|------|-------------|------------|-----------|----------------|---------|
| `201` | `#pay-pending` | `#pay-success` | "sent" or success | `#pay-pending`, `#pay-error`, `#pay-uncertain` | localStorage key cleared |
| `200` replay | `#pay-uncertain` or `#pay-pending` | `#pay-success` | same as 201 | `#pay-uncertain`, `#pay-error` | localStorage key cleared |
| `400 missing_key` | `#pay-pending` | `#pay-error` | "idempotency key required" | `#pay-pending`, `#pay-uncertain` | key unchanged |
| `400 malformed` | `#pay-pending` | `#pay-error` | "invalid" | `#pay-pending`, `#pay-uncertain` | key unchanged |
| `401` | `#pay-pending` | redirect `/login` | — | `#pay-error` | — |
| `409 insufficient_funds` | `#pay-pending` | `#pay-error` | "insufficient funds" | `#pay-uncertain` | **key retained** |
| `409 key_reuse` | `#pay-pending` | `#pay-error` | "already sent" or "different" | `#pay-pending` | key locked to other body |
| `422` | `#pay-pending` | `#pay-error` | specific message per error | `#pay-uncertain` | key unchanged |
| Network error | `#pay-pending` | `#pay-uncertain` | non-empty text | `#pay-error`, `#pay-success`, `#pay-pending` | **key retained; form disabled** |

### A5. `401` assertion
```javascript
expect(document.querySelector('#pay-error')).toBeNull();
expect(window.location.pathname).toBe('/login');
```

### A9. Network error / `pay-uncertain`
```javascript
expect(document.querySelector('#pay-uncertain')).not.toBeNull();
expect(document.querySelector('#pay-error')).toBeNull();
expect(document.querySelector('#pay-success')).toBeNull();
expect(document.querySelector('#pay-form button[type="submit"]').disabled).toBe(true);
// idempotency key must remain
const keys = Object.keys(localStorage).filter(k => k.startsWith('pay_intent_'));
expect(keys.length).toBeGreaterThan(0);
```

### A10. `pay-uncertain` two-source strict recovery

| Balance | Activity | DOM State |
|---------|----------|-----------|
| Old | Missing | `#pay-uncertain` (stays) |
| New | Missing | `#pay-uncertain` (stays — must not exit on balance alone) |
| New | Confirmed | `#pay-success` (exit; localStorage key cleared) |

---

## §B. `POST /requests/{id}/pay` / `decline` / `cancel` → DOM State

| Trigger | Prior State | Must Exist | Text Must | Must NOT |
|---------|-------------|------------|-----------|---------|
| pay `201` | `item-{id}` pending | `#pay-success` | "sent" | `#pay-error` |
| pay `403`/`404` non-party | `item-{id}` shown | `#pay-error` | "not authorized" | `#pay-uncertain` |
| pay `409` insufficient | `item-{id}` pending | `#pay-error` | "insufficient funds" | `#pay-uncertain` |
| pay `422` invalid amount *(B1-gated)* | `item-{id}` pending | `#pay-error` | "invalid" or "out of range" | `#pay-uncertain` |
| decline `200` | `item-{id}` pending | `item-{id}` | `data-status="declined"` | — |
| cancel `200` | `item-{id}` outgoing | `item-{id}` | `data-status="cancelled"` | — |

**Critical:** `403`/`404` must NOT enter `#pay-uncertain` — this is a permanent authorization denial, not a network error.

**B1 caveat (request-pay 422):** Until Stage 1 defect B1 is fixed (`payRequest` at `handlers.js:383` skips `validateAmountField`), `POST /requests/{id}/pay` with a seeded invalid-amount request returns **201**, not 422. The 422 row above only holds post-B1 fix. Do not run the 422 row against the current server — it will fail. In a correct Stage 1 (post-fix), requests with out-of-range amounts cannot be created via the API, so this case is only reachable via seeded fixtures in test scenarios.

---

## §C. Authorizations — Capture / Void → DOM State

| Trigger | Prior State | Must Exist | Text Must | Must NOT |
|---------|-------------|------------|-----------|---------|
| Capture `201` | `authorization-capture-{id}` active | `#pay-success` | "captured" | `#authorization-error` |
| Capture `409` not_open/expired | `authorization-capture-{id}` | `#authorization-error` | "already settled" or "expired" | "try again" |
| Capture `422` exceeds | `authorization-capture-{id}` | `#authorization-error` | "exceeds hold" | `#pay-success` |
| Void `200` (open→voided) | `authorization-void-{id}` active | `authorization-item-{id}` | `data-status="voided"` | `#authorization-error` |
| Void `200` (already voided) | `authorization-void-{id}` second click | `authorization-item-{id}` | `data-status="voided"` | `#authorization-error` |
| Void `409` not_open | `authorization-void-{id}` active | `#authorization-error` | "already settled" | — |

Note: `authorization-error` is the spec-defined error hook for both capture and void failures.

---

## §D. `POST /settlements` → DOM State

| Trigger | Must Exist | Text Must | Must NOT |
|---------|------------|-----------|---------|
| `201` success | `#pay-success` | count or "batch processed" | `#pay-error` |
| `422` entry failure | `#pay-error` | entry index + specific reason | generic "batch failed" |
| `403` not operator | `#pay-error` | "not authorized" | `#pay-uncertain` |

---

## §E. Cross-Tab Session Conflict

| Tab 2 Action | Must Exist | Text Must | Must NOT |
|-------------|------------|-----------|---------|
| Submit with stale balance, server `409` | `#pay-error` | "insufficient funds" | `#pay-uncertain` |

---

## §F. `pay-uncertain` Strict Recovery — State Machine

```typescript
// Playwright test sequence
// 1. Set up intercept BEFORE click
await page.route('**/payments', route => route.abort('failed'));
await page.click('#pay-form button[type="submit"]');

// 2. Verify uncertain state
await expect(page.locator('#pay-uncertain')).toBeVisible();
await expect(page.locator('#pay-error')).not.toBeVisible();

// 3. Restore network
await page.unroute('**/payments');

// 4. Click wallet-refresh — balance debited, activity may lag
await page.click('#wallet-refresh');
// Must stay in pay-uncertain if activity hasn't confirmed
await expect(page.locator('#pay-uncertain')).toBeVisible();
await expect(page.locator('#pay-success')).not.toBeVisible();

// 5. Activity confirms — both sources agree → exit to pay-success
await page.click('#wallet-refresh'); // or wait for next activity poll
await expect(page.locator('#pay-success')).toBeVisible();
await expect(page.locator('#pay-uncertain')).not.toBeVisible();
```
