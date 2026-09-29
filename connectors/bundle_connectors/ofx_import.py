"""Dependency-free, read-only OFX/QFX statement import."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
from datetime import datetime, timedelta, timezone, tzinfo
from decimal import Decimal, InvalidOperation
from io import TextIOBase
from pathlib import Path
from typing import Any, Iterable

from .csv_import import VALID_CURRENCIES
from .protocol import ConnectorBatch, ConnectorContractError, require_utc, timestamp_text


CURRENCY_EXPONENTS = {"USD": 2, "EUR": 2, "GBP": 2, "JPY": 0, "KWD": 3}
_DEBIT_TYPES = frozenset(
    {"DEBIT", "FEE", "SRVCHG", "PAYMENT", "CASH", "WITHDRAWAL", "CHECK", "ATM", "REPEATPMT"}
)
_CREDIT_TYPES = frozenset(
    {"CREDIT", "INT", "DIV", "DIRECTDEPOSIT", "DEP", "DEPOSIT", "RETURN", "REFUND"}
)
_TAG_NAME = r"[A-Za-z][A-Za-z0-9_.:-]*"
_TRANSACTION_BLOCK = re.compile(
    rf"<\s*STMTTRN\s*>(?P<value>.*?)(?=<\s*STMTTRN\s*>|<\s*/\s*(?:BANKTRANLIST|CCSTMTRS|OFX)\s*>|$)",
    re.IGNORECASE | re.DOTALL,
)


def _clean(value: str) -> str:
    return html.unescape(value).strip()


def _tag_values(document: str, tag: str) -> list[str]:
    pattern = re.compile(
        rf"<\s*{re.escape(tag)}\s*>(?P<value>.*?)(?=<\s*/?\s*{_TAG_NAME}\s*>|$)",
        re.IGNORECASE | re.DOTALL,
    )
    return [_clean(match.group("value")) for match in pattern.finditer(document)]


def _first_tag(document: str, tag: str, *, required: bool = True) -> str:
    values = _tag_values(document, tag)
    if not values or not values[0]:
        if required:
            raise ConnectorContractError(f"OFX is missing required tag {tag}")
        return ""
    return values[0]


def _first_block(document: str, tag: str, stop_tags: tuple[str, ...]) -> str:
    opening = re.search(rf"<\s*{re.escape(tag)}\s*>", document, re.IGNORECASE)
    if opening is None:
        return ""
    start = opening.end()
    closing = re.search(
        rf"<\s*/\s*{re.escape(tag)}\s*>", document[start:], re.IGNORECASE
    )
    if closing is not None:
        return document[start : start + closing.start()]
    if not stop_tags:
        return document[start:]
    stop_pattern = re.compile(
        rf"<\s*(?:{'|'.join(re.escape(value) for value in stop_tags)})\b|"
        rf"<\s*/\s*(?:{'|'.join(re.escape(value) for value in stop_tags)})\s*>",
        re.IGNORECASE,
    )
    stop = stop_pattern.search(document, start)
    return document[start : stop.start() if stop is not None else len(document)]


def _parse_timestamp(value: str, source_timezone: tzinfo | None) -> datetime:
    # OFX uses YYYYMMDDHHMMSS[.fraction][offset:label]. The offset is hours
    # from GMT; a missing offset must be supplied by the caller rather than
    # silently treated as local time.
    match = re.fullmatch(
        r"(?P<date>\d{8})(?P<time>\d{6})?(?:\.\d+)?(?:\[(?P<offset>[+-]?\d+):[^\]]*\])?",
        value.strip(),
    )
    if match is None:
        raise ConnectorContractError(f"invalid OFX timestamp: {value!r}")
    date_text = match.group("date")
    time_text = match.group("time") or "000000"
    try:
        parsed = datetime.strptime(date_text + time_text, "%Y%m%d%H%M%S")
    except ValueError as exc:
        raise ConnectorContractError(f"invalid OFX timestamp: {value!r}") from exc
    offset = match.group("offset")
    if offset is not None:
        parsed = parsed.replace(tzinfo=timezone(timedelta(hours=int(offset))))
    elif source_timezone is None:
        raise ConnectorContractError(
            "OFX timestamp has no offset; source_timezone is required"
        )
    else:
        parsed = parsed.replace(tzinfo=source_timezone)
    return parsed.astimezone(timezone.utc)


def _minor_units(value: str, currency: str) -> int:
    if currency not in CURRENCY_EXPONENTS:
        raise ConnectorContractError(f"unsupported OFX currency {currency!r}")
    try:
        amount = Decimal(value.strip())
    except (InvalidOperation, ValueError) as exc:
        raise ConnectorContractError(f"invalid OFX amount {value!r}") from exc
    if not amount.is_finite():
        raise ConnectorContractError("OFX amounts must be finite")
    scale = Decimal(10) ** CURRENCY_EXPONENTS[currency]
    scaled = amount * scale
    if scaled != scaled.to_integral_value():
        raise ConnectorContractError(
            f"OFX amount {value!r} has more precision than {currency} supports"
        )
    return int(scaled)


def _canonical_block(block: str) -> str:
    return " ".join(block.split())


def _account_context(document: str, source_id: str) -> tuple[str, str, str, str]:
    credit_card_block = _first_block(
        document,
        "CCACCTFROM",
        ("LEDGERBAL", "BANKTRANLIST", "CCSTMTRS", "STMTRS"),
    )
    bank_block = _first_block(
        document,
        "BANKACCTFROM",
        ("LEDGERBAL", "BANKTRANLIST", "CCSTMTRS", "STMTRS"),
    )
    account_block = credit_card_block or bank_block
    if not account_block:
        raise ConnectorContractError("OFX is missing BANKACCTFROM or CCACCTFROM")
    account_number = _first_tag(account_block, "ACCTID")
    account_id = f"{source_id}:{account_number}"
    if credit_card_block:
        account_type = "credit_card"
    else:
        raw_type = _first_tag(account_block, "ACCTTYPE", required=False).lower()
        account_type = raw_type if raw_type in {"checking", "savings"} else "other"
    last_four = account_number[-4:] if len(account_number) >= 4 else account_number
    display_name = f"OFX account {last_four}"
    return account_id, display_name, account_type, account_block


def import_ofx(
    stream: TextIOBase,
    *,
    source_id: str,
    imported_at: datetime,
    source_version: int = 1,
    source_timezone: tzinfo | None = None,
    connector_id: str = "ofx",
) -> ConnectorBatch:
    if not source_id:
        raise ConnectorContractError("source_id is required")
    if source_version < 1:
        raise ConnectorContractError("source_version must be positive")
    if connector_id not in {"ofx", "qfx"}:
        raise ConnectorContractError("connector_id must be ofx or qfx")
    imported = require_utc(imported_at)
    document = stream.read()
    if not document.strip():
        raise ConnectorContractError("OFX document is empty")

    currency = _first_tag(document, "CURDEF").upper()
    if currency not in VALID_CURRENCIES:
        raise ConnectorContractError(f"unsupported OFX currency {currency!r}")
    account_id, display_name, account_type, account_block = _account_context(document, source_id)

    accounts: tuple[dict[str, Any], ...] = (
        {
            "account_id": account_id,
            "source_id": source_id,
            "display_name": display_name,
            "account_type": account_type,
            "currency": currency,
        },
    )

    balances: list[dict[str, Any]] = []
    ledger_block = _first_block(document, "LEDGERBAL", ("AVAILBAL", "STMTRS", "CCSTMTRS"))
    if ledger_block:
        balance_text = _first_tag(ledger_block, "BALAMT")
        balance_minor = _minor_units(balance_text, currency)
        balances.append(
            {
                "account_id": account_id,
                "current_minor": balance_minor,
                "available_minor": balance_minor,
                "observed_at": timestamp_text(imported),
                "status": "valid",
                "source_ref": {
                    "source_id": source_id,
                    "source_kind": connector_id,
                    "external_object_id": "ledger",
                    "imported_at": timestamp_text(imported),
                    "observed_at": timestamp_text(imported),
                    "content_hash": "sha256:"
                    + hashlib.sha256(_canonical_block(ledger_block).encode("utf-8")).hexdigest(),
                    "source_version": source_version,
                },
            }
        )

    transactions: list[dict[str, Any]] = []
    seen_fitids: set[str] = set()
    for block_match in _TRANSACTION_BLOCK.finditer(document):
        block = block_match.group("value")
        fitid = _first_tag(block, "FITID")
        if fitid in seen_fitids:
            raise ConnectorContractError(f"duplicate OFX FITID {fitid!r}")
        seen_fitids.add(fitid)
        signed_minor = _minor_units(_first_tag(block, "TRNAMT"), currency)
        transaction_type = _first_tag(block, "TRNTYPE", required=False).upper()
        if ((transaction_type in _DEBIT_TYPES and signed_minor > 0) or
                (transaction_type in _CREDIT_TYPES and signed_minor < 0)):
            raise ConnectorContractError(
                f"OFX TRNAMT sign conflicts with TRNTYPE for FITID {fitid!r}"
            )
        direction = "credit" if signed_minor >= 0 else "debit"
        transaction_id = f"{source_id}:{fitid}"
        observed_at = _parse_timestamp(
            _first_tag(block, "DTPOSTED"), source_timezone
        )
        name = _first_tag(block, "NAME", required=False)
        memo = _first_tag(block, "MEMO", required=False)
        description = " - ".join(value for value in (name, memo) if value)
        if not description:
            description = transaction_type or "OFX transaction"
        source_ref = {
            "source_id": source_id,
            "source_kind": connector_id,
            "external_object_id": fitid,
            "imported_at": timestamp_text(imported),
            "observed_at": timestamp_text(observed_at),
            "content_hash": "sha256:"
            + hashlib.sha256(_canonical_block(block).encode("utf-8")).hexdigest(),
            "source_version": source_version,
        }
        transactions.append(
            {
                "transaction_id": transaction_id,
                "account_id": account_id,
                "source_transaction_id": fitid,
                "revision": 1,
                "amount": {"minor_units": abs(signed_minor), "currency": currency},
                "direction": direction,
                "transaction_state": "posted",
                "merchant_raw": name,
                "description": description,
                "observed_at": timestamp_text(observed_at),
                "source_ref": source_ref,
            }
        )

    return ConnectorBatch(
        connector_id=connector_id,
        started_at=imported,
        completed_at=imported,
        accounts=accounts,
        balances=tuple(balances),
        transactions=tuple(transactions),
        warnings=(),
    )


def main(argv: Iterable[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ofx_path", type=Path)
    parser.add_argument("--source-id", required=True)
    parser.add_argument("--imported-at", required=True, help="RFC3339 timestamp with timezone")
    parser.add_argument(
        "--source-timezone",
        help="UTC offset such as +00:00 for OFX timestamps without an embedded offset",
    )
    args = parser.parse_args(list(argv) if argv is not None else None)
    source_timezone = None
    if args.source_timezone is not None:
        try:
            sign = -1 if args.source_timezone.startswith("-") else 1
            hours, minutes = args.source_timezone.lstrip("+-").split(":")
            source_timezone = timezone(sign * timedelta(hours=int(hours), minutes=int(minutes)))
        except (ValueError, IndexError) as exc:
            raise ConnectorContractError("source-timezone must be formatted +HH:MM or -HH:MM") from exc
    imported_at = datetime.fromisoformat(args.imported_at.replace("Z", "+00:00"))
    with args.ofx_path.open("r", encoding="utf-8") as stream:
        batch = import_ofx(
            stream,
            source_id=args.source_id,
            imported_at=imported_at,
            source_timezone=source_timezone,
            connector_id="qfx" if args.ofx_path.suffix.lower() == ".qfx" else "ofx",
        )
    print(json.dumps(batch.to_dict(), indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
