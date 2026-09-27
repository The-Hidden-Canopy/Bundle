# Pocketful Stage-Boundary Consumer-Contract Checklist

*Stage 2 UI consumes Stage 1 HTTP API. This checklist documents every contract point the UI depends on, what Stage 1 provides, and what Stage 2 adds or changes.*

---

## How to read this document

- **Stage 1 contract** = what the server provides now (commit `ddc0ffb`)
- **Stage 2 delta** = what changes or is added in Stage 2
- **UI obligation** = what the browser client must do with the contract

---

## 1. Authentication

| Endpoint | Method | Request body | Success | Error codes | Stage 2 delta |
|----------|--------|-------------|---------|-------------|---------------|
| `/auth/signup` | POST | `{email, password, display_name, handle}` | 201 `{token}` | `422 validation_failed` | none |
| `/auth/login` | POST | `{email, password}` | 200 `{token}` | `401 unauthenticated` | none |

**UI obligations:**
- Store token in `localStorage` as Bearer credential
- On any `401` from any endpoint → clear token, redirect `/login`
- No logout endpoint — client-side token removal only

---

## 2. User Profile

| Endpoint | Method | Auth | Stage 1 response | Stage 2 delta |
|----------|--------|------|-----------------|---------------|
| `GET /me` | GET | Bearer | `{user_id, display_name, handle, balance, currency, minor_units}` | Adds `total`, `available`, `held` (`balance` = `total`; `available` = `total − held`) |

**UI obligations:**
- Read `balance` from `GET /me` (Stage 1); read `available`/`held` from `GET /me` (Stage 2)
- Never derive `available` client-side from authorization list — it's a server field
- All amounts as BigInt or string; never `Number()` on monetary values

---

## 3. Payments

| Endpoint | Method | Auth | Idempotency | Request body | Success | Error codes |
|----------|--------|------|-------------|-------------|---------|-------------|
| `POST /payments` | POST | Bearer | Required header | `{to_handle, amount, note?, visibility}` | 201 `{id, ...}` / 200 (replay) | `409 insufficient_funds`, `409 idempotency_key_reuse`, `422 validation_failed`, `401` |

**Stage 2 delta:** `insufficient_funds` checked against `available` (not `balance`) — holds reduce effective funds.

**UI obligations:**
- Generate UUID idempotency key client-side; persist in `localStorage`
- On network failure (no status code) → enter `pay-uncertain` state; retain key
- Exit `pay-uncertain` only when BOTH `GET /me` balance AND `GET /activity` record agree
- `409 insufficient_funds` does NOT consume key; key remains valid after funding
- Only `201`/`200` permanently lock a key to its body

---

## 4. Activity Feed

| Endpoint | Method | Auth | Query params | Response |
|----------|--------|------|-------------|---------|
| `GET /activity` | GET | Bearer | `limit`, `offset` | `{payments: [{id, from_handle, to_handle, amount, note, visibility, ...}], has_more}` |

**UI obligations:**
- Response array field is `payments` (not `items`)
- Paginate via `offset + limit` when `has_more: true`
- Private payments visible only to sender and receiver; absent for third parties

---

## 5. Requests

| Endpoint | Method | Auth | Idempotency | Request body | Success | Error codes |
|----------|--------|------|-------------|-------------|---------|-------------|
| `POST /requests` | POST | Bearer | Required | `{from_handle, amount, note?}` | 201 `{id, status: "pending", ...}` | `422`, `401` |
| `GET /requests` | GET | Bearer | — | — | 200 `{requests: [{id, status, amount, ...}]}` | `401` |
| `POST /requests/{id}/pay` | POST | Bearer | Required | `{}` | 201 | `403/404` (non-party), `409`, `401` |
| `POST /requests/{id}/decline` | POST | Bearer | Required | `{}` | 200 | `403/404`, `401` |
| `POST /requests/{id}/cancel` | POST | Bearer | Required | `{}` | 200 | `403/404`, `401` |

**UI obligations:**
- Response array field is `requests` (not `items`)
- Separate `incoming-list` and `outgoing-list` DOM containers
- `request-pay` button only on pending incoming; `request-cancel` only on pending outgoing
- On `403`/`404` action → show `request-error`, refresh list

---

## 6. Authorizations

| Endpoint | Method | Auth | Idempotency | Request body | Success | Error codes |
|----------|--------|------|-------------|-------------|---------|-------------|
| `POST /authorizations` | POST | Bearer | Required | `{to_handle, amount, note?, visibility?}` | 201 `{authorization_id, from_handle, to_handle, amount, status: "open", captured_amount: 0, payment_id: null}` | `422`, `401` |
| `GET /authorizations` | GET | Bearer | — | query: `direction`, `status`, `limit`, `offset` | 200 `{authorizations: [...], has_more}` | `401` |
| `POST /authorizations/{id}/capture` | POST | **Receiver** | Required | `{amount?, final?}` | **201** `{payment_id, from_handle, to_handle, amount, authorization_id}` | `409 authorization_not_open`, `409 authorization_expired`, `422 capture_exceeds_authorization`, `422 validation_failed`, `403` (not receiver), `404` |
| `POST /authorizations/{id}/void` | POST | Creator | **None** | `{}` | 200 `{authorization_id, status: "voided"}` | `409 authorization_not_open`, `403`, `401` |

**Critical contract points:**
- Only the **receiver** (the `to_handle`) may capture — not the creator
- Capture returns **201** (not 200) with created payment in payment shape
- Void has **no idempotency key**; void on already-voided → 200 (idempotent no-op, not an error)
- Capture after void → `409 authorization_not_open`
- Capture after expiry → `409 authorization_expired` (distinct from `authorization_not_open`)
- Capture amount > remaining hold → `422 capture_exceeds_authorization`
- `authorization-error` DOM hook covers all capture and void failures; no `capture_failed` or `void_failed` error codes exist
- Pre-flight `GET /me` or `GET /authorizations` cannot prevent a subsequent capture 409 — authorization may expire in the gap; always re-fetch after any capture failure

**Stage 2 delta:** `insufficient_funds` on capture checked against `available`.

---

## 7. Splits

| Endpoint | Method | Auth | Idempotency | Request body | Success | Error codes |
|----------|--------|------|-------------|-------------|---------|-------------|
| `POST /splits` | POST | Bearer | Required | `{amount, handles: [handle, ...], note?}` | 201 | `422`, `401` |

**UI obligations:**
- Client-side preview must use the same floor-division + remainder-to-first-N algorithm as the server
- Preview computed locally in `split-preview` before submit

*Note: exact endpoint path and body shape to be confirmed against spec §9 before implementation.*

---

## 8. Settlements (Operator-only)

| Endpoint | Method | Auth | Idempotency | Request body | Success | Error codes |
|----------|--------|------|-------------|-------------|---------|-------------|
| `POST /settlements` | POST | Bearer (operator) | Required | `{transfers: [{from_handle, to_handle, amount, note?, visibility?}]}` 1–32 entries | 201 | `403` (not operator), `409 insufficient_funds`, `422 validation_failed` |

**Stage 2 delta:** `insufficient_funds` checked against `available`.

**UI obligations:**
- Settlement is all-or-nothing; no partial application
- On `422`: show which entry failed (entry index + reason)

---

## 9. Stage 1 → Stage 2 Boundary Changes Summary

| Contract point | Stage 1 | Stage 2 |
|---------------|---------|---------|
| `GET /me` balance fields | `balance` only | Adds `total`, `available`, `held` |
| `insufficient_funds` check | Against `balance` | Against `available` (payments, settlements, capture) |
| Authorization capture caller | (spec clarification) | Receiver only |
| Authorization capture status | 201 | 201 (unchanged) |

---

## 10. Known Stage 1 Deviations (non-blocking, queued for next revision)

1. `minor_units` accepts any non-negative integer; spec allows only `{0, 2, 3}`
2. No server-enforced per-request read timeout
3. Dead code: unused `authorization_not_open` error helper

None of these affect the Stage 2 UI contract.

---

## 11. Preserved Across Stage Boundary

- Token format and auth header unchanged
- Idempotency key header name unchanged (`Idempotency-Key`)
- Error response shape unchanged: `{error: {code: string, message?: string}}`
- `POST /_test/reset` signature unchanged (Stage 2 adds no new reset fields)
- All Stage 1 endpoints remain at same paths with same method verbs
