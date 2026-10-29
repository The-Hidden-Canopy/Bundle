# Bundle local interface

A local web interface for Bundle. It runs on this computer only and every figure
it shows comes from the native `bundle` engine; nothing is computed or invented
in the page.

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUNDLE_BUILD_TESTS=ON
cmake --build build --config Release
PYTHONPATH=ui python -m bundle_ui --db ~/bundle/finance.db --open
```

`--db` is created on first use. `--port` defaults to 8765 (`0` picks a free one);
`--native` (or `BUNDLE_NATIVE_CLI`) points at the `bundle` executable if it is not
in `build/`. Stop with Ctrl+C and start again with the same `--db` to reopen your
data.

## How it is put together

- `bundle_ui/backend.py` is the narrow adapter. Every read and write runs the
  native CLI: `snapshot` (read-only view), `calculate --json [--no-record]
  [--what-if-*]`, `csv-to-batch`, `import-batch [--dry-run]`, `export`,
  `verify-archive`, `restore`. OFX/QFX files are parsed by the Python reference
  parser and admitted through the native `import-batch`.
- `bundle_ui/server.py` is a stdlib HTTP server bound to `127.0.0.1`. It refuses
  other `Host` headers, requires a per-session token on `/api/*`, sends a strict
  Content-Security-Policy and does not log requests.
- `bundle_ui/static/` is the page: plain HTML, CSS and JavaScript, no build step
  and no third-party code.
- `bundle_ui/demo.py` builds the synthetic demo in a separate database
  (`bundle-demo/demo.db` next to yours), through the same import and restore paths.

## What it does

| Area | Implemented | Not implemented (stated in the interface) |
|---|---|---|
| Overview | Safe-to-spend for 7-90 days in the Confirmed, Expected and Stress views, the full breakdown, a consistency check (safe = lowest projected balance - floor), the projected-balance chart with a table alternative, upcoming commitments marked counted or not, data freshness | |
| Activity | Transactions with category, account, source format, import date and state; transfers and refunds matched by the reconciliation engine and kept out of spending | Matching CSV/OFX rows: those formats carry no posted time, so their rows are never matched (the page says so) |
| Accounts | Observed balances with observation time, source and import history; warnings for no balance, source-marked stale, and older than 7 days | Live balances, bank linking, synchronization |
| Plan | Protected goals (seed jar), commitments with confidence, budgets as recorded, forecast, what-if purchase (compute only) | Editing budgets, goals or commitments: the backend has no editing API, so plans arrive in a `bundle-finance-1` plan or backup |
| Import & backup | CSV, OFX, QFX and bundle-connector-1 previews checked by a rolled-back trial import; duplicate files change nothing; later statements continue the account's source as the next version and leave out rows already stored unchanged; export with verification, verify, restore (checked first, all-or-nothing) | Applying corrections to already-imported rows from a statement file (such files are refused, nothing written); encryption |
| Protected cash floor | Saved as an input of an append-only calculation receipt, so it survives restarts and travels with backups | |

The database and backups are plain local files and are **not encrypted**
(risk-register R-005).

## Tests

```text
BUNDLE_NATIVE_CLI=build/core/persistence/bundle python -m unittest discover -s tests/ui -v
```

`tests/ui` (24 tests) covers failed import and retry, duplicate and overlapping
statements, repeated commits, stale observations, an empty workspace,
calculation consistency against the native receipt, the confidence contract for
each view, transfer/refund classification, export/verify/restore including a
damaged backup, restart persistence, and the server's token, host and CSP rules.
`BUNDLE_PARITY_REQUIRE_NATIVE=1` turns a missing binary into a failure (CI).
