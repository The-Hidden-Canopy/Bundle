"""Synthetic demo workspace. Every name and amount here is invented.

The demo is loaded through the same paths a user's data takes: two
bundle-connector-1 batches admitted by the native `bundle import-batch`, then a
bundle-finance-1 planning archive restored by `bundle restore`. Dates are
anchored to the time the demo is built so commitments are upcoming.
"""

from __future__ import annotations

import json
import shutil
import tempfile
from datetime import datetime, timedelta
from pathlib import Path
from typing import TYPE_CHECKING, Any

from bundle_connectors.archive import write_export

if TYPE_CHECKING:
    from .backend import BundleBackend

DEMO_NOTE = "Synthetic demo data: every name and amount is invented."


def _t(value: datetime) -> str:
    return value.isoformat().replace("+00:00", "Z")


def _source(source_id: str, observed: datetime, imported: datetime, tag: str) -> dict[str, Any]:
    return {"source_id": source_id, "source_kind": "local_api", "imported_at": _t(imported),
            "observed_at": _t(observed), "content_hash": "sha256:" + tag * 32, "source_version": 1,
            "metadata": {"note": DEMO_NOTE}}


def _tx(tx_id: str, account: str, minor: int, direction: str, merchant: str, description: str, posted: datetime,
        source: dict[str, Any], *, state: str = "posted", category: str | None = None) -> dict[str, Any]:
    record = {"transaction_id": tx_id, "account_id": account, "source_transaction_id": tx_id, "revision": 1,
              "amount": {"minor_units": minor, "currency": "USD"}, "direction": direction,
              "transaction_state": state, "merchant_raw": merchant, "description": description,
              "category_id": category, "observed_at": _t(posted), "source_ref": source}
    if state == "posted":
        record["posted_at"] = _t(posted)
    else:
        record["authorized_at"] = _t(posted)
    return record


def demo_batches(anchor: datetime) -> list[dict[str, Any]]:
    day = timedelta(days=1)
    # Batch 1: account setup ten days ago, with a savings balance observed then (it ages into a warning).
    older = anchor - 10 * day
    s1 = _source("demo-feed-1", older, older, "d1")
    first = {
        "schema": "bundle-connector-1", "connector_id": "demo-feed",
        "started_at": _t(older), "completed_at": _t(older),
        "accounts": [
            {"account_id": "checking", "source_id": "demo-feed-1", "currency": "USD",
             "display_name": "Everyday Checking", "institution_name": "Demo Credit Union",
             "account_type": "checking", "last_four": "1234"},
            {"account_id": "savings", "source_id": "demo-feed-1", "currency": "USD",
             "display_name": "Rainy Day Savings", "institution_name": "Demo Credit Union",
             "account_type": "savings", "last_four": "5678"},
        ],
        "balances": [
            {"account_id": "savings", "current_minor": 120000, "available_minor": 120000,
             "observed_at": _t(older), "status": "valid", "source_ref": s1},
        ],
        "transactions": [
            _tx("demo-rent-last", "checking", 85000, "debit", "DEMO LANDLORD", "Rent (last month)",
                older - 2 * day, s1, category="housing"),
            _tx("demo-books", "checking", 1800, "debit", "RIVERSIDE BOOKS", "Book purchase",
                older - day, s1, category="books"),
        ],
        "external_commitments": [], "next_cursor": None, "warnings": [],
    }
    # Batch 2: recent activity and a fresh checking balance.
    recent = anchor - timedelta(hours=1)
    s2 = _source("demo-feed-2", recent, recent, "d2")
    second = {
        "schema": "bundle-connector-1", "connector_id": "demo-feed",
        "started_at": _t(recent), "completed_at": _t(recent), "accounts": [],
        "balances": [
            {"account_id": "checking", "current_minor": 248450, "available_minor": 248000,
             "observed_at": _t(recent), "status": "valid", "source_ref": s2},
        ],
        "transactions": [
            _tx("demo-payroll", "checking", 240000, "credit", "DEMO EMPLOYER", "Payroll",
                anchor - 6 * day, s2, category="income"),
            _tx("demo-grocer", "checking", 6420, "debit", "DEMO GROCER", "Groceries",
                anchor - 3 * day, s2, category="groceries"),
            _tx("demo-transfer-out", "checking", 10000, "debit", "TRANSFER TO SAVINGS", "Transfer to savings",
                anchor - 2 * day, s2, category="transfer"),
            _tx("demo-transfer-in", "savings", 10000, "credit", "TRANSFER FROM CHECKING", "Transfer from checking",
                anchor - 2 * day + timedelta(hours=2), s2, category="transfer"),
            _tx("demo-books-refund", "checking", 1800, "credit", "RIVERSIDE BOOKS", "Book refund",
                anchor - 2 * day, s2, category="books"),
            _tx("demo-coffee", "checking", 450, "debit", "DEMO CAFE", "Coffee", anchor - 3 * timedelta(hours=1),
                s2, state="pending", category="eating_out"),
        ],
        "external_commitments": [], "next_cursor": None, "warnings": [],
    }
    return [first, second]


def demo_planning(anchor: datetime) -> dict[str, list[dict[str, Any]]]:
    day = timedelta(days=1)
    created = _t(anchor - 10 * day)

    def commitment(cid: str, name: str, ctype: str, due: datetime, confidence: str, model: dict[str, Any]) -> dict:
        return {"commitment_id": cid, "name": name, "merchant_id": None, "amount_model": model, "currency": "USD",
                "cadence": "monthly", "next_expected_at": _t(due), "grace_window_millis": 86_400_000,
                "commitment_type": ctype, "status": "active", "confidence": confidence, "user_confirmed": 1,
                "created_at": created, "updated_at": created, "evidence": []}

    usd = lambda minor: {"currency": "USD", "minor_units": minor}  # noqa: E731
    return {
        "commitments": [
            commitment("demo-rent", "Rent", "housing", anchor + 3 * day, "verified",
                       {"kind": "fixed", "primary": usd(85000)}),
            commitment("demo-phone", "Phone", "utilities", anchor + 5 * day, "verified",
                       {"kind": "fixed", "primary": usd(9600)}),
            commitment("demo-utilities", "Utilities", "utilities", anchor + 7 * day, "estimated",
                       {"kind": "range", "primary": usd(15800), "minimum": usd(12000), "maximum": usd(20000)}),
            commitment("demo-streaming", "Streaming subscription", "subscription", anchor + 9 * day, "supported",
                       {"kind": "fixed", "primary": usd(3400)}),
        ],
        "goals": [
            {"goal_id": "demo-buffer", "name": "Emergency buffer", "target_minor": 300000, "currency": "USD",
             "target_date": _t(anchor + 12 * day), "current_reserved_minor": 120000,
             "planned_contribution_minor": 25000, "priority": 1, "protected": 1, "status": "active",
             "plans": [{"goal_id": "demo-buffer", "account_id": "savings"}]},
            {"goal_id": "demo-bike", "name": "New bike", "target_minor": 80000, "currency": "USD",
             "target_date": _t(anchor + 75 * day), "current_reserved_minor": 60000,
             "planned_contribution_minor": 10000, "priority": 2, "protected": 0, "status": "active",
             "plans": [{"goal_id": "demo-bike", "account_id": "checking"}]},
        ],
        "budgets": [
            {"budget_id": "demo-month", "name": "This month", "mode": "category_caps",
             "period_rule": "calendar_month", "rollover_policy": "none", "starts_at": _t(anchor - 10 * day),
             "ends_at": None, "status": "active",
             "allocations": [
                 {"budget_id": "demo-month", "category_id": "groceries", "limit_minor": 50000,
                  "spent_minor": 6420, "pending_minor": 0, "remaining_minor": 43580, "burn_rate_basis_points": 1284},
                 {"budget_id": "demo-month", "category_id": "eating_out", "limit_minor": 12000,
                  "spent_minor": 0, "pending_minor": 450, "remaining_minor": 11550, "burn_rate_basis_points": 375},
             ]},
        ],
    }


def build_demo_workspace(backend: "BundleBackend", database: Path, anchor: datetime) -> None:
    """Rebuild the demo database from scratch. Only ever touches the demo folder."""
    folder = database.parent
    if folder.name != "bundle-demo" or database.name != "demo.db":
        raise ValueError("refusing to rebuild a database outside the bundle-demo folder")
    folder.mkdir(parents=True, exist_ok=True)
    for suffix in ("", "-wal", "-shm"):
        Path(str(database) + suffix).unlink(missing_ok=True)
    shutil.rmtree(folder / "demo-backups", ignore_errors=True)
    with tempfile.TemporaryDirectory() as temporary:
        temporary = Path(temporary)
        for index, batch in enumerate(demo_batches(anchor)):
            path = temporary / f"batch-{index}.json"
            path.write_text(json.dumps(batch), encoding="utf-8")
            backend._run("import-batch", "--db", database, path)
        planning = demo_planning(anchor)
        write_export(temporary / "planning", created_at=anchor, **planning)
        backend._run("restore", "--db", database, temporary / "planning")
    # A protected cash floor of $500, persisted the way the interface persists it:
    # as an input of a recorded calculation receipt.
    as_of = anchor.replace(second=0, microsecond=0)
    backend._run("calculate", "--db", database, "--as-of", _t(as_of), "--horizon-end", _t(as_of + timedelta(days=14)),
                 "--mode", "expected", "--floor-minor", "50000")
