# BUNDLE

BUNDLE is an open-source, local-first personal finance application that brings
balances, transactions, recurring commitments, budgets, goals, forecasts, and
safe-to-spend into one explainable view.

The project is released by The Hidden Canopy LLC under Apache License 2.0 and
follows the [Open Canopy Contract](OPEN_CANOPY_CONTRACT.md). The core must be
useful without a BUNDLE account, a Hidden Canopy service, mandatory telemetry,
cloud AI, or a hosted database.

## Current status

This checkout begins the first domain slice from the financial engineering
specification:

- integer minor-unit money with explicit currency codes;
- checked same-currency arithmetic;
- source provenance records;
- accounts and balance observations with freshness and history fields;
- transaction revisions with explicit state;
- UTC timestamp and validation boundaries;
- append-only canonical-store and domain-event behavior;
- append-only calculation receipts with input-hash traceability;
- deterministic commitments, budget projections, forecasts, safe-to-spend,
  scenarios, and insight boundaries;
- transfer/refund relationship recognition without rewriting transaction truth;
- read-only activity projections with deterministic merchant normalization,
  user-alias precedence, revision collapse, and adversarial filters;
- strict CSV and dependency-free OFX/QFX file import with explicit timezone and
  decimal minor-unit validation;
- bounded read-only connector manifests;
- hash-verified `bundle-finance-1` export/import;
- executable SQLite migration schema with append-only history triggers;
- transactional Python SQLite admission for connector batches, with durable
  idempotency, full-batch source hashing, and event-bearing writes;
- transactional `bundle-finance-1` archive export/restore for canonical and
  represented planning records, with refusal on unrepresented tables;
- dependency-free C++20 build and unit tests.

This is local source/unit and persistence-boundary evidence only. It is not a
bank connector, financial adviser, payment rail, live-balance service, or
finished mobile application. No external network calls, credentials,
telemetry, or model provider are used by the current core or the SQLite
admission bridge.

## Build and test

From a clean checkout with CMake 3.20 or newer and a C++20 compiler:

```powershell
cmake -S . -B build -DBUNDLE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure

$env:PYTHONPATH = "connectors"
py -3 -m unittest discover -s connectors/tests -v
```

The financial core is deliberately kept independent of commercial connector
SDKs and UI frameworks. The SQLite migration schema, Python file-import tools,
and a tested local Python admission bridge are present. A native C++ SQLite
adapter and phone UI remain later layers; neither is represented as complete
by the current evidence.

## Governance boundaries

- Money is never represented as binary floating-point.
- Cross-currency arithmetic is rejected unless an explicit exchange-rate
  observation is introduced by a later bounded layer.
- Financial objects retain source references; projections must not replace
  source history.
- Stale and unavailable states are explicit and cannot be silently presented as
  fresh data.
- AI and Riptide are not part of this core and will not receive mutation
  authority.
- Provider connectors will be read-only by default and optional.

The authoritative design is the supplied BUNDLE financial engineering
specification. The implementation intentionally stops before external bank
connectors, payment execution, mobile UI, and AI explanation. Forecasting and
local file-based import/export are deterministic core capabilities; they do not
claim live provider behavior. OFX/QFX parsing accepts statement files supplied
by the user; it does not authenticate to, contact, or represent a bank.

## License

Apache License 2.0. See [LICENSE](LICENSE), [NOTICE](NOTICE), and the
[Open Canopy Contract](OPEN_CANOPY_CONTRACT.md).
