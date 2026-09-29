"""Transactional local SQLite admission for bounded connector batches."""

from __future__ import annotations

import hashlib
import json
import sqlite3
from datetime import datetime
from pathlib import Path
from typing import Any, Mapping

from .archive import read_export, write_export
from .protocol import ConnectorBatch, ConnectorContractError, require_utc, timestamp_text


class SQLiteStoreError(ConnectorContractError):
    """Raised when a batch cannot be admitted without losing provenance."""


_ACCOUNT_TYPES = frozenset(
    {"checking", "savings", "cash", "credit_card", "prepaid", "investment", "loan", "other"}
)
_TRANSACTION_STATES = frozenset({"pending", "posted", "reversed", "removed", "unknown"})
_DIRECTIONS = frozenset({"debit", "credit"})
_BALANCE_STATUSES = frozenset({"valid", "stale", "unavailable", "unknown"})
_ACCOUNT_FRESHNESS = frozenset({"fresh", "aging", "stale", "unavailable"})
_ACCOUNT_STATUSES = frozenset({"active", "closed", "unavailable", "unknown"})

_SOURCE_COLUMNS = (
    "source_id", "source_kind", "provider_id", "external_object_id", "imported_at",
    "observed_at", "content_hash", "source_version", "metadata_json",
)
_ACCOUNT_COLUMNS = (
    "account_id", "source_id", "source_version", "display_name", "institution_name",
    "account_type", "account_subtype", "currency", "current_minor", "available_minor",
    "balance_observed_at", "balance_freshness", "last_four", "status",
    "include_in_safe_to_spend", "include_in_net_position",
)
_BALANCE_COLUMNS = (
    "observation_id", "account_id", "current_minor", "available_minor", "observed_at",
    "recorded_at", "source_id", "source_version", "status",
)
_TRANSACTION_COLUMNS = (
    "transaction_id", "account_id", "source_transaction_id", "revision", "amount_minor",
    "currency", "direction", "transaction_state", "merchant_raw", "merchant_id",
    "description", "category_id", "authorized_at", "posted_at", "observed_at",
    "recurring_candidate_id", "source_id", "source_version",
    "supersedes_transaction_revision", "user_note", "user_tags_json",
)
_COMMITMENT_COLUMNS = (
    "commitment_id", "name", "merchant_id", "amount_model_json", "currency",
    "cadence", "next_expected_at", "grace_window_millis", "commitment_type",
    "status", "confidence", "user_confirmed", "created_at", "updated_at",
)
_COMMITMENT_EVIDENCE_COLUMNS = (
    "commitment_id", "source_id", "source_version", "related_transaction_id",
)
_BUDGET_COLUMNS = (
    "budget_id", "name", "mode", "period_rule", "rollover_policy", "starts_at",
    "ends_at", "status",
)
_BUDGET_ALLOCATION_COLUMNS = (
    "budget_id", "category_id", "limit_minor", "spent_minor", "pending_minor",
    "remaining_minor", "burn_rate_basis_points",
)
_GOAL_COLUMNS = (
    "goal_id", "name", "target_minor", "currency", "target_date",
    "current_reserved_minor", "planned_contribution_minor", "priority", "protected",
    "status",
)
_GOAL_PLAN_COLUMNS = ("goal_id", "account_id")
_OVERRIDE_COLUMNS = (
    "override_id", "object_type", "object_id", "field", "old_projection_json",
    "new_user_value_json", "created_at",
)
_RECEIPT_COLUMNS = (
    "receipt_id", "engine", "engine_version", "calculated_at", "input_refs_json",
    "input_hash", "policy_id", "policy_version", "outputs_json",
)
_ARCHIVE_UNSUPPORTED_TABLES = (
    "connector_state", "merchants", "merchant_aliases", "category_assignments",
    "income_streams", "planned_events", "forecast_runs", "forecast_points", "insights",
    "scenarios", "settings",
)


def _canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def _sha256(value: Any) -> str:
    return "sha256:" + hashlib.sha256(_canonical(value)).hexdigest()


def _required_text(value: Mapping[str, Any], key: str, label: str) -> str:
    result = value.get(key)
    if not isinstance(result, str) or not result:
        raise SQLiteStoreError(f"{label}.{key} is required")
    return result


def _required_sha256(value: Any, label: str) -> str:
    if (
        not isinstance(value, str)
        or len(value) != len("sha256:") + 64
        or not value.startswith("sha256:")
        or any(character not in "0123456789abcdefABCDEF" for character in value[7:])
    ):
        raise SQLiteStoreError(f"{label} must be a SHA-256 digest")
    return value


def _source_ref(value: Mapping[str, Any], label: str) -> Mapping[str, Any]:
    source_ref = value.get("source_ref")
    if not isinstance(source_ref, Mapping):
        raise SQLiteStoreError(f"{label}.source_ref is required")
    source_id = _required_text(source_ref, "source_id", f"{label}.source_ref")
    content_hash = _required_text(source_ref, "content_hash", f"{label}.source_ref")
    if (
        len(content_hash) != len("sha256:") + 64
        or not content_hash.startswith("sha256:")
        or any(character not in "0123456789abcdefABCDEF" for character in content_hash[7:])
    ):
        raise SQLiteStoreError(f"{label}.source_ref.content_hash must be a SHA-256 digest")
    source_kind = _required_text(source_ref, "source_kind", f"{label}.source_ref")
    source_version = source_ref.get("source_version")
    if not isinstance(source_version, int) or isinstance(source_version, bool) or source_version < 1:
        raise SQLiteStoreError(f"{label}.source_ref.source_version must be positive")
    imported_at = _timestamp(
        source_ref.get("imported_at"), f"{label}.source_ref.imported_at"
    )
    observed_at = _timestamp(
        source_ref.get("observed_at"), f"{label}.source_ref.observed_at"
    )
    return {
        "source_id": source_id,
        "source_kind": source_kind,
        "provider_id": source_ref.get("provider_id"),
        "external_object_id": source_ref.get("external_object_id"),
        "imported_at": imported_at,
        "observed_at": observed_at,
        "content_hash": content_hash,
        "source_version": source_version,
        "metadata_json": json.dumps(source_ref.get("metadata", {}), sort_keys=True, separators=(",", ":")),
    }


def _timestamp(value: Any, label: str, *, optional: bool = False) -> str | None:
    if value is None and optional:
        return None
    if not isinstance(value, str) or not value:
        raise SQLiteStoreError(f"{label} must be an RFC3339 timestamp")
    normalized = value[:-1] + "+00:00" if value.endswith("Z") else value
    try:
        parsed = datetime.fromisoformat(normalized)
    except ValueError as exc:
        raise SQLiteStoreError(f"{label} is not a valid timestamp") from exc
    if parsed.tzinfo is None or parsed.utcoffset() is None:
        raise SQLiteStoreError(f"{label} must include an explicit timezone")
    return timestamp_text(parsed)


def _minor_amount(value: Any, currency: str, label: str) -> int:
    if not isinstance(value, Mapping):
        raise SQLiteStoreError(f"{label}.amount must be an object")
    minor = value.get("minor_units")
    if not isinstance(minor, int) or isinstance(minor, bool) or minor < 0:
        raise SQLiteStoreError(f"{label}.amount.minor_units must be a non-negative integer")
    if value.get("currency") != currency:
        raise SQLiteStoreError(f"{label}.amount currency does not match account currency")
    return minor


def _row_dict(row: tuple[Any, ...], columns: tuple[str, ...]) -> dict[str, Any]:
    return dict(zip(columns, row))


def _archive_source(value: Any, label: str) -> Mapping[str, Any]:
    if not isinstance(value, Mapping):
        raise SQLiteStoreError(f"{label}.source must be an object")
    if "metadata_json" in value:
        metadata_json = value["metadata_json"]
        if not isinstance(metadata_json, str):
            raise SQLiteStoreError(f"{label}.source.metadata_json must be JSON text")
        try:
            metadata = json.loads(metadata_json)
        except json.JSONDecodeError as exc:
            raise SQLiteStoreError(f"{label}.source.metadata_json is invalid JSON") from exc
    else:
        metadata = value.get("metadata", {})
    if not isinstance(metadata, Mapping):
        raise SQLiteStoreError(f"{label}.source metadata must contain an object")
    source_ref = dict(value)
    source_ref["metadata"] = metadata
    return _source_ref({"source_ref": source_ref}, label)


def _archive_json_document(value: Any, label: str, *, object_only: bool = False) -> Any:
    if not isinstance(value, str):
        raise SQLiteStoreError(f"{label} must be JSON text")
    try:
        parsed = json.loads(value)
    except json.JSONDecodeError as exc:
        raise SQLiteStoreError(f"{label} is invalid JSON") from exc
    if object_only and not isinstance(parsed, Mapping):
        raise SQLiteStoreError(f"{label} must contain an object")
    return parsed


def _archive_json_text(value: Any, label: str, *, object_only: bool = False) -> str:
    if object_only and not isinstance(value, Mapping):
        raise SQLiteStoreError(f"{label} must contain an object")
    try:
        return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    except (TypeError, ValueError) as exc:
        raise SQLiteStoreError(f"{label} is not JSON serializable") from exc


def _archive_source_key(source: Mapping[str, Any]) -> tuple[str, int]:
    return str(source["source_id"]), int(source["source_version"])


class SQLiteFinanceStore:
    """Local, transactional admission boundary for connector output.

    The C++ domain store remains the portable reference implementation. This
    adapter provides a durable Python bridge over the same migration for local
    file import and restore workflows; it never opens a network connection.
    """

    def __init__(self, database: str | Path, migration: str | Path | None = None) -> None:
        self.database = Path(database)
        if self.database.name in {"", ".", ".."}:
            raise SQLiteStoreError("database path must name a local file")
        migration_path = Path(migration) if migration is not None else (
            Path(__file__).parents[2] / "core" / "persistence" / "migrations" / "001_finance.sql"
        )
        if not migration_path.is_file():
            raise SQLiteStoreError(f"migration file does not exist: {migration_path}")
        self.connection = sqlite3.connect(self.database, timeout=5.0)
        self.connection.execute("PRAGMA foreign_keys = ON")
        self.connection.execute("PRAGMA busy_timeout = 5000")
        try:
            self.connection.executescript(migration_path.read_text(encoding="utf-8"))
            self.connection.commit()
            self._validate_persisted_hashes()
        except Exception:
            self.connection.close()
            raise

    def close(self) -> None:
        self.connection.close()

    def __enter__(self) -> "SQLiteFinanceStore":
        return self

    def __exit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        if exc_type is None:
            self.connection.commit()
        else:
            self.connection.rollback()
        self.close()

    def _validate_persisted_hashes(self) -> None:
        source_rows = self.connection.execute(
            "SELECT " + ", ".join(_SOURCE_COLUMNS) + " FROM sources"
        ).fetchall()
        for row in source_rows:
            _archive_source(_row_dict(row, _SOURCE_COLUMNS), "stored source")
        for table, column, label in (
            ("event_log", "content_hash", "stored event"),
            ("forecast_runs", "input_hash", "stored forecast run"),
            ("calculation_receipts", "input_hash", "stored calculation receipt"),
        ):
            for (value,) in self.connection.execute(f"SELECT {column} FROM {table}"):
                _required_sha256(value, f"{label}.{column}")

    def _ensure_event(
        self,
        event_id: str,
        event_type: str,
        object_id: str,
        occurred_at: str,
        content_hash: str,
    ) -> None:
        existing = self.connection.execute(
            "SELECT event_type, object_id, occurred_at, content_hash "
            "FROM event_log WHERE event_id = ?",
            (event_id,),
        ).fetchone()
        if existing is not None:
            if tuple(existing) != (event_type, object_id, occurred_at, content_hash):
                raise SQLiteStoreError(f"event conflict for {event_id!r}")
            return
        self.connection.execute(
            "INSERT INTO event_log (event_id, event_type, object_id, occurred_at, content_hash) "
            "VALUES (?, ?, ?, ?, ?)",
            (event_id, event_type, object_id, occurred_at, content_hash),
        )

    def _ensure_source(self, source: Mapping[str, Any]) -> None:
        existing = self.connection.execute(
            "SELECT content_hash FROM sources WHERE source_id = ? AND source_version = ?",
            (source["source_id"], source["source_version"]),
        ).fetchone()
        if existing is not None:
            if existing[0] != source["content_hash"]:
                raise SQLiteStoreError(
                    f"source conflict for {source['source_id']!r} version {source['source_version']}"
                )
            return
        self.connection.execute(
            """
            INSERT INTO sources (
                source_id, source_kind, provider_id, external_object_id,
                imported_at, observed_at, content_hash, source_version, metadata_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                source["source_id"], source["source_kind"], source["provider_id"],
                source["external_object_id"], source["imported_at"], source["observed_at"],
                source["content_hash"], source["source_version"], source["metadata_json"],
            ),
        )

    def _ensure_account(
        self,
        account: Mapping[str, Any],
        source: Mapping[str, Any],
    ) -> None:
        account_id = _required_text(account, "account_id", "account")
        currency = _required_text(account, "currency", f"account {account_id}").upper()
        account_type = str(account.get("account_type", "other")).lower()
        if account_type not in _ACCOUNT_TYPES:
            raise SQLiteStoreError(f"account {account_id!r} has unsupported account_type")
        existing = self.connection.execute(
            "SELECT source_id, source_version, currency FROM accounts WHERE account_id = ?",
            (account_id,),
        ).fetchone()
        if existing is not None:
            existing_source_id, existing_source_version, existing_currency = existing
            if existing_source_id != source["source_id"] or existing_currency != currency:
                raise SQLiteStoreError(f"account conflict for {account_id!r}")
            if existing_source_version > source["source_version"]:
                raise SQLiteStoreError(
                    f"account {account_id!r} received an older source version"
                )
            if existing_source_version == source["source_version"]:
                return
            self.connection.execute(
                """
                UPDATE accounts
                SET source_id = ?, source_version = ?, display_name = ?,
                    institution_name = ?, account_type = ?, account_subtype = ?,
                    currency = ?, last_four = ?
                WHERE account_id = ?
                """,
                (
                    source["source_id"], source["source_version"],
                    str(account.get("display_name", account_id)),
                    str(account.get("institution_name", "")), account_type,
                    str(account.get("account_subtype", "")), currency,
                    account.get("last_four"), account_id,
                ),
            )
            self._ensure_event(
                f"account-source:{account_id}:{source['source_id']}:{source['source_version']}",
                "account.source_projection.updated",
                account_id,
                source["observed_at"],
                source["content_hash"],
            )
            return
        self.connection.execute(
            """
            INSERT INTO accounts (
                account_id, source_id, source_version, display_name,
                institution_name, account_type, account_subtype, currency,
                current_minor, available_minor, balance_observed_at,
                balance_freshness, last_four, status,
                include_in_safe_to_spend, include_in_net_position
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                account_id, source["source_id"], source["source_version"],
                str(account.get("display_name", account_id)),
                str(account.get("institution_name", "")), account_type,
                str(account.get("account_subtype", "")), currency,
                0, 0, source["observed_at"], "unavailable",
                account.get("last_four"), "active", 1, 1,
            ),
        )
        self._ensure_event(
            f"account:{account_id}",
            "account.appended",
            account_id,
            source["observed_at"],
            source["content_hash"],
        )

    def _append_balance(
        self,
        balance: Mapping[str, Any],
        source: Mapping[str, Any],
        recorded_at: str,
    ) -> None:
        account_id = _required_text(balance, "account_id", "balance")
        account = self.connection.execute(
            "SELECT currency, balance_observed_at, balance_freshness FROM accounts "
            "WHERE account_id = ?",
            (account_id,),
        ).fetchone()
        if account is None:
            raise SQLiteStoreError(f"balance references unknown account {account_id!r}")
        currency = account[0]
        current_minor = balance.get("current_minor")
        available_minor = balance.get("available_minor", current_minor)
        if not isinstance(current_minor, int) or isinstance(current_minor, bool):
            raise SQLiteStoreError("balance.current_minor must be an integer")
        if not isinstance(available_minor, int) or isinstance(available_minor, bool):
            raise SQLiteStoreError("balance.available_minor must be an integer")
        observed_at = _timestamp(balance.get("observed_at"), "balance.observed_at")
        status = str(balance.get("status", "valid")).lower()
        if status not in _BALANCE_STATUSES:
            raise SQLiteStoreError(f"balance has unsupported status {status!r}")
        observation_id = str(
            balance.get("observation_id")
            or f"{source['source_id']}:{account_id}:{observed_at}"
        )
        existing = self.connection.execute(
            "SELECT " + ", ".join(_BALANCE_COLUMNS) + " FROM balance_observations "
            "WHERE observation_id = ?",
            (observation_id,),
        ).fetchone()
        expected = (
            observation_id, account_id, current_minor, available_minor, observed_at,
            recorded_at, source["source_id"], source["source_version"], status,
        )
        if existing is None:
            self.connection.execute(
                """
                INSERT INTO balance_observations (
                    observation_id, account_id, current_minor, available_minor,
                    observed_at, recorded_at, source_id, source_version, status
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    observation_id, account_id, current_minor, available_minor,
                    observed_at, recorded_at,
                    source["source_id"], source["source_version"],
                    status,
                ),
            )
        elif tuple(existing) != expected:
            raise SQLiteStoreError(f"balance conflict for {observation_id!r}")
        freshness = "stale" if status == "stale" else "fresh" if status == "valid" else "unavailable"
        current_observed_at = _timestamp(
            account[1], f"account {account_id}.balance_observed_at"
        )
        current_instant = datetime.fromisoformat(current_observed_at.replace("Z", "+00:00"))
        observed_instant = datetime.fromisoformat(observed_at.replace("Z", "+00:00"))
        freshness_rank = {"unavailable": 1, "stale": 2, "fresh": 3}
        should_project = (
            observed_instant > current_instant
            or (
                observed_instant == current_instant
                and freshness_rank[freshness] >= freshness_rank.get(account[2], 1)
            )
        )
        if should_project:
            self.connection.execute(
                """
                UPDATE accounts
                SET current_minor = ?, available_minor = ?, balance_observed_at = ?,
                    balance_freshness = ?, status = ?
                WHERE account_id = ?
                """,
                (
                    current_minor, available_minor, observed_at,
                    freshness, "active", account_id,
                ),
            )
            self._ensure_event(
                f"account-balance:{account_id}:{observation_id}",
                "account.balance_projection.updated",
                account_id,
                observed_at,
                source["content_hash"],
            )

    def _append_transaction(
        self,
        transaction: Mapping[str, Any],
        source: Mapping[str, Any],
    ) -> None:
        transaction_id = _required_text(transaction, "transaction_id", "transaction")
        account_id = _required_text(transaction, "account_id", f"transaction {transaction_id}")
        account = self.connection.execute(
            "SELECT currency FROM accounts WHERE account_id = ?", (account_id,)
        ).fetchone()
        if account is None:
            raise SQLiteStoreError(f"transaction references unknown account {account_id!r}")
        currency = account[0]
        source_transaction_id = str(transaction.get("source_transaction_id", transaction_id))
        revision = transaction.get("revision", 1)
        if not isinstance(revision, int) or isinstance(revision, bool) or revision < 1:
            raise SQLiteStoreError(f"transaction {transaction_id!r} has invalid revision")
        amount_minor = _minor_amount(transaction.get("amount"), currency, f"transaction {transaction_id}")
        direction = str(transaction.get("direction", "")).lower()
        state = str(transaction.get("transaction_state", "")).lower()
        if direction not in _DIRECTIONS or state not in _TRANSACTION_STATES:
            raise SQLiteStoreError(f"transaction {transaction_id!r} has invalid direction or state")
        observed_at = _timestamp(transaction.get("observed_at"), f"transaction {transaction_id}.observed_at")
        authorized_at = _timestamp(transaction.get("authorized_at"), f"transaction {transaction_id}.authorized_at", optional=True)
        posted_at = _timestamp(transaction.get("posted_at"), f"transaction {transaction_id}.posted_at", optional=True)
        supersedes = transaction.get("supersedes_transaction_revision")
        if supersedes is not None and (not isinstance(supersedes, int) or supersedes < 1):
            raise SQLiteStoreError(f"transaction {transaction_id!r} has invalid supersession")
        merchant_id = transaction.get("merchant_id")
        category_id = transaction.get("category_id")
        recurring_candidate_id = transaction.get("recurring_candidate_id")
        merchant_raw = str(transaction.get("merchant_raw", ""))
        description = str(transaction.get("description", ""))
        user_note = str(transaction.get("user_note", ""))
        user_tags = transaction.get("user_tags", [])
        if not isinstance(user_tags, list) or any(not isinstance(tag, str) for tag in user_tags):
            raise SQLiteStoreError(f"transaction {transaction_id!r}.user_tags must be a string list")
        user_tags_json = json.dumps(user_tags, separators=(",", ":"))
        existing = self.connection.execute(
            "SELECT " + ", ".join(_TRANSACTION_COLUMNS) +
            " FROM transaction_revisions WHERE transaction_id = ?",
            (transaction_id,),
        ).fetchone()
        if existing is not None:
            expected = (
                transaction_id, account_id, source_transaction_id, revision, amount_minor,
                currency, direction, state, merchant_raw, merchant_id, description,
                category_id, authorized_at, posted_at, observed_at,
                recurring_candidate_id, source["source_id"], source["source_version"],
                supersedes, user_note, user_tags_json,
            )
            if tuple(existing) != expected:
                raise SQLiteStoreError(f"transaction conflict for {transaction_id!r}")
            return
        current = self.connection.execute(
            "SELECT current_revision FROM transactions WHERE account_id = ? AND source_transaction_id = ?",
            (account_id, source_transaction_id),
        ).fetchone()
        if current is None:
            if revision != 1 or supersedes is not None:
                raise SQLiteStoreError(f"transaction {transaction_id!r} does not start at revision 1")
            self.connection.execute(
                "INSERT INTO transactions (account_id, source_transaction_id, current_revision) VALUES (?, ?, ?)",
                (account_id, source_transaction_id, revision),
            )
        else:
            if revision != current[0] + 1 or supersedes != current[0]:
                raise SQLiteStoreError(f"transaction {transaction_id!r} has a revision gap")

        self.connection.execute(
            """
            INSERT INTO transaction_revisions (
                transaction_id, account_id, source_transaction_id, revision,
                amount_minor, currency, direction, transaction_state,
                merchant_raw, merchant_id, description, category_id,
                authorized_at, posted_at, observed_at, recurring_candidate_id,
                source_id, source_version, supersedes_transaction_revision,
                user_note, user_tags_json
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                transaction_id, account_id, source_transaction_id, revision,
                amount_minor, currency, direction, state,
                merchant_raw, merchant_id,
                description, category_id,
                authorized_at, posted_at, observed_at,
                recurring_candidate_id,
                source["source_id"], source["source_version"], supersedes,
                user_note, user_tags_json,
            ),
        )
        if current is not None:
            self.connection.execute(
                "UPDATE transactions SET current_revision = ? WHERE account_id = ? AND source_transaction_id = ?",
                (revision, account_id, source_transaction_id),
            )
            self._ensure_event(
                f"transaction-current:{account_id}:{source_transaction_id}:{revision}",
                "transaction.current_revision.updated",
                source_transaction_id,
                observed_at,
                source["content_hash"],
            )

    def append_batch(self, batch: ConnectorBatch) -> str:
        """Admit a connector batch atomically and return its deterministic batch ID."""
        if batch.external_commitments:
            raise SQLiteStoreError(
                "external commitments require explicit planning admission and are not silently discarded"
            )
        payload = batch.to_dict()
        batch_hash = _sha256(payload)
        batch_id = f"{batch.connector_id}:{batch_hash[7:]}"
        source_candidates = list(batch.transactions) + list(batch.balances)
        account_source_candidates: list[tuple[str, int]] = []
        for index, account in enumerate(batch.accounts):
            account_source_id = account.get("source_id")
            if account_source_id is None:
                if "source_version" in account:
                    raise SQLiteStoreError(
                        f"account {index}.source_version requires source_id"
                    )
                continue
            if not isinstance(account_source_id, str) or not account_source_id:
                raise SQLiteStoreError(f"account {index}.source_id must be a non-empty string")
            account_source_version = account.get("source_version", 1)
            if (
                not isinstance(account_source_version, int)
                or isinstance(account_source_version, bool)
                or account_source_version < 1
            ):
                raise SQLiteStoreError(f"account {index}.source_version must be positive")
            account_source_candidates.append((account_source_id, account_source_version))

        source: dict[str, Any] | None
        if source_candidates:
            source = dict(_source_ref(source_candidates[0], "batch record"))
        elif batch.accounts:
            source = {
                "source_id": batch_id,
                "source_kind": batch.connector_id,
                "provider_id": None,
                "external_object_id": None,
                "imported_at": timestamp_text(require_utc(batch.started_at)),
                "observed_at": timestamp_text(require_utc(batch.completed_at)),
                "content_hash": batch_hash,
                "source_version": 1,
                "metadata_json": "{}",
            }
        else:
            source = None
        if source is not None and not source_candidates and account_source_candidates:
            source["source_id"], source["source_version"] = account_source_candidates[0]
        # The durable source identity represents the complete admitted batch.
        # Row-level hashes are useful connector evidence, but using only the
        # first row here would allow a later-row change to reuse a source ID.
        if source is not None:
            source = dict(source)
            source["content_hash"] = batch_hash
            for index, record in enumerate(source_candidates):
                record_source = _source_ref(record, f"batch record {index}")
                if (record_source["source_id"], record_source["source_version"]) != (
                    source["source_id"], source["source_version"]
                ):
                    raise SQLiteStoreError("connector batch contains mixed source identities")
            if any(candidate != (source["source_id"], source["source_version"])
                   for candidate in account_source_candidates):
                raise SQLiteStoreError("connector batch contains mixed account source identities")

        try:
            with self.connection:
                self.connection.execute("BEGIN IMMEDIATE")
                if source is not None:
                    self._ensure_source(source)
                    for account in batch.accounts:
                        self._ensure_account(account, source)
                recorded_at = timestamp_text(batch.completed_at)
                if source is not None:
                    for balance in batch.balances:
                        self._append_balance(balance, source, recorded_at)
                    for transaction in batch.transactions:
                        self._append_transaction(transaction, source)
                self.connection.execute(
                    """
                    INSERT INTO import_batches (
                        batch_id, connector_id, started_at, completed_at,
                        next_cursor, warnings_json
                    ) VALUES (?, ?, ?, ?, ?, ?)
                    ON CONFLICT(batch_id) DO NOTHING
                    """,
                    (
                        batch_id, batch.connector_id, timestamp_text(batch.started_at),
                        timestamp_text(batch.completed_at), batch.next_cursor,
                        json.dumps(list(batch.warnings), separators=(",", ":")),
                    ),
                )
                self._ensure_event(
                    f"import-batch:{batch_id}",
                    "import_batch.admitted",
                    batch_id,
                    timestamp_text(batch.completed_at),
                    batch_hash,
                )
        except sqlite3.Error as exc:
            raise SQLiteStoreError(f"SQLite admission rejected batch {batch_id!r}: {exc}") from exc
        return batch_id

    def _assert_archive_supported(self) -> None:
        for table in _ARCHIVE_UNSUPPORTED_TABLES:
            count = self.connection.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
            if count:
                raise SQLiteStoreError(
                    f"bundle-finance-1 archive cannot represent non-empty table {table!r}"
                )

    def _source_rows(self) -> dict[tuple[str, int], dict[str, Any]]:
        rows = self.connection.execute(
            "SELECT " + ", ".join(_SOURCE_COLUMNS) + " FROM sources"
        ).fetchall()
        output = {}
        for row in rows:
            record = _row_dict(row, _SOURCE_COLUMNS)
            normalized = _archive_source(record, "stored source")
            metadata = json.loads(normalized["metadata_json"])
            archive_record = dict(normalized)
            archive_record.pop("metadata_json")
            archive_record["metadata"] = metadata
            output[(str(row[0]), int(row[7]))] = archive_record
        return output

    @staticmethod
    def _attach_archive_source(
        record: dict[str, Any],
        source_rows: Mapping[tuple[str, int], Mapping[str, Any]],
        label: str,
    ) -> None:
        key = (str(record["source_id"]), int(record["source_version"]))
        source = source_rows.get(key)
        if source is None:
            raise SQLiteStoreError(f"{label} references an unknown source {key!r}")
        record["source"] = dict(source)

    def export_archive(self, destination: str | Path, *, created_at: datetime) -> Path:
        """Export the connector-owned canonical slice as bundle-finance-1.

        The current adapter owns source/account/balance/transaction data. Any
        future table outside that slice causes an explicit failure rather than
        being silently omitted from a user backup.
        """
        self._assert_archive_supported()
        source_rows = self._source_rows()

        account_rows = self.connection.execute(
            "SELECT " + ", ".join(_ACCOUNT_COLUMNS) + " FROM accounts ORDER BY account_id"
        ).fetchall()
        accounts = []
        for row in account_rows:
            record = _row_dict(row, _ACCOUNT_COLUMNS)
            self._attach_archive_source(record, source_rows, "account")
            accounts.append(record)

        balance_rows = self.connection.execute(
            "SELECT " + ", ".join(_BALANCE_COLUMNS) +
            " FROM balance_observations ORDER BY observation_id"
        ).fetchall()
        balances = []
        for row in balance_rows:
            record = _row_dict(row, _BALANCE_COLUMNS)
            self._attach_archive_source(record, source_rows, "balance")
            balances.append(record)

        transaction_select = ", ".join(
            f"r.{column}" for column in _TRANSACTION_COLUMNS
        )
        transaction_rows = self.connection.execute(
            "SELECT " + transaction_select + ", t.current_revision "
            "FROM transaction_revisions AS r "
            "JOIN transactions AS t "
            "ON t.account_id = r.account_id "
            "AND t.source_transaction_id = r.source_transaction_id "
            "ORDER BY r.account_id, r.source_transaction_id, r.revision"
        ).fetchall()
        transactions = []
        for row in transaction_rows:
            record = _row_dict(row[: len(_TRANSACTION_COLUMNS)], _TRANSACTION_COLUMNS)
            record["current_revision"] = row[-1]
            try:
                user_tags = json.loads(record.pop("user_tags_json"))
            except (TypeError, json.JSONDecodeError) as exc:
                raise SQLiteStoreError(
                    f"transaction {record.get('transaction_id')!r} has invalid user tags JSON"
                ) from exc
            if not isinstance(user_tags, list) or any(not isinstance(tag, str) for tag in user_tags):
                raise SQLiteStoreError(
                    f"transaction {record.get('transaction_id')!r} has invalid user tags"
                )
            record["user_tags"] = user_tags
            self._attach_archive_source(record, source_rows, "transaction")
            transactions.append(record)

        evidence_rows = self.connection.execute(
            "SELECT " + ", ".join(_COMMITMENT_EVIDENCE_COLUMNS) +
            " FROM commitment_evidence ORDER BY commitment_id, source_id, source_version, related_transaction_id"
        ).fetchall()
        evidence_by_commitment: dict[str, list[dict[str, Any]]] = {}
        for row in evidence_rows:
            record = _row_dict(row, _COMMITMENT_EVIDENCE_COLUMNS)
            self._attach_archive_source(record, source_rows, "commitment evidence")
            evidence_by_commitment.setdefault(str(record["commitment_id"]), []).append(record)

        commitment_rows = self.connection.execute(
            "SELECT " + ", ".join(_COMMITMENT_COLUMNS) +
            " FROM commitments ORDER BY commitment_id"
        ).fetchall()
        commitments = []
        for row in commitment_rows:
            record = _row_dict(row, _COMMITMENT_COLUMNS)
            commitment_id = str(record["commitment_id"])
            record["amount_model"] = _archive_json_document(
                record.pop("amount_model_json"),
                f"commitment {commitment_id}.amount_model_json",
                object_only=True,
            )
            record["evidence"] = evidence_by_commitment.get(commitment_id, [])
            commitments.append(record)

        allocation_rows = self.connection.execute(
            "SELECT " + ", ".join(_BUDGET_ALLOCATION_COLUMNS) +
            " FROM budget_allocations ORDER BY budget_id, category_id"
        ).fetchall()
        allocations_by_budget: dict[str, list[dict[str, Any]]] = {}
        for row in allocation_rows:
            record = _row_dict(row, _BUDGET_ALLOCATION_COLUMNS)
            allocations_by_budget.setdefault(str(record["budget_id"]), []).append(record)

        budget_rows = self.connection.execute(
            "SELECT " + ", ".join(_BUDGET_COLUMNS) +
            " FROM budgets ORDER BY budget_id"
        ).fetchall()
        budgets = []
        for row in budget_rows:
            record = _row_dict(row, _BUDGET_COLUMNS)
            record["allocations"] = allocations_by_budget.get(str(record["budget_id"]), [])
            budgets.append(record)

        goal_plan_rows = self.connection.execute(
            "SELECT " + ", ".join(_GOAL_PLAN_COLUMNS) +
            " FROM goal_plans ORDER BY goal_id, account_id"
        ).fetchall()
        plans_by_goal: dict[str, list[dict[str, Any]]] = {}
        for row in goal_plan_rows:
            record = _row_dict(row, _GOAL_PLAN_COLUMNS)
            plans_by_goal.setdefault(str(record["goal_id"]), []).append(record)

        goal_rows = self.connection.execute(
            "SELECT " + ", ".join(_GOAL_COLUMNS) +
            " FROM goals ORDER BY goal_id"
        ).fetchall()
        goals = []
        for row in goal_rows:
            record = _row_dict(row, _GOAL_COLUMNS)
            record["plans"] = plans_by_goal.get(str(record["goal_id"]), [])
            goals.append(record)

        override_rows = self.connection.execute(
            "SELECT " + ", ".join(_OVERRIDE_COLUMNS) +
            " FROM user_overrides ORDER BY override_id"
        ).fetchall()
        overrides = []
        for row in override_rows:
            record = _row_dict(row, _OVERRIDE_COLUMNS)
            override_id = str(record["override_id"])
            record["old_projection"] = _archive_json_document(
                record.pop("old_projection_json"),
                f"override {override_id}.old_projection_json",
            )
            record["new_user_value"] = _archive_json_document(
                record.pop("new_user_value_json"),
                f"override {override_id}.new_user_value_json",
            )
            overrides.append(record)

        receipt_rows = self.connection.execute(
            "SELECT " + ", ".join(_RECEIPT_COLUMNS) +
            " FROM calculation_receipts ORDER BY receipt_id"
        ).fetchall()
        receipts = []
        for row in receipt_rows:
            record = _row_dict(row, _RECEIPT_COLUMNS)
            receipt_id = str(record["receipt_id"])
            _required_sha256(record["input_hash"], f"receipt {receipt_id}.input_hash")
            input_refs = _archive_json_document(
                record.pop("input_refs_json"),
                f"receipt {receipt_id}.input_refs_json",
            )
            if not isinstance(input_refs, list) or any(not isinstance(ref, str) for ref in input_refs):
                raise SQLiteStoreError(f"receipt {receipt_id}.input_refs_json must be a string list")
            record["input_refs"] = input_refs
            record["outputs"] = _archive_json_document(
                record.pop("outputs_json"),
                f"receipt {receipt_id}.outputs_json",
                object_only=True,
            )
            receipts.append(record)

        referenced_sources = {
            (str(record["source_id"]), int(record["source_version"]))
            for record in (*accounts, *balances, *transactions)
        }
        referenced_sources.update(
            (str(evidence["source_id"]), int(evidence["source_version"]))
            for commitment in commitments
            for evidence in commitment["evidence"]
        )
        if referenced_sources != set(source_rows):
            raise SQLiteStoreError(
                "bundle-finance-1 archive would omit an unreferenced source record"
            )

        return write_export(
            Path(destination),
            created_at=created_at,
            accounts=accounts,
            balances=balances,
            transactions=transactions,
            commitments=commitments,
            budgets=budgets,
            goals=goals,
            overrides=overrides,
            receipts=receipts,
        )

    def _insert_or_verify(
        self,
        table: str,
        columns: tuple[str, ...],
        key_columns: tuple[str, ...],
        record: Mapping[str, Any],
        label: str,
    ) -> bool:
        if any(column not in record for column in columns):
            raise SQLiteStoreError(f"{label} is missing a required persisted field")
        where = " AND ".join(f"{column} = ?" for column in key_columns)
        existing = self.connection.execute(
            f"SELECT {', '.join(columns)} FROM {table} WHERE {where}",
            tuple(record[column] for column in key_columns),
        ).fetchone()
        values = tuple(record[column] for column in columns)
        if existing is not None:
            if tuple(existing) != values:
                raise SQLiteStoreError(f"{label} conflicts with existing local data")
            return False
        placeholders = ", ".join("?" for _ in columns)
        self.connection.execute(
            f"INSERT INTO {table} ({', '.join(columns)}) VALUES ({placeholders})",
            values,
        )
        return True

    def _restore_account(
        self,
        record: Mapping[str, Any],
        source: Mapping[str, Any],
    ) -> None:
        account_id = _required_text(record, "account_id", "archive account")
        if (record.get("source_id"), record.get("source_version")) != (
            source["source_id"], source["source_version"]
        ):
            raise SQLiteStoreError(f"archive account {account_id!r} has a source mismatch")
        currency = _required_text(record, "currency", f"archive account {account_id}").upper()
        account_type = str(record.get("account_type", "")).lower()
        if account_type not in _ACCOUNT_TYPES:
            raise SQLiteStoreError(f"archive account {account_id!r} has an invalid account type")
        if record.get("balance_freshness") not in _ACCOUNT_FRESHNESS:
            raise SQLiteStoreError(f"archive account {account_id!r} has invalid freshness")
        if record.get("status") not in _ACCOUNT_STATUSES:
            raise SQLiteStoreError(f"archive account {account_id!r} has invalid status")
        last_four = record.get("last_four")
        if last_four is not None and (
            not isinstance(last_four, str)
            or len(last_four) != 4
            or not last_four.isdigit()
        ):
            raise SQLiteStoreError(f"archive account {account_id!r} has invalid last_four")
        for field in ("source_version", "current_minor", "available_minor", "include_in_safe_to_spend", "include_in_net_position"):
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool):
                raise SQLiteStoreError(f"archive account {account_id!r}.{field} must be an integer")
        _timestamp(record.get("balance_observed_at"), f"archive account {account_id}.balance_observed_at")
        values = dict(record)
        values["currency"] = currency
        values["account_type"] = account_type
        inserted = self._insert_or_verify(
            "accounts", _ACCOUNT_COLUMNS, ("account_id",), values, f"archive account {account_id!r}"
        )
        if inserted:
            self._ensure_event(
                f"account:{account_id}",
                "account.appended",
                account_id,
                str(record["balance_observed_at"]),
                source["content_hash"],
            )

    def _restore_balance(
        self,
        record: Mapping[str, Any],
        source: Mapping[str, Any],
    ) -> None:
        observation_id = _required_text(record, "observation_id", "archive balance")
        if (record.get("source_id"), record.get("source_version")) != (
            source["source_id"], source["source_version"]
        ):
            raise SQLiteStoreError(f"archive balance {observation_id!r} has a source mismatch")
        for field in ("current_minor", "available_minor", "source_version"):
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool):
                raise SQLiteStoreError(f"archive balance {observation_id!r}.{field} must be an integer")
        status = str(record.get("status", "")).lower()
        if status not in _BALANCE_STATUSES:
            raise SQLiteStoreError(f"archive balance {observation_id!r} has invalid status")
        _timestamp(record.get("observed_at"), f"archive balance {observation_id}.observed_at")
        _timestamp(record.get("recorded_at"), f"archive balance {observation_id}.recorded_at")
        values = dict(record)
        values["status"] = status
        self._insert_or_verify(
            "balance_observations", _BALANCE_COLUMNS, ("observation_id",), values,
            f"archive balance {observation_id!r}",
        )

    def _restore_commitment(self, record: Mapping[str, Any]) -> None:
        commitment_id = _required_text(record, "commitment_id", "archive commitment")
        evidence = record.get("evidence", [])
        if not isinstance(evidence, list) or any(not isinstance(item, Mapping) for item in evidence):
            raise SQLiteStoreError(f"archive commitment {commitment_id!r}.evidence must be a list of objects")
        for field in ("name", "currency", "cadence", "commitment_type", "status", "confidence"):
            _required_text(record, field, f"archive commitment {commitment_id!r}")
        for field in ("grace_window_millis", "user_confirmed"):
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool):
                raise SQLiteStoreError(f"archive commitment {commitment_id!r}.{field} must be an integer")
        if record["user_confirmed"] not in (0, 1):
            raise SQLiteStoreError(f"archive commitment {commitment_id!r}.user_confirmed is invalid")
        for field in ("next_expected_at", "created_at", "updated_at"):
            _timestamp(record.get(field), f"archive commitment {commitment_id}.{field}")
        values = dict(record)
        values["amount_model_json"] = _archive_json_text(
            values.pop("amount_model", None),
            f"archive commitment {commitment_id}.amount_model",
            object_only=True,
        )
        values.pop("evidence", None)
        inserted = self._insert_or_verify(
            "commitments", _COMMITMENT_COLUMNS, ("commitment_id",), values,
            f"archive commitment {commitment_id!r}",
        )
        event_hash = _sha256(record)
        self._ensure_event(
            f"commitment:{commitment_id}", "commitment.appended", commitment_id,
            str(record["updated_at"]), event_hash,
        )
        for evidence_index, evidence_record in enumerate(evidence):
            evidence_label = f"archive commitment {commitment_id!r}.evidence[{evidence_index}]"
            source = _archive_source(evidence_record.get("source"), evidence_label)
            source_id = _required_text(evidence_record, "source_id", evidence_label)
            source_version = evidence_record.get("source_version")
            if not isinstance(source_version, int) or isinstance(source_version, bool) or source_version < 1:
                raise SQLiteStoreError(f"{evidence_label}.source_version must be positive")
            if (source_id, source_version) != (source["source_id"], source["source_version"]):
                raise SQLiteStoreError(f"{evidence_label} has a source mismatch")
            related_transaction_id = evidence_record.get("related_transaction_id")
            if related_transaction_id is not None and not isinstance(related_transaction_id, str):
                raise SQLiteStoreError(f"{evidence_label}.related_transaction_id must be text or null")
            evidence_values = {
                "commitment_id": commitment_id,
                "source_id": source_id,
                "source_version": source_version,
                "related_transaction_id": related_transaction_id,
            }
            self._insert_or_verify(
                "commitment_evidence", _COMMITMENT_EVIDENCE_COLUMNS,
                ("commitment_id", "source_id", "source_version", "related_transaction_id"),
                evidence_values, evidence_label,
            )
            self._ensure_event(
                f"commitment-evidence:{commitment_id}:{source_id}:{source_version}:"
                f"{related_transaction_id or ''}",
                "commitment_evidence.appended",
                commitment_id,
                source["observed_at"],
                _sha256(evidence_record),
            )

    def _restore_budget(self, record: Mapping[str, Any]) -> None:
        budget_id = _required_text(record, "budget_id", "archive budget")
        allocations = record.get("allocations", [])
        if not isinstance(allocations, list) or any(not isinstance(item, Mapping) for item in allocations):
            raise SQLiteStoreError(f"archive budget {budget_id!r}.allocations must be a list of objects")
        for field in ("name", "mode", "period_rule", "rollover_policy", "status"):
            _required_text(record, field, f"archive budget {budget_id!r}")
        _timestamp(record.get("starts_at"), f"archive budget {budget_id}.starts_at")
        _timestamp(record.get("ends_at"), f"archive budget {budget_id}.ends_at", optional=True)
        values = dict(record)
        values.pop("allocations", None)
        self._insert_or_verify(
            "budgets", _BUDGET_COLUMNS, ("budget_id",), values,
            f"archive budget {budget_id!r}",
        )
        self._ensure_event(
            f"budget:{budget_id}", "budget.appended", budget_id,
            str(record["starts_at"]), _sha256(record),
        )
        for allocation_index, allocation in enumerate(allocations):
            allocation_label = f"archive budget {budget_id!r}.allocations[{allocation_index}]"
            if allocation.get("budget_id") != budget_id:
                raise SQLiteStoreError(f"{allocation_label} has a budget mismatch")
            _required_text(allocation, "category_id", allocation_label)
            for field in _BUDGET_ALLOCATION_COLUMNS[2:]:
                value = allocation.get(field)
                if not isinstance(value, int) or isinstance(value, bool):
                    raise SQLiteStoreError(f"{allocation_label}.{field} must be an integer")
            allocation_values = {column: allocation.get(column) for column in _BUDGET_ALLOCATION_COLUMNS}
            self._insert_or_verify(
                "budget_allocations", _BUDGET_ALLOCATION_COLUMNS,
                ("budget_id", "category_id"), allocation_values, allocation_label,
            )
            self._ensure_event(
                f"budget-allocation:{budget_id}:{allocation['category_id']}",
                "budget_allocation.appended", budget_id, str(record["starts_at"]),
                _sha256(allocation),
            )

    def _restore_goal(self, record: Mapping[str, Any]) -> None:
        goal_id = _required_text(record, "goal_id", "archive goal")
        plans = record.get("plans", [])
        if not isinstance(plans, list) or any(not isinstance(item, Mapping) for item in plans):
            raise SQLiteStoreError(f"archive goal {goal_id!r}.plans must be a list of objects")
        for field in ("name", "currency", "status"):
            _required_text(record, field, f"archive goal {goal_id!r}")
        for field in ("target_minor", "current_reserved_minor", "planned_contribution_minor", "priority", "protected"):
            value = record.get(field)
            if not isinstance(value, int) or isinstance(value, bool):
                raise SQLiteStoreError(f"archive goal {goal_id!r}.{field} must be an integer")
        if any(record[field] < 0 for field in ("target_minor", "current_reserved_minor", "planned_contribution_minor")):
            raise SQLiteStoreError(f"archive goal {goal_id!r} contains a negative amount")
        if record["protected"] not in (0, 1):
            raise SQLiteStoreError(f"archive goal {goal_id!r}.protected is invalid")
        _timestamp(record.get("target_date"), f"archive goal {goal_id}.target_date", optional=True)
        values = dict(record)
        values.pop("plans", None)
        self._insert_or_verify(
            "goals", _GOAL_COLUMNS, ("goal_id",), values,
            f"archive goal {goal_id!r}",
        )
        self._ensure_event(
            f"goal:{goal_id}", "goal.appended", goal_id,
            str(record.get("target_date") or "1970-01-01T00:00:00Z"), _sha256(record),
        )
        for plan_index, plan in enumerate(plans):
            plan_label = f"archive goal {goal_id!r}.plans[{plan_index}]"
            if plan.get("goal_id") != goal_id:
                raise SQLiteStoreError(f"{plan_label} has a goal mismatch")
            account_id = _required_text(plan, "account_id", plan_label)
            plan_values = {"goal_id": goal_id, "account_id": account_id}
            self._insert_or_verify(
                "goal_plans", _GOAL_PLAN_COLUMNS, ("goal_id", "account_id"),
                plan_values, plan_label,
            )
            self._ensure_event(
                f"goal-plan:{goal_id}:{account_id}", "goal_plan.appended", goal_id,
                str(record.get("target_date") or "1970-01-01T00:00:00Z"),
                _sha256(plan),
            )

    def _restore_override(self, record: Mapping[str, Any]) -> None:
        override_id = _required_text(record, "override_id", "archive override")
        for field in ("object_type", "object_id", "field"):
            _required_text(record, field, f"archive override {override_id!r}")
        _timestamp(record.get("created_at"), f"archive override {override_id}.created_at")
        values = dict(record)
        values["old_projection_json"] = _archive_json_text(
            values.pop("old_projection", None),
            f"archive override {override_id}.old_projection",
        )
        values["new_user_value_json"] = _archive_json_text(
            values.pop("new_user_value", None),
            f"archive override {override_id}.new_user_value",
        )
        self._insert_or_verify(
            "user_overrides", _OVERRIDE_COLUMNS, ("override_id",), values,
            f"archive override {override_id!r}",
        )
        self._ensure_event(
            f"override:{override_id}", "user_override.appended", override_id,
            str(record["created_at"]), _sha256(record),
        )

    def _restore_receipt(self, record: Mapping[str, Any]) -> None:
        receipt_id = _required_text(record, "receipt_id", "archive receipt")
        for field in ("engine", "engine_version", "policy_id", "policy_version"):
            _required_text(record, field, f"archive receipt {receipt_id!r}")
        _timestamp(record.get("calculated_at"), f"archive receipt {receipt_id}.calculated_at")
        input_refs = record.get("input_refs")
        if not isinstance(input_refs, list) or any(not isinstance(ref, str) for ref in input_refs):
            raise SQLiteStoreError(f"archive receipt {receipt_id!r}.input_refs must be a string list")
        input_hash = _required_sha256(record.get("input_hash"), f"archive receipt {receipt_id}.input_hash")
        values = dict(record)
        values["input_refs_json"] = _archive_json_text(values.pop("input_refs"), f"archive receipt {receipt_id}.input_refs")
        values["outputs_json"] = _archive_json_text(
            values.pop("outputs", None), f"archive receipt {receipt_id}.outputs", object_only=True
        )
        values["input_hash"] = input_hash
        self._insert_or_verify(
            "calculation_receipts", _RECEIPT_COLUMNS, ("receipt_id",), values,
            f"archive receipt {receipt_id!r}",
        )
        self._ensure_event(
            f"calculation-receipt:{receipt_id}", "calculation_receipt.appended", receipt_id,
            str(record["calculated_at"]), _sha256(record),
        )

    def import_archive(self, source: str | Path) -> None:
        """Restore the represented archive slice atomically and idempotently."""
        archive = read_export(Path(source))
        accounts = archive.get("accounts.json")
        balances = archive.get("balances.jsonl")
        transactions = archive.get("transactions.jsonl")
        commitments = archive.get("commitments.json")
        budgets = archive.get("budgets.json")
        goals = archive.get("goals.json")
        overrides = archive.get("overrides.jsonl")
        receipts = archive.get("receipts.jsonl")
        if not isinstance(accounts, list) or not isinstance(balances, list) or not isinstance(transactions, list):
            raise SQLiteStoreError("bundle-finance-1 archive has invalid canonical record lists")
        for filename, records in (
            ("commitments.json", commitments),
            ("budgets.json", budgets),
            ("goals.json", goals),
            ("overrides.jsonl", overrides),
            ("receipts.jsonl", receipts),
        ):
            if not isinstance(records, list) or any(not isinstance(record, Mapping) for record in records):
                raise SQLiteStoreError(f"bundle-finance-1 archive has invalid records in {filename}")

        source_records: dict[tuple[str, int], Mapping[str, Any]] = {}
        for index, record in enumerate((*accounts, *balances, *transactions)):
            if not isinstance(record, Mapping):
                raise SQLiteStoreError(f"archive canonical record {index} must be an object")
            normalized_source = _archive_source(record.get("source"), f"archive record {index}")
            key = _archive_source_key(normalized_source)
            existing = source_records.get(key)
            if existing is not None and dict(existing) != dict(normalized_source):
                raise SQLiteStoreError(f"archive contains conflicting source record {key!r}")
            source_records[key] = normalized_source
        for commitment_index, commitment in enumerate(commitments):
            evidence = commitment.get("evidence", [])
            if not isinstance(evidence, list) or any(not isinstance(item, Mapping) for item in evidence):
                raise SQLiteStoreError(
                    f"archive commitment {commitment_index}.evidence must be a list of objects"
                )
            for evidence_index, evidence_record in enumerate(evidence):
                normalized_source = _archive_source(
                    evidence_record.get("source"),
                    f"archive commitment {commitment_index}.evidence[{evidence_index}]",
                )
                key = _archive_source_key(normalized_source)
                existing = source_records.get(key)
                if existing is not None and dict(existing) != dict(normalized_source):
                    raise SQLiteStoreError(f"archive contains conflicting source record {key!r}")
                source_records[key] = normalized_source

        transaction_revisions: dict[tuple[str, str], list[int]] = {}
        transaction_current: dict[tuple[str, str], int] = {}
        for index, record in enumerate(transactions):
            if not isinstance(record, Mapping):
                raise SQLiteStoreError(f"archive transaction {index} must be an object")
            key = (str(record.get("account_id", "")), str(record.get("source_transaction_id", "")))
            revision = record.get("revision")
            current_revision = record.get("current_revision")
            if not isinstance(revision, int) or isinstance(revision, bool) or revision < 1:
                raise SQLiteStoreError(f"archive transaction {index} has an invalid revision")
            if not isinstance(current_revision, int) or isinstance(current_revision, bool) or current_revision < 1:
                raise SQLiteStoreError(f"archive transaction {index} has an invalid current revision")
            transaction_revisions.setdefault(key, []).append(revision)
            prior_current = transaction_current.setdefault(key, current_revision)
            if prior_current != current_revision:
                raise SQLiteStoreError(f"archive transaction group {key!r} has conflicting current revisions")
        for key, revisions in transaction_revisions.items():
            if transaction_current[key] != max(revisions):
                raise SQLiteStoreError(f"archive transaction group {key!r} has an inconsistent current revision")

        try:
            with self.connection:
                self.connection.execute("BEGIN IMMEDIATE")
                for key in sorted(source_records):
                    self._ensure_source(source_records[key])
                for index, record in enumerate(accounts):
                    normalized_source = _archive_source(record.get("source"), f"archive account {index}")
                    self._restore_account(record, normalized_source)
                for index, record in enumerate(balances):
                    normalized_source = _archive_source(record.get("source"), f"archive balance {index}")
                    self._restore_balance(record, normalized_source)
                ordered_transactions = sorted(
                    transactions,
                    key=lambda record: (
                        str(record.get("account_id", "")),
                        str(record.get("source_transaction_id", "")),
                        int(record.get("revision", 0)),
                    ),
                )
                for index, record in enumerate(ordered_transactions):
                    normalized_source = _archive_source(record.get("source"), f"archive transaction {index}")
                    transaction = dict(record)
                    transaction["amount"] = {
                        "minor_units": transaction.pop("amount_minor"),
                        "currency": transaction.get("currency"),
                    }
                    transaction["user_tags"] = transaction.get("user_tags", [])
                    transaction["source_ref"] = {
                        "source_id": normalized_source["source_id"],
                        "source_kind": normalized_source["source_kind"],
                        "imported_at": normalized_source["imported_at"],
                        "observed_at": normalized_source["observed_at"],
                        "content_hash": normalized_source["content_hash"],
                        "source_version": normalized_source["source_version"],
                    }
                    self._append_transaction(transaction, normalized_source)
                for record in commitments:
                    self._restore_commitment(record)
                for record in budgets:
                    self._restore_budget(record)
                for record in goals:
                    self._restore_goal(record)
                for record in overrides:
                    self._restore_override(record)
                for record in receipts:
                    self._restore_receipt(record)
        except sqlite3.Error as exc:
            raise SQLiteStoreError("SQLite archive restore failed") from exc
