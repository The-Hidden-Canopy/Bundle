#!/usr/bin/env python3
"""Fail-closed checks for the public BUNDLE repository surface."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

_FORBIDDEN_PATH = (
    re.compile(
        r"(^|/)(?:\.git|\.codex-remote-attachments|BUNDLE|build(?:-[^/]*)?|"
        r"private|internal|secrets|data|exports)(?:/|$)"
    ),
    re.compile(r"(?:\.sqlite3?|\.db(?:-wal|-shm)?|\.log|\.jsonl|\.partial)$"),
)

_FORBIDDEN_CONTENT = (
    (re.compile(r"hf_[A-Za-z0-9]{20,}"), "token-shaped Hugging Face credential"),
    (re.compile(r"ghp_[A-Za-z0-9]{20,}"), "token-shaped GitHub credential"),
    (re.compile(r"github_pat_[A-Za-z0-9_]{20,}"), "token-shaped GitHub credential"),
    (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key material"),
    (
        re.compile(r"(?:[A-Za-z]:|/)(?:\\|/)(?:Users|home|HiddenCanopy)(?:\\|/)", re.IGNORECASE),
        "machine-specific filesystem path",
    ),
)


def _git_files(root: Path) -> list[str]:
    result = subprocess.run(
        [
            "git",
            "-c",
            "safe.directory=*",
            "-C",
            str(root),
            "ls-files",
            "-co",
            "--exclude-standard",
            "-z",
        ],
        check=True,
        capture_output=True,
    )
    return [item for item in result.stdout.decode("utf-8").split("\0") if item]


def _git_history_paths(root: Path) -> list[str]:
    result = subprocess.run(
        [
            "git",
            "-c",
            "safe.directory=*",
            "-C",
            str(root),
            "rev-list",
            "--objects",
            "--all",
        ],
        check=False,
        capture_output=True,
    )
    if result.returncode != 0:
        return []
    return [
        path.replace("\\", "/")
        for line in result.stdout.decode("utf-8", errors="replace").splitlines()
        for _object_id, separator, path in [line.partition(" ")]
        if separator and path
    ]


def _read_text(path: Path) -> str | None:
    try:
        data = path.read_bytes()
    except OSError:
        return None
    if b"\0" in data or len(data) > 8 * 1024 * 1024:
        return None
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return None


def _is_ignored(root: Path, relative: str) -> bool:
    result = subprocess.run(
        [
            "git",
            "-c",
            "safe.directory=*",
            "-C",
            str(root),
            "check-ignore",
            "--no-index",
            "-q",
            "--",
            relative,
        ],
        check=False,
        capture_output=True,
    )
    return result.returncode == 0


def _nested_git_metadata(root: Path) -> list[str]:
    findings: list[str] = []
    for current, directories, _files in os.walk(root):
        current_path = Path(current)
        for directory in list(directories):
            path = current_path / directory
            relative = path.relative_to(root).as_posix()
            if _is_ignored(root, relative):
                directories.remove(directory)
                continue
            if directory == ".git" and path != root / ".git":
                findings.append(relative)
                directories.remove(directory)
    return sorted(findings)


def _content_findings(relative: str, text: str) -> list[str]:
    return [
        f"{label} in {relative}"
        for pattern, label in _FORBIDDEN_CONTENT
        if pattern.search(text)
    ]


def check_boundary(root: Path = ROOT, *, include_history: bool = False) -> list[str]:
    root = root.resolve()
    findings: list[str] = []
    files = [item.replace("\\", "/") for item in _git_files(root)]

    for relative in sorted(files):
        if any(pattern.search(relative) for pattern in _FORBIDDEN_PATH):
            findings.append(f"private/generated path is publishable: {relative}")
        text = _read_text(root / Path(relative))
        if text is not None:
            findings.extend(_content_findings(relative, text))

    findings.extend(
        f"nested Git metadata: {relative}" for relative in _nested_git_metadata(root)
    )

    if include_history:
        for relative in sorted(set(_git_history_paths(root))):
            if any(pattern.search(relative) for pattern in _FORBIDDEN_PATH):
                findings.append(
                    f"private/generated path is reachable in Git history: {relative}"
                )

    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument(
        "--history",
        action="store_true",
        help="also inspect paths reachable from local Git refs",
    )
    args = parser.parse_args()
    findings = check_boundary(args.root, include_history=args.history)
    if findings:
        print("BUNDLE PUBLIC BOUNDARY: FAIL")
        for finding in findings:
            print(f"- {finding}")
        return 1
    print("BUNDLE PUBLIC BOUNDARY: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
