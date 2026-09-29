# Privacy boundary

The default BUNDLE posture is local database, local calculations, local insight
generation, and local export.

The core does not send transaction history, account identifiers, balances,
merchant history, goals, or budget data to a model provider. If a future
optional remote model feature exists, the data class leaving the device must be
shown before use.

Connector credentials belong in platform secure storage. The local finance
database stores a credential reference rather than a raw provider secret when
the platform offers that facility. Custom cryptography is not part of BUNDLE.
