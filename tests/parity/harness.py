"""Shared helpers for the Python-reference vs native-C++ parity suite.

The Python bridge (connectors/bundle_connectors) is the reference. The native
path is exercised only through the `bundle` executable, exactly as a user would
run it. Databases are compared with an independent sqlite3 reader.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
import sys
import unittest
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "fixtures"
sys.path.insert(0, str(ROOT / "connectors"))

from bundle_connectors.archive import write_export  # noqa: E402
from bundle_connectors.csv_import import import_csv  # noqa: E402
from bundle_connectors.protocol import ConnectorBatch  # noqa: E402
from bundle_connectors.sqlite_store import SQLiteFinanceStore  # noqa: E402

UTC = timezone.utc
IMPORTED_AT = datetime(2026, 1, 4, 12, 0, tzinfo=UTC)
CSV_SOURCE = "csv-2026-01"


def native_cli() -> Path:
    configured = os.environ.get("BUNDLE_NATIVE_CLI")
    candidates = [Path(configured)] if configured else [
        ROOT / "build" / "core" / "persistence" / name for name in ("bundle.exe", "bundle")
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    unavailable(
        "native CLI not built; build with CMake or set BUNDLE_NATIVE_CLI to the bundle executable"
    )


def unavailable(reason: str) -> None:
    """Skip locally; fail when BUNDLE_PARITY_REQUIRE_NATIVE=1 (CI), so missing
    parity coverage can never be reported as a pass."""
    if os.environ.get("BUNDLE_PARITY_REQUIRE_NATIVE") == "1":
        raise RuntimeError(f"parity prerequisite missing: {reason}")
    raise unittest.SkipTest(reason)


def native(*args: str | Path, expect: int | None = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(native_cli()), *map(str, args)], capture_output=True, text=True, encoding="utf-8", timeout=120
    )
    if expect is not None and result.returncode != expect:
        raise AssertionError(
            f"bundle {' '.join(map(str, args))} exited {result.returncode}, expected {expect}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def parse_time(value: str) -> datetime:
    return datetime.fromisoformat(value[:-1] + "+00:00" if value.endswith("Z") else value)


def batch_from_document(document: dict[str, Any]) -> ConnectorBatch:
    """The bundle-connector-1 document -> ConnectorBatch (inverse of to_dict())."""
    return ConnectorBatch(
        connector_id=document["connector_id"],
        started_at=parse_time(document["started_at"]),
        completed_at=parse_time(document["completed_at"]),
        accounts=tuple(document["accounts"]),
        balances=tuple(document["balances"]),
        transactions=tuple(document["transactions"]),
        external_commitments=tuple(document["external_commitments"]),
        next_cursor=document["next_cursor"],
        warnings=tuple(document["warnings"]),
    )


def csv_batch() -> ConnectorBatch:
    with (FIXTURES / "statement.csv").open("r", encoding="utf-8", newline="") as stream:
        return import_csv(stream, source_id=CSV_SOURCE, imported_at=IMPORTED_AT)


def fixture_document(name: str) -> dict[str, Any]:
    return json.loads((FIXTURES / name).read_text(encoding="utf-8"))


def dump(database: Path) -> dict[str, list[tuple[Any, ...]]]:
    """Every row of every table in rowid order; schema_migrations.applied_at is volatile."""
    connection = sqlite3.connect(f"file:{database}?mode=ro", uri=True)
    try:
        tables = [row[0] for row in connection.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name")]
        output: dict[str, list[tuple[Any, ...]]] = {}
        for table in tables:
            if table == "schema_migrations":
                output[table] = connection.execute("SELECT version FROM schema_migrations ORDER BY version").fetchall()
                continue
            output[table] = connection.execute(f"SELECT * FROM {table} ORDER BY rowid").fetchall()
        return output
    finally:
        connection.close()


def tree(directory: Path) -> dict[str, bytes]:
    return {path.name: path.read_bytes() for path in sorted(directory.iterdir()) if path.is_file()}


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def add_planning_rows(store: SQLiteFinanceStore) -> None:
    """Planning records inserted the way the Python suite does (direct SQL)."""
    execute = store.connection.execute
    execute(
        "INSERT INTO commitments VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        ("rent", "Rent", None, '{"kind":"fixed","primary":{"currency":"USD","minor_units":95000}}', "USD",
         "monthly", "2026-02-01T00:00:00Z", 86_400_000, "housing", "active", "supported", 1,
         "2026-01-04T12:00:00Z", "2026-01-04T12:00:00Z"),
    )
    execute(
        "INSERT INTO commitments VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        ("utilities", "Utilities \u26a1", None,
         '{"kind":"range","maximum":{"currency":"USD","minor_units":12000},'
         '"minimum":{"currency":"USD","minor_units":8000},"primary":{"currency":"USD","minor_units":10000}}',
         "USD", "monthly", "2026-01-20T00:00:00Z", 0, "utilities", "active", "inferred", 0,
         "2026-01-04T12:00:00Z", "2026-01-04T12:00:00Z"),
    )
    execute("INSERT INTO commitment_evidence VALUES (?, ?, ?, ?)", ("rent", CSV_SOURCE, 1, "chk-002"))
    execute("INSERT INTO budgets VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            ("monthly", "Monthly", "category_caps", "calendar_month", "none", "2026-01-01T00:00:00Z", None, "active"))
    execute("INSERT INTO budget_allocations VALUES (?, ?, ?, ?, ?, ?, ?)", ("monthly", "food", 60000, 4200, 0, 55800, 700))
    execute("INSERT INTO goals VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            ("buffer", "Emergency buffer", 200000, "USD", "2026-02-15T00:00:00Z", 50000, 25000, 1, 1, "active"))
    execute("INSERT INTO goal_plans VALUES (?, ?)", ("buffer", "checking"))
    execute("INSERT INTO user_overrides VALUES (?, ?, ?, ?, ?, ?, ?)",
            ("override-1", "transaction", "chk-003", "category_id", "null", '"coffee"', "2026-01-04T12:00:00Z"))


def build_python_reference(database: Path) -> None:
    """CSV + bank batch v1 + v2 revision + planning rows through the Python bridge."""
    with SQLiteFinanceStore(database) as store:
        store.append_batch(csv_batch())
        store.append_batch(batch_from_document(fixture_document("batch-bank-v1.json")))
        store.append_batch(batch_from_document(fixture_document("batch-bank-v2-revision.json")))
        add_planning_rows(store)


def materialize_fixture(name: str, destination: Path) -> Path:
    """Build a fresh database from the committed older-schema SQL fixture (read-only)."""
    target = destination / name.replace(".sql", ".db")
    connection = sqlite3.connect(target)
    connection.executescript((FIXTURES / "legacy" / name).read_text(encoding="utf-8"))
    connection.close()
    return target


__all__ = [
    "FIXTURES", "IMPORTED_AT", "CSV_SOURCE", "SQLiteFinanceStore", "write_export", "native", "native_cli",
    "batch_from_document", "csv_batch", "fixture_document", "dump", "tree", "file_sha256", "add_planning_rows",
    "build_python_reference", "materialize_fixture", "unavailable",
]
