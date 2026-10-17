from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "check_public_boundary.py"


class PublicBoundaryTests(unittest.TestCase):
    def _run(self, *arguments: str, root: Path = ROOT) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(SCRIPT), "--root", str(root), *arguments],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_public_worktree_passes(self) -> None:
        current = self._run()
        self.assertEqual(current.returncode, 0, current.stdout + current.stderr)

    def test_history_mode_does_not_silence_legacy_findings(self) -> None:
        history = self._run("--history")
        if history.returncode:
            self.assertIn("reachable in Git history", history.stdout)

    def test_forced_private_path_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            subprocess.run(["git", "init", "-q", str(root)], check=True)
            private_file = root / "private" / "provider.json"
            private_file.parent.mkdir()
            private_file.write_text("{\"provider\": \"deployment-only\"}\n", encoding="utf-8")
            subprocess.run(["git", "-C", str(root), "add", "private/provider.json"], check=True)

            result = self._run(root=root)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("private/generated path is publishable", result.stdout)


if __name__ == "__main__":
    unittest.main()
