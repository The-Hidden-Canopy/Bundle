"""Interface adapter and local server, end to end through the native `bundle`.

    BUNDLE_NATIVE_CLI=build/core/persistence/bundle python -m unittest discover -s tests/ui -v

Every record is synthetic (the demo workspace or examples/native-sqlite). With
BUNDLE_PARITY_REQUIRE_NATIVE=1 a missing native binary fails instead of skipping.
"""

from __future__ import annotations

import http.client
import json
import os
import shutil
import sqlite3
import sys
import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "ui"))

from bundle_ui.backend import BackendError, BundleBackend, find_native_cli  # noqa: E402
from bundle_ui.server import serve_in_thread  # noqa: E402

SAMPLE_CSV = ROOT / "examples" / "native-sqlite" / "statement.csv"
NOW = datetime(2026, 3, 10, 12, 0, tzinfo=timezone.utc)


def native_or_skip() -> Path:
    try:
        return find_native_cli()
    except BackendError as error:
        if os.environ.get("BUNDLE_PARITY_REQUIRE_NATIVE") == "1":
            raise RuntimeError(f"UI test prerequisite missing: {error.message}") from None
        raise unittest.SkipTest(error.message) from None


class Clock:
    def __init__(self, now: datetime = NOW) -> None:
        self.now = now

    def __call__(self) -> datetime:
        return self.now


def event_count(database: Path) -> int:
    with sqlite3.connect(database) as connection:
        return connection.execute("SELECT COUNT(*) FROM event_log").fetchone()[0]


class BackendCase(unittest.TestCase):
    def setUp(self) -> None:
        self.native = native_or_skip()
        self.directory = Path(tempfile.mkdtemp(prefix="bundle-ui-test-"))
        self.database = self.directory / "mine.db"
        self.clock = Clock()
        self.backend = self.new_backend()
        self.backend._run("init", "--db", self.database)

    def tearDown(self) -> None:
        shutil.rmtree(self.directory, ignore_errors=True)

    def new_backend(self) -> BundleBackend:
        return BundleBackend(self.database, native_cli=self.native, clock=self.clock)

    def import_sample(self, backend: BundleBackend | None = None) -> dict:
        backend = backend or self.backend
        preview = backend.preview_import("statement.csv", SAMPLE_CSV.read_text(encoding="utf-8"))
        return backend.commit_import(preview["preview_id"]) | {"preview": preview}


class EmptyWorkspace(BackendCase):
    def test_empty_database_has_no_accounts_and_refuses_calculation_cleanly(self) -> None:
        snapshot = self.backend.snapshot()
        self.assertEqual(snapshot["accounts"], [])
        self.assertEqual(snapshot["transactions"], [])
        self.assertIsNone(self.backend.saved_floor(snapshot))
        with self.assertRaises(BackendError) as caught:
            self.backend.calculate(mode="expected", horizon_days=14, floor_minor=0)
        self.assertTrue(caught.exception.recovery)
        self.assertEqual(event_count(self.database), 0)

    def test_invalid_calculation_inputs_are_rejected_before_the_engine(self) -> None:
        for kwargs in ({"mode": "optimistic", "horizon_days": 14, "floor_minor": 0},
                       {"mode": "expected", "horizon_days": 13, "floor_minor": 0},
                       {"mode": "expected", "horizon_days": 14, "floor_minor": -1}):
            with self.assertRaises(BackendError):
                self.backend.calculate(**kwargs)


class Imports(BackendCase):
    def test_failed_import_writes_nothing_and_a_corrected_file_then_imports(self) -> None:
        with self.assertRaises(BackendError) as caught:
            self.backend.preview_import("broken.csv", "account_id,amount\nbroken,notanumber\n")
        self.assertIn("missing columns", caught.exception.message)
        self.assertIn("account_name", caught.exception.recovery)
        for name, content in (("notes.txt", "hello"), ("empty.csv", "  \n"), ("batch.json", "{not json")):
            with self.assertRaises(BackendError):
                self.backend.preview_import(name, content)
        self.assertEqual(event_count(self.database), 0)
        result = self.import_sample()  # retry with a valid file
        self.assertTrue(result["changed"])
        self.assertGreater(event_count(self.database), 0)

    def test_preview_is_a_rolled_back_trial_and_predicts_the_write(self) -> None:
        preview = self.backend.preview_import("statement.csv", SAMPLE_CSV.read_text(encoding="utf-8"))
        self.assertFalse(preview["already_imported"])
        self.assertEqual(event_count(self.database), 0)
        self.backend.commit_import(preview["preview_id"])
        self.assertEqual(event_count(self.database), preview["new_events"])

    def test_duplicate_import_and_repeated_commit_change_nothing(self) -> None:
        first = self.import_sample()
        count = event_count(self.database)
        again = self.backend.commit_import(first["preview"]["preview_id"])  # double click
        self.assertFalse(again["changed"])
        self.clock.now = NOW + timedelta(days=2)  # same file on another day
        preview = self.backend.preview_import("statement.csv", SAMPLE_CSV.read_text(encoding="utf-8"))
        self.assertTrue(preview["already_imported"])
        self.assertEqual(preview["new_events"], 0)
        self.assertFalse(self.backend.commit_import(preview["preview_id"])["changed"])
        self.assertEqual(event_count(self.database), count)

    def test_later_overlapping_statement_adds_only_new_rows(self) -> None:
        first = self.import_sample()
        count = len(self.backend.snapshot()["transactions"])
        self.clock.now = NOW + timedelta(days=30)
        later = SAMPLE_CSV.read_text(encoding="utf-8").rstrip("\n") + \
            "\nchecking,Everyday Checking,checking,USD,ex-900,4200,debit,posted,Lunch,2026-02-03T12:00:00Z,DEMO DELI\n"
        preview = self.backend.preview_import("february.csv", later)
        self.assertFalse(preview["already_imported"])
        self.assertEqual(preview["source_id"], first["preview"]["source_id"])
        self.assertEqual(preview["source_version"], 2)
        self.assertEqual(preview["skipped_known"], count)
        self.assertEqual(preview["transaction_count"], 1)
        self.assertTrue(self.backend.commit_import(preview["preview_id"])["changed"])
        self.assertEqual(len(self.backend.snapshot()["transactions"]), count + 1)
        again = self.backend.preview_import("february.csv", later)  # the later file twice
        self.assertTrue(again["already_imported"])
        self.assertFalse(self.backend.commit_import(again["preview_id"])["changed"])

    def test_changed_rows_and_foreign_source_ids_are_refused_without_writes(self) -> None:
        self.import_sample()
        count = event_count(self.database)
        changed = SAMPLE_CSV.read_text(encoding="utf-8").replace("Coffee beans", "Tea leaves")
        with self.assertRaises(BackendError) as caught:
            self.backend.preview_import("statement.csv", changed)
        self.assertIn("differ", caught.exception.message)
        with self.assertRaises(BackendError) as caught:
            self.backend.preview_import("statement.csv", changed, source_id="someone-else")
        self.assertIn("already belong", caught.exception.message)
        self.assertEqual(event_count(self.database), count)

    def test_expired_preview_is_explained(self) -> None:
        with self.assertRaises(BackendError) as caught:
            self.backend.commit_import("nope")
        self.assertIn("expired", caught.exception.message)

    def test_csv_transactions_are_marked_unmatchable(self) -> None:
        self.import_sample()
        transactions = self.backend.snapshot()["transactions"]
        self.assertTrue(transactions)
        self.assertTrue(all(not tx["matchable"] for tx in transactions))
        self.assertTrue(all(tx["classification"] in ("spending", "income") for tx in transactions))


class Calculations(BackendCase):
    def setUp(self) -> None:
        super().setUp()
        self.backend.use_demo()

    def test_safe_to_spend_adds_up_in_every_mode_and_matches_the_native_receipt(self) -> None:
        for mode in ("confirmed", "expected", "stress"):
            result = self.backend.calculate(mode=mode, horizon_days=14, floor_minor=50000)
            outputs = result["receipt"]["outputs"]
            self.assertEqual(outputs["safe_to_spend_minor"],
                             outputs["minimum_projected_balance_minor"] - outputs["protected_floor_minor"], mode)
            lowest = min(point["closing_minor"] for point in result["forecast"])
            self.assertEqual(lowest, outputs["minimum_projected_balance_minor"], mode)
            self.assertFalse(result["recorded"])
            direct = json.loads(self.backend._run(
                "calculate", "--db", self.backend.active.database, "--as-of", result["as_of"],
                "--horizon-end", result["horizon_end"], "--mode", mode, "--floor-minor", "50000",
                "--json", "--no-record"))
            self.assertEqual(direct["receipt"]["outputs"], outputs, mode)

    def test_modes_follow_the_confidence_contract(self) -> None:
        out = {mode: self.backend.calculate(mode=mode, horizon_days=14, floor_minor=0)["receipt"]["outputs"]
               for mode in ("confirmed", "expected", "stress")}
        # Confirmed: verified rent and phone only, no goal contributions (goals are "supported").
        self.assertEqual(out["confirmed"]["commitment_total_minor"], 85000 + 9600)
        self.assertEqual(out["confirmed"]["protected_goal_total_minor"], 0)
        # Expected adds the supported subscription and the protected goal, not the estimated utilities.
        self.assertEqual(out["expected"]["commitment_total_minor"], 85000 + 9600 + 3400)
        self.assertEqual(out["expected"]["protected_goal_total_minor"], 25000)
        # Stress adds the estimated utilities at the top of its range.
        self.assertEqual(out["stress"]["commitment_total_minor"], 85000 + 9600 + 3400 + 20000)
        self.assertGreaterEqual(out["confirmed"]["safe_to_spend_minor"], out["expected"]["safe_to_spend_minor"])
        self.assertGreaterEqual(out["expected"]["safe_to_spend_minor"], out["stress"]["safe_to_spend_minor"])

    def test_protected_savings_stay_out_of_spendable_money(self) -> None:
        snapshot = self.backend.snapshot()
        savings = next(a for a in snapshot["accounts"] if a["account_id"] == "savings")
        self.assertEqual(savings["available_minor"], 120000)
        result = self.backend.calculate(mode="expected", horizon_days=14, floor_minor=0)
        self.assertEqual(result["receipt"]["outputs"]["protected_goal_total_minor"], 25000)

    def test_compute_only_and_what_if_never_write(self) -> None:
        database = self.backend.active.database
        before = event_count(database)
        self.backend.calculate(mode="expected", horizon_days=30, floor_minor=12345)
        what_if = self.backend.calculate(mode="expected", horizon_days=14, floor_minor=50000,
                                         what_if={"amount_minor": 40000, "date": "2026-03-15T12:00:00Z"})
        self.assertEqual(event_count(database), before)
        w = what_if["what_if"]
        self.assertEqual(w["safe_to_spend_before_minor"] - w["safe_to_spend_after_minor"], 40000)
        with self.assertRaises(BackendError):
            self.backend.calculate(mode="expected", horizon_days=14, floor_minor=0,
                                   what_if={"amount_minor": 0, "date": "2026-03-15T12:00:00Z"})

    def test_saved_floor_is_recorded_once_per_identical_calculation(self) -> None:
        database = self.backend.active.database
        first = self.backend.calculate(mode="expected", horizon_days=14, floor_minor=70000, record=True)
        self.assertTrue(first["stored"])
        count = event_count(database)
        second = self.backend.calculate(mode="expected", horizon_days=14, floor_minor=70000, record=True)
        self.assertEqual(second["receipt"]["receipt_id"], first["receipt"]["receipt_id"])
        self.assertEqual(event_count(database), count)
        self.assertEqual(self.backend.saved_floor(self.backend.snapshot())["floor_minor"], 70000)


class Classification(BackendCase):
    def test_demo_transfers_refunds_and_pending_items(self) -> None:
        self.backend.use_demo()
        snapshot = self.backend.snapshot()
        by_id = {tx["transaction_id"]: tx for tx in snapshot["transactions"]}
        out = [tx for tx in by_id.values() if tx["classification"] == "transfer_out"]
        into = [tx for tx in by_id.values() if tx["classification"] == "transfer_in"]
        self.assertEqual(len(out), 1)
        self.assertEqual(len(into), 1)
        self.assertEqual(out[0]["related_transaction_id"], into[0]["transaction_id"])
        self.assertNotEqual(out[0]["account_id"], into[0]["account_id"])
        refunds = [tx for tx in by_id.values() if tx["classification"] == "refund"]
        self.assertEqual(len(refunds), 1)
        purchase = by_id[refunds[0]["related_transaction_id"]]
        self.assertEqual(purchase["classification"], "refunded_purchase")
        coffee = next(tx for tx in by_id.values() if tx["description"] == "Coffee")
        self.assertEqual(coffee["transaction_state"], "pending")
        self.assertEqual(coffee["classification"], "spending")

    def test_stale_observation_is_flagged_by_age(self) -> None:
        self.backend.use_demo()
        accounts = {a["account_id"]: a for a in self.backend.snapshot()["accounts"]}
        self.assertTrue(accounts["savings"]["stale_by_age"])  # observed ten days before the demo anchor
        self.assertFalse(accounts["checking"]["stale_by_age"])
        self.clock.now = NOW + timedelta(days=8)
        self.assertTrue(self.backend.snapshot()["accounts"][0]["stale_by_age"])

    def test_demo_is_separate_from_user_data_and_rebuilds_cleanly(self) -> None:
        self.backend.use_demo()
        self.backend.use_demo()  # repeated: rebuilt, not doubled
        self.assertEqual(len(self.backend.snapshot()["transactions"]), 8)
        self.backend.use_user_data()
        self.assertEqual(self.backend.snapshot()["accounts"], [])
        self.assertEqual(event_count(self.database), 0)


class Backups(BackendCase):
    def test_export_verify_restore_into_a_new_workspace(self) -> None:
        self.import_sample()
        self.backend.calculate(mode="expected", horizon_days=14, floor_minor=25000, record=True)
        exported = self.backend.export_backup()
        self.assertTrue(exported["verification"]["ok"])
        self.assertEqual(len(self.backend.list_backups()), 1)
        other = BundleBackend(self.directory / "other.db", native_cli=self.native, clock=self.clock)
        other._run("init", "--db", other.user.database)
        other.restore_backup(exported["path"])
        mine, theirs = self.backend.snapshot(), other.snapshot()
        self.assertEqual(len(theirs["transactions"]), len(mine["transactions"]))
        self.assertEqual(other.saved_floor(theirs)["floor_minor"], 25000)
        count = event_count(other.user.database)
        other.restore_backup(exported["path"])  # repeated restore is a no-op
        self.assertEqual(event_count(other.user.database), count)

    def test_damaged_backup_is_refused_without_changes(self) -> None:
        self.import_sample()
        exported = self.backend.export_backup()
        damaged = Path(exported["path"]) / "transactions.jsonl"
        damaged.write_bytes(damaged.read_bytes().replace(b"Coffee", b"Toffee"))
        other = BundleBackend(self.directory / "other.db", native_cli=self.native, clock=self.clock)
        other._run("init", "--db", other.user.database)
        with self.assertRaises(BackendError) as caught:
            other.restore_backup(exported["path"])
        self.assertIn("damaged", caught.exception.recovery)
        self.assertEqual(event_count(other.user.database), 0)
        with self.assertRaises(BackendError):
            self.backend.verify_backup(str(self.directory / "missing"))

    def test_two_exports_in_the_same_second_get_distinct_folders(self) -> None:
        self.import_sample()
        first = self.backend.export_backup()["path"]
        second = self.backend.export_backup()["path"]
        self.assertNotEqual(first, second)


class Restart(BackendCase):
    def test_data_floor_and_backups_survive_a_new_process(self) -> None:
        self.import_sample()
        self.backend.calculate(mode="stress", horizon_days=30, floor_minor=33300, record=True)
        self.backend.export_backup()
        before = self.backend.snapshot()
        reopened = self.new_backend()  # what `python -m bundle_ui --db` does after a restart
        after = reopened.snapshot()
        self.assertEqual(after["transactions"], before["transactions"])
        self.assertEqual(reopened.saved_floor(after)["floor_minor"], 33300)
        self.assertEqual(len(reopened.list_backups()), 1)


class Server(BackendCase):
    def setUp(self) -> None:
        super().setUp()
        self.server, self.token, url = serve_in_thread(self.backend)
        self.port = self.server.server_address[1]

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        super().tearDown()

    def request(self, method: str, path: str, body: dict | None = None, *, token: str | None = None,
                host: str | None = None) -> tuple[int, dict | bytes, http.client.HTTPResponse]:
        connection = http.client.HTTPConnection("127.0.0.1", self.port, timeout=60)
        headers = {"Host": host or f"127.0.0.1:{self.port}", "Content-Type": "application/json"}
        if token is not None:
            headers["X-Bundle-Token"] = token
        connection.request(method, path, body=json.dumps(body) if body is not None else None, headers=headers)
        response = connection.getresponse()
        data = response.read()
        connection.close()
        try:
            return response.status, json.loads(data), response
        except ValueError:
            return response.status, data, response

    def test_api_requires_the_session_token_and_a_local_host(self) -> None:
        self.assertEqual(self.request("GET", "/api/state")[0], 403)
        self.assertEqual(self.request("GET", "/api/state", token="wrong")[0], 403)
        self.assertEqual(self.request("GET", "/api/state", token=self.token, host="evil.example:80")[0], 403)
        status, payload, _ = self.request("GET", "/api/state", token=self.token)
        self.assertEqual(status, 200)
        self.assertEqual(payload["snapshot"]["accounts"], [])

    def test_page_carries_the_token_and_a_strict_policy(self) -> None:
        status, body, response = self.request("GET", "/")
        self.assertEqual(status, 200)
        self.assertIn(self.token.encode(), body)
        self.assertIn("default-src 'self'", response.getheader("Content-Security-Policy"))
        self.assertEqual(self.request("GET", "/../backend.py")[0], 404)
        status, sample, _ = self.request("GET", "/sample/statement.csv")
        self.assertEqual(status, 200)
        self.assertTrue(sample.startswith(b"account_id,"))
        for name in ("app.js", "app.css", "jar.js", "favicon.svg"):
            self.assertEqual(self.request("GET", "/" + name)[0], 200, name)

    def test_import_flow_and_errors_over_http(self) -> None:
        status, error, _ = self.request("POST", "/api/import/preview",
                                        {"filename": "bad.csv", "content": "a,b\n1,2\n"}, token=self.token)
        self.assertEqual(status, 422)
        self.assertTrue(error["recovery"])
        status, preview, _ = self.request("POST", "/api/import/preview",
                                          {"filename": "statement.csv",
                                           "content": SAMPLE_CSV.read_text(encoding="utf-8")}, token=self.token)
        self.assertEqual(status, 200)
        status, result, _ = self.request("POST", "/api/import/commit", {"preview_id": preview["preview_id"]},
                                         token=self.token)
        self.assertEqual((status, result["changed"]), (200, True))
        status, result, _ = self.request("POST", "/api/import/commit", {"preview_id": preview["preview_id"]},
                                         token=self.token)
        self.assertEqual((status, result["changed"]), (200, False))
        status, calc, _ = self.request("GET", "/api/calculate?mode=expected&horizon_days=14&floor_minor=0",
                                       token=self.token)
        self.assertEqual(status, 200)
        self.assertFalse(calc["recorded"])
        self.assertEqual(self.request("GET", "/api/calculate?mode=x&horizon_days=14&floor_minor=0",
                                      token=self.token)[0], 400)


if __name__ == "__main__":
    unittest.main()
