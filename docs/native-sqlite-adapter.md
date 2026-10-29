# Native C++ SQLite adapter (R-003)

`core/persistence/` is a native C++ port of the Python SQLite bridge
(`connectors/bundle_connectors/sqlite_store.py`, `archive.py`, `csv_import.py`)
over the same migration, `core/persistence/migrations/001_finance.sql`. The
`bundle` executable is the native application path that uses it. The Python
bridge remains the reference implementation and is unchanged.

This adapter is local persistence only. It does not encrypt data at rest, manage
keys, contact a network, or secure real financial data (see R-005). Use
synthetic data.

## Build and run

Requirements: CMake 3.20+, a C++20 compiler. SQLite comes from the system if
`find_package(SQLite3)` finds it; otherwise CMake downloads the official
amalgamation (3.54.1) and verifies its published SHA3-256 before use. For an
offline or shared build cache, point it at a local copy of the same zip:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUNDLE_BUILD_TESTS=ON
  [-DBUNDLE_SQLITE_AMALGAMATION_URL=/path/to/sqlite-amalgamation-3540100.zip]
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is `build/core/persistence/bundle` (`bundle.exe` on Windows;
with multi-config generators it is under `Release/`). `bundle help` lists every
command; `examples/native-sqlite/README.md` walks through a full session.
`-DBUNDLE_BUILD_NATIVE_SQLITE=OFF` builds the core without SQLite.

Library layout: `bundle_persistence` depends on `bundle_core` and SQLite;
`bundle_core` gains no new dependency.

Read-only and compute-only commands used by the local interface (`ui/`):
`snapshot` prints a `bundle-snapshot-1` JSON view (accounts, current
transactions with transfer/refund classification, balances, plan records,
receipts, import batches); `calculate --json` adds the day-by-day forecast,
`--no-record` computes without storing a receipt and `--what-if-minor/-date
[-account]` evaluates a one-off purchase (never stored); `import-batch
--dry-run` / `import-csv --dry-run` run the full admission in a transaction
that is rolled back and print the batch ID and the events it would add. These
are exercised by `tests/ui`; they have no Python-reference counterpart.

## Contract and evidence matrix

| Contract (Python reference) | Native behavior | Evidence |
| --- | --- | --- |
| Migration applied on open; persisted digests validated; malformed legacy hashes refuse to open | Same verdicts. Validation runs read-only *before* migrating and schema statements apply in one transaction (finding C). | `MigrationParity` (older SQL fixtures, originals hash-checked), Python's legacy-hash cases |
| `append_batch`: full-batch SHA-256 source identity, deterministic batch ID, account/balance/transaction admission, revision chains, conflict refusal, event IDs | Statement-for-statement port; identical rows and `event_log` order | `test_import_sequence_persists_identical_rows_and_event_order`, CSV/batch fixtures with non-ASCII text, offsets, fractions, revisions, stale balances |
| Duplicate import is a no-op; changed content under a reused source identity and older source versions are rejected | Same; re-admission reports "nothing changed" | `test_duplicate_imports_cannot_double_count`, `test_rejected_batches_match_reference_and_write_nothing` (incl. a late failure after writes) |
| Every write in one `BEGIN IMMEDIATE` transaction; errors roll back | Same; interrupted at every statement, each kill recovers to the pre-write state | `bundle.persistence.recovery` (33 import, 66 restore, 10 calculation kill points), mutation check in the task entry |
| `bundle-finance-1` export: file set, ordering, canonical JSON, manifest, secret-field refusal, unrepresented tables refused | Byte-identical archives; written to a staging directory and renamed, so an interrupted export never appears at the destination | `test_exports_are_byte_identical_and_restore_cross_compatibly`, recovery test (export killed after each file) |
| Restore: manifest/size/SHA-256 checks, conflict-against-local-data detection, idempotent, regenerated events | Same verdicts and rows (except finding A) | `test_malformed_and_tampered_archives_are_rejected_without_writes` (9 tamper/malformation cases + late conflict), CLI end-to-end |
| Calculation receipts: append-only, `sha256:` input hash, archive form regenerates the same event | Native computes safe-to-spend with the existing C++ planning engine and records the receipt keyed by its input hash; identical inputs record once | `CalculationParity` (independent arithmetic check, Python restores and re-exports native receipts byte-identically) |
| CSV import (`csv_import.py`) | Same batch document (excel dialect, `str.strip()`, `int()` with `_`) | `test_csv_converts_to_the_identical_connector_batch`, `bundle.persistence.formats` |
| Later statement (`source_version` > 1) | Same batch document; the account carries the version too (R-014) | `test_later_csv_statement_continues_the_source_as_the_next_version` |

The Python bridge has no calculation engine, so "calculation parity" means: the
native result is checked against an independently derived expected value for
the fixture, recorded deterministically, and round-trips through the Python
restore/export path unchanged. It is not a comparison of two calculators.

## Deliberate differences

Rows A-C were bugs in the Python bridge found by the parity suite; they are
fixed in both implementations (R-010, R-011, R-013), so both now behave as the
right-hand column describes. The remaining rows are fail-closed narrowings of
the native path.

| Area | Python reference | Native |
| --- | --- | --- |
| A. Restore of commitment evidence with `related_transaction_id = NULL` | Previously matched keys with `=`, never finds the NULL row, inserts a duplicate on every restore (3 restores -> 3 copies), contrary to the documented idempotency | Matches keys with `IS`; restore is idempotent |
| B. JSONL with raw U+2028/U+2029/U+0085 in text | Previously wrote them raw (`ensure_ascii=False`) but reads with `str.splitlines()`, so it cannot restore its own export | Splits on `\n` only; restores it |
| C. Opening a database that is then refused | Previously migrated (tables, triggers, WAL) before validating, so the refused file has been altered; `executescript` commits statement by statement | Validates first, migrates atomically; a refused file is untouched |
| Timestamps | `datetime.fromisoformat` (very permissive) | Supported subset: `YYYY-MM-DD`/`YYYYMMDD`, `T` or space, `HH[:MM[:SS[.f]]]` or `HHMM[SS]`, `Z`/`±HH`/`±HHMM`/`±HH:MM[:SS[.f]]`. Output text identical. ISO week dates, other separators and `HH:MM:SS.` (no digits) are rejected |
| Case folding (`str.lower()`/`upper()` on account type, currency, status, direction, state) | Unicode-aware | ASCII; non-ASCII input in these fields is rejected |
| `str(value)` on JSON containers (e.g. a dict as `display_name`) | Stores the Python repr | Rejected |
| JSON integers beyond 64 bits; lone surrogate escapes | Accepted (arbitrary ints; lone surrogates crash later on export) | Rejected with an explicit error |
| Year range after UTC conversion outside 1..9999 | `OverflowError` escapes uncaught | Explicit `out of range` error |

Observed reference behavior reproduced natively (not a divergence, recorded
because it surprised the test author): when a batch both creates an account and
reports its balance, the account's placeholder `balance_observed_at` is the
batch source's `observed_at`, so a balance observed earlier in the same batch is
stored but never projected onto the account.

## Running the parity suite

```text
python -m unittest discover -s tests/parity -v
```

It needs the built `bundle` executable (or `BUNDLE_NATIVE_CLI=<path>`) and
CMake on `PATH` for the example-archive check. The `ReferenceFindings` tests
assert the documented contract on both paths for findings A and B.

Older-schema fixtures are committed as SQL text under
`tests/parity/fixtures/legacy/` because the public boundary gate refuses binary
`*.db` files; `make_legacy_fixtures.py` documents how they were produced.

## Known gaps

- CI builds against the runner's system SQLite (3.45.1); the pinned
  amalgamation path (3.54.1) is exercised only by local builds.
- JSON Schema files under `schemas/` are not validated natively; the batch shape
  check mirrors `ConnectorBatch` and the schema's required/extra keys only.
- Income streams are not read by the native calculation; it refuses to compute
  when `income_streams` has rows instead of silently ignoring them.
- No encryption at rest or key management (R-005).
