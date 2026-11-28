"""Synthetic local goal authority and adapter tests; no real accounts or payments."""
from __future__ import annotations
import json
import threading
import unittest
from datetime import timedelta
from pathlib import Path
from unittest.mock import Mock
from test_ui_backend import BackendCase, NOW, event_count
from bundle_ui.backend import BackendError, BundleBackend, Workspace
from bundle_ui.server import Api, ROUTES


def request(database, **changes):
    return {"request_id": "0123456789abcdef0123456789abcdef", "name": "Synthetic laptop goal",
            "currency": "KWD", "target_minor": 123456, "target_date": "2026-12-01",
            "protected": False, "workspace_database": str(database), **changes}


class GoalAdapter(unittest.TestCase):
    def test_validation_before_native_call_and_native_payload(self):
        backend = object.__new__(BundleBackend)
        backend.active = Workspace(Path("synthetic.db"))
        backend._lock = threading.Lock()
        backend.clock = lambda: NOW
        def native(*args, **kwargs):
            self.assertEqual(args[:3], ("create-goal", "--db", str(backend.active.database)))
            self.assertTrue(kwargs["expect_json"])
            payload = json.loads(args[3].read_text(encoding="utf-8"))
            self.assertEqual(payload["target_minor"], 123456)
            self.assertEqual(payload["currency"], "KWD")
            self.assertEqual(payload["target_date"], "2026-12-01T12:00:00Z")
            self.assertNotIn("workspace_database", payload)
            return {"created": True}
        backend._run = Mock(side_effect=native)
        for changes in ({"target_minor": True}, {"target_minor": 1.2}, {"target_minor": -1},
                        {"target_minor": 2**53}, {"currency": "ZZZ"}, {"name": " "},
                        {"protected": "yes"}, {"target_date": "2026-02-30"}, {"request_id": "bad"},
                        {"workspace_database": "different"}, {"reserved_minor": 5}):
            with self.assertRaises(BackendError, msg=str(changes)):
                backend.create_goal(request(backend.active.database, **changes))
        backend._run.assert_not_called()
        self.assertIs(ROUTES[("POST", "/api/goals/create")], Api.create_goal)
        self.assertTrue(Api(backend).create_goal({}, request(backend.active.database))["created"])
        backend._run.assert_called_once()


class GoalNative(BackendCase):
    def test_native_goal_roundtrip_has_provenance_and_does_not_move_money(self):
        self.import_sample()
        before = self.backend.snapshot()
        body = request(self.backend.active.database)
        first = Api(self.backend).create_goal({}, body)
        self.assertTrue(first["created"])
        created = self.backend.snapshot()
        for field in ("accounts", "transactions", "balance_observations"):
            self.assertEqual(before[field], created[field])
        self.assertEqual(created["event_count"], before["event_count"] + 2)
        goal = next(g for g in created["goals"] if g["goal_id"] == first["goal_id"])
        self.assertEqual(goal["currency"], "KWD")
        self.assertEqual(goal["target_minor"], 123456)
        self.assertEqual(goal["current_reserved_minor"], 0)
        self.assertEqual(goal["planned_contribution_minor"], 0)
        self.assertTrue(goal["user_created_at"])
        self.clock.now += timedelta(days=1)
        self.assertFalse(self.backend.create_goal(body)["created"])
        self.assertEqual(event_count(self.database), created["event_count"])
        with self.assertRaises(BackendError):
            self.backend.create_goal(body | {"target_minor": 123457})
        self.assertEqual(event_count(self.database), created["event_count"])
        archive = self.backend.export_backup()["path"]
        other = BundleBackend(self.directory / "restored.db", native_cli=self.native, clock=self.clock)
        other.restore_backup(archive)
        restored = next(g for g in other.snapshot()["goals"] if g["goal_id"] == first["goal_id"])
        self.assertEqual(restored, goal)
        restored_count = event_count(other.active.database)
        self.assertFalse(other.create_goal(body | {"workspace_database": str(other.active.database)})["created"])
        self.assertEqual(event_count(other.active.database), restored_count)

    def test_native_rejects_invalid_cli_without_goal_or_event(self):
        path = self.directory / "bad-goal.json"
        payload = {"request_id":"0123456789abcdef0123456789abcdef","name":"Synthetic","currency":"USD",
                   "target_minor":-1,"target_date":None,"protected":False,"created_at":"2026-11-26T12:00:00Z"}
        path.write_text(json.dumps(payload), encoding="utf-8")
        with self.assertRaises(BackendError):
            self.backend._run("create-goal", *self.backend._db(), path, expect_json=True)
        self.assertEqual(self.backend.snapshot()["goals"], [])
        self.assertEqual(event_count(self.database), 0)
