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
| R-003 | The native C++ core and the SQLite persistence layer diverge. | Closed for this slice | `core/persistence/` implements a native C++ SQLite adapter over the same migration and archive format; the `bundle` CLI uses it. `tests/parity/` (14 tests) compares the Python bridge and the native CLI on shared synthetic fixtures: identical rows, event order and byte-identical archives, rejections without writes, tamper detection, older-schema fixtures and calculation receipts. Crash-at-every-statement recovery and CLI end-to-end tests run in CTest. GitHub Actions run 40931227786 on `df8600e` (Python 3.11.17 and 3.12.15): CTest 10/10, parity 14/14 in strict mode (a missing native binary fails instead of skipping), connector tests 31/31. Locally on Windows: parity 14/14 on CPython 3.11.9 and 3.12.10. Three Python bridge defects found by the suite are fixed (R-010, R-011, R-013). | Keep parity in CI in strict mode. Bounded: CI links the runner's system SQLite (3.45.1); the pinned-amalgamation path (3.54.1) is verified only by local Windows builds. Not an encryption boundary (R-005). |
| R-004 | Mobile or MAUI behavior is implied by the portable financial core. | Bounded | The README and architecture docs explicitly state that phone UI and MAUI runtime behavior are not represented by current evidence. | Require a real mobile build, device/emulator run, and retained receipt before claiming mobile capability. |
| R-005 | Local financial data is treated as encrypted or securely stored without a key-management design. | Open | No encryption-at-rest, key storage, rotation, recovery, or deletion contract is implemented in this slice. | Specify and test a local storage security boundary before storing sensitive data outside the current test fixtures. |
| R-006 | Provider connectivity, live balances, payment execution, or financial advice is inferred from file import and deterministic forecasts. | Bounded | CSV and OFX/QFX are local file parsers; no credentials, network calls, payment rail, or adviser authority is present. | Treat each provider/payment surface as a separate read-only, credential-scoped, receipt-producing capability with explicit authorization tests. |
| R-007 | Locally passing code is promoted without the exact source, tests, and exclusions being reviewable. | Closed for this slice | Changes since `b6ce92d` were committed explicitly, checked locally (CTest, connector and parity suites, `scripts/check_public_boundary.py`), pushed to `main` as fast-forwards, and the remote SHA and CI result were verified (GitHub Actions run 40931227786 on `df8600e` (Python 3.11.17 and 3.12.15)). | Repeat the same review, push and remote-verification steps for each later change. |
| R-008 | The public remote accepts the source but does not continuously re-run the boundary tests. | Closed for this slice | `.github/workflows/ci.yml` runs on every push and pull request on Python 3.11 and 3.12: an explicit Release configure (verified in `CMakeCache.txt`), CTest, the NDEBUG negative control, the Python connector tests (including the public-boundary checker tests), the native parity suite in strict mode, and schema parsing. GitHub Actions run 40931227786 on `df8600e` (Python 3.11.17 and 3.12.15) passed every step. | History-mode boundary checking is not run in CI (shallow checkout); that remains R-009. |
| R-009 | Public history exposes excluded attachment or legacy material. | Open | `python scripts/check_public_boundary.py --history` currently reports reachable `.codex-remote-attachments/...` objects. The current working tree passes, but history is not clean. | Create a reviewed clean public baseline or complete an authorized history rewrite and secret audit before describing the repository as history-cleared. Do not delete or rewrite the current ancestry implicitly. |
| R-010 | Restoring the same archive twice silently duplicates rows in the Python bridge. | Closed for this slice | `_insert_or_verify` matches keys with `=`, so `commitment_evidence` rows with a NULL `related_transaction_id` are inserted again on every restore (3 restores produce 3 copies). Fixed: both paths match keys with `IS`. Covered by `test_repeated_restore_does_not_duplicate_null_keyed_evidence` (fails on the previous code) and `tests/parity` `test_A_*`. | Keep the regression tests. |
| R-011 | The Python bridge cannot restore an archive it exported. | Closed for this slice | Text containing raw U+2028, U+2029 or U+0085 is written unescaped (`ensure_ascii=False`) but `read_export` splits JSONL with `str.splitlines()`, which breaks the record. Fixed: both readers split JSONL on newline only. Covered by `test_archive_round_trips_text_with_unicode_line_separators` (fails on the previous code) and `tests/parity` `test_B_*`. | Keep the regression tests. |
| R-012 | The Release-mode C++ test gate checks nothing. | Closed for this slice | The 144 `assert()` checks in `core/tests/unit/*.cpp` (several of which also performed test setup, so their scenarios did not run at all under `NDEBUG`) are replaced by `CHECK` from `core/tests/support/bundle_test/check.hpp`, which never depends on `NDEBUG`. Verified in a single-config Ninja build with `CMAKE_BUILD_TYPE=Release` (`-O3 -DNDEBUG` on every test target): CTest 10/10; a deliberately broken check in `test_store.cpp` failed the suite and was reverted; the separate negative control `bundle_check_negative_control` (not registered in CTest) exits 1 with `CHECK failed` after showing `assert(false)` is inert. CI configures `CMAKE_BUILD_TYPE=Release` explicitly and runs the negative control on every push (GitHub Actions run 40931227786 on `df8600e` (Python 3.11.17 and 3.12.15)). | Write new C++ tests with `CHECK`, not `assert()`. |
| R-013 | Opening a database that is then refused still alters it. | Closed for this slice | The Python bridge applies the migration (new tables, triggers, WAL mode) before validating persisted digests, and `executescript` commits statement by statement, so a refused or failed open leaves schema changes behind. Fixed: both paths validate persisted digests before migrating and apply the schema in one transaction. Covered by `test_refused_open_leaves_the_database_file_untouched` (fails on the previous code) and `MigrationParity`. | Keep the regression tests. |
| R-014 | A later statement for an account already imported from CSV or OFX/QFX cannot be admitted. | Closed for this slice | Accounts belong to one source series, and a later statement must continue it with a higher `source_version`. The CSV converters (Python reference and native) and the OFX/QFX parser put the version on every record but not on the account, so any version above 1 was refused with "mixed account source identities" on both paths. Fixed in both; version 1 output is unchanged, so stored batch identities stay valid. Covered by `tests/parity` `test_later_csv_statement_continues_the_source_as_the_next_version` and `tests/ui` `test_later_overlapping_statement_adds_only_new_rows`. | Corrections to already-stored rows from a statement file (for example pending to posted with a new amount) are still refused: the file formats carry no revision, and no correction contract exists yet. |
| R-015 | The local interface misrepresents data or exposes it beyond this computer. | Bounded | `ui/` reads and writes only through the native CLI; the server binds 127.0.0.1, refuses other `Host` headers, requires a per-session token and sends a strict CSP. Calculations shown are native receipts (checked equal in `tests/ui`); plans are shown read-only; the demo is synthetic and kept in a separate database. 24 tests in `tests/ui`, plus a scripted headless-browser walk-through of every page on synthetic data. | Not encrypted (R-005). Not tested with assistive technology or across browsers beyond Chromium-based Edge; accessibility is by construction (labels, focus order, reduced motion, text alongside every graphic), not audited. |

## Evidence recorded for the current slice

Remote: GitHub Actions run 40931227786 on `df8600e` (Python 3.11.17 and 3.12.15) passed on both Python versions:

- explicit Release configure (`CMAKE_BUILD_TYPE=Release`), C++20 build and 10
  CTest cases: the seven core suites (money, provenance, store, planning,
  budgets, activity, reconciliation) plus native persistence formats,
  interrupted-write recovery and the CLI end-to-end flow;
- the NDEBUG negative control exits 1 with `CHECK failed`;
- 31 Python connector, SQLite and public-boundary tests;
- 14 native parity tests in strict mode;
- JSON schema parsing.

Local (Windows, MinGW GCC, Ninja, Release, pinned SQLite amalgamation 3.54.1):
the same suites pass on CPython 3.11.9 and 3.12.10, and the public-boundary
checker passes on the working tree.

These checks establish source, local-persistence and CI behavior only. They do
not establish provider freshness, encryption, mobile runtime behavior or
payment execution.

## Change protocol

When a risk changes status, update the row and the evidence in the same commit
as the code or test change that caused the status transition. Never mark a risk
closed solely because a unit test passes if the stated runtime or promotion
boundary remains unverified.

## R-003 task entry: native C++ SQLite adapter

Status: **closed for this slice**. On `main`: `1fc87a8` (adapter, CLI, tests,
docs), `213babf` (Python bridge fixes R-010, R-011, R-013), `e56458a` (boundary
checker tracked), `df8600e` (R-012, explicit Release, parity in CI). Synthetic
data only; this adapter is not an encryption or key-management boundary (R-005).

### Implementation

- `core/persistence/`: `bundle_persistence` library (JSON with Python `json`
  semantics, SHA-256, timestamp normalization, archive reader/writer, CSV
  import port, SQLite store, safe-to-spend receipts) and the `bundle` CLI
  (`init`, `import-csv`, `import-batch`, `csv-to-batch`, `accounts`, `history`,
  `calculate`, `export`, `restore`, `verify-archive`, `help`).
- SQLite: the system library if found (CI: 3.45.1), else the 3.54.1
  amalgamation pinned by its published SHA3-256 (local Windows builds).
  `bundle_core` gains no dependency.
- Every write runs in one `BEGIN IMMEDIATE` transaction; exports are written to
  a staging directory and renamed into place; a refused open leaves the file
  untouched.

### Focused tests

| Suite | Command | Local | CI (run 40931227786) |
| --- | --- | --- | --- |
| Byte-level formats | `ctest --test-dir build -R bundle.persistence.formats` | Pass | Pass (3.11, 3.12 jobs) |
| Interrupted writes: kill at every statement of import (33), restore (66), calculation (10); late rollback; export killed after each of 9 files | `ctest --test-dir build -R bundle.persistence.recovery` | Pass | Pass |
| Mutation check: transaction begin/commit replaced with no-ops | manual | Recovery test and both late-failure parity cases failed as required; original restored | not run in CI |
| Python bridge regressions for R-010, R-011, R-013 | `python -m unittest discover -s connectors/tests -v` | 31/31 on 3.11.9 and 3.12.10 | 31/31 on 3.11.17 and 3.12.15 |
| Python reference vs native parity | `BUNDLE_PARITY_REQUIRE_NATIVE=1 python -m unittest discover -s tests/parity -v` | 14/14 on 3.11.9 and 3.12.10 | 14/14 on 3.11.17 and 3.12.15 |
| Strict mode guard | same, with `BUNDLE_NATIVE_CLI` pointing to a missing file | `FAILED (errors=14)`; without strict mode `OK (skipped=14)` | CI always runs strict and rejects any `skipped=` |

### End-to-end acceptance

| Check | Evidence | Result |
| --- | --- | --- |
| Same synthetic fixtures through Python and C++; persisted rows, event order and archives compared | `ImportParity`, `ArchiveParity` | Identical (local and CI) |
| Duplicate imports cannot double-count | `test_duplicate_imports_cannot_double_count`, CLI end-to-end | Pass |
| Malformed or tampered archives rejected without partial writes | 9 tamper/malformation cases, late-conflict restore, CLI tamper step | Pass |
| Interrupted writes, rollback, restart and recovery | `bundle.persistence.recovery` | Pass |
| Restore into a clean database; byte-stable re-export | `ArchiveParity`, CLI end-to-end | All 9 archive files byte-identical |
| Migration from supported older fixtures; originals unchanged | `MigrationParity` (SQL fixtures, SHA-256 checked before and after) | Pass |
| Native CLI: import -> history -> calculate -> export -> restart -> restore -> verify | `ctest --test-dir build -R bundle.cli.end_to_end`; `examples/native-sqlite/README.md` run verbatim | Pass |

### Remaining limits

- The pinned SQLite amalgamation path is exercised only by local Windows
  builds; CI uses the runner's system SQLite.
- The transaction mutation check was a one-off local run, not a CI job.
- The public-boundary gate refuses committed `*.db`/`*.jsonl` files, so the
  fixtures are SQL text and a generated example archive.
- R-009 is unchanged: in the main checkout the history check finds
  `.codex-remote-attachments/...` reachable only from a local Codex checkpoint
  ref; the remote advertises only `main`, which does not reach those objects,
  and GitHub does not know the checkpoint commit. This is not proof that public
  history is clean.
