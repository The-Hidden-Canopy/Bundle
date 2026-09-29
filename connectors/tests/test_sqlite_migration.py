import sqlite3
import tempfile
import unittest
from pathlib import Path


MIGRATION = Path(__file__).parents[2] / "core" / "persistence" / "migrations" / "001_finance.sql"


class SQLiteMigrationTests(unittest.TestCase):
    def test_finance_schema_is_executable_and_history_is_append_only(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            database = Path(temporary) / "finance.db"
            connection = sqlite3.connect(database)
            connection.executescript(MIGRATION.read_text(encoding="utf-8"))
            tables = {
                row[0]
                for row in connection.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                )
            }
            required = {
                "schema_migrations",
                "sources",
                "accounts",
                "balance_observations",
                "transactions",
                "transaction_revisions",
                "commitments",
                "budgets",
                "goals",
                "planned_events",
                "forecast_runs",
                "forecast_points",
                "insights",
                "scenarios",
                "event_log",
                "calculation_receipts",
            }
            self.assertTrue(required.issubset(tables))
            self.assertEqual(
                connection.execute("SELECT version FROM schema_migrations").fetchall(),
                [(1,)],
            )

            connection.execute(
                "INSERT INTO sources VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (
                    "s", "csv", None, None, "2026-01-01T00:00:00Z",
                    "2026-01-01T00:00:00Z",
                    "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                    1, "{}",
                ),
            )
            self.assertEqual(
                connection.execute("SELECT COUNT(*) FROM event_log").fetchone()[0],
                1,
            )
            connection.execute(
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
                    "checking", "s", 1, "Checking", "Local", "checking", "",
                    "USD", 1000, 1000, "2026-01-01T00:00:00Z", "fresh",
                    None, "active", 1, 1,
                ),
            )
            connection.execute(
                """
                INSERT INTO balance_observations (
                    observation_id, account_id, current_minor, available_minor,
                    observed_at, recorded_at, source_id, source_version, status
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    "balance-1", "checking", 1000, 1000,
                    "2026-01-01T00:00:00Z", "2026-01-01T00:00:01Z", "s", 1, "valid",
                ),
            )
            connection.execute(
                """
                INSERT INTO transactions (account_id, source_transaction_id, current_revision)
                VALUES (?, ?, ?)
                """,
                ("checking", "fit-1", 1),
            )
            connection.execute(
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
                    "tx-1", "checking", "fit-1", 1, 500, "USD", "debit", "posted",
                    "SHOP", None, "Shop", None, "2026-01-01T00:00:00Z",
                    "2026-01-01T00:00:00Z", "2026-01-01T00:00:01Z", None,
                    "s", 1, None, "", "[]",
                ),
            )
            self.assertEqual(
                connection.execute(
                    "SELECT event_type FROM event_log ORDER BY rowid"
                ).fetchall(),
                [
                    ("source.appended",),
                    ("balance_observation.appended",),
                    ("transaction_revision.appended",),
                ],
            )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute("UPDATE sources SET metadata_json = '{}' WHERE source_id = 's'")
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute("DELETE FROM sources WHERE source_id = 's'")

            connection.execute(
                "INSERT INTO event_log VALUES (?, ?, ?, ?, ?)",
                (
                    "e", "source.appended", "s", "2026-01-01T00:00:00Z",
                    "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                ),
            )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute("DELETE FROM event_log WHERE event_id = 'e'")

            connection.execute(
                """
                INSERT INTO calculation_receipts (
                    receipt_id, engine, engine_version, calculated_at,
                    input_refs_json, input_hash, policy_id, policy_version, outputs_json
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    "receipt-1", "forecast", "0.1.0", "2026-01-01T00:00:00Z",
                    "[]",
                    "sha256:0000000000000000000000000000000000000000000000000000000000000000",
                    "expected", "1", "{}",
                ),
            )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute(
                    """
                    INSERT INTO calculation_receipts (
                        receipt_id, engine, engine_version, calculated_at,
                        input_refs_json, input_hash, policy_id, policy_version, outputs_json
                    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                    """,
                    (
                        "receipt-invalid-hash", "forecast", "0.1.0",
                        "2026-01-01T00:00:00Z", "[]", "short", "expected", "1", "{}",
                    ),
                )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute(
                    "UPDATE calculation_receipts SET outputs_json = '{}' WHERE receipt_id = 'receipt-1'"
                )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute("DELETE FROM calculation_receipts WHERE receipt_id = 'receipt-1'")
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute(
                    "INSERT INTO sources VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        "bad", "csv", None, None, "2026-01-01T00:00:00Z",
                        "2026-01-01T00:00:00Z", "short", 1, "{}",
                    ),
                )
            with self.assertRaises(sqlite3.IntegrityError):
                connection.execute(
                    "INSERT INTO forecast_runs VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                    (
                        "run-invalid-hash", "2026-01-01T00:00:00Z",
                        "2026-01-01T00:00:00Z", "2026-02-01T00:00:00Z",
                        "expected", "short", "expected", "1",
                    ),
                )
            connection.close()


if __name__ == "__main__":
    unittest.main()
