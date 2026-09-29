# BUNDLE architecture

BUNDLE follows the Open Canopy Contract and the financial engineering
specification. The portable core owns canonical money, provenance, history,
planning, forecast, safe-to-spend, and insight calculations.

## Authority direction

```text
input / connector
        |
        v
source reference -> canonical store -> deterministic projections
                                      |
                                      +-> forecast
                                      +-> safe-to-spend
                                      +-> insights
                                      +-> scenarios
```

No UI, connector, model, or Riptide presentation layer writes financial truth
directly. The C++ store is an append-only in-memory reference boundary. The
tested Python SQLite admission bridge is a local persistence adapter over the
same migration: it admits a complete connector batch transactionally, binds
the source identity to the complete batch hash, preserves idempotent replays,
and emits event-log entries. A native C++ SQLite adapter remains a separate
later layer.

The Python bridge can export and restore the represented `bundle-finance-1`
slice through the archive format: accounts, balances, transactions,
commitments and evidence, budgets and allocations, goals and funding plans,
user overrides, and calculation receipts. Restore is transactional and
idempotent. It refuses to export when unsupported persisted tables contain
rows, which prevents a partial backup from being presented as complete.

Activity is a read-only projection over the latest transaction revision for
each account/source-transaction pair. Merchant normalization and aliases can
change presentation and search, but they cannot change amount, date, state, or
revision history. Conflicting user-confirmed aliases remain unresolved.

## Evidence boundary

The current build proves source/unit behavior and the tested local Python
SQLite admission boundary. It does not prove bank connectivity, fresh
provider balances, financial advice, mobile behavior, native C++ SQLite
integration, payment execution, or model/Riptide behavior. The OFX/QFX reader
is a local file parser, not a provider connector.
