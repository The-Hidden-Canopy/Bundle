"""Python reference vs native C++ adapter: same fixtures, same persisted bytes.

Run (after building the native CLI):

    python -m unittest discover -s tests/parity -v

Set BUNDLE_NATIVE_CLI if the executable is not at build/core/persistence/bundle(.exe).
"""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
import sqlite3
import subprocess
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from harness import (
    CSV_SOURCE, FIXTURES, SQLiteFinanceStore, add_planning_rows, batch_from_document, build_python_reference,
    csv_batch, dump, file_sha256, fixture_document, materialize_fixture, native, tree, unavailable, write_export,
)
from bundle_connectors.csv_import import import_csv  # noqa: E402 (harness puts connectors on the path)

UTC = timezone.utc
CREATED_AT = "2026-01-06T00:00:00Z"
FIXTURE_HASHES = {
    "v1-python-reference.sql": "42f595b8561cfc749631b5e868f75968cd911434c365ec8285f41d9991073fd5",
    "pre-constraint-valid.sql": "ed179a212e3206ecc3998e62c6eb6e4afe19a9f3985cd5667351a62669be17c9",
    "pre-constraint-malformed-hash.sql": "f4c2695a5a507ec1c0603afa86e124013d86d6425ae823258deee7fb131d04ab",
}


def write_json(path: Path, document: object) -> Path:
    path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
    return path


def native_import_all(database: Path, temporary: Path) -> None:
    native("import-csv", "--db", database, "--source-id", CSV_SOURCE, "--imported-at", "2026-01-04T12:00:00Z",
           FIXTURES / "statement.csv")
    native("import-batch", "--db", database, FIXTURES / "batch-bank-v1.json")
    native("import-batch", "--db", database, FIXTURES / "batch-bank-v2-revision.json")


def python_import_all(database: Path) -> None:
    with SQLiteFinanceStore(database) as store:
        store.append_batch(csv_batch())
        store.append_batch(batch_from_document(fixture_document("batch-bank-v1.json")))
        store.append_batch(batch_from_document(fixture_document("batch-bank-v2-revision.json")))


class ImportParity(unittest.TestCase):
    def test_csv_converts_to_the_identical_connector_batch(self) -> None:
        produced = native("csv-to-batch", "--source-id", CSV_SOURCE, "--imported-at", "2026-01-04T12:00:00Z",
                          FIXTURES / "statement.csv").stdout
        self.assertEqual(json.loads(produced), csv_batch().to_dict())

    def test_later_csv_statement_continues_the_source_as_the_next_version(self) -> None:
        later = ("account_id,account_name,account_type,currency,transaction_id,amount_minor,direction,state,"
                 "description,observed_at\nchecking,Everyday Checking,Checking,USD,chk-004,4200,debit,posted,"
                 "Lunch,2026-02-03T12:00:00Z\n")
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            path = temporary / "later.csv"
            path.write_text(later, encoding="utf-8")
            produced = json.loads(native("csv-to-batch", "--source-id", CSV_SOURCE, "--imported-at",
                                         "2026-02-04T12:00:00Z", "--source-version", "2", path).stdout)
            with path.open("r", encoding="utf-8", newline="") as stream:
                reference = import_csv(stream, source_id=CSV_SOURCE, source_version=2,
                                       imported_at=datetime(2026, 2, 4, 12, 0, tzinfo=UTC))
            self.assertEqual(produced, reference.to_dict())
            self.assertEqual(produced["accounts"][0]["source_version"], 2)
            # Version 1 output is unchanged (no source_version key), so stored batch IDs stay valid.
            self.assertNotIn("source_version", csv_batch().to_dict()["accounts"][0])
            native("import-csv", "--db", temporary / "native.db", "--source-id", CSV_SOURCE, "--imported-at",
                   "2026-01-04T12:00:00Z", FIXTURES / "statement.csv")
            native("import-batch", "--db", temporary / "native.db", write_json(temporary / "b.json", produced))
            with SQLiteFinanceStore(temporary / "python.db") as store:
                store.append_batch(csv_batch())
                store.append_batch(reference)
            reference_rows, candidate_rows = dump(temporary / "python.db"), dump(temporary / "native.db")
            for table in reference_rows:
                self.assertEqual(candidate_rows[table], reference_rows[table], f"table {table} differs")

    def test_import_sequence_persists_identical_rows_and_event_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            python_import_all(temporary / "python.db")
            native_import_all(temporary / "native.db", temporary)
            reference, candidate = dump(temporary / "python.db"), dump(temporary / "native.db")
            self.assertEqual(set(reference), set(candidate))
            for table in reference:
                self.assertEqual(candidate[table], reference[table], f"table {table} differs")
            self.assertGreater(len(reference["event_log"]), 10)

    def test_duplicate_imports_cannot_double_count(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            database = temporary / "native.db"
            # Each batch re-admitted right after itself is a no-op (same batch ID, no rows, no events).
            steps = [
                ("import-csv", "--db", database, "--source-id", CSV_SOURCE, "--imported-at",
                 "2026-01-04T12:00:00Z", FIXTURES / "statement.csv"),
                ("import-batch", "--db", database, FIXTURES / "batch-bank-v1.json"),
                ("import-batch", "--db", database, FIXTURES / "batch-bank-v2-revision.json"),
            ]
            for step in steps:
                first = native(*step).stdout
                state = dump(database)
                for _ in range(2):
                    again = native(*step).stdout
                    self.assertEqual(again.split()[2], first.split()[2])  # same batch ID
                    self.assertIn("nothing changed", again)
                    self.assertEqual(dump(database), state)
            # Replaying the older v1 batch after v2 is rejected (reference contract), with no writes.
            final = dump(database)
            result = native("import-batch", "--db", database, FIXTURES / "batch-bank-v1.json", expect=1)
            self.assertIn("older source version", result.stderr)
            self.assertEqual(dump(database), final)
            python_db = temporary / "python.db"
            python_import_all(python_db)
            with self.assertRaises(ValueError):
                with SQLiteFinanceStore(python_db) as store:
                    store.append_batch(batch_from_document(fixture_document("batch-bank-v1.json")))
            connection = sqlite3.connect(database)
            totals = connection.execute(
                "SELECT COUNT(*), SUM(amount_minor) FROM transaction_revisions r JOIN transactions t "
                "ON t.account_id = r.account_id AND t.source_transaction_id = r.source_transaction_id "
                "AND t.current_revision = r.revision").fetchone()
            connection.close()
            self.assertEqual(totals, (5, 125000 + 95000 + 1250 + 50000 + 4150))

    def test_rejected_batches_match_reference_and_write_nothing(self) -> None:
        base = fixture_document("batch-bank-v1.json")
        cases: dict[str, dict] = {}
        changed = copy.deepcopy(base)
        changed["transactions"][0]["amount"]["minor_units"] = 9999  # same source identity, changed content
        cases["changed content under an existing source"] = changed
        older = copy.deepcopy(fixture_document("batch-bank-v2-revision.json"))
        older["accounts"][0]["source_version"] = 1
        cases["mixed account source versions"] = older
        gap = copy.deepcopy(base)
        gap["transactions"][0].update(revision=3, supersedes_transaction_revision=2, transaction_id="gap")
        gap["transactions"][0]["source_ref"] = dict(gap["transactions"][0]["source_ref"], source_version=3)
        gap["accounts"][0]["source_version"] = 3
        cases["revision gap"] = gap
        naive = copy.deepcopy(base)
        naive["balances"][0]["source_ref"] = dict(naive["balances"][0]["source_ref"], observed_at="2026-01-04T11:59:00")
        cases["naive source timestamp"] = naive
        short = copy.deepcopy(base)
        short["balances"][0]["source_ref"] = dict(short["balances"][0]["source_ref"], content_hash="sha256:short")
        cases["short content hash"] = short
        currency = copy.deepcopy(base)
        currency["transactions"][0]["amount"]["currency"] = "EUR"
        cases["amount currency mismatch"] = currency
        unknown = copy.deepcopy(base)
        unknown["balances"][0]["account_id"] = "no-such-account"
        cases["balance for unknown account"] = unknown
        external = copy.deepcopy(base)
        external["external_commitments"] = [{"name": "rent"}]
        cases["external commitments"] = external
        negative = copy.deepcopy(base)
        negative["transactions"][0]["amount"]["minor_units"] = -1
        cases["negative amount"] = negative
        state = copy.deepcopy(base)
        state["transactions"][0]["transaction_state"] = "settled"
        cases["unsupported state"] = state
        # Fails late: a new source, an account update, balances and a valid first
        # transaction are written inside the transaction before the second
        # transaction's revision gap is detected. Nothing may survive.
        late = copy.deepcopy(base)
        v3 = dict(late["transactions"][0]["source_ref"], source_version=3, content_hash="sha256:" + "ef" * 32)
        late["accounts"][0]["source_version"] = 3
        for balance in late["balances"]:
            balance["source_ref"] = v3
            balance["observation_id"] = "late-" + balance["account_id"]
            balance["observed_at"] = "2026-01-06T00:00:00Z"
        valid = dict(late["transactions"][0], transaction_id="card-late-ok", source_transaction_id="card-late-ok",
                     source_ref=v3)
        gapped = dict(late["transactions"][0], transaction_id="card-late-gap", source_transaction_id="card-001",
                      revision=5, supersedes_transaction_revision=4, source_ref=v3)
        late["transactions"] = [valid, gapped]
        cases["late revision gap after writes"] = late

        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            python_import_all(temporary / "python.db")
            native_import_all(temporary / "native.db", temporary)
            reference_before, native_before = dump(temporary / "python.db"), dump(temporary / "native.db")
            for label, document in cases.items():
                with self.subTest(case=label):
                    path = write_json(temporary / "case.json", document)
                    with self.assertRaises(ValueError, msg="Python reference must reject"):
                        with SQLiteFinanceStore(temporary / "python.db") as store:
                            store.append_batch(batch_from_document(document))
                    result = native("import-batch", "--db", temporary / "native.db", path, expect=1)
                    self.assertIn("rejected", result.stderr)
                    self.assertEqual(dump(temporary / "python.db"), reference_before)
                    self.assertEqual(dump(temporary / "native.db"), native_before)


class ArchiveParity(unittest.TestCase):
    def test_exports_are_byte_identical_and_restore_cross_compatibly(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            build_python_reference(temporary / "python.db")
            with SQLiteFinanceStore(temporary / "python.db") as store:
                store.export_archive(temporary / "python-archive", created_at=datetime(2026, 1, 6, tzinfo=UTC))
            # Same logical state reached natively: imports + planning restored from the Python archive.
            native_import_all(temporary / "native.db", temporary)
            native("restore", "--db", temporary / "native.db", temporary / "python-archive")
            native("export", "--db", temporary / "native.db", "--created-at", CREATED_AT, temporary / "native-archive")
            self.assertEqual(tree(temporary / "native-archive"), tree(temporary / "python-archive"))

            # Restore into clean databases on both sides from the same archive.
            native("restore", "--db", temporary / "clean-native.db", temporary / "python-archive")
            with SQLiteFinanceStore(temporary / "clean-python.db") as store:
                store.import_archive(temporary / "native-archive")
            self.assertEqual(dump(temporary / "clean-native.db"), dump(temporary / "clean-python.db"))
            native("export", "--db", temporary / "clean-native.db", "--created-at", CREATED_AT,
                   temporary / "round-trip")
            self.assertEqual(tree(temporary / "round-trip"), tree(temporary / "python-archive"))

    def test_malformed_and_tampered_archives_are_rejected_without_writes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            build_python_reference(temporary / "python.db")
            with SQLiteFinanceStore(temporary / "python.db") as store:
                store.export_archive(temporary / "good", created_at=datetime(2026, 1, 6, tzinfo=UTC))
            native("restore", "--db", temporary / "target.db", temporary / "good")
            baseline = dump(temporary / "target.db")

            def variant(name: str, mutate) -> Path:
                target = temporary / name
                shutil.copytree(temporary / "good", target)
                mutate(target)
                return target

            def rehash(target: Path, filename: str, payload: bytes) -> None:
                (target / filename).write_bytes(payload)
                manifest = json.loads((target / "manifest.json").read_text(encoding="utf-8"))
                for entry in manifest["files"]:
                    if entry["path"] == filename:
                        entry["bytes"] = len(payload)
                        entry["sha256"] = hashlib.sha256(payload).hexdigest()
                (target / "manifest.json").write_text(
                    json.dumps(manifest, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n",
                    encoding="utf-8")

            def tamper_commitment(target: Path) -> None:
                records = json.loads((target / "commitments.json").read_text(encoding="utf-8"))
                records[0]["name"] = "Tampered rent"
                rehash(target, "commitments.json",
                       (json.dumps(records, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode())

            def tamper_late_receipt(target: Path) -> None:
                # Valid earlier files, conflicting record in the *last* restored file.
                lines = (target / "transactions.jsonl").read_text(encoding="utf-8").splitlines()
                record = json.loads(lines[-1])
                record["description"] = "changed late"
                lines[-1] = json.dumps(record, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
                rehash(target, "transactions.jsonl", ("\n".join(lines) + "\n").encode())

            cases = {
                "bit flip without rehash": lambda t: (t / "accounts.json").write_bytes(
                    (t / "accounts.json").read_bytes().replace(b"Everyday", b"Everydey")),
                "missing data file": lambda t: (t / "goals.json").unlink(),
                "truncated jsonl": lambda t: (t / "balances.jsonl").write_bytes(
                    (t / "balances.jsonl").read_bytes()[:-20]),
                "unsupported schema": lambda t: (t / "manifest.json").write_text(
                    (t / "manifest.json").read_text(encoding="utf-8").replace("bundle-finance-1", "bundle-finance-9"),
                    encoding="utf-8"),
                "rehashed conflicting commitment": tamper_commitment,
                "rehashed conflicting transaction": tamper_late_receipt,
                "blank jsonl line rehashed": lambda t: rehash(
                    t, "overrides.jsonl", (t / "overrides.jsonl").read_bytes() + b"\n"),
                "invalid json rehashed": lambda t: rehash(t, "budgets.json", b"[{\"budget_id\":}]\n"),
                "naive manifest time": lambda t: (t / "manifest.json").write_text(
                    (t / "manifest.json").read_text(encoding="utf-8").replace(
                        '"2026-01-06T00:00:00Z"', '"2026-01-06T00:00:00"'), encoding="utf-8"),
            }
            # Fails late: restore into a database whose only conflict is the *last*
            # goal, so sources, accounts, balances, transactions, commitments and
            # budgets are written inside the transaction before the conflict.
            late_target = temporary / "late.db"
            native("init", "--db", late_target)
            connection = sqlite3.connect(late_target)
            connection.execute("INSERT INTO goals VALUES ('buffer', 'Different buffer', 1, 'USD', NULL, 0, 0, 1, 0, "
                               "'active')")
            connection.commit()
            connection.close()
            late_before = dump(late_target)
            result = native("restore", "--db", late_target, temporary / "good", expect=1)
            self.assertIn("conflicts with existing local data", result.stderr)
            self.assertEqual(dump(late_target), late_before)
            shutil.copyfile(late_target, temporary / "late-python.db")
            with self.assertRaises(ValueError):
                with SQLiteFinanceStore(temporary / "late-python.db") as store:
                    store.import_archive(temporary / "good")
            self.assertEqual(dump(temporary / "late-python.db"), late_before)

            for label, mutate in cases.items():
                with self.subTest(case=label):
                    archive = variant(label.replace(" ", "-"), mutate)
                    result = native("restore", "--db", temporary / "target.db", archive, expect=1)
                    self.assertIn("rejected", result.stderr)
                    self.assertEqual(dump(temporary / "target.db"), baseline)
                    shutil.copyfile(temporary / "python.db", temporary / "python-copy.db")
                    with self.assertRaises(ValueError, msg="Python reference must reject too"):
                        with SQLiteFinanceStore(temporary / "python-copy.db") as store:
                            store.import_archive(archive)

    def test_export_refuses_unrepresented_rows_and_existing_destination(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            native_import_all(temporary / "native.db", temporary)
            native("export", "--db", temporary / "native.db", "--created-at", CREATED_AT, temporary / "a")
            result = native("export", "--db", temporary / "native.db", "--created-at", CREATED_AT, temporary / "a",
                            expect=1)
            self.assertIn("already exists", result.stderr)
            connection = sqlite3.connect(temporary / "native.db")
            connection.execute("INSERT INTO merchants VALUES ('m', 'Synthetic', NULL, 'inferred', 0)")
            connection.commit()
            connection.close()
            result = native("export", "--db", temporary / "native.db", "--created-at", CREATED_AT, temporary / "b",
                            expect=1)
            self.assertIn("merchants", result.stderr)
            self.assertFalse((temporary / "b").exists())


class ExamplePlanningArchive(unittest.TestCase):
    def test_cmake_packer_matches_python_reference_writer(self) -> None:
        cmake = shutil.which("cmake")
        if cmake is None:
            unavailable("cmake not on PATH")
        example = FIXTURES.parents[2] / "examples" / "native-sqlite"
        records = {name: json.loads((example / "planning-records" / name).read_text(encoding="utf-8"))
                   for name in ("accounts.json", "budgets.json", "commitments.json", "goals.json")}
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            write_export(temporary / "python", created_at=datetime(2026, 1, 4, 12, tzinfo=UTC),
                         accounts=records["accounts.json"], budgets=records["budgets.json"],
                         commitments=records["commitments.json"], goals=records["goals.json"])
            result = subprocess.run([cmake, f"-DOUT={temporary / 'cmake'}", "-P",
                                     str(example / "make-planning-archive.cmake")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(tree(temporary / "cmake"), tree(temporary / "python"))


class CalculationParity(unittest.TestCase):
    def test_calculation_is_deterministic_and_receipts_round_trip_through_python(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            build_python_reference(temporary / "python.db")
            with SQLiteFinanceStore(temporary / "python.db") as store:
                store.export_archive(temporary / "seed", created_at=datetime(2026, 1, 6, tzinfo=UTC))
            native("restore", "--db", temporary / "native.db", temporary / "seed")
            args = ("calculate", "--db", temporary / "native.db", "--as-of", "2026-01-04T12:00:00Z",
                    "--horizon-end", "2026-02-28T00:00:00Z", "--floor-minor", "20000")
            first = native(*args).stdout.splitlines()
            second = native(*args).stdout.splitlines()
            self.assertIn("(recorded)", first[1])
            self.assertIn("already recorded", second[1])
            self.assertEqual(json.loads(first[2]), json.loads(second[2]))
            receipt = json.loads(first[2])
            outputs = receipt["outputs"]
            # Independent arithmetic check for this fixture (expected mode):
            # liquid = included available balances. The card balance (observed 11:00) is never
            # projected because the account was created by the same batch with a placeholder
            # observed time of 11:59 -- reference behavior, reproduced natively -- so it is 0.
            # utilities is 'inferred' (included in expected mode); rent 2026-02-01, utilities
            # 2026-01-20 and 2026-02-20, protected goal 25000 on 2026-02-15.
            liquid = 300000 + 50000 + 0
            self.assertEqual(outputs["current_liquid_minor"], liquid)
            self.assertEqual(outputs["commitment_total_minor"], 95000 + 10000 + 10000)
            self.assertEqual(outputs["protected_goal_total_minor"], 25000)
            self.assertEqual(outputs["safe_to_spend_minor"], liquid - 95000 - 20000 - 25000 - 20000)
            self.assertEqual(native(*args[:-2], "--floor-minor", "20000", "--mode", "stress").returncode, 0)

            native("export", "--db", temporary / "native.db", "--created-at", CREATED_AT, temporary / "with-receipt")
            with SQLiteFinanceStore(temporary / "python-restored.db") as store:
                store.import_archive(temporary / "with-receipt")
                store.export_archive(temporary / "python-reexport", created_at=datetime(2026, 1, 6, tzinfo=UTC))
            self.assertEqual(tree(temporary / "python-reexport"), tree(temporary / "with-receipt"))
            native_events = [row for row in dump(temporary / "native.db")["event_log"]
                             if row[1] == "calculation_receipt.appended"]
            python_events = [row for row in dump(temporary / "python-restored.db")["event_log"]
                             if row[1] == "calculation_receipt.appended"]
            self.assertEqual(sorted(native_events), sorted(python_events))
            self.assertEqual(len(native_events), 2)  # expected + stress receipts


class MigrationParity(unittest.TestCase):
    def test_supported_older_fixtures_open_without_modifying_originals(self) -> None:
        for name, expected in FIXTURE_HASHES.items():
            self.assertEqual(file_sha256(FIXTURES / "legacy" / name), expected, f"{name} fixture changed")
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            for name in ("v1-python-reference.sql", "pre-constraint-valid.sql"):
                with self.subTest(fixture=name):
                    (temporary / "native").mkdir(exist_ok=True)
                    (temporary / "python").mkdir(exist_ok=True)
                    native_copy = materialize_fixture(name, temporary / "native")
                    python_copy = materialize_fixture(name, temporary / "python")
                    native("init", "--db", native_copy)
                    SQLiteFinanceStore(python_copy).close()
                    self.assertEqual(dump(native_copy), dump(python_copy))
                    # The migrated database keeps working on the native path.
                    native("export", "--db", native_copy, "--created-at", CREATED_AT, temporary / f"export-{name}")
            malformed = materialize_fixture("pre-constraint-malformed-hash.sql", temporary / "native")
            before = file_sha256(malformed)
            result = native("history", "--db", malformed, expect=1)
            self.assertIn("SHA-256", result.stderr)
            self.assertEqual(file_sha256(malformed), before, "a refused native open must leave the file untouched")
            python_malformed = materialize_fixture("pre-constraint-malformed-hash.sql", temporary / "python")
            python_before = file_sha256(python_malformed)
            with self.assertRaises(ValueError):
                SQLiteFinanceStore(python_malformed)
            # R-013: a refused open must leave the file untouched on both paths.
            self.assertEqual(file_sha256(python_malformed), python_before)
        for name, expected in FIXTURE_HASHES.items():
            self.assertEqual(file_sha256(FIXTURES / "legacy" / name), expected, f"{name} fixture was modified")


class ReferenceFindings(unittest.TestCase):
    """Regression checks for reference bugs found by this suite (R-010, R-011):
    both implementations must follow the documented contract."""

    def _evidence_archive(self, temporary: Path) -> Path:
        with SQLiteFinanceStore(temporary / "seed.db") as store:
            store.append_batch(csv_batch())
            add_planning_rows(store)
            store.connection.execute("INSERT INTO commitment_evidence VALUES (?, ?, ?, ?)",
                                     ("rent", CSV_SOURCE, 1, None))
            store.export_archive(temporary / "archive", created_at=datetime(2026, 1, 6, tzinfo=UTC))
        return temporary / "archive"

    def test_A_native_restore_is_idempotent_for_null_evidence_keys(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            archive = self._evidence_archive(temporary)
            for _ in range(3):
                native("restore", "--db", temporary / "native.db", archive)
            connection = sqlite3.connect(temporary / "native.db")
            self.assertEqual(connection.execute("SELECT COUNT(*) FROM commitment_evidence").fetchone()[0], 2)
            connection.close()

    def test_A_python_reference_restore_is_idempotent_for_null_evidence_keys(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            archive = self._evidence_archive(temporary)
            with SQLiteFinanceStore(temporary / "python.db") as store:
                for _ in range(3):
                    store.import_archive(archive)
                count = store.connection.execute("SELECT COUNT(*) FROM commitment_evidence").fetchone()[0]
            self.assertEqual(count, 2)  # was 4 before R-010 was fixed

    def _line_separator_archive(self, temporary: Path) -> Path:
        csv = (FIXTURES / "statement.csv").read_text(encoding="utf-8").replace("Payroll deposit", "Payroll\u2028deposit")
        (temporary / "s.csv").write_text(csv, encoding="utf-8", newline="")
        native("import-csv", "--db", temporary / "seed.db", "--source-id", CSV_SOURCE, "--imported-at",
               "2026-01-04T12:00:00Z", temporary / "s.csv")
        native("export", "--db", temporary / "seed.db", "--created-at", CREATED_AT, temporary / "archive")
        return temporary / "archive"

    def test_B_native_restores_text_containing_line_separators(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            archive = self._line_separator_archive(temporary)
            native("restore", "--db", temporary / "native.db", archive)
            self.assertEqual(dump(temporary / "native.db")["transaction_revisions"],
                             dump(temporary / "seed.db")["transaction_revisions"])

    def test_B_python_reference_restores_its_own_export_with_line_separators(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temporary = Path(temporary)
            archive = self._line_separator_archive(temporary)
            with SQLiteFinanceStore(temporary / "python.db") as store:
                store.import_archive(archive)  # raised before R-011 was fixed
            self.assertEqual(dump(temporary / "python.db")["transaction_revisions"],
                             dump(temporary / "seed.db")["transaction_revisions"])


if __name__ == "__main__":
    unittest.main()
