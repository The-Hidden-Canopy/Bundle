import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "ui"))
from bundle_ui.backend import BundleBackend

class PreviewCurrencyTest(unittest.TestCase):
    def test_preview_keeps_currency_totals_separate(self):
        backend = object.__new__(BundleBackend)
        backend._previews = {}
        def tx(currency, units, direction):
            return {"amount": {"currency": currency, "minor_units": units}, "direction": direction}
        batch = {"transactions": [tx("USD", 20, "debit"), tx("USD", 5, "debit"),
                                  tx("JPY", 500, "credit"), tx("KWD", 250, "debit")]}
        result = backend._store_preview("synthetic.json", "json", batch, {})
        self.assertEqual(result["currency_totals"], [
            {"currency": "JPY", "debits_minor": 0, "credits_minor": 500},
            {"currency": "KWD", "debits_minor": 250, "credits_minor": 0},
            {"currency": "USD", "debits_minor": 25, "credits_minor": 0}])
        self.assertIsNone(result["credits_minor"])
        self.assertIsNone(result["debits_minor"])
        single = backend._store_preview("synthetic.json", "json", {"transactions": batch["transactions"][:2]}, {})
        self.assertEqual(single["debits_minor"], 25)
        self.assertEqual(single["credits_minor"], 0)
        empty = backend._store_preview("synthetic.json", "json", {}, {})
        self.assertIsNone(empty["debits_minor"])
        self.assertEqual(empty["currency_totals"], [])

    def test_floor_receipts_are_scoped_to_single_included_currency(self):
        backend = object.__new__(BundleBackend)
        def account(code, included=1):
            return {"status": "active", "currency": code, "include_in_safe_to_spend": included}
        def receipt(code, floor):
            return {"engine": "bundle.safe_to_spend", "outputs": {"currency": code, "protected_floor_minor": floor},
                    "receipt_id": code, "calculated_at": "2026-11-26", "policy_id": "expected"}
        snapshot = {"accounts": [account("JPY", 0), account("USD")],
                    "receipts": [receipt("JPY", 100), receipt("USD", 20)]}
        self.assertEqual(backend.saved_floor(snapshot)["floor_minor"], 20)
        self.assertEqual(backend.saved_floor(snapshot)["currency"], "USD")
        snapshot["accounts"].append(account("EUR"))
        self.assertIsNone(backend.saved_floor(snapshot))
        snapshot["accounts"].pop()
        snapshot["receipts"] = [receipt("USD", 2**53), receipt("USD", 20), receipt("JPY", 100)]
        self.assertTrue(backend.saved_floor(snapshot)["unavailable"])
        snapshot["receipts"] = [receipt(None, 100), receipt("USD", 20)]
        self.assertTrue(backend.saved_floor(snapshot)["unavailable"])
        snapshot["receipts"] = [receipt("JPY", 100)]
        self.assertIsNone(backend.saved_floor(snapshot))
