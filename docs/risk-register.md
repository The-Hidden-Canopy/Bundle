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
| R-003 | The native C++ core and the SQLite persistence layer diverge. | Pending review | `core/persistence/` implements a native C++ SQLite adapter over the same migration and the `bundle` CLI uses it. The compatibility matrix in `docs/native-sqlite-adapter.md` is backed by `tests/parity/` (Python reference vs native: identical rows, event order and byte-identical archives), crash-at-every-statement recovery tests and a CLI end-to-end CTest. Deliberate differences where the reference violates its own contract are recorded as R-010, R-011 and R-013. Local only: not pushed, reviewed or run in CI. | Review the local branch `r003-native-sqlite-adapter`; add the native build, CTest and `tests/parity` to CI (needs SQLite headers, or network/cache access for the pinned amalgamation); run parity under CPython 3.11; then close for this slice. See the R-003 task entry below. |
| R-004 | Mobile or MAUI behavior is implied by the portable financial core. | Bounded | The README and architecture docs explicitly state that phone UI and MAUI runtime behavior are not represented by current evidence. | Require a real mobile build, device/emulator run, and retained receipt before claiming mobile capability. |
| R-005 | Local financial data is treated as encrypted or securely stored without a key-management design. | Open | No encryption-at-rest, key storage, rotation, recovery, or deletion contract is implemented in this slice. | Specify and test a local storage security boundary before storing sensitive data outside the current test fixtures. |
| R-006 | Provider connectivity, live balances, payment execution, or financial advice is inferred from file import and deterministic forecasts. | Bounded | CSV and OFX/QFX are local file parsers; no credentials, network calls, payment rail, or adviser authority is present. | Treat each provider/payment surface as a separate read-only, credential-scoped, receipt-producing capability with explicit authorization tests. |
| R-007 | Locally passing code is promoted without the exact source, tests, and exclusions being reviewable. | Pending review | This ledger, `.gitignore`, source, tests, schemas, and docs are being staged explicitly. No commit or push has been performed by this change. | Review the staged manifest and diff, commit with a focused message, push the intended branch, then verify remote SHA and CI results. |
| R-008 | The public remote accepts the source but does not continuously re-run the boundary tests. | Open | `.github/workflows/ci.yml` now defines the C++ build/tests, Python connector tests, and schema validation. Remote execution is not evidence until the workflow runs after push. | Push the reviewed commit and retain the GitHub Actions result before promotion. |
| R-009 | Public history exposes excluded attachment or legacy material. | Open | `python scripts/check_public_boundary.py --history` currently reports reachable `.codex-remote-attachments/...` objects. The current working tree passes, but history is not clean. | Create a reviewed clean public baseline or complete an authorized history rewrite and secret audit before describing the repository as history-cleared. Do not delete or rewrite the current ancestry implicitly. |
| R-010 | Restoring the same archive twice silently duplicates rows in the Python bridge. | Open | `_insert_or_verify` matches keys with `=`, so `commitment_evidence` rows with a NULL `related_transaction_id` are inserted again on every restore (3 restores produce 3 copies). Reproduced by `tests/parity` (`test_A_python_reference_...`, expected failure). The native adapter matches with `IS` and is idempotent. | Decide whether to fix the reference (match NULL keys with `IS`); then un-mark the expected failure. |
| R-011 | The Python bridge cannot restore an archive it exported. | Open | Text containing raw U+2028, U+2029 or U+0085 is written unescaped (`ensure_ascii=False`) but `read_export` splits JSONL with `str.splitlines()`, which breaks the record. Reproduced by `tests/parity` (`test_B_python_reference_...`, expected failure). The native reader splits on newline only and restores it. | Decide whether to fix the reference reader (split on newline only); then un-mark the expected failure. |
| R-012 | The Release-mode C++ test gate checks nothing. | Open | `core/tests/unit/*.cpp` use `assert()` (144 checks); the documented gate builds `--config Release`, which defines `NDEBUG` and removes every assertion. In a Debug build all seven suites pass, so no current failure is hidden, but a regression would not be caught by the documented gate. The new persistence tests use a check that is active in every build type. | Make the core tests independent of `NDEBUG` (or run the gate in Debug) and record the result. |
| R-013 | Opening a database that is then refused still alters it. | Open | The Python bridge applies the migration (new tables, triggers, WAL mode) before validating persisted digests, and `executescript` commits statement by statement, so a refused or failed open leaves schema changes behind. The native adapter validates first and applies the schema atomically (`MigrationParity`). | Decide whether to align the reference; until then the native behavior is the stricter one. |

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

## R-003 task entry: native C++ SQLite adapter

Status: **implemented locally, pending review**. Branch
`r003-native-sqlite-adapter` in a separate local clone (push disabled until
review). No other worker's checkout or branch was modified. Synthetic data only;
this adapter is not an encryption or key-management boundary (R-005).

### Implementation

- `core/persistence/`: `bundle_persistence` library (JSON with Python `json`
  semantics, SHA-256, timestamp normalization, archive reader/writer, CSV
  import port, SQLite store, safe-to-spend receipts) and the `bundle` CLI
  (`init`, `import-csv`, `import-batch`, `csv-to-batch`, `accounts`, `history`,
  `calculate`, `export`, `restore`, `verify-archive`, `help`).
- SQLite: the system library if found, else the 3.54.1 amalgamation pinned by
  its published SHA3-256. `bundle_core` gains no dependency.
- The migration SQL is embedded byte-for-byte (LF-normalized like Python's
  `read_text()`); no migration file is needed at run time.
- Every write runs in one `BEGIN IMMEDIATE` transaction; exports are written to
  a staging directory and renamed into place.

### Focused tests

| Suite | Command | Result |
| --- | --- | --- |
| Byte-level formats (JSON dumps, float repr, SHA-256, timestamps, CSV dialect, JSONL order) | `ctest --test-dir build -R bundle.persistence.formats` | Pass |
| Interrupted writes: child process killed at every statement of import (33 points), restore (66) and calculation (10); late in-process rollback; export killed after each of 9 files | `ctest --test-dir build -R bundle.persistence.recovery` | Pass; every kill recovered to the pre-write state with `integrity_check` ok |
| Mutation check: transaction begin/commit replaced with no-ops | manual (recorded in the change description) | Recovery test and both late-failure parity cases fail as required; original restored and re-verified |
| Python reference vs native parity (14 tests) | `python -m unittest discover -s tests/parity -v` | 12 pass; 2 expected failures are R-010 and R-011 asserted against the reference |

### End-to-end acceptance

| Check | Evidence | Result |
| --- | --- | --- |
| Same synthetic fixtures through Python and C++; persisted rows, event order and archives compared | `ImportParity`, `ArchiveParity` | Identical |
| Duplicate imports cannot double-count | `test_duplicate_imports_cannot_double_count`, CLI end-to-end | Pass |
| Malformed or tampered archives rejected without partial writes | 9 tamper/malformation cases, late-conflict restore, CLI tamper step | Pass |
| Interrupted writes, rollback, restart and recovery | `bundle.persistence.recovery` | Pass |
| Restore into a clean database; byte-stable re-export | `ArchiveParity`, CLI end-to-end | All 9 archive files byte-identical |
| Migration from supported older fixtures; originals unchanged | `MigrationParity` (SQL fixtures, SHA-256 checked before and after) | Pass |
| Native CLI: import -> history -> calculate -> export -> restart -> restore -> verify | `ctest --test-dir build -R bundle.cli.end_to_end`; `examples/native-sqlite/README.md` run verbatim | Pass |

### Remaining gaps (open)

- Not run in CI. The workflow file is outside this change: it is listed in
  `.gitignore` yet tracked, and another checkout has uncommitted edits to it.
- Parity run on CPython 3.12 only (CI uses 3.11).
- Pre-existing and unrelated: `connectors/tests/test_public_boundary.py` fails
  in any fresh clone (3 failures) because `scripts/check_public_boundary.py` is
  git-ignored.
- The public-boundary gate refuses committed `*.db`/`*.jsonl` files even for
  synthetic fixtures, so the fixtures here are SQL text and a generated example
  archive. Decision needed: keep this, or allowlist synthetic fixture paths in
  the gate.
- R-009, reported separately with nothing changed: in the main checkout the
  history check finds `.codex-remote-attachments/...` reachable only from a local
  Codex checkpoint ref (`refs/codex/turn-diffs/checkpoints/...`). The remote
  advertises only `main`, which does not reach those objects, and GitHub does not
  know the checkpoint commit. This is not proof that public history is clean.
