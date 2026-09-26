"""Real file/shell tools for Flint, scoped to one working directory.

FLINT_WORKDIR (env, defaults to the repo root one level up from this file)
is the only directory these tools will touch. Every path argument is
resolved against it and rejected if it escapes (via ``..`` or an absolute
path elsewhere) - the shell tool otherwise has no additional sandboxing
beyond running with that directory as its cwd.
"""

from __future__ import annotations

import os
import subprocess
from pathlib import Path

from langchain_core.tools import tool

WORKDIR = Path(os.getenv("FLINT_WORKDIR", Path(__file__).resolve().parent.parent)).resolve()


def _resolve(path: str) -> Path:
    candidate = (WORKDIR / path).resolve()
    if WORKDIR not in candidate.parents and candidate != WORKDIR:
        raise ValueError(f"Path '{path}' escapes the allowed working directory {WORKDIR}")
    return candidate


@tool
def read_file(path: str) -> str:
    """Read a text file's contents. Path is relative to the project root."""
    try:
        target = _resolve(path)
        if not target.is_file():
            return f"Error: {path} is not a file"
        return target.read_text(encoding="utf-8", errors="replace")
    except Exception as e:  # noqa: BLE001 -- must surface to the LLM as text, not crash the turn
        return f"Error reading {path}: {e}"


@tool
def write_file(path: str, content: str) -> str:
    """Write (create or overwrite) a text file. Path is relative to the project root."""
    try:
        target = _resolve(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
        return f"Wrote {len(content)} chars to {path}"
    except Exception as e:  # noqa: BLE001 -- must surface to the LLM as text, not crash the turn
        return f"Error writing {path}: {e}"


@tool
def run_shell(command: str, timeout_seconds: int = 120) -> str:
    """Run a shell command in the project root and return its stdout/stderr.

    Use this to actually execute tests (pytest, npm test, etc.), not just
    describe what should be run.
    """
    try:
        result = subprocess.run(
            command,
            shell=True,
            cwd=WORKDIR,
            capture_output=True,
            text=True,
            timeout=timeout_seconds,
        )
        output = f"exit_code={result.returncode}\n--- stdout ---\n{result.stdout}\n--- stderr ---\n{result.stderr}"
        return output[:8000]
    except subprocess.TimeoutExpired:
        return f"Error: command timed out after {timeout_seconds}s"
    except Exception as e:  # noqa: BLE001 -- must surface to the LLM as text, not crash the turn
        return f"Error running command: {e}"


FLINT_TOOLS = [read_file, write_file, run_shell]
