# Pocketful Stage 2 UI & Client-State Planning Brief v5

*Phase 0 planning only. No code implemented.*

---

## §1. Routes

| Route | Purpose |
|-------|---------|
| `/` | Balance + pay form + request creation form + activity feed |
| `/login` | Login/signup screen |
| `/requests` | Request management (incoming + outgoing lists) |
| `/split` | Bill split form |
| `/authorizations` | Authorization management |

---

## §2. Auth and Session

**Token storage:** Bearer token in `localStorage`. Tokens don't expire (Stage 1 §6). Multiple valid tokens per account.

**Logout:** Client-side only — clear token from localStorage, redirect to `/login`. No server endpoint.

**Post-reset behavior:** `POST /_test/reset` clears all tokens. Any open browser session's next request returns `401` → redirect to `/login`.

**DOM hooks:**
- `signup-email`, `signup-password`, `signup-display-name` (required), `signup-submit`
- `login-email`, `login-password`, `login-submit`
- `auth-error` — present only when there is an error
- `current-user` — visible on all screens when signed in; text contains display name
- `current-handle` — text is exactly the caller's handle (no prefix/suffix)
- `logout-button`

---

## §3. Balance Display

**Source:** `GET /me` → `{ user_id, display_name, handle, balance, total, available, held, currency, minor_units }`
(`balance` = `total`; `available` = `total − held`)

**DOM hooks:**
- `wallet-balance` — carries `data-amount="{minor_units_integer}"` attribute
- `wallet-available` — carries `data-amount="{minor_units_integer}"` attribute
- `wallet-held` — carries `data-amount="{minor_units_integer}"` attribute; absent when zero
- `wallet-refresh` — button that refreshes both balance AND activity feed without clearing the pay form

**Ordering guard:** `wallet-refresh` responses processed in request-sequence order. A delayed earlier response must never overwrite a later refresh result.

**Integer safety:** All amounts as BigInt or string. Never `Number()` or float arithmetic on monetary values.

---

## §4. Pay Form (on `/`)

**Named input DOM hooks:** `pay-handle`, `pay-amount`, `pay-note`, `pay-visibility` (select: `public`/`private`)

Idempotency key generated client-side (UUID), persisted in `localStorage` keyed by payment intent. Survives page reloads. Decimal input accepted; converted to minor units before submit.

**State machine:**

| State | DOM hook | Trigger |
|-------|----------|---------|
| Idle | `pay-form` | Default |
| Submitting | `pay-pending` | Request in-flight |
| Success | `pay-success` | `201` or `200` (idempotent replay) |
| Insufficient funds | `pay-error` | `409 insufficient_funds` |
| Key reuse conflict | `pay-error` | `409 idempotency_key_reuse` |
| Validation error | `pay-error` | `422` |
| Lost response | `pay-uncertain` | Network error / no status code received |
| Auth expired | redirect `/login` | `401` |
| Other error | `pay-error` | Other 4xx/5xx |

**`pay-uncertain` handling (strict):**
- Client stays in `pay-uncertain`; retains the same idempotency key
- Form retryable with same key and same body
- UI exits `pay-uncertain` only when **both** `GET /me` (balance) AND `GET /activity` (payment record) agree on the outcome
- Balance-only update insufficient to exit — must wait for activity confirmation

**Recovery after `409 insufficient_funds`:** Same idempotency key remains valid. Retry succeeds after funding. Only `201`/`200` permanently locks a key to its body.

**Competing clients:** Another client may spend balance after this browser's last read. `409 insufficient_funds` shows `pay-error`, refreshes balance and feed, preserves form inputs.

---

## §5. Activity Feed (on `/`)

**Source:** `GET /activity?limit=50&offset=0` → `{ payments, has_more }`

`has_more` drives whether more items exist. Next page: `offset + limit`.

**DOM hooks (per-ID):**
- `activity-list` — container
- `activity-item-{payment_id}` — one per payment; carries `data-visibility="public"` or `"private"`
- `activity-parties-{payment_id}` — text contains both handles
- `activity-amount-{payment_id}` — formatted amount
- `activity-note-{payment_id}` — note text, present even when empty
- `empty-activity` — shown when nothing visible

**Auth required:** `401` → redirect `/login`. No pre-auth public feed.

---

## §6. Requests (`/requests`)

**Source:** `GET /requests` → filtered to authenticated user's party requests.

**DOM hooks:**
- `incoming-list` — incoming requests container
- `outgoing-list` — outgoing requests container
- `item-{request_id}` — one per request; carries `data-status="{status}"`
- `request-amount-{request_id}` — formatted amount
- `request-pay-{request_id}` — button; only on pending incoming requests
- `request-decline-{request_id}` — button; only on pending incoming requests
- `request-cancel-{request_id}` — button; only on pending outgoing requests
- `request-error` — shown when action refused
- `empty-requests` — shown when both lists empty

**Competing clients:** If a request is cancelled by another client while the pay button is visible, action returns an error → show `request-error` and refresh list.

---

## §7. Split Form (`/split`)

**API body field:** `participant_handles` (array of handles). Caller may be included or omitted.

**Response shape:** `{split_id, amount, currency, note, shares: [{handle, amount}], requests: [...], created_at}`
- `shares`: all participants including caller, in order, sums to `amount`
- `requests`: all participants except caller (caller is requester on each generated request)
- A caller-only split produces `requests: []`
- Never checks balance at creation time

**DOM hooks:** `split-amount`, `split-handles`, `split-note`, `split-submit`, `split-preview`, `split-share-{handle}`, `split-error`

**Client-side preview:** `split-preview` shows all computed shares (including caller's own share) before submitting, using the same floor-division + remainder-to-first-N algorithm as the server (Stage 1 §9). Shares that will generate a payment request (all except caller) should be visually distinguished. Preview must match submitted result.

**Error codes:** `422 validation_failed` (amount out of range, empty/duplicate handles, note > 200 chars); `404 not_found` (unknown handle).

Decimal input accepted; converted to minor units.

---

## §8. Authorizations (`/authorizations`)

**Source:** `GET /authorizations?direction=outgoing&status=open&limit=50&offset=0`

**Form DOM hooks:** `authorize-handle`, `authorize-amount`, `authorize-note`, `authorize-visibility` (select: `public`/`private`), `authorize-submit`, `authorize-error`

**List DOM hooks:**
- `authorization-list`
- `authorization-item-{id}`
- `authorization-amount-{id}`
- `authorization-captured-{id}`
- `authorization-expires-{id}`
- `authorization-capture-amount-{id}` — input for capture amount
- `authorization-capture-{id}` — capture button
- `authorization-void-{id}` — void button
- `authorization-error` — shown for both capture and void failures
- `empty-authorizations`

**Status field values:** `open`, `captured`, `voided`, `expired`

**Capture:** Returns `201` with the created payment (same shape as `POST /payments`). Default `final: true` (releases remaining hold after capture). Explicit `{"final": false}` for partial capture. UI design decision: expose as a checkbox "Release remaining hold after capture" defaulting checked. Spec does not prescribe the form control.

**Void idempotency:** Void on already-voided → `200`, no error shown. Both calls transition to `voided` state.

---

## §9. Settlements

**Endpoint:** `POST /settlements` (Stage 1 §11, operator-only). Persists into Stage 2 unchanged except: `insufficient_funds` now checked against `available` (not `balance`).

**Body:** `{ transfers: [{ from_handle, to_handle, amount, note?, visibility? }] }` — 1–32 entries.

On `422`: UI must show which entry failed (entry index + reason), not generic "batch failed".

---

## §10. Upgrade Compatibility

When `POST /_test/import` upgrades state:
- Browser signed in before upgrade remains signed in
- Pending requests remain payable
- Lost payment responses (`pay-uncertain`) remain retryable with same key and body
- No page reload required

---

## §11. Riptide Integration (Optional)

Pure advisory function. Never mutates financial state. Called before confirming pay. If unavailable → resolves neutral; no blocking, no error shown. Pass most recently fetched balance (post-`wallet-refresh`) to avoid stale-data advisory.

---

## §12. Concurrent Write Behavior

Writes linearize normally per spec. No special "service unavailable" UI state for concurrent writes. Server returns ordinary results (`201`, `409`, etc.). Cross-tab behavior: second tab receives `409 insufficient_funds` on stale-balance submit — handled normally as `pay-error`.

---

## §13. Responsive Design and Accessibility

- Required flows must remain clear and usable at **375 CSS-pixel viewport** and conventional desktop widths, without horizontal scrolling.
- Inputs need **visible labels**.
- **Keyboard focus** must be apparent.
- Text and controls need **sufficient contrast**.
