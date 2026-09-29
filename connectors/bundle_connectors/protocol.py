"""The bounded JSON seam between optional connectors and the finance core."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any, Mapping


class ConnectorContractError(ValueError):
    """Raised when a connector attempts to exceed its declared boundary."""


def require_utc(value: datetime) -> datetime:
    if value.tzinfo is None or value.utcoffset() is None:
        raise ConnectorContractError("timestamps must be timezone-aware")
    return value.astimezone(timezone.utc)


def timestamp_text(value: datetime) -> str:
    return require_utc(value).isoformat().replace("+00:00", "Z")


def _bounded_mapping(value: Mapping[str, Any], label: str) -> dict[str, Any]:
    if not isinstance(value, Mapping):
        raise ConnectorContractError(f"{label} must be an object")
    output = dict(value)
    for key in output:
        if not isinstance(key, str) or len(key) > 128:
            raise ConnectorContractError(f"{label} contains an invalid field name")
        lowered = key.lower()
        if any(
            secret in lowered
            for secret in (
                "password", "secret", "credential", "access_token", "auth_token",
                "refresh_token", "api_key", "api-key",
            )
        ):
            raise ConnectorContractError(f"{label} contains a forbidden secret-looking field")
    return output


@dataclass(frozen=True)
class ConnectorManifest:
    connector_id: str
    version: str
    capabilities: tuple[str, ...]
    network_required: bool
    writes_external_state: bool = False

    def __post_init__(self) -> None:
        if not self.connector_id or not self.version:
            raise ConnectorContractError("connector id and version are required")
        if self.writes_external_state:
            raise ConnectorContractError("finance connectors must not write external state")
        if not self.capabilities:
            raise ConnectorContractError("at least one capability is required")
        if any(not capability.endswith(".read") for capability in self.capabilities):
            raise ConnectorContractError("connector capabilities must be read-only")

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": "bundle-connector-1",
            "id": self.connector_id,
            "version": self.version,
            "capabilities": list(self.capabilities),
            "network_required": self.network_required,
            "writes_external_state": False,
        }


@dataclass(frozen=True)
class ConnectorBatch:
    connector_id: str
    started_at: datetime
    completed_at: datetime
    accounts: tuple[Mapping[str, Any], ...] = ()
    balances: tuple[Mapping[str, Any], ...] = ()
    transactions: tuple[Mapping[str, Any], ...] = ()
    external_commitments: tuple[Mapping[str, Any], ...] = ()
    next_cursor: str | None = None
    warnings: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        started = require_utc(self.started_at)
        completed = require_utc(self.completed_at)
        if completed < started:
            raise ConnectorContractError("completed_at cannot precede started_at")
        if not self.connector_id:
            raise ConnectorContractError("connector id is required")
        if self.next_cursor is not None and len(self.next_cursor) > 4096:
            raise ConnectorContractError("cursor exceeds the bounded connector contract")
        if any(not isinstance(warning, str) or len(warning) > 4096 for warning in self.warnings):
            raise ConnectorContractError("connector warnings must be bounded strings")
        object.__setattr__(self, "started_at", started)
        object.__setattr__(self, "completed_at", completed)

        for field_name in (
            "accounts",
            "balances",
            "transactions",
            "external_commitments",
        ):
            values = getattr(self, field_name)
            if len(values) > 1_000_000:
                raise ConnectorContractError(f"{field_name} exceeds the batch bound")
            normalized = tuple(_bounded_mapping(value, field_name) for value in values)
            object.__setattr__(self, field_name, normalized)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema": "bundle-connector-1",
            "connector_id": self.connector_id,
            "started_at": timestamp_text(self.started_at),
            "completed_at": timestamp_text(self.completed_at),
            "accounts": list(self.accounts),
            "balances": list(self.balances),
            "transactions": list(self.transactions),
            "external_commitments": list(self.external_commitments),
            "next_cursor": self.next_cursor,
            "warnings": list(self.warnings),
        }
