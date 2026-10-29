"""Narrow adapter between the Bundle interface and the real Bundle backend.

Every read and write goes through the native `bundle` executable (the same
binary the parity suite checks against the Python bridge). The only Python
bridge code used directly is the OFX/QFX parser, whose output is handed to the
native admission path as a bundle-connector-1 batch. Nothing here talks to a
network, a bank or a model.
"""

from __future__ import annotations

import hashlib
import io
import json
import os
import re
import secrets
import subprocess
import sys
import tempfile
import threading
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "connectors"))

from bundle_connectors.ofx_import import import_ofx  # noqa: E402
from bundle_connectors.protocol import ConnectorContractError  # noqa: E402

MODES = ("confirmed", "expected", "stress")
HORIZON_DAYS = (7, 14, 30, 60, 90)
MAX_UPLOAD_BYTES = 10 * 1024 * 1024
STALE_AFTER_DAYS = 7  # interface rule for "may be out of date", shown as such


class BackendError(Exception):
    """A rejected request with a message the interface can show as-is."""

    def __init__(self, message: str, *, recovery: str = "", status: int = 400) -> None:
        super().__init__(message)
        self.message = message
        self.recovery = recovery
        self.status = status

    def to_dict(self) -> dict[str, Any]:
        return {"error": self.message, "recovery": self.recovery}


def find_native_cli() -> Path:
    configured = os.environ.get("BUNDLE_NATIVE_CLI")
    candidates = [Path(configured)] if configured else [
        REPO / build / "core" / "persistence" / name
        for build in ("build", "build-release")
        for name in ("bundle.exe", "bundle")
    ] + [REPO / "build" / "core" / "persistence" / "Release" / "bundle.exe"]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    raise BackendError(
        "The native bundle executable was not found.",
        recovery="Build it with CMake (see README) or set BUNDLE_NATIVE_CLI to its path.",
        status=500,
    )


def utc_now() -> datetime:
    return datetime.now(timezone.utc).replace(microsecond=0)


def rfc3339(value: datetime) -> str:
    return value.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")


def parse_time(value: str) -> datetime:
    return datetime.fromisoformat(value[:-1] + "+00:00" if value.endswith("Z") else value)


@dataclass
class ImportPreview:
    preview_id: str
    filename: str
    file_format: str
    batch: dict[str, Any]
    summary: dict[str, Any]


@dataclass
class Workspace:
    """One local database. `demo` marks the synthetic demo workspace."""

    database: Path
    demo: bool = False
    backups: Path = field(init=False)

    def __post_init__(self) -> None:
        self.database = self.database.resolve()
        self.backups = self.database.parent / (self.database.stem + "-backups")


class BundleBackend:
    def __init__(self, database: Path, *, native_cli: Path | None = None,
                 clock: Any = utc_now) -> None:
        self.native = native_cli or find_native_cli()
        self.clock = clock
        self.user = Workspace(Path(database))
        self.active = self.user
        self._previews: dict[str, ImportPreview] = {}
        self._lock = threading.Lock()  # one native write at a time per process

    # -- native process -----------------------------------------------------------------
    def _run(self, *arguments: str | Path, expect_json: bool = False) -> Any:
        result = subprocess.run(
            [str(self.native), *map(str, arguments)],
            capture_output=True, text=True, encoding="utf-8", timeout=300,
        )
        if result.returncode != 0:
            message = (result.stderr or result.stdout).strip()
            message = re.sub(r"^bundle: (rejected: )?", "", message).split("\nRun 'bundle help'")[0]
            raise BackendError(message or f"bundle exited with status {result.returncode}",
                               recovery=_recovery_for(message), status=422 if result.returncode == 1 else 400)
        return json.loads(result.stdout) if expect_json else result.stdout

    def _db(self) -> list[str]:
        return ["--db", str(self.active.database)]

    # -- workspaces ---------------------------------------------------------------------
    def workspace_info(self) -> dict[str, Any]:
        backups = self.list_backups()
        return {
            "database": str(self.active.database),
            "demo": self.active.demo,
            "user_database": str(self.user.database),
            "backups_directory": str(self.active.backups),
            "backups": backups,
            "native_cli": str(self.native),
        }

    def use_demo(self, anchor: datetime | None = None) -> dict[str, Any]:
        from .demo import build_demo_workspace

        demo_db = self.user.database.parent / "bundle-demo" / "demo.db"
        with self._lock:
            build_demo_workspace(self, demo_db, anchor or self.clock())
        self.active = Workspace(demo_db, demo=True)
        return self.workspace_info()

    def use_user_data(self) -> dict[str, Any]:
        self.active = self.user
        return self.workspace_info()

    # -- reads --------------------------------------------------------------------------
    def snapshot(self) -> dict[str, Any]:
        snapshot = self._run("snapshot", *self._db(), expect_json=True)
        now = self.clock()
        for account in snapshot["accounts"]:
            observed = parse_time(account["balance_observed_at"])
            age = now - observed
            account["age_days"] = round(age.total_seconds() / 86400, 1)
            account["stale_by_age"] = age > timedelta(days=STALE_AFTER_DAYS)
            account["has_observed_balance"] = account["balance_freshness"] != "unavailable"
        snapshot["stale_after_days"] = STALE_AFTER_DAYS
        snapshot["now"] = rfc3339(now)
        return snapshot

    def calculate(self, *, mode: str, horizon_days: int, floor_minor: int, record: bool = False,
                  what_if: dict[str, Any] | None = None) -> dict[str, Any]:
        if mode not in MODES:
            raise BackendError("Choose Confirmed, Expected or Stress.")
        if horizon_days not in HORIZON_DAYS:
            raise BackendError(f"Choose a period of {', '.join(map(str, HORIZON_DAYS))} days.")
        if floor_minor < 0:
            raise BackendError("The protected cash floor cannot be negative.")
        now = self.clock()
        as_of = now.replace(second=0)
        horizon = as_of + timedelta(days=horizon_days)
        arguments = ["calculate", *self._db(), "--as-of", rfc3339(as_of), "--horizon-end", rfc3339(horizon),
                     "--mode", mode, "--floor-minor", str(floor_minor), "--json"]
        if not record:
            arguments.append("--no-record")
        if what_if:
            amount = int(what_if.get("amount_minor", 0))
            date = str(what_if.get("date", ""))
            if amount <= 0:
                raise BackendError("Enter a what-if amount greater than zero.")
            arguments += ["--what-if-minor", str(amount), "--what-if-date", date]
            if what_if.get("account_id"):
                arguments += ["--what-if-account", str(what_if["account_id"])]
        with self._lock:
            result = self._run(*arguments, expect_json=True)
        result["as_of"] = rfc3339(as_of)
        result["horizon_end"] = rfc3339(horizon)
        result["horizon_days"] = horizon_days
        return result

    def saved_floor(self, snapshot: dict[str, Any]) -> dict[str, Any] | None:
        """The protected floor is persisted as an input of the latest calculation receipt."""
        for receipt in snapshot["receipts"]:
            outputs = receipt.get("outputs") or {}
            if receipt.get("engine") == "bundle.safe_to_spend" and "protected_floor_minor" in outputs:
                return {"floor_minor": outputs["protected_floor_minor"], "receipt_id": receipt["receipt_id"],
                        "calculated_at": receipt["calculated_at"], "mode": receipt["policy_id"]}
        return None

    def history(self, limit: int = 100) -> list[dict[str, Any]]:
        output = self._run("history", *self._db(), "--json")
        events = [json.loads(line) for line in output.splitlines() if line.strip()]
        return events[-limit:][::-1]

    # -- import -------------------------------------------------------------------------
    def preview_import(self, filename: str, content: str, *, source_id: str = "",
                       statement_utc_offset: str = "") -> dict[str, Any]:
        data = content.encode("utf-8")
        if len(data) > MAX_UPLOAD_BYTES:
            raise BackendError("That file is larger than 10 MB.", recovery="Split the statement into smaller files.")
        if not data.strip():
            raise BackendError("The file is empty.", recovery="Choose the exported statement file, not an empty one.")
        suffix = Path(filename).suffix.lower()
        digest = hashlib.sha256(data).hexdigest()
        file_format = {".csv": "csv", ".ofx": "ofx", ".qfx": "qfx", ".json": "bundle-connector-1"}.get(suffix)
        if file_format is None:
            raise BackendError(
                f"Bundle cannot read '{suffix or filename}' files.",
                recovery="Supported formats: canonical Bundle CSV (.csv), OFX (.ofx), QFX (.qfx) and "
                         "bundle-connector-1 batch JSON (.json).",
            )
        offset = None
        if file_format in ("ofx", "qfx") and statement_utc_offset:
            match = re.fullmatch(r"([+-])(\d{2}):(\d{2})", statement_utc_offset.strip())
            if not match:
                raise BackendError("Enter the statement time zone as an offset such as -05:00.")
            sign = 1 if match.group(1) == "+" else -1
            offset = timezone(sign * timedelta(hours=int(match.group(2)), minutes=int(match.group(3))))
        requested_source = source_id.strip()
        fingerprint = f"{file_format.split('-')[0]}-{digest[:12]}"
        skipped = 0
        version: int | None = None
        if file_format == "bundle-connector-1":
            try:
                batch = json.loads(content)
            except json.JSONDecodeError as error:
                raise BackendError(f"The JSON file is not valid: {error.msg} (line {error.lineno}).",
                                   recovery="Export the batch again from its connector.") from error
            source_id, imported_at = "(set by the batch)", None
        else:
            snapshot = self._run("snapshot", *self._db(), expect_json=True)
            probe = self._convert(file_format, data, content, requested_source or fingerprint, 1,
                                  rfc3339(self.clock()), offset)
            series = self._series_for(snapshot, probe, requested_source)
            if series is None:
                # A new source. An identical file re-uses its original import time, so it
                # produces an identical batch and is admitted once (no double counting).
                source_id, version = requested_source or fingerprint, 1
                imported_at = self._known_import_time(snapshot, source_id) or rfc3339(self.clock())
                batch = self._convert(file_format, data, content, source_id, version, imported_at, offset)
            else:
                # A later statement for accounts Bundle already has: it continues their
                # source series as the next version, and rows already stored unchanged are
                # left out (statements usually overlap). Changed rows are refused.
                source_id, version = series
                imported_at = rfc3339(self.clock())
                batch = self._convert(file_format, data, content, source_id, version, imported_at, offset)
                batch, skipped = self._without_known_rows(snapshot, batch)
                if not batch["transactions"] and not batch["balances"]:
                    return self._store_preview(filename, file_format, batch, {
                        "source_id": source_id, "source_version": version, "imported_at": imported_at,
                        "batch_id": None, "already_imported": True, "new_events": 0, "skipped_known": skipped})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "batch.json"
            path.write_text(json.dumps(batch, ensure_ascii=False), encoding="utf-8")
            check = self._run("import-batch", *self._db(), "--dry-run", path, expect_json=True)
        return self._store_preview(filename, file_format, batch, {
            "source_id": source_id, "source_version": version, "imported_at": imported_at,
            "batch_id": check["batch_id"], "already_imported": check["already_admitted"],
            "new_events": check["new_events"], "skipped_known": skipped})

    def _store_preview(self, filename: str, file_format: str, batch: dict[str, Any],
                       check: dict[str, Any]) -> dict[str, Any]:
        transactions = batch.get("transactions", [])
        dates = sorted(t.get("posted_at") or t.get("observed_at") or "" for t in transactions)
        summary = {
            "filename": filename,
            "format": file_format,
            **check,
            "accounts": [{"account_id": a.get("account_id"), "display_name": a.get("display_name"),
                          "currency": a.get("currency")} for a in batch.get("accounts", [])],
            "transaction_count": len(transactions),
            "balance_count": len(batch.get("balances", [])),
            "first_date": dates[0] if dates else None,
            "last_date": dates[-1] if dates else None,
            "debits_minor": sum(t["amount"]["minor_units"] for t in transactions if t.get("direction") == "debit"),
            "credits_minor": sum(t["amount"]["minor_units"] for t in transactions if t.get("direction") == "credit"),
            "sample": [
                {"transaction_id": t.get("transaction_id"), "date": t.get("posted_at") or t.get("observed_at"),
                 "description": t.get("description"), "direction": t.get("direction"),
                 "amount_minor": t["amount"]["minor_units"], "currency": t["amount"]["currency"],
                 "state": t.get("transaction_state")}
                for t in transactions[:25]
            ],
        }
        preview = ImportPreview(secrets.token_hex(8), filename, file_format, batch, summary)
        self._previews[preview.preview_id] = preview
        return {"preview_id": preview.preview_id, **summary}

    def commit_import(self, preview_id: str) -> dict[str, Any]:
        preview = self._previews.get(preview_id)
        if preview is None:
            raise BackendError("That preview has expired.", recovery="Choose the file again to preview it.")
        if preview.summary["batch_id"] is None:
            return {"batch_id": None, "changed": False, "message": "nothing changed: every row is already stored"}
        with self._lock, tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "batch.json"
            path.write_text(json.dumps(preview.batch, ensure_ascii=False), encoding="utf-8")
            output = self._run("import-batch", *self._db(), path)
        changed = "nothing changed" not in output
        return {"batch_id": preview.summary["batch_id"], "changed": changed, "message": output.strip()}

    @staticmethod
    def _known_import_time(snapshot: dict[str, Any], source_id: str) -> str | None:
        for source in snapshot["sources"]:
            if source["source_id"] == source_id and source["source_version"] == 1:
                return source["imported_at"]
        return None

    def _convert(self, file_format: str, data: bytes, content: str, source_id: str, version: int,
                 imported_at: str, offset: timezone | None) -> dict[str, Any]:
        if file_format == "csv":
            with tempfile.TemporaryDirectory() as temporary:
                path = Path(temporary) / "statement.csv"
                path.write_bytes(data)
                return self._run("csv-to-batch", "--source-id", source_id, "--imported-at", imported_at,
                                 "--source-version", str(version), path, expect_json=True)
        try:
            parsed = import_ofx(io.StringIO(content), source_id=source_id, imported_at=parse_time(imported_at),
                                source_version=version, source_timezone=offset, connector_id=file_format)
        except ConnectorContractError as error:
            raise BackendError(str(error), recovery=_recovery_for(str(error))) from error
        return parsed.to_dict()

    @staticmethod
    def _series_for(snapshot: dict[str, Any], batch: dict[str, Any], requested: str) -> tuple[str, int] | None:
        """The (source_id, next version) a statement must use for accounts already stored."""
        owners = {a["account_id"]: a["source_id"] for a in snapshot["accounts"]}
        known = {owners[a["account_id"]] for a in batch["accounts"] if a["account_id"] in owners}
        if not known:
            return None
        if len(known) > 1:
            raise BackendError("This file mixes accounts that were imported from different sources.",
                               recovery="Split it into one file per account and import each one. Nothing was written.")
        owner = known.pop()
        if requested and requested != owner:
            raise BackendError(f"These accounts already belong to source '{owner}'.",
                               recovery="Leave Source ID empty so the statement continues that source. "
                                        "Nothing was written.")
        versions = [s["source_version"] for s in snapshot["sources"] if s["source_id"] == owner]
        return owner, max(versions, default=0) + 1

    @staticmethod
    def _without_known_rows(snapshot: dict[str, Any], batch: dict[str, Any]) -> tuple[dict[str, Any], int]:
        """Drop rows already stored unchanged; refuse rows that changed (no correction path yet)."""
        stored = {t["transaction_id"]: t for t in snapshot["transactions"]}
        fields = ("account_id", "direction", "transaction_state", "description", "merchant_raw", "observed_at")
        keep, skipped, changed = [], 0, []
        for row in batch["transactions"]:
            existing = stored.get(row["transaction_id"])
            if existing is None:
                keep.append(row)
            elif all((row.get(f) or "") == (existing.get(f) or "") for f in fields) \
                    and row["amount"]["minor_units"] == existing["amount_minor"]:
                skipped += 1
            else:
                changed.append(row["transaction_id"])
        if changed:
            shown = ", ".join(changed[:5]) + (" and more" if len(changed) > 5 else "")
            raise BackendError(
                f"{len(changed)} transaction(s) in this file differ from the copies already stored: {shown}.",
                recovery="Bundle cannot apply corrections to already-imported transactions from a statement "
                         "file yet. Nothing was written.")
        latest: dict[str, dict[str, Any]] = {}
        for observation in snapshot["balance_observations"]:  # newest first
            latest.setdefault(observation["account_id"], observation)
        balances = [b for b in batch["balances"]
                    if (latest.get(b["account_id"]) or {}).get("available_minor") != b["available_minor"]
                    or (latest.get(b["account_id"]) or {}).get("current_minor") != b["current_minor"]]
        return {**batch, "transactions": keep, "balances": balances}, skipped

    # -- backup -------------------------------------------------------------------------
    def export_backup(self) -> dict[str, Any]:
        now = self.clock()
        self.active.backups.mkdir(parents=True, exist_ok=True)
        destination = self.active.backups / f"bundle-backup-{now.strftime('%Y%m%d-%H%M%S')}"
        suffix = 1
        while destination.exists():
            destination = self.active.backups / f"bundle-backup-{now.strftime('%Y%m%d-%H%M%S')}-{suffix}"
            suffix += 1
        with self._lock:
            self._run("export", *self._db(), "--created-at", rfc3339(now), destination)
        return {"path": str(destination), "verification": self.verify_backup(str(destination))}

    def verify_backup(self, path: str) -> dict[str, Any]:
        target = Path(path).expanduser()
        output = self._run("verify-archive", target)
        manifest = json.loads((target / "manifest.json").read_text(encoding="utf-8"))
        return {"path": str(target), "ok": True, "message": output.strip(), "created_at": manifest["created_at"],
                "files": [{"path": f["path"], "bytes": f["bytes"]} for f in manifest["files"]]}

    def restore_backup(self, path: str) -> dict[str, Any]:
        self.verify_backup(path)  # reject damaged archives before touching the database
        with self._lock:
            output = self._run("restore", *self._db(), Path(path).expanduser())
        return {"message": output.strip()}

    def list_backups(self) -> list[dict[str, Any]]:
        if not self.active.backups.is_dir():
            return []
        found = []
        for candidate in sorted(self.active.backups.iterdir(), reverse=True):
            manifest = candidate / "manifest.json"
            if manifest.is_file():
                try:
                    created = json.loads(manifest.read_text(encoding="utf-8")).get("created_at")
                except (OSError, ValueError):
                    created = None
                found.append({"path": str(candidate), "name": candidate.name, "created_at": created})
        return found


def _recovery_for(message: str) -> str:
    text = message.lower()
    if "source conflict" in text:
        return ("A different file was already imported under this source ID. If this is a corrected statement, "
                "import it with a new source ID; nothing was written.")
    if "older source version" in text:
        return "A newer version of this account's data is already stored; nothing was written."
    if "missing columns" in text:
        return ("The CSV does not have Bundle's required columns: account_id, account_name, account_type, currency, "
                "transaction_id, amount_minor, direction, state, description, observed_at.")
    if "timezone" in text or "explicit timezone" in text or "offset" in text:
        return "Add a UTC offset to the times in the file, or enter the statement's time zone offset and retry."
    if "conflicts with existing local data" in text:
        return "The backup disagrees with data already in this workspace; nothing was restored."
    if "hash or size mismatch" in text or "not valid json" in text or "manifest" in text:
        return "The backup folder was changed or damaged after export. Use another backup; nothing was restored."
    if "already exists" in text:
        return "Choose a new folder name for the backup."
    if "cannot represent non-empty table" in text:
        return "This database holds records the backup format cannot carry yet, so no partial backup was made."
    if "no active accounts" in text:
        return "Import a statement with at least one account first."
    if "duplicate transaction_id" in text:
        return "Each transaction_id must appear once in a CSV statement. Fix the file and try again."
    return "Nothing was written. Check the file or values and try again."
