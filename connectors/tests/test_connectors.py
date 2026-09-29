import io
import json
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path

from bundle_connectors.archive import read_export, write_export
from bundle_connectors.csv_import import import_csv
from bundle_connectors.ofx_import import import_ofx
from bundle_connectors.protocol import ConnectorContractError, ConnectorManifest


UTC = timezone.utc


CSV = """account_id,account_name,account_type,currency,transaction_id,amount_minor,direction,state,description,observed_at,merchant_raw
checking,Checking,checking,USD,t-1,950,debit,posted,Rent,2026-01-01T00:00:00Z,RENT
checking,Checking,checking,USD,t-2,780,credit,posted,Paycheck,2026-01-02T00:00:00Z,EMPLOYER
"""


OFX = """OFXHEADER:100
DATA:OFXSGML
<OFX><SIGNONMSGSRSV1><SONRS><STATUS><CODE>0<SEVERITY>INFO</STATUS></SONRS></SIGNONMSGSRSV1>
<BANKMSGSRSV1><STMTTRNRS><STMTRS><CURDEF>USD
<BANKACCTFROM><BANKID>111<ACCTID>123456<ACCTTYPE>CHECKING</BANKACCTFROM>
<BANKTRANLIST><STMTTRN><TRNTYPE>DEBIT<DTPOSTED>20260101120000<TRNAMT>-12.34<FITID>fit-1<NAME>COFFEE SHOP<MEMO>Morning</STMTTRN>
<STMTTRN><TRNTYPE>CREDIT<DTPOSTED>20260102120000<TRNAMT>1000.00<FITID>fit-2<NAME>PAYROLL</STMTTRN></BANKTRANLIST>
<LEDGERBAL><BALAMT>987.66<DTASOF>20260102120000</LEDGERBAL>
</STMTRS></STMTTRNRS></BANKMSGSRSV1></OFX>
"""


QFX_CREDIT_CARD = """OFXHEADER:100
DATA:OFXSGML
<OFX><CREDITCARDMSGSRSV1><CCSTMTTRNRS><CCSTMTRS><CURDEF>USD
<CCACCTFROM><ACCTID>987654</CCACCTFROM>
<BANKTRANLIST><STMTTRN><TRNTYPE>CREDIT<DTPOSTED>20260102120000[-5:EST]<TRNAMT>10.00<FITID>qfx-1<NAME>REFUND</STMTTRN></BANKTRANLIST>
</CCSTMTRS></CCSTMTTRNRS></CREDITCARDMSGSRSV1></OFX>
"""


class ConnectorTests(unittest.TestCase):
    def test_csv_import_is_exact_and_provenance_bearing(self) -> None:
        batch = import_csv(
            io.StringIO(CSV),
            source_id="csv-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
        )
        self.assertEqual([row["account_id"] for row in batch.accounts], ["checking"])
        self.assertEqual(batch.transactions[0]["amount"]["minor_units"], 950)
        self.assertTrue(batch.transactions[0]["source_ref"]["content_hash"].startswith("sha256:"))
        self.assertEqual(batch.to_dict()["schema"], "bundle-connector-1")

    def test_csv_rejects_duplicates_and_naive_timestamps(self) -> None:
        duplicate = CSV.replace("t-2", "t-1")
        with self.assertRaises(ConnectorContractError):
            import_csv(
                io.StringIO(duplicate),
                source_id="csv-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            )
        with self.assertRaises(ConnectorContractError):
            import_csv(
                io.StringIO(CSV.replace("2026-01-01T00:00:00Z", "2026-01-01T00:00:00")),
                source_id="csv-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            )

    def test_ofx_import_is_decimal_and_requires_timezone_for_naive_statement_dates(self) -> None:
        batch = import_ofx(
            io.StringIO(OFX),
            source_id="ofx-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            source_timezone=UTC,
        )
        self.assertEqual(batch.accounts[0]["account_id"], "ofx-1:123456")
        self.assertEqual(batch.transactions[0]["amount"]["minor_units"], 1234)
        self.assertEqual(batch.transactions[0]["direction"], "debit")
        self.assertEqual(batch.transactions[1]["amount"]["minor_units"], 100000)
        self.assertEqual(batch.balances[0]["current_minor"], 98766)
        self.assertEqual(batch.transactions[0]["source_ref"]["external_object_id"], "fit-1")
        with self.assertRaises(ConnectorContractError):
            import_ofx(
                io.StringIO(OFX),
                source_id="ofx-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            )

    def test_ofx_rejects_duplicate_fitids_and_excess_precision(self) -> None:
        duplicate = OFX.replace("fit-2", "fit-1")
        with self.assertRaises(ConnectorContractError):
            import_ofx(
                io.StringIO(duplicate),
                source_id="ofx-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
                source_timezone=UTC,
            )
        precise = OFX.replace("-12.34", "-12.345")
        with self.assertRaises(ConnectorContractError):
            import_ofx(
                io.StringIO(precise),
                source_id="ofx-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
                source_timezone=UTC,
            )

    def test_qfx_credit_card_and_embedded_offset_are_supported(self) -> None:
        batch = import_ofx(
            io.StringIO(QFX_CREDIT_CARD),
            source_id="qfx-1",
            imported_at=datetime(2026, 1, 3, tzinfo=UTC),
            connector_id="qfx",
        )
        self.assertEqual(batch.accounts[0]["account_type"], "credit_card")
        self.assertEqual(batch.transactions[0]["direction"], "credit")
        self.assertEqual(batch.transactions[0]["amount"]["minor_units"], 1000)
        self.assertEqual(batch.transactions[0]["source_ref"]["source_kind"], "qfx")

    def test_ofx_rejects_contradictory_transaction_signs(self) -> None:
        contradictory = OFX.replace("<TRNTYPE>DEBIT<DTPOSTED>20260101120000<TRNAMT>-12.34", "<TRNTYPE>DEBIT<DTPOSTED>20260101120000<TRNAMT>12.34")
        with self.assertRaises(ConnectorContractError):
            import_ofx(
                io.StringIO(contradictory),
                source_id="ofx-1",
                imported_at=datetime(2026, 1, 3, tzinfo=UTC),
                source_timezone=UTC,
            )

    def test_connector_manifest_cannot_gain_write_authority(self) -> None:
        with self.assertRaises(ConnectorContractError):
            ConnectorManifest("bank", "1.0.0", ("payments.write",), network_required=True)
        with self.assertRaises(ConnectorContractError):
            ConnectorManifest("bank", "1.0.0", ("transactions.read",), network_required=True, writes_external_state=True)

    def test_export_round_trip_is_hash_verified_and_portable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            destination = Path(temporary) / "bundle-finance-1"
            write_export(
                destination,
                created_at=datetime(2026, 1, 3, tzinfo=UTC),
                accounts=({"account_id": "checking", "currency": "USD"},),
                transactions=({"transaction_id": "t-1", "amount_minor": 950},),
            )
            restored = read_export(destination)
            self.assertEqual(restored["accounts.json"][0]["account_id"], "checking")
            self.assertEqual(restored["transactions.jsonl"][0]["amount_minor"], 950)

    def test_export_rejects_secret_looking_fields(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            secret_destination = Path(temporary) / "bundle-finance-1"
            with self.assertRaises(ConnectorContractError):
                write_export(
                    secret_destination,
                    created_at=datetime(2026, 1, 3, tzinfo=UTC),
                    accounts=({"account_id": "checking", "access_token": "must-not-export"},),
                )
            self.assertFalse(secret_destination.exists())
            with self.assertRaises(ConnectorContractError):
                write_export(
                    Path(temporary) / "api-key",
                    created_at=datetime(2026, 1, 3, tzinfo=UTC),
                    accounts=({"account_id": "checking", "api_key": "must-not-export"},),
                )
            with self.assertRaises(ConnectorContractError):
                write_export(
                    Path(temporary) / "invalid-records",
                    created_at=datetime(2026, 1, 3, tzinfo=UTC),
                    transactions=(None,),
                )

    def test_export_jsonl_order_is_deterministic_and_manifest_errors_are_bounded(self) -> None:
        balances = (
            {"observation_id": "balance-2", "current_minor": 2},
            {"observation_id": "balance-1", "current_minor": 1},
        )
        with tempfile.TemporaryDirectory() as temporary:
            first = Path(temporary) / "first"
            second = Path(temporary) / "second"
            write_export(
                first,
                created_at=datetime(2026, 1, 3, tzinfo=UTC),
                balances=balances,
            )
            write_export(
                second,
                created_at=datetime(2026, 1, 3, tzinfo=UTC),
                balances=tuple(reversed(balances)),
            )
            self.assertEqual(
                (first / "balances.jsonl").read_bytes(),
                (second / "balances.jsonl").read_bytes(),
            )

            (first / "manifest.json").write_text("{\"schema\": \"bundle-finance-1\"}", encoding="utf-8")
            with self.assertRaises(ConnectorContractError):
                read_export(first)


if __name__ == "__main__":
    unittest.main()
