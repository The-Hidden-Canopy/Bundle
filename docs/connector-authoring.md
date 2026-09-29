# Connector authoring

Connectors are optional adapters. They emit bounded JSON matching
`bundle-connector-1`; they do not own budgets, goals, forecast logic, or
external payment authority.

Every connector must declare:

- a stable identifier and version;
- explicit read-only capabilities;
- whether a network is required;
- `writes_external_state: false`.

Network-capable connectors are not part of the core build. Manual, CSV, OFX,
QFX, JSON, and BUNDLE-export inputs remain valid without a commercial provider.
Connector output enters source validation and idempotent admission before it
can affect canonical projections.

The current SQLite bridge rejects non-empty `external_commitments` until an
explicit planning admission path exists; it never accepts and drops those
records silently.

If a previously admitted source object changes, the connector must make the
change explicit: advance `source_version`, provide the next transaction
`revision`, and identify the revision it supersedes. Reusing the same source
ID and version with different batch content is rejected; an older source
version cannot move an account projection backward.

The OFX/QFX reader accepts a statement stream and emits the same bounded batch
shape as CSV. It uses exact decimal conversion to currency minor units,
preserves FITID as the external object identifier, rejects duplicate FITIDs,
and requires an explicit caller timezone when the statement omits an embedded
offset. It never stores credentials or opens a network connection.
