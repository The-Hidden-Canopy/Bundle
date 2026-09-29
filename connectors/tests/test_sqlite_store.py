import io
import hashlib
import json
import shutil
import sqlite3
import tempfile
import unittest
from dataclasses import replace
from datetime import datetime, timezone
from pathlib import Path

from bundle_connectors.csv_import import import_csv
from bundle_connectors.protocol import ConnectorBatch
from bundle_connectors.sqlite_store import SQLiteFinanceStore, SQLiteStoreError


UTC = timezone.utc


CSV = """account_id,account_name,account_type,currency,transaction_id,amount_minor,direction,state,description,observed_at,merchant_raw
checking,Checking,checking,USD,t-1,950,debit,posted,Rent,2026-01-01T00:00:00Z,RENT
checking,Checking,checking,USD,t-2,780,credit,posted,Paycheck,2026-01-02T00:00:00Z,EMPLOYER
"""


class SQLiteStoreTests(unittest.TestCase):
    def test_batch_admission_is_durable_idempotent_and_event_bearing(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        with tempfile.TemporaryDirectory() as temporary:
            database = Path(temporary) / "finance.db"
            with SQLiteFinanceStore(database) as store:
                batch_id = store.append_batch(batch)
                event_count = store.connection.execute("SELECT COUNT(*) FROM event_log").fetchone()[0]
                self.assertEqual(store.append_batch(batch), batch_id)
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0], 1
                )
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM accounts").fetchone()[0], 1
                )
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM transaction_revisions").fetchone()[0], 2
                )
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM event_log").fetchone()[0], event_count
                )

            connection = sqlite3.connect(database)
            try:
                self.assertEqual(connection.execute("SELECT COUNT(*) FROM transactions").fetchone()[0], 2)
            finally:
                connection.close()

    def test_conflicting_source_is_rejected_without_partial_mutation(self) -> None:
        original = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        changed = import_csv(
            io.StringIO(CSV.replace("Paycheck", "Bonus")),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(original)
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(changed)
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM transaction_revisions").fetchone()[0], 2
                )
                self.assertEqual(
                    store.connection.execute(
                        "SELECT amount_minor FROM transaction_revisions WHERE transaction_id = ?",
                        ("t-1",),
                    ).fetchone()[0],
                    950,
                )

    def test_external_commitments_are_rejected_until_planning_admission_exists(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        batch = replace(batch, external_commitments=({"name": "rent"},))
        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(batch)
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                    0,
                )

    def test_stale_balance_remains_nonfresh_and_uses_batch_completion_time(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        source_ref = dict(batch.transactions[0]["source_ref"])
        stale_balance = {
            "account_id": "checking",
            "current_minor": 950,
            "available_minor": 900,
            "observed_at": "2026-01-02T00:00:00Z",
            "status": "stale",
            "source_ref": source_ref,
        }
        stale_batch = replace(batch, balances=(stale_balance,))

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(stale_batch)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT balance_freshness FROM accounts WHERE account_id = ?",
                        ("checking",),
                    ).fetchone()[0],
                    "stale",
                )
                self.assertEqual(
                    store.connection.execute(
                        "SELECT recorded_at FROM balance_observations WHERE observation_id = ?",
                        ("csv-1:checking:2026-01-02T00:00:00Z",),
                    ).fetchone()[0],
                    "2026-01-03T00:00:00Z",
                )

    def test_newer_valid_balance_cannot_be_overwritten_by_older_stale_row(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        source_ref = dict(batch.transactions[0]["source_ref"])
        valid_balance = {
            "account_id": "checking",
            "current_minor": 1_000,
            "available_minor": 950,
            "observed_at": "2026-01-03T00:00:00Z",
            "status": "valid",
            "source_ref": source_ref,
        }
        stale_balance = {
            "account_id": "checking",
            "current_minor": 700,
            "available_minor": 600,
            "observed_at": "2026-01-02T00:00:00Z",
            "status": "stale",
            "source_ref": source_ref,
        }
        ordered_batch = replace(batch, balances=(valid_balance, stale_balance))

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(ordered_batch)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT current_minor, balance_freshness FROM accounts WHERE account_id = ?",
                        ("checking",),
                    ).fetchone(),
                    (1_000, "fresh"),
                )

    def test_naive_or_short_source_hash_is_rejected_before_writes(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        for mutation in (
            {"imported_at": "2026-01-03T00:00:00"},
            {"content_hash": "sha256:short"},
        ):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as temporary:
                transaction = dict(batch.transactions[0])
                source_ref = dict(transaction["source_ref"])
                source_ref.update(mutation)
                transaction["source_ref"] = source_ref
                invalid_batch = replace(
                    batch,
                    transactions=(transaction, batch.transactions[1]),
                )
                with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                    with self.assertRaises(SQLiteStoreError):
                        store.append_batch(invalid_batch)
                    self.assertEqual(
                        store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                        0,
                    )

    def test_legacy_malformed_hashes_fail_closed_when_database_opens(self) -> None:
        legacy_tables = {
            "sources": (
                "CREATE TABLE sources ("
                "source_id TEXT, source_kind TEXT, provider_id TEXT, external_object_id TEXT, "
                "imported_at TEXT, observed_at TEXT, content_hash TEXT, source_version INTEGER, "
                "metadata_json TEXT)"
            ),
            "event_log": (
                "CREATE TABLE event_log (event_id TEXT, event_type TEXT, object_id TEXT, "
                "occurred_at TEXT, content_hash TEXT)"
            ),
            "forecast_runs": (
                "CREATE TABLE forecast_runs (run_id TEXT, calculated_at TEXT, start_at TEXT, "
                "horizon_end TEXT, confidence_mode TEXT, input_hash TEXT, policy_id TEXT, "
                "policy_version TEXT)"
            ),
            "calculation_receipts": (
                "CREATE TABLE calculation_receipts (receipt_id TEXT, engine TEXT, "
                "engine_version TEXT, calculated_at TEXT, input_refs_json TEXT, input_hash TEXT, "
                "policy_id TEXT, policy_version TEXT, outputs_json TEXT)"
            ),
        }
        with tempfile.TemporaryDirectory() as temporary:
            for table, create_sql in legacy_tables.items():
                with self.subTest(table=table):
                    database = Path(temporary) / f"legacy-{table}.db"
                    connection = sqlite3.connect(database)
                    connection.execute(create_sql)
                    if table == "sources":
                        connection.execute(
                            "INSERT INTO sources VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                            (
                                "legacy", "csv", None, None,
                                "2026-01-01T00:00:00Z", "2026-01-01T00:00:00Z",
                                "short", 1, "{}",
                            ),
                        )
                    elif table == "event_log":
                        connection.execute(
                            "INSERT INTO event_log VALUES (?, ?, ?, ?, ?)",
                            ("event-1", "test", "object-1", "2026-01-01T00:00:00Z", "short"),
                        )
                    elif table == "forecast_runs":
                        connection.execute(
                            "INSERT INTO forecast_runs VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                            (
                                "run-1", "2026-01-01T00:00:00Z", "2026-01-01T00:00:00Z",
                                "2026-02-01T00:00:00Z", "expected", "short", "expected", "1",
                            ),
                        )
                    else:
                        connection.execute(
                            "INSERT INTO calculation_receipts VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                            (
                                "receipt-1", "forecast", "0.1.0", "2026-01-01T00:00:00Z",
                                "[]", "short", "expected", "1", "{}",
                            ),
                        )
                    connection.commit()
                    connection.close()
                    with self.assertRaises(SQLiteStoreError):
                        SQLiteFinanceStore(database)

    def test_explicit_source_version_and_transaction_revision_are_admitted(self) -> None:
        original = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            source_version=1,
        )
        revised_transaction = dict(original.transactions[1])
        revised_source_ref = dict(revised_transaction["source_ref"])
        revised_source_ref["source_version"] = 2
        revised_transaction.update(
            {
                "transaction_id": "t-2-revision-2",
                "revision": 2,
                "amount": {"minor_units": 800, "currency": "USD"},
                "description": "Corrected paycheck",
                "supersedes_transaction_revision": 1,
                "source_ref": revised_source_ref,
            }
        )
        revised = replace(
            original,
            accounts=(dict(original.accounts[0], source_version=2),),
            transactions=(revised_transaction,),
        )

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(original)
                revised_batch_id = store.append_batch(revised)
                self.assertEqual(store.append_batch(revised), revised_batch_id)
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(original)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT source_version FROM accounts WHERE account_id = ?",
                        ("checking",),
                    ).fetchone()[0],
                    2,
                )
                self.assertEqual(
                    store.connection.execute(
                        "SELECT current_revision FROM transactions "
                        "WHERE account_id = ? AND source_transaction_id = ?",
                        ("checking", "t-2"),
                    ).fetchone()[0],
                    2,
                )
                self.assertEqual(
                    store.connection.execute(
                        "SELECT amount_minor FROM transaction_revisions "
                        "WHERE transaction_id = ?",
                        ("t-2-revision-2",),
                    ).fetchone()[0],
                    800,
                )

    def test_duplicate_transaction_id_cannot_discard_changed_source_lineage(self) -> None:
        original = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            source_version=1,
        )
        changed_transaction = dict(original.transactions[0])
        changed_source = dict(changed_transaction["source_ref"])
        changed_source["source_version"] = 2
        changed_transaction["source_ref"] = changed_source
        changed_transaction["description"] = "Changed description"
        changed_account = dict(original.accounts[0])
        changed_account["source_version"] = 2
        changed = replace(
            original,
            accounts=(changed_account,),
            transactions=(changed_transaction,),
        )

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(original)
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(changed)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT source_version FROM accounts WHERE account_id = ?",
                        ("checking",),
                    ).fetchone()[0],
                    1,
                )
                self.assertEqual(
                    store.connection.execute(
                        "SELECT description FROM transaction_revisions WHERE transaction_id = ?",
                        ("t-1",),
                    ).fetchone()[0],
                    "Rent",
                )
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                    1,
                )

    def test_duplicate_balance_id_cannot_discard_status_or_source_lineage(self) -> None:
        original = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            source_version=1,
        )
        balance = {
            "observation_id": "balance-1",
            "account_id": "checking",
            "current_minor": 1_000,
            "available_minor": 900,
            "observed_at": "2026-01-03T00:00:00Z",
            "status": "valid",
            "source_ref": dict(original.transactions[0]["source_ref"]),
        }
        original = replace(original, transactions=(), balances=(balance,))
        changed_balance = dict(balance)
        changed_balance["status"] = "stale"
        changed_source = dict(changed_balance["source_ref"])
        changed_source["source_version"] = 2
        changed_balance["source_ref"] = changed_source
        changed_account = dict(original.accounts[0])
        changed_account["source_version"] = 2
        changed = replace(
            original,
            accounts=(changed_account,),
            balances=(changed_balance,),
        )

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(original)
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(changed)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT status, source_version FROM balance_observations "
                        "WHERE observation_id = ?",
                        ("balance-1",),
                    ).fetchone(),
                    ("valid", 1),
                )
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                    1,
                )

    def test_account_only_batches_reject_mixed_sources_and_accept_one_source(self) -> None:
        started = datetime(2026, 1, 3, tzinfo=UTC)
        mixed = ConnectorBatch(
            connector_id="accounts",
            started_at=started,
            completed_at=started,
            accounts=(
                {"account_id": "checking", "source_id": "source-a", "currency": "USD"},
                {"account_id": "savings", "source_id": "source-b", "currency": "USD"},
            ),
        )
        valid = ConnectorBatch(
            connector_id="accounts",
            started_at=started,
            completed_at=started,
            accounts=(
                {"account_id": "checking", "source_id": "source-a", "currency": "USD"},
                {"account_id": "savings", "source_id": "source-a", "currency": "USD"},
            ),
        )
        orphan_version = ConnectorBatch(
            connector_id="accounts",
            started_at=started,
            completed_at=started,
            accounts=(
                {"account_id": "checking", "source_version": 2, "currency": "USD"},
            ),
        )
        empty = ConnectorBatch(
            connector_id="accounts",
            started_at=started,
            completed_at=started,
        )

        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(mixed)
                with self.assertRaises(SQLiteStoreError):
                    store.append_batch(orphan_version)
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM accounts").fetchone()[0],
                    0,
                )
                store.append_batch(valid)
                self.assertEqual(
                    store.connection.execute(
                        "SELECT COUNT(*) FROM sources WHERE source_id = ?",
                        ("source-a",),
                    ).fetchone()[0],
                    1,
                )
                empty_batch_id = store.append_batch(empty)
                self.assertEqual(store.append_batch(empty), empty_batch_id)
                self.assertEqual(
                    store.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                    1,
                )

    def test_sqlite_archive_round_trip_is_transactional_and_idempotent(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-archive",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        with tempfile.TemporaryDirectory() as temporary:
            database = Path(temporary) / "finance.db"
            archive = Path(temporary) / "bundle-finance-1"
            with SQLiteFinanceStore(database) as store:
                store.append_batch(batch)
                store.export_archive(
                    archive,
                    created_at=datetime(2026, 1, 4, tzinfo=UTC),
                )

            restored_database = Path(temporary) / "restored.db"
            with SQLiteFinanceStore(restored_database) as restored:
                restored.import_archive(archive)
                restored.import_archive(archive)
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM sources").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM accounts").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM transaction_revisions").fetchone()[0],
                    2,
                )
                self.assertEqual(
                    restored.connection.execute(
                        "SELECT amount_minor FROM transaction_revisions WHERE transaction_id = ?",
                        ("t-1",),
                    ).fetchone()[0],
                    950,
                )

    def test_sqlite_archive_round_trip_preserves_planning_and_receipt_records(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-planning",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        with tempfile.TemporaryDirectory() as temporary:
            database = Path(temporary) / "finance.db"
            archive = Path(temporary) / "bundle-finance-1"
            with SQLiteFinanceStore(database) as store:
                store.append_batch(batch)
                store.connection.execute(
                    """
                    INSERT INTO commitments (
                        commitment_id, name, merchant_id, amount_model_json, currency,
                        cadence, next_expected_at, grace_window_millis, commitment_type,
                        status, confidence, user_confirmed, created_at, updated_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "rent", "Rent", None,
                        '{"kind":"fixed","primary":{"currency":"USD","minor_units":95000}}',
                        "USD", "monthly", "2026-02-01T00:00:00Z", 86_400_000,
                        "housing", "active", "supported", 1,
                        "2026-01-03T00:00:00Z", "2026-01-03T00:00:00Z",
                    ),
                )
                store.connection.execute(
                    """
                    INSERT INTO commitment_evidence (
                        commitment_id, source_id, source_version, related_transaction_id
                    ) VALUES (?, ?, ?, ?)
                    """,
                    ("rent", "csv-planning", 1, "t-1"),
                )
                store.connection.execute(
                    """
                    INSERT INTO budgets (
                        budget_id, name, mode, period_rule, rollover_policy,
                        starts_at, ends_at, status
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "monthly", "Monthly", "category_caps", "calendar_month",
                        "none", "2026-01-01T00:00:00Z", None, "active",
                    ),
                )
                store.connection.execute(
                    """
                    INSERT INTO budget_allocations (
                        budget_id, category_id, limit_minor, spent_minor, pending_minor,
                        remaining_minor, burn_rate_basis_points
                    ) VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    ("monthly", "housing", 120000, 95000, 0, 25000, 7916),
                )
                store.connection.execute(
                    """
                    INSERT INTO goals (
                        goal_id, name, target_minor, currency, target_date,
                        current_reserved_minor, planned_contribution_minor, priority,
                        protected, status
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "buffer", "Emergency buffer", 200000, "USD",
                        "2026-12-31T00:00:00Z", 50000, 25000, 1, 1, "active",
                    ),
                )
                store.connection.execute(
                    "INSERT INTO goal_plans (goal_id, account_id) VALUES (?, ?)",
                    ("buffer", "checking"),
                )
                store.connection.execute(
                    """
                    INSERT INTO user_overrides (
                        override_id, object_type, object_id, field,
                        old_projection_json, new_user_value_json, created_at
                    ) VALUES (?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "override-1", "transaction", "t-1", "category_id",
                        'null', '"housing"', "2026-01-03T00:00:00Z",
                    ),
                )
                store.connection.execute(
                    """
                    INSERT INTO calculation_receipts (
                        receipt_id, engine, engine_version, calculated_at,
                        input_refs_json, input_hash, policy_id, policy_version, outputs_json
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "receipt-1", "forecast", "0.1.0", "2026-01-03T00:00:00Z",
                        '["account:checking","commitment:rent"]',
                        "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                        "expected", "1", '{"safe_to_spend_minor":100000}',
                    ),
                )
                store.export_archive(archive, created_at=datetime(2026, 1, 4, tzinfo=UTC))

            restored_database = Path(temporary) / "restored.db"
            restored_archive = Path(temporary) / "bundle-finance-1-restored"
            with SQLiteFinanceStore(restored_database) as restored:
                restored.import_archive(archive)
                tampered_archive = Path(temporary) / "bundle-finance-1-tampered"
                shutil.copytree(archive, tampered_archive)
                commitments_path = tampered_archive / "commitments.json"
                tampered_commitments = json.loads(commitments_path.read_text(encoding="utf-8"))
                tampered_commitments[0]["name"] = "Tampered rent"
                tampered_payload = (
                    json.dumps(
                        tampered_commitments,
                        ensure_ascii=False,
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    + "\n"
                ).encode("utf-8")
                commitments_path.write_bytes(tampered_payload)
                tampered_manifest_path = tampered_archive / "manifest.json"
                tampered_manifest = json.loads(tampered_manifest_path.read_text(encoding="utf-8"))
                for entry in tampered_manifest["files"]:
                    if entry["path"] == "commitments.json":
                        entry["bytes"] = len(tampered_payload)
                        entry["sha256"] = hashlib.sha256(tampered_payload).hexdigest()
                tampered_manifest_path.write_text(
                    json.dumps(
                        tampered_manifest,
                        ensure_ascii=False,
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    + "\n",
                    encoding="utf-8",
                )
                with self.assertRaises(SQLiteStoreError):
                    restored.import_archive(tampered_archive)
                self.assertEqual(
                    restored.connection.execute(
                        "SELECT name FROM commitments WHERE commitment_id = ?",
                        ("rent",),
                    ).fetchone()[0],
                    "Rent",
                )
                restored.import_archive(archive)
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM commitments").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM commitment_evidence").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM budget_allocations").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM goal_plans").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM user_overrides").fetchone()[0],
                    1,
                )
                self.assertEqual(
                    restored.connection.execute("SELECT COUNT(*) FROM calculation_receipts").fetchone()[0],
                    1,
                )
                restored.export_archive(
                    restored_archive,
                    created_at=datetime(2026, 1, 4, tzinfo=UTC),
                )

            for filename in ("manifest.json", "commitments.json", "budgets.json", "goals.json", "overrides.jsonl", "receipts.jsonl"):
                self.assertEqual(
                    (archive / filename).read_bytes(),
                    (restored_archive / filename).read_bytes(),
                )

    def test_sqlite_archive_refuses_unrepresented_rows(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-archive",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        with tempfile.TemporaryDirectory() as temporary:
            with SQLiteFinanceStore(Path(temporary) / "finance.db") as store:
                store.append_batch(batch)
                store.connection.execute(
                    "INSERT INTO merchants (merchant_id, canonical_name, confidence, user_confirmed) "
                    "VALUES (?, ?, ?, ?)",
                    ("merchant-1", "Example", "inferred", 0),
                )
                with self.assertRaises(SQLiteStoreError):
                    store.export_archive(
                        Path(temporary) / "bundle-finance-1",
                        created_at=datetime(2026, 1, 4, tzinfo=UTC),
                    )


if __name__ == "__main__":
    unittest.main()
