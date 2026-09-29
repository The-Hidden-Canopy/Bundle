"""Optional, bounded desktop connector helpers for BUNDLE."""

from .protocol import ConnectorBatch, ConnectorContractError, ConnectorManifest
from .ofx_import import import_ofx
from .sqlite_store import SQLiteFinanceStore, SQLiteStoreError

__all__ = [
    "ConnectorBatch",
    "ConnectorContractError",
    "ConnectorManifest",
    "SQLiteFinanceStore",
    "SQLiteStoreError",
    "import_ofx",
]
