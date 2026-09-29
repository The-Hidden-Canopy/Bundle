"""Strict BUNDLE CSV import with no provider or network assumptions."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from datetime import datetime, timezone
from io import TextIOBase
from pathlib import Path
from typing import Any, Iterable

from .protocol import ConnectorBatch, ConnectorContractError, timestamp_text


REQUIRED_COLUMNS = frozenset(
    {
        "account_id",
        "account_name",
        "account_type",
        "currency",
        "transaction_id",
        "amount_minor",
        "direction",
        "state",
        "description",
        "observed_at",
    }
)
VALID_CURRENCIES = frozenset({"USD", "EUR", "GBP", "JPY", "KWD"})
VALID_DIRECTIONS = frozenset({"debit", "credit"})
VALID_STATES = frozenset({"pending", "posted", "reversed", "removed", "unknown"})


def parse_utc(value: str) -> datetime:
    if not value or value.endswith("Z") is False and "+" not in value and "-" not in value[10:]:
        raise ConnectorContractError("observed_at must include an explicit timezone")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise ConnectorContractError(f"invalid timestamp: {value!r}") from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise ConnectorContractError("timestamps must be timezone-aware")
    return parsed.astimezone(timezone.utc)


def _canonical_row(row: dict[str, str]) -> str:
    return json.dumps(row, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def _int_minor(value: str, label: str) -> int:
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ConnectorContractError(f"{label} must be an integer minor-unit value") from exc
    if parsed < 0:
        raise ConnectorContractError(f"{label} cannot be negative")
    return parsed


def import_csv(
    stream: TextIOBase,
    *,
    source_id: str,
    imported_at: datetime,
    source_version: int = 1,
) -> ConnectorBatch:
    if not source_id:
        raise ConnectorContractError("source_id is required")
    imported = parse_utc(timestamp_text(imported_at))
    if source_version < 1:
        raise ConnectorContractError("source_version must be positive")

    reader = csv.DictReader(stream)
    columns = frozenset(reader.fieldnames or ())
    missing = REQUIRED_COLUMNS - columns
    if missing:
        raise ConnectorContractError("CSV is missing columns: " + ", ".join(sorted(missing)))

    accounts: dict[str, dict[str, Any]] = {}
    transactions: list[dict[str, Any]] = []
    seen_transaction_ids: set[str] = set()
    for line_number, raw_row in enumerate(reader, start=2):
        row = {key: (value or "").strip() for key, value in raw_row.items() if key is not None}
        account_id = row["account_id"]
        transaction_id = row["transaction_id"]
        currency = row["currency"].upper()
        if not account_id or not transaction_id:
            raise ConnectorContractError(f"line {line_number}: account and transaction IDs are required")
        if transaction_id in seen_transaction_ids:
            raise ConnectorContractError(f"line {line_number}: duplicate transaction_id {transaction_id!r}")
        seen_transaction_ids.add(transaction_id)
        if currency not in VALID_CURRENCIES:
            raise ConnectorContractError(f"line {line_number}: unsupported currency {currency!r}")
        direction = row["direction"].lower()
        state = row["state"].lower()
        if direction not in VALID_DIRECTIONS:
            raise ConnectorContractError(f"line {line_number}: invalid direction {direction!r}")
        if state not in VALID_STATES:
            raise ConnectorContractError(f"line {line_number}: invalid state {state!r}")
        if account_id in accounts and accounts[account_id]["currency"] != currency:
            raise ConnectorContractError(f"line {line_number}: account currency changed within import")
        observed_at = parse_utc(row["observed_at"])
        content_hash = hashlib.sha256(_canonical_row(row).encode("utf-8")).hexdigest()
        accounts.setdefault(
            account_id,
            {
                "account_id": account_id,
                "source_id": source_id,
                "display_name": row["account_name"],
                "account_type": row["account_type"],
                "currency": currency,
            },
        )
        transactions.append(
            {
                "transaction_id": transaction_id,
                "account_id": account_id,
                "source_transaction_id": transaction_id,
                "revision": 1,
                "amount": {"minor_units": _int_minor(row["amount_minor"], "amount_minor"), "currency": currency},
                "direction": direction,
                "transaction_state": state,
                "merchant_raw": row.get("merchant_raw", ""),
                "description": row["description"],
                "observed_at": timestamp_text(observed_at),
                "source_ref": {
                    "source_id": source_id,
                    "source_kind": "csv",
                    "external_object_id": transaction_id,
                    "imported_at": timestamp_text(imported),
                    "observed_at": timestamp_text(observed_at),
                    "content_hash": "sha256:" + content_hash,
                    "source_version": source_version,
                },
            }
        )

    return ConnectorBatch(
        connector_id="csv",
        started_at=imported,
        completed_at=imported,
        accounts=tuple(accounts[key] for key in sorted(accounts)),
        transactions=tuple(transactions),
        warnings=(),
    )


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--source-id", required=True)
    parser.add_argument("--imported-at", required=True, help="RFC3339 timestamp with timezone")
    args = parser.parse_args(list(argv) if argv is not None else None)
    imported_at = parse_utc(args.imported_at)
    with args.csv_path.open("r", encoding="utf-8", newline="") as stream:
        batch = import_csv(stream, source_id=args.source_id, imported_at=imported_at)
    print(json.dumps(batch.to_dict(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
