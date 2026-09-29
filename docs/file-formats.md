# BUNDLE file formats

## `bundle-finance-1` export

An export is a directory containing:

```text
bundle-finance-1/
  manifest.json
  accounts.json
  balances.jsonl
  transactions.jsonl
  commitments.json
  budgets.json
  goals.json
  overrides.jsonl
  receipts.jsonl
```

`manifest.json` records the UTC creation time and SHA-256 digest and byte count
for every data file. Export creation refuses to overwrite an existing
destination. Import rejects path traversal, missing files, hash mismatches, and
unsupported schemas. JSONL records are ordered by their stable object
identifier so equivalent exports have identical bytes; malformed manifests or
payloads fail through the connector contract rather than leaking parser
exceptions.

The current Python SQLite bridge can export and restore accounts, balances,
transactions, commitments with nested evidence, budgets with allocations,
goals with funding plans, user overrides, and calculation receipts through
this format. Restore is transactional and idempotent; if the database
contains tables not represented by the archive bridge, export refuses to
proceed rather than silently dropping records. Audit events are regenerated
from restored records, including planning and receipt events.

The export path rejects secret-looking fields such as passwords, credentials,
access tokens, and private keys. Provider credentials do not belong in the
finance archive.

## Canonical CSV import

The first dependency-free CSV path requires these columns:

```text
account_id,account_name,account_type,currency,transaction_id,
amount_minor,direction,state,description,observed_at
```

`amount_minor` is an exact non-negative integer. `observed_at` must contain an
explicit timezone. The importer rejects duplicate transaction IDs, mixed
currencies within an account, unsupported states, and guessed local timestamps.

## OFX/QFX input

OFX and QFX statement files are accepted through the local `bundle-ofx-import`
entry point. Signed decimal transaction values are converted with `Decimal`
using the currency exponent; binary floating point is not used. FITID values
become stable source transaction identifiers and are retained in the source
reference. A statement timestamp without an embedded offset is rejected unless
the caller supplies an explicit source timezone. Duplicate FITIDs and excess
currency precision are rejected.
