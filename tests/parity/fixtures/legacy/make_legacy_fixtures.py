"""Regenerates the committed older-schema fixtures (synthetic data only).

    python tests/parity/fixtures/legacy/make_legacy_fixtures.py

Each fixture is committed as SQL text (sqlite3 iterdump) because the public
boundary gate refuses binary *.db files. Tests materialize a fresh database
from the SQL, never write to the .sql files, and check their SHA-256 before and
after. Rerun this only when intentionally replacing a fixture.
"""

from __future__ import annotations

import sqlite3
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1]))

from harness import build_python_reference  # noqa: E402

VALID_HASH = "sha256:" + "0" * 64


def _fresh(name: str) -> Path:
    path = HERE / name
    for suffix in ("", "-wal", "-shm"):
        Path(str(path) + suffix).unlink(missing_ok=True)
    return path


def v1_python_reference() -> None:
    """A database written by the existing Python bridge before the native adapter existed."""
    path = _fresh("v1-python-reference.db")
    build_python_reference(path)
    connection = sqlite3.connect(path)
    connection.execute("PRAGMA wal_checkpoint(TRUNCATE)")
    connection.execute("PRAGMA journal_mode = DELETE")  # single self-contained file
    connection.close()


def _legacy(name: str, content_hash: str) -> None:
    """Tables created before the constraint/trigger migration (no CHECKs, no triggers)."""
    path = _fresh(name)
    connection = sqlite3.connect(path)
    connection.executescript(
        """
        CREATE TABLE sources (source_id TEXT, source_kind TEXT, provider_id TEXT, external_object_id TEXT,
            imported_at TEXT, observed_at TEXT, content_hash TEXT, source_version INTEGER, metadata_json TEXT);
        CREATE TABLE accounts (account_id TEXT PRIMARY KEY, source_id TEXT NOT NULL, source_version INTEGER NOT NULL,
            display_name TEXT NOT NULL, institution_name TEXT NOT NULL, account_type TEXT NOT NULL,
            account_subtype TEXT NOT NULL, currency TEXT NOT NULL, current_minor INTEGER NOT NULL,
            available_minor INTEGER NOT NULL, balance_observed_at TEXT NOT NULL, balance_freshness TEXT NOT NULL,
            last_four TEXT, status TEXT NOT NULL, include_in_safe_to_spend INTEGER NOT NULL,
            include_in_net_position INTEGER NOT NULL);
        CREATE TABLE event_log (event_id TEXT, event_type TEXT, object_id TEXT, occurred_at TEXT, content_hash TEXT);
        """
    )
    connection.execute("INSERT INTO sources VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                       ("legacy-src", "csv", None, None, "2025-12-01T00:00:00Z", "2025-12-01T00:00:00Z",
                        content_hash, 1, "{}"))
    connection.execute("INSERT INTO accounts VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                       ("legacy-checking", "legacy-src", 1, "Legacy Checking", "", "checking", "", "USD",
                        10000, 9000, "2025-12-01T00:00:00Z", "fresh", None, "active", 1, 1))
    connection.execute("INSERT INTO event_log VALUES (?, ?, ?, ?, ?)",
                       ("source:legacy-src:1", "source.appended", "legacy-src", "2025-12-01T00:00:00Z", content_hash))
    connection.commit()
    connection.close()


def _to_sql(name: str) -> None:
    database = HERE / f"{name}.db"
    connection = sqlite3.connect(database)
    newline = chr(10)
    sql = newline.join(connection.iterdump()) + newline
    connection.close()
    database.unlink()
    (HERE / f"{name}.sql").write_text(sql, encoding="utf-8", newline=newline)


if __name__ == "__main__":
    import hashlib

    v1_python_reference()
    _legacy("pre-constraint-valid.db", VALID_HASH)
    _legacy("pre-constraint-malformed-hash.db", "short")
    for name in ("v1-python-reference", "pre-constraint-valid", "pre-constraint-malformed-hash"):
        _to_sql(name)
        print(f"{name}.sql", hashlib.sha256((HERE / f"{name}.sql").read_bytes()).hexdigest())
