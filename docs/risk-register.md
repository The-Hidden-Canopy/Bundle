# BUNDLE risk register

This is the Git-tracked risk ledger for the implementation of the BUNDLE
financial engineering specification. It records what the current source and
tests establish, what remains bounded or open, and the evidence required before
promotion. It is not a claim that an unimplemented product surface exists.

The ledger applies to the public repository surface in this checkout. The
supplied specification attachment, nested `BUNDLE/` snapshot, generated build
output, and Python caches are deliberately excluded from Git tracking.

Status meanings:

- **Closed for this slice** means the stated boundary is implemented and
  adversarial tests cover the relevant failure mode.
- **Bounded** means the current slice fails closed or explicitly excludes the
  capability; it is not a completed product feature.
- **Open** means an implementation or runtime proof is still required.
- **Pending review** means the code may exist locally but has not yet been
  committed, pushed, or independently reviewed from the remote repository.

| ID | Risk | Status | Current evidence | Required closure or promotion gate |
| --- | --- | --- | --- | --- |
| R-001 | Archive export or restore silently drops financial records. | Closed for this slice | `connectors/bundle_connectors/sqlite_store.py` exports and restores the represented accounts, balances, transactions, commitments/evidence, budgets/allocations, goals/plans, overrides, and receipts. Unsupported populated tables fail closed. `connectors/tests/test_sqlite_store.py` verifies round trips, idempotent restore, byte-stable re-export, and tamper rejection. | Add a versioned archive file and adversarial tests before representing any currently unsupported table. |
| R-002 | A direct SQLite mutation changes financial truth without a corresponding history or domain event. | Open at the adapter boundary | The governed Python admission and archive-restore paths emit events and the migration protects the existing append-only/history tables. Direct SQL writes to represented planning tables are not a safe public mutation API. | Keep production writes behind the governed adapter, or add a schema-level audit design with canonical content hashes and tests for every writable table. |
| R-003 | The native C++ core and the SQLite persistence layer diverge. | Open | The C++20 core and Python SQLite bridge are separately tested; a native C++ SQLite adapter is not implemented. | Implement the adapter only with an explicit compatibility test matrix against the migration and archive/event invariants. |
| R-004 | Mobile or MAUI behavior is implied by the portable financial core. | Bounded | The README and architecture docs explicitly state that phone UI and MAUI runtime behavior are not represented by current evidence. | Require a real mobile build, device/emulator run, and retained receipt before claiming mobile capability. |
| R-005 | Local financial data is treated as encrypted or securely stored without a key-management design. | Open | No encryption-at-rest, key storage, rotation, recovery, or deletion contract is implemented in this slice. | Specify and test a local storage security boundary before storing sensitive data outside the current test fixtures. |
| R-006 | Provider connectivity, live balances, payment execution, or financial advice is inferred from file import and deterministic forecasts. | Bounded | CSV and OFX/QFX are local file parsers; no credentials, network calls, payment rail, or adviser authority is present. | Treat each provider/payment surface as a separate read-only, credential-scoped, receipt-producing capability with explicit authorization tests. |
| R-007 | Locally passing code is promoted without the exact source, tests, and exclusions being reviewable. | Pending review | This ledger, `.gitignore`, source, tests, schemas, and docs are being staged explicitly. No commit or push has been performed by this change. | Review the staged manifest and diff, commit with a focused message, push the intended branch, then verify remote SHA and CI results. |
| R-008 | The public remote accepts the source but does not continuously re-run the boundary tests. | Open | `.github/workflows/ci.yml` now defines the C++ build/tests, Python connector tests, and schema validation. Remote execution is not evidence until the workflow runs after push. | Push the reviewed commit and retain the GitHub Actions result before promotion. |
| R-009 | The first public push exposes unrelated or sensitive legacy history. | Open | The new `Bundle` remote is empty, but local `main` descends from the previous project history, including deleted legacy files. A normal first push would publish that ancestry in addition to the staged public tree. | Publish from a clean orphan public baseline or complete an explicit history and secret audit before pushing. Do not push the current `main` ancestry blindly. |

## Evidence recorded for the current slice

The implementation evidence to review with this ledger is:

- C++20 build and seven CTest cases for money, provenance, store, planning,
  budgets, activity, and reconciliation boundaries.
- Python connector and SQLite tests covering 25 cases, including malformed
  hashes, duplicate lineage, stale balance projections, archive tampering,
  idempotent restore, secret-field rejection, and unsupported-table refusal.
- JSON schema parsing and trailing-whitespace checks performed locally before
  staging.

These checks establish source and local-persistence behavior only. They do not
establish provider freshness, encryption, mobile runtime behavior, payment
execution, or a remote CI result.

## Change protocol

When a risk changes status, update the row and the evidence in the same commit
as the code or test change that caused the status transition. Never mark a risk
closed solely because a unit test passes if the stated runtime or promotion
boundary remains unverified.
