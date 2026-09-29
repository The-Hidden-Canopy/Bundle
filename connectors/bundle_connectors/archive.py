"""Portable, local-only bundle-finance-1 export and import."""

from __future__ import annotations

import hashlib
import json
from datetime import datetime
from pathlib import Path
from typing import Any, Mapping, Sequence

from .protocol import ConnectorContractError, timestamp_text, require_utc


ARCHIVE_FILES = (
    "accounts.json",
    "balances.jsonl",
    "transactions.jsonl",
    "commitments.json",
    "budgets.json",
    "goals.json",
    "overrides.jsonl",
    "receipts.jsonl",
)
SECRET_PARTS = (
    "password", "secret", "credential", "access_token", "auth_token",
    "refresh_token", "api_key", "api-key", "private_key",
)


def _reject_secrets(value: Any, path: str = "record") -> None:
    if isinstance(value, Mapping):
        for key, child in value.items():
            lowered = str(key).lower()
            if any(part in lowered for part in SECRET_PARTS):
                raise ConnectorContractError(f"export contains forbidden secret field at {path}.{key}")
            _reject_secrets(child, f"{path}.{key}")
    elif isinstance(value, Sequence) and not isinstance(value, (str, bytes, bytearray)):
        for index, child in enumerate(value):
            _reject_secrets(child, f"{path}[{index}]")


def _json_bytes(value: Any) -> bytes:
    _reject_secrets(value)
    try:
        serialized = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    except (TypeError, ValueError) as exc:
        raise ConnectorContractError("archive payload is not JSON serializable") from exc
    return (serialized + "\n").encode("utf-8")


def _record_sequence(values: Sequence[Mapping[str, Any]], label: str) -> list[Mapping[str, Any]]:
    if isinstance(values, (str, bytes, bytearray)) or not isinstance(values, Sequence):
        raise ConnectorContractError(f"{label} must be a sequence of objects")
    records = list(values)
    if any(not isinstance(record, Mapping) for record in records):
        raise ConnectorContractError(f"{label} must contain only objects")
    return records


def _jsonl_bytes(values: Sequence[Mapping[str, Any]]) -> bytes:
    values = _record_sequence(values, "JSONL records")

    def sort_key(value: Mapping[str, Any]) -> tuple[str, str]:
        for field in (
            "id",
            "transaction_id",
            "observation_id",
            "commitment_id",
            "budget_id",
            "goal_id",
            "override_id",
            "receipt_id",
        ):
            identifier = value.get(field)
            if identifier is not None:
                return field, str(identifier)
        return "record", json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))

    try:
        ordered = sorted(values, key=sort_key)
    except (TypeError, ValueError) as exc:
        raise ConnectorContractError("archive JSONL record is not JSON serializable") from exc
    _reject_secrets(ordered)
    try:
        return b"".join(
            (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")
            for value in ordered
        )
    except (TypeError, ValueError) as exc:
        raise ConnectorContractError("archive JSONL record is not JSON serializable") from exc


def _safe_relative(path: str) -> Path:
    candidate = Path(path)
    if candidate.is_absolute() or ".." in candidate.parts or candidate.name != path:
        raise ConnectorContractError(f"unsafe archive path: {path!r}")
    return candidate


def write_export(
    destination: Path,
    *,
    created_at: datetime,
    accounts: Sequence[Mapping[str, Any]] = (),
    balances: Sequence[Mapping[str, Any]] = (),
    transactions: Sequence[Mapping[str, Any]] = (),
    commitments: Sequence[Mapping[str, Any]] = (),
    budgets: Sequence[Mapping[str, Any]] = (),
    goals: Sequence[Mapping[str, Any]] = (),
    overrides: Sequence[Mapping[str, Any]] = (),
    receipts: Sequence[Mapping[str, Any]] = (),
) -> Path:
    created = require_utc(created_at)
    if destination.exists():
        raise ConnectorContractError("export destination already exists")
    accounts = _record_sequence(accounts, "accounts")
    balances = _record_sequence(balances, "balances")
    transactions = _record_sequence(transactions, "transactions")
    commitments = _record_sequence(commitments, "commitments")
    budgets = _record_sequence(budgets, "budgets")
    goals = _record_sequence(goals, "goals")
    overrides = _record_sequence(overrides, "overrides")
    receipts = _record_sequence(receipts, "receipts")
    payloads = {
        "accounts.json": _json_bytes(sorted(accounts, key=lambda value: str(value.get("account_id", "")))),
        "balances.jsonl": _jsonl_bytes(balances),
        "transactions.jsonl": _jsonl_bytes(transactions),
        "commitments.json": _json_bytes(sorted(commitments, key=lambda value: str(value.get("commitment_id", "")))),
        "budgets.json": _json_bytes(sorted(budgets, key=lambda value: str(value.get("budget_id", "")))),
        "goals.json": _json_bytes(sorted(goals, key=lambda value: str(value.get("goal_id", "")))),
        "overrides.jsonl": _jsonl_bytes(overrides),
        "receipts.jsonl": _jsonl_bytes(receipts),
    }
    try:
        destination.mkdir(parents=True)
    except OSError as exc:
        raise ConnectorContractError("could not create export destination") from exc
    files: list[dict[str, Any]] = []
    for name in ARCHIVE_FILES:
        path = destination / _safe_relative(name)
        try:
            path.write_bytes(payloads[name])
        except OSError as exc:
            raise ConnectorContractError(f"could not write export file: {name}") from exc
        files.append({"path": name, "bytes": len(payloads[name]), "sha256": hashlib.sha256(payloads[name]).hexdigest()})
    manifest = {
        "schema": "bundle-finance-1",
        "created_at": timestamp_text(created),
        "files": files,
    }
    try:
        (destination / "manifest.json").write_bytes(_json_bytes(manifest))
    except OSError as exc:
        raise ConnectorContractError("could not write export manifest") from exc
    return destination


def read_export(source: Path) -> dict[str, Any]:
    if not source.is_dir():
        raise ConnectorContractError("export source must be a directory")
    manifest_path = source / "manifest.json"
    if not manifest_path.is_file():
        raise ConnectorContractError("manifest.json is required")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ConnectorContractError("manifest.json is not valid UTF-8 JSON") from exc
    if (
        not isinstance(manifest, Mapping)
        or set(manifest) != {"schema", "created_at", "files"}
        or manifest.get("schema") != "bundle-finance-1"
    ):
        raise ConnectorContractError("unsupported export schema")
    created_at = manifest.get("created_at")
    if not isinstance(created_at, str):
        raise ConnectorContractError("manifest created_at must be a timestamp")
    try:
        require_utc(datetime.fromisoformat(created_at.replace("Z", "+00:00")))
    except (TypeError, ValueError) as exc:
        raise ConnectorContractError("manifest created_at must be a timezone-aware timestamp") from exc
    entries = manifest.get("files")
    if not isinstance(entries, list) or len(entries) != len(ARCHIVE_FILES):
        raise ConnectorContractError("manifest file list does not match bundle-finance-1")
    if any(
        not isinstance(entry, Mapping)
        or set(entry) != {"path", "bytes", "sha256"}
        or not isinstance(entry.get("path"), str)
        or not isinstance(entry.get("bytes"), int)
        or isinstance(entry.get("bytes"), bool)
        or entry.get("bytes", -1) < 0
        or not isinstance(entry.get("sha256"), str)
        or len(entry["sha256"]) != 64
        or any(character not in "0123456789abcdef" for character in entry["sha256"])
        for entry in entries
    ) or {entry["path"] for entry in entries} != set(ARCHIVE_FILES):
        raise ConnectorContractError("manifest file list does not match bundle-finance-1")

    output: dict[str, Any] = {"manifest": manifest}
    for entry in entries:
        name = entry["path"]
        path = source / _safe_relative(name)
        if not path.is_file():
            raise ConnectorContractError(f"missing export file: {name}")
        try:
            payload = path.read_bytes()
        except OSError as exc:
            raise ConnectorContractError(f"could not read export file: {name}") from exc
        if len(payload) != entry["bytes"] or hashlib.sha256(payload).hexdigest() != entry["sha256"]:
            raise ConnectorContractError(f"hash or size mismatch for {name}")
        try:
            decoded = payload.decode("utf-8")
            if name.endswith(".jsonl"):
                lines = decoded.splitlines()
                if any(not line.strip() for line in lines):
                    raise ConnectorContractError(f"export file contains a blank JSONL line: {name}")
                parsed = [json.loads(line) for line in lines]
            else:
                parsed = json.loads(decoded)
        except ConnectorContractError:
            raise
        except (UnicodeError, json.JSONDecodeError) as exc:
            raise ConnectorContractError(f"export file is not valid JSON: {name}") from exc
        if not isinstance(parsed, list) or any(not isinstance(record, Mapping) for record in parsed):
            raise ConnectorContractError(f"export file must contain a list of objects: {name}")
        output[name] = parsed
    return output
