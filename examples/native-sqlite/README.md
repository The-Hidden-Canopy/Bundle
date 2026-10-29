# Native SQLite example (synthetic data)

A complete session with the native `bundle` executable: import a CSV statement,
add balances, restore planning records, inspect history, calculate
safe-to-spend, export, restore into a clean database and verify. All records are
synthetic. The database is a plain local SQLite file and is not encrypted.

Files:

- `statement.csv` - canonical BUNDLE CSV (two accounts, four transactions)
- `balances.json` - a `bundle-connector-1` batch with two balance observations
- `planning-records/` - two commitments and one protected goal
- `make-planning-archive.cmake` - packs those records into a `bundle-finance-1` archive

## 1. Build

From the repository root:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUNDLE_BUILD_TESTS=ON
cmake --build build --config Release
```

Use `build/core/persistence/bundle` (or `bundle.exe`; with Visual Studio
generators, `build/core/persistence/Release/bundle.exe`). Below it is written as
`bundle`. Run `bundle help` for every command and option.

## 2. Session

```text
bundle import-csv --db demo.db --source-id example-csv-2026-01 --imported-at 2026-01-04T12:00:00Z examples/native-sqlite/statement.csv
bundle import-csv --db demo.db --source-id example-csv-2026-01 --imported-at 2026-01-04T12:00:00Z examples/native-sqlite/statement.csv
bundle import-batch --db demo.db examples/native-sqlite/balances.json
cmake -DOUT=planning -P examples/native-sqlite/make-planning-archive.cmake
bundle restore --db demo.db planning
bundle accounts --db demo.db
bundle history --db demo.db
bundle calculate --db demo.db --as-of 2026-01-04T12:00:00Z --horizon-end 2026-02-28T00:00:00Z --floor-minor 20000
bundle export --db demo.db --created-at 2026-01-06T00:00:00Z backup
bundle restore --db restored.db backup
bundle verify-archive backup
```

What to expect:

- The second `import-csv` prints `nothing changed`: an identical batch is never counted twice.
- `accounts` shows checking at 143125 current / 141250 available and savings at 50000, both `fresh`.
- `history` lists every append in the order it was recorded (sources, accounts,
  transaction revisions, balance projections, planning records, import batches).
- `calculate` prints `safe_to_spend_minor 42250 USD`:
  191250 available - 95000 rent - 2 x 4500 phone - 25000 protected goal - 20000 floor.
  The result is stored as a calculation receipt; running it again prints
  `already recorded` instead of adding a second receipt.
- `export` refuses a destination that already exists.
- `restore` into the new database is all-or-nothing and can be repeated safely;
  exporting `restored.db` again produces byte-identical archive files.

## 3. Errors you may see

| Message | Meaning |
| --- | --- |
| `bundle: <command> requires --db` | A required option is missing (exit code 2). |
| `must be RFC 3339 with an explicit offset` | Times need `Z` or `+HH:MM`, e.g. `2026-01-04T12:00:00Z`. |
| `rejected: ... conflicts with existing local data` | The archive or batch disagrees with what is already stored; nothing was written (exit 1). |
| `rejected: hash or size mismatch for <file>` | The archive was altered after export; nothing was written. |
| `rejected: source conflict for ...` | Same source ID and version, different content: re-export from the source with a new `source_version`. |
| `rejected: ... received an older source version` | A newer version of that account's source is already stored. |
| `rejected: bundle-finance-1 archive cannot represent non-empty table ...` | The database holds rows the archive format cannot carry, so export stops rather than producing an incomplete backup. |

Exit codes: 0 ok, 1 rejected (database unchanged), 2 usage error, 3 internal error.

The same flow runs automatically as the `bundle.cli.end_to_end` CTest.
