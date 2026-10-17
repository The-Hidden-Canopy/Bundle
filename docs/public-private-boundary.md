# BUNDLE public/private boundary

BUNDLE's public repository is the standalone financial core. It must remain
useful without a Hidden Canopy account, a hosted service, a model provider,
mandatory telemetry, or a bank credential.

## Public foundation

The public tree may contain:

- money, currency, source, account, balance, transaction, and planning types;
- deterministic projections, forecasts, scenarios, and safe-to-spend math;
- local CSV and OFX/QFX file import;
- the bounded connector manifest and archive schemas;
- local SQLite admission and archive restore with event-bearing writes;
- tests, fixtures, and documentation that do not contain personal financial
  records or provider secrets.

These surfaces are local capabilities. A file importer is not a bank
connection, a forecast is not financial advice, and an archive is not a
provider backup unless the corresponding capability is implemented and
separately evidenced.

## Private or out-of-tree adapters

The following do not belong in the public foundation:

- bank OAuth, access tokens, refresh tokens, or institution-specific SDKs;
- hosted synchronization, account identity, organization policy, or Hub
  authority;
- payment execution, write-capable provider APIs, or credential brokers;
- proprietary model providers, prompts, ranking, personalization, or private
  financial telemetry;
- customer data, exports, databases, logs, and deployment configuration.

Those capabilities may live in a separate private adapter or service. They
must consume a bounded public contract and may not make the public C++ core
depend on a network, credential, or hosted database.

## Release checks

Before staging a public change, run:

```text
python scripts/check_public_boundary.py
python scripts/check_public_boundary.py --history
```

The checker rejects private/generated paths, nested repositories, machine
paths, token-shaped credentials, private keys, and the ignored `BUNDLE/`
source snapshot. `.gitignore` is only a forward guard: any previously tracked
private material still requires history remediation and credential rotation.

The ordinary CI gate checks the current public tree. History mode is a
publication gate and must be clean before creating a new public baseline or
advertising a repository as history-cleared.

The public checkout may keep an ignored local source snapshot for development,
but it is not part of the public repository and must not be force-added.
